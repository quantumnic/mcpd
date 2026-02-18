/**
 * mcpd — Session Management
 *
 * Multi-session support with configurable limits and timeouts.
 * Replaces the single-session model with proper session tracking,
 * enabling multiple concurrent MCP clients (e.g. Claude + GPT simultaneously).
 *
 * MIT License
 */

#ifndef MCPD_SESSION_H
#define MCPD_SESSION_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <map>
#include <vector>
#include <functional>

namespace mcpd {

struct Session {
    String id;
    String clientName;
    unsigned long createdAt;     // millis()
    unsigned long lastActivity;  // millis()
    bool initialized;

    Session() : createdAt(0), lastActivity(0), initialized(false) {}
    Session(const String& sid, const String& client)
        : id(sid), clientName(client),
          createdAt(millis()), lastActivity(millis()),
          initialized(true) {}

    unsigned long ageMs() const { return millis() - createdAt; }
    unsigned long idleMs() const { return millis() - lastActivity; }
    void touch() { lastActivity = millis(); }
};

/**
 * SessionManager — tracks active sessions with limits and timeouts.
 */
class SessionManager {
public:
    SessionManager() = default;

    /**
     * Set max concurrent sessions. 0 = unlimited. Default: 4.
     */
    void setMaxSessions(size_t max) { _maxSessions = max; }

    /**
     * Set idle timeout in milliseconds. Sessions idle longer are pruned.
     * 0 = no timeout. Default: 30 minutes.
     */
    void setIdleTimeout(unsigned long timeoutMs) { _idleTimeoutMs = timeoutMs; }

    /**
     * Create a new session. Returns empty string if limit reached.
     */
    String createSession(const String& clientName) {
        pruneExpired();

        if (_maxSessions > 0 && _sessions.size() >= _maxSessions) {
            // Try to evict oldest idle session
            if (!_evictOldest()) {
                return "";  // All sessions active, can't create more
            }
        }

        String sid = _generateId();
        _sessions[sid] = Session(sid, clientName);
        return sid;
    }

    /**
     * Validate and touch a session. Returns true if session is valid.
     */
    bool validateSession(const String& id) {
        auto it = _sessions.find(id);
        if (it == _sessions.end()) return false;
        it->second.touch();
        return true;
    }

    /**
     * Remove a session (client disconnect or DELETE).
     */
    bool removeSession(const String& id) {
        return _sessions.erase(id) > 0;
    }

    /**
     * Get session info. Returns nullptr if not found.
     */
    const Session* getSession(const String& id) const {
        auto it = _sessions.find(id);
        return (it != _sessions.end()) ? &it->second : nullptr;
    }

    /**
     * Prune expired/idle sessions.
     */
    void pruneExpired() {
        if (_idleTimeoutMs == 0) return;
        std::vector<String> toRemove;
        for (const auto& kv : _sessions) {
            if (kv.second.idleMs() > _idleTimeoutMs) {
                toRemove.push_back(kv.first);
            }
        }
        for (const auto& id : toRemove) {
            Serial.printf("[mcpd] Session expired (idle %lums): %s\n",
                          _sessions[id].idleMs(), id.c_str());
            _sessions.erase(id);
        }
    }

    size_t activeCount() const { return _sessions.size(); }
    size_t maxSessions() const { return _maxSessions; }
    unsigned long idleTimeout() const { return _idleTimeoutMs; }

    /**
     * Get a summary for diagnostics.
     */
    String summary() const {
        JsonDocument doc;
        doc["activeSessions"] = _sessions.size();
        doc["maxSessions"] = _maxSessions;
        doc["idleTimeoutMs"] = _idleTimeoutMs;
        JsonArray arr = doc["sessions"].to<JsonArray>();
        for (const auto& kv : _sessions) {
            JsonObject s = arr.add<JsonObject>();
            s["id"] = kv.second.id.substring(0, 8) + "...";
            s["client"] = kv.second.clientName;
            s["idleMs"] = kv.second.idleMs();
            s["ageMs"] = kv.second.ageMs();
        }
        String result;
        serializeJson(doc, result);
        return result;
    }

private:
    std::map<String, Session> _sessions;
    size_t _maxSessions = 4;
    unsigned long _idleTimeoutMs = 30UL * 60 * 1000;  // 30 minutes

    bool _evictOldest() {
        if (_sessions.empty()) return false;
        String oldestId;
        unsigned long maxIdle = 0;
        for (const auto& kv : _sessions) {
            if (kv.second.idleMs() > maxIdle) {
                maxIdle = kv.second.idleMs();
                oldestId = kv.first;
            }
        }
        if (!oldestId.isEmpty()) {
            Serial.printf("[mcpd] Evicting oldest session: %s (idle %lums)\n",
                          oldestId.c_str(), maxIdle);
            _sessions.erase(oldestId);
            return true;
        }
        return false;
    }

    String _generateId() {
        String sid = "";
        for (int i = 0; i < 16; i++) {
            char hex[3];
            snprintf(hex, sizeof(hex), "%02x", (uint8_t)random(256));
            sid += hex;
        }
        return sid;
    }
};

} // namespace mcpd

#endif // MCPD_SESSION_H
