/**
 * mcpd — State Store (Key-Value State with Change Tracking)
 *
 * General-purpose key-value state store for microcontrollers. Stores
 * arbitrary string values by key, tracks changes, notifies listeners,
 * and serializes to/from JSON. Designed for storing device state,
 * calibration data, user preferences, and runtime config that can
 * optionally be exposed as MCP resources.
 *
 * Features:
 *   - Namespaced keys (e.g. "sensor.calibration.offset")
 *   - Change listeners with old/new value
 *   - Dirty tracking for efficient persistence
 *   - Bounded size (max entries, evicts oldest-accessed on overflow)
 *   - Snapshot export/import (JSON)
 *   - TTL support (optional per-key expiry)
 *   - Transaction support (batch changes with commit/rollback)
 *
 * Usage:
 *   mcpd::StateStore state(128);
 *   state.set("wifi.rssi", "-67");
 *   state.set("sensor.temp", "22.5");
 *   state.onChange([](const char* key, const char* oldVal, const char* newVal) {
 *       Serial.printf("State changed: %s = %s\n", key, newVal);
 *   });
 *   String json = state.toJSON();
 */

#ifndef MCPD_STATE_STORE_H
#define MCPD_STATE_STORE_H

#ifdef MCPD_TEST
#include <Arduino.h>
#else
#include <Arduino.h>
#endif

#include <vector>
#include <map>
#include <functional>
#include <string>
#include <cstring>

namespace mcpd {

/**
 * Change listener callback: (key, oldValue, newValue)
 * oldValue is "" for new keys, newValue is "" for deletions.
 */
using StateChangeListener = std::function<void(const char* key, const char* oldVal, const char* newVal)>;

struct StateEntry {
    String value;
    unsigned long lastAccess = 0;   // millis() of last get/set
    unsigned long createdAt = 0;    // millis() of creation
    unsigned long ttlMs = 0;        // 0 = no expiry
    bool dirty = false;             // changed since last clearDirty()
};

class StateStore {
public:
    /**
     * Create a state store with maximum capacity.
     * @param maxEntries Maximum number of key-value pairs (0 = unlimited)
     */
    explicit StateStore(size_t maxEntries = 0)
        : _maxEntries(maxEntries), _inTransaction(false) {}

    /**
     * Set a key-value pair. Overwrites existing values.
     * @return true if the value changed (or was new)
     */
    bool set(const char* key, const char* value, unsigned long ttlMs = 0) {
        if (!key || !key[0]) return false;

        // If in transaction, buffer the change
        if (_inTransaction) {
            _txBuffer[std::string(key)] = {String(value), ttlMs};
            return true;
        }

        return _applySet(key, value, ttlMs);
    }

    /**
     * Get a value by key.
     * @return The value, or "" if not found or expired
     */
    String get(const char* key) const {
        if (!key) return "";
        auto it = _entries.find(std::string(key));
        if (it == _entries.end()) return "";

        // Check TTL
        if (it->second.ttlMs > 0) {
            unsigned long age = millis() - it->second.createdAt;
            if (age > it->second.ttlMs) return "";
        }

        // Update access time (mutable)
        const_cast<StateEntry&>(it->second).lastAccess = millis();
        return it->second.value;
    }

    /**
     * Check if a key exists (and is not expired).
     */
    bool has(const char* key) const {
        if (!key) return false;
        auto it = _entries.find(std::string(key));
        if (it == _entries.end()) return false;
        if (it->second.ttlMs > 0) {
            unsigned long age = millis() - it->second.createdAt;
            if (age > it->second.ttlMs) return false;
        }
        return true;
    }

    /**
     * Remove a key.
     * @return true if the key existed
     */
    bool remove(const char* key) {
        if (!key) return false;
        auto it = _entries.find(std::string(key));
        if (it == _entries.end()) return false;

        String oldVal = it->second.value;
        _entries.erase(it);
        _notifyListeners(key, oldVal.c_str(), "");
        return true;
    }

    /**
     * Get all keys matching a prefix (namespace).
     * E.g., keys("sensor.") returns all sensor keys.
     */
    std::vector<String> keys(const char* prefix = "") const {
        std::vector<String> result;
        std::string pfx(prefix ? prefix : "");
        for (auto& kv : _entries) {
            if (pfx.empty() || kv.first.compare(0, pfx.size(), pfx) == 0) {
                // Skip expired
                if (kv.second.ttlMs > 0) {
                    unsigned long age = millis() - kv.second.createdAt;
                    if (age > kv.second.ttlMs) continue;
                }
                result.push_back(String(kv.first.c_str()));
            }
        }
        return result;
    }

    /**
     * Number of entries (including expired, call purgeExpired() first for accuracy).
     */
    size_t count() const { return _entries.size(); }

    /**
     * Maximum capacity (0 = unlimited).
     */
    size_t maxEntries() const { return _maxEntries; }

    /**
     * Remove all expired entries.
     * @return Number of entries purged
     */
    size_t purgeExpired() {
        size_t purged = 0;
        unsigned long now = millis();
        for (auto it = _entries.begin(); it != _entries.end(); ) {
            if (it->second.ttlMs > 0 && (now - it->second.createdAt) > it->second.ttlMs) {
                _notifyListeners(it->first.c_str(), it->second.value.c_str(), "");
                it = _entries.erase(it);
                purged++;
            } else {
                ++it;
            }
        }
        return purged;
    }

    /**
     * Clear all entries.
     */
    void clear() {
        // Notify for each removal
        for (auto& kv : _entries) {
            _notifyListeners(kv.first.c_str(), kv.second.value.c_str(), "");
        }
        _entries.clear();
    }

    /**
     * Register a change listener. Returns listener ID for removal.
     */
    size_t onChange(StateChangeListener listener) {
        size_t id = _nextListenerId++;
        _listeners.push_back({id, std::move(listener)});
        return id;
    }

    /**
     * Remove a change listener by ID.
     */
    bool removeListener(size_t id) {
        for (auto it = _listeners.begin(); it != _listeners.end(); ++it) {
            if (it->id == id) {
                _listeners.erase(it);
                return true;
            }
        }
        return false;
    }

    /**
     * Get dirty keys (changed since last clearDirty()).
     */
    std::vector<String> dirtyKeys() const {
        std::vector<String> result;
        for (auto& kv : _entries) {
            if (kv.second.dirty) {
                result.push_back(String(kv.first.c_str()));
            }
        }
        return result;
    }

    /**
     * Check if any entries are dirty.
     */
    bool isDirty() const {
        for (auto& kv : _entries) {
            if (kv.second.dirty) return true;
        }
        return false;
    }

    /**
     * Clear dirty flags on all entries.
     */
    void clearDirty() {
        for (auto& kv : _entries) {
            kv.second.dirty = false;
        }
    }

    // ── Transactions ───────────────────────────────────────────────

    /**
     * Begin a transaction. Changes are buffered until commit().
     */
    void begin() {
        _inTransaction = true;
        _txBuffer.clear();
    }

    /**
     * Commit buffered changes atomically.
     * @return Number of changes applied
     */
    size_t commit() {
        if (!_inTransaction) return 0;
        _inTransaction = false;
        size_t applied = 0;
        for (auto& kv : _txBuffer) {
            if (_applySet(kv.first.c_str(), kv.second.value.c_str(), kv.second.ttlMs)) {
                applied++;
            }
        }
        _txBuffer.clear();
        return applied;
    }

    /**
     * Rollback buffered changes.
     */
    void rollback() {
        _inTransaction = false;
        _txBuffer.clear();
    }

    /**
     * Check if a transaction is in progress.
     */
    bool inTransaction() const { return _inTransaction; }

    // ── Serialization ──────────────────────────────────────────────

    /**
     * Export all (non-expired) state as JSON object.
     * Format: {"key1":"val1","key2":"val2",...}
     */
    String toJSON() const {
        String json = "{";
        bool first = true;
        unsigned long now = millis();
        for (auto& kv : _entries) {
            if (kv.second.ttlMs > 0 && (now - kv.second.createdAt) > kv.second.ttlMs)
                continue;
            if (!first) json += ",";
            json += "\"";
            json += _escapeJSON(kv.first.c_str());
            json += "\":\"";
            json += _escapeJSON(kv.second.value.c_str());
            json += "\"";
            first = false;
        }
        json += "}";
        return json;
    }

    /**
     * Export detailed state as JSON (includes metadata).
     * Format: {"key":{"value":"v","ttl":0,"dirty":false,"age":1234},...}
     */
    String toDetailedJSON() const {
        String json = "{";
        bool first = true;
        unsigned long now = millis();
        for (auto& kv : _entries) {
            if (kv.second.ttlMs > 0 && (now - kv.second.createdAt) > kv.second.ttlMs)
                continue;
            if (!first) json += ",";
            json += "\"";
            json += _escapeJSON(kv.first.c_str());
            json += "\":{\"value\":\"";
            json += _escapeJSON(kv.second.value.c_str());
            json += "\",\"ttl\":";
            json += String((unsigned long)kv.second.ttlMs);
            json += ",\"dirty\":";
            json += kv.second.dirty ? "true" : "false";
            json += ",\"age\":";
            json += String((unsigned long)(now - kv.second.createdAt));
            json += "}";
            first = false;
        }
        json += "}";
        return json;
    }

    /**
     * Import state from simple JSON object ({"key":"value",...}).
     * Merges with existing state (does not clear first).
     * @return Number of keys imported
     */
    size_t fromJSON(const char* json) {
        if (!json) return 0;
        size_t imported = 0;

        // Simple JSON parser for flat {"key":"value"} objects
        const char* p = json;
        while (*p && *p != '{') p++;
        if (!*p) return 0;
        p++; // skip '{'

        while (*p) {
            // Skip whitespace
            while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',')) p++;
            if (*p == '}' || !*p) break;

            // Parse key
            if (*p != '"') break;
            String key = _parseJSONString(p);
            if (key.length() == 0) break;

            // Skip colon
            while (*p && *p != ':') p++;
            if (!*p) break;
            p++;
            while (*p && (*p == ' ' || *p == '\t')) p++;

            // Parse value
            if (*p != '"') break;
            String value = _parseJSONString(p);

            set(key.c_str(), value.c_str());
            imported++;
        }
        return imported;
    }

    /**
     * Stats as JSON.
     */
    String statsJSON() const {
        String json = "{\"count\":";
        json += String((unsigned long)_entries.size());
        json += ",\"maxEntries\":";
        json += String((unsigned long)_maxEntries);
        json += ",\"dirty\":";
        size_t dirtyCount = 0;
        for (auto& kv : _entries) {
            if (kv.second.dirty) dirtyCount++;
        }
        json += String((unsigned long)dirtyCount);
        json += ",\"listeners\":";
        json += String((unsigned long)_listeners.size());
        json += ",\"inTransaction\":";
        json += _inTransaction ? "true" : "false";
        json += "}";
        return json;
    }

private:
    struct TxEntry {
        String value;
        unsigned long ttlMs;
    };

    struct ListenerEntry {
        size_t id;
        StateChangeListener fn;
    };

    std::map<std::string, StateEntry> _entries;
    std::vector<ListenerEntry> _listeners;
    size_t _maxEntries;
    size_t _nextListenerId = 0;
    bool _inTransaction;
    std::map<std::string, TxEntry> _txBuffer;

    bool _applySet(const char* key, const char* value, unsigned long ttlMs) {
        std::string k(key);
        auto it = _entries.find(k);

        if (it != _entries.end()) {
            // Existing key — check if value actually changed
            if (it->second.value == value && it->second.ttlMs == ttlMs) {
                it->second.lastAccess = millis();
                return false; // no change
            }
            String oldVal = it->second.value;
            it->second.value = value;
            it->second.lastAccess = millis();
            it->second.ttlMs = ttlMs;
            if (ttlMs > 0) it->second.createdAt = millis(); // reset TTL
            it->second.dirty = true;
            _notifyListeners(key, oldVal.c_str(), value);
            return true;
        }

        // New key — check capacity
        if (_maxEntries > 0 && _entries.size() >= _maxEntries) {
            _evictOldest();
        }

        StateEntry entry;
        entry.value = value;
        entry.lastAccess = millis();
        entry.createdAt = millis();
        entry.ttlMs = ttlMs;
        entry.dirty = true;
        _entries[k] = std::move(entry);

        _notifyListeners(key, "", value);
        return true;
    }

    void _evictOldest() {
        if (_entries.empty()) return;
        auto oldest = _entries.begin();
        for (auto it = _entries.begin(); it != _entries.end(); ++it) {
            if (it->second.lastAccess < oldest->second.lastAccess) {
                oldest = it;
            }
        }
        _notifyListeners(oldest->first.c_str(), oldest->second.value.c_str(), "");
        _entries.erase(oldest);
    }

    void _notifyListeners(const char* key, const char* oldVal, const char* newVal) {
        for (auto& l : _listeners) {
            l.fn(key, oldVal, newVal);
        }
    }

    static String _escapeJSON(const char* s) {
        String result;
        while (*s) {
            if (*s == '"') result += "\\\"";
            else if (*s == '\\') result += "\\\\";
            else if (*s == '\n') result += "\\n";
            else if (*s == '\r') result += "\\r";
            else if (*s == '\t') result += "\\t";
            else result += *s;
            s++;
        }
        return result;
    }

    /**
     * Parse a JSON string starting at p (which points to opening '"').
     * Advances p past the closing '"'.
     */
    static String _parseJSONString(const char*& p) {
        if (*p != '"') return "";
        p++; // skip opening quote
        String result;
        while (*p && *p != '"') {
            if (*p == '\\' && *(p + 1)) {
                p++;
                switch (*p) {
                    case '"':  result += '"'; break;
                    case '\\': result += '\\'; break;
                    case 'n':  result += '\n'; break;
                    case 'r':  result += '\r'; break;
                    case 't':  result += '\t'; break;
                    default:   result += *p; break;
                }
            } else {
                result += *p;
            }
            p++;
        }
        if (*p == '"') p++; // skip closing quote
        return result;
    }
};

} // namespace mcpd

#endif // MCPD_STATE_STORE_H
