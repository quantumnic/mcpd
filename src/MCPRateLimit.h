/**
 * MCPRateLimit — Request rate limiting for mcpd
 *
 * Protects embedded devices from being overwhelmed by too many requests.
 * Uses a token bucket algorithm — efficient, constant memory, O(1) per check.
 *
 * Features:
 *   - Global rate limiting (single bucket)
 *   - Per-key rate limiting (e.g. per-client, per-tool, per-IP)
 *   - Configurable burst capacity
 *   - Stats tracking (allowed/denied counts)
 *   - JSON serialization for diagnostics
 *   - Penalty support (consume extra tokens for expensive operations)
 *
 * Usage:
 *   server.setRateLimit(10, 5);  // 10 requests/sec, burst of 5
 *   // Requests exceeding the limit get a JSON-RPC -32000 error
 *
 *   // Per-key limiting:
 *   mcpd::KeyedRateLimiter limiter(10.0, 5, 16);
 *   if (limiter.tryAcquire("client-123")) { ... }
 */

#ifndef MCP_RATE_LIMIT_H
#define MCP_RATE_LIMIT_H

#include <Arduino.h>
#include <cstring>

namespace mcpd {

/**
 * Token bucket rate limiter.
 *
 * Refills tokens at a steady rate. Each request consumes one token.
 * Burst capacity allows short spikes above the sustained rate.
 */
class RateLimiter {
public:
    RateLimiter() = default;

    /**
     * Configure the rate limiter.
     * @param requestsPerSecond  Sustained request rate
     * @param burstCapacity      Maximum burst size (tokens in bucket)
     */
    void configure(float requestsPerSecond, size_t burstCapacity) {
        _rps = requestsPerSecond;
        _capacity = burstCapacity;
        _tokens = (float)burstCapacity;  // Start full
        _lastRefill = millis();
        _enabled = true;
        _totalAllowed = 0;
        _totalDenied = 0;
    }

    /** Disable rate limiting */
    void disable() {
        _enabled = false;
    }

    /** Check if rate limiting is enabled */
    bool isEnabled() const { return _enabled; }

    /**
     * Try to consume one or more tokens. Returns true if allowed, false if rate limited.
     * @param cost  Number of tokens to consume (default 1). Use >1 for expensive operations.
     */
    bool tryAcquire(float cost = 1.0f) {
        if (!_enabled) return true;
        if (cost <= 0.0f) return true;

        _refill();

        if (_tokens >= cost) {
            _tokens -= cost;
            _totalAllowed++;
            return true;
        }

        _totalDenied++;
        return false;
    }

    /** Get current token count */
    float availableTokens() const { return _tokens; }

    /** Get configured rate */
    float requestsPerSecond() const { return _rps; }

    /** Get burst capacity */
    size_t burstCapacity() const { return _capacity; }

    /** Get total allowed requests since configuration */
    unsigned long totalAllowed() const { return _totalAllowed; }

    /** Get total denied requests since configuration */
    unsigned long totalDenied() const { return _totalDenied; }

    /** Reset counters */
    void resetStats() {
        _totalAllowed = 0;
        _totalDenied = 0;
    }

    /**
     * Estimated time (ms) until the next token is available.
     * Returns 0 if tokens are available or limiter is disabled.
     */
    unsigned long retryAfterMs() const {
        if (!_enabled || _tokens >= 1.0f) return 0;
        if (_rps <= 0.0f) return 0;
        float deficit = 1.0f - _tokens;
        return (unsigned long)((deficit / _rps) * 1000.0f) + 1;
    }

    /** Serialize state to JSON string */
    String toJson() const {
        String json = "{";
        json += "\"enabled\":";
        json += _enabled ? "true" : "false";
        json += ",\"requestsPerSecond\":";
        json += String(_rps, 1);
        json += ",\"burstCapacity\":";
        json += String((unsigned long)_capacity);
        json += ",\"availableTokens\":";
        json += String(_tokens, 2);
        json += ",\"totalAllowed\":";
        json += String(_totalAllowed);
        json += ",\"totalDenied\":";
        json += String(_totalDenied);
        json += "}";
        return json;
    }

private:
    bool _enabled = false;
    float _rps = 0;
    size_t _capacity = 0;
    float _tokens = 0;
    unsigned long _lastRefill = 0;
    unsigned long _totalAllowed = 0;
    unsigned long _totalDenied = 0;

    void _refill() {
        unsigned long now = millis();
        unsigned long elapsed = now - _lastRefill;
        if (elapsed == 0) return;

        _lastRefill = now;
        float newTokens = (elapsed / 1000.0f) * _rps;
        _tokens += newTokens;
        if (_tokens > (float)_capacity) {
            _tokens = (float)_capacity;
        }
    }
};

/**
 * Per-key rate limiter using a fixed-size bucket pool.
 *
 * Each unique key (e.g. client ID, IP, tool name) gets its own token bucket.
 * When the pool is full, the least-recently-used bucket is evicted.
 * Ideal for MCU environments with bounded memory.
 */
class KeyedRateLimiter {
public:
    /**
     * @param requestsPerSecond  Per-key sustained rate
     * @param burstCapacity      Per-key burst capacity
     * @param maxKeys            Maximum number of tracked keys (pool size)
     */
    KeyedRateLimiter(float requestsPerSecond = 10.0f,
                     size_t burstCapacity = 5,
                     size_t maxKeys = 16)
        : _rps(requestsPerSecond)
        , _capacity(burstCapacity)
        , _maxKeys(maxKeys > 0 ? maxKeys : 1)
        , _count(0)
        , _totalAllowed(0)
        , _totalDenied(0)
        , _evictions(0)
        , _enabled(true)
    {
        _buckets = new Bucket[_maxKeys];
    }

    ~KeyedRateLimiter() {
        delete[] _buckets;
    }

    // Non-copyable
    KeyedRateLimiter(const KeyedRateLimiter&) = delete;
    KeyedRateLimiter& operator=(const KeyedRateLimiter&) = delete;

    /** Enable/disable */
    void setEnabled(bool on) { _enabled = on; }
    bool isEnabled() const { return _enabled; }

    /**
     * Try to acquire a token for the given key.
     * @param key   Identifier (client, tool, IP, etc.)
     * @param cost  Tokens to consume (default 1)
     * @return true if allowed, false if rate-limited
     */
    bool tryAcquire(const char* key, float cost = 1.0f) {
        if (!_enabled) return true;
        if (!key || key[0] == '\0') return true;
        if (cost <= 0.0f) return true;

        Bucket* b = _findOrCreate(key);
        if (!b) {
            // Should not happen since _findOrCreate always returns a bucket
            _totalDenied++;
            return false;
        }

        _refillBucket(b);

        if (b->tokens >= cost) {
            b->tokens -= cost;
            b->lastAccess = millis();
            _totalAllowed++;
            return true;
        }

        b->denied++;
        _totalDenied++;
        return false;
    }

    /** Get the number of active keys being tracked */
    size_t activeKeys() const { return _count; }

    /** Get pool capacity */
    size_t maxKeys() const { return _maxKeys; }

    /** Get total allowed across all keys */
    unsigned long totalAllowed() const { return _totalAllowed; }

    /** Get total denied across all keys */
    unsigned long totalDenied() const { return _totalDenied; }

    /** Get eviction count (how many times a key was replaced) */
    unsigned long evictions() const { return _evictions; }

    /** Check if a key is currently tracked */
    bool hasKey(const char* key) const {
        if (!key) return false;
        for (size_t i = 0; i < _count; i++) {
            if (std::strcmp(_buckets[i].key, key) == 0) return true;
        }
        return false;
    }

    /** Remove a specific key from tracking */
    bool removeKey(const char* key) {
        if (!key) return false;
        for (size_t i = 0; i < _count; i++) {
            if (std::strcmp(_buckets[i].key, key) == 0) {
                // Shift remaining
                for (size_t j = i; j + 1 < _count; j++) {
                    _buckets[j] = _buckets[j + 1];
                }
                _count--;
                _buckets[_count].key[0] = '\0';
                return true;
            }
        }
        return false;
    }

    /** Reset all stats and clear all tracked keys */
    void reset() {
        for (size_t i = 0; i < _count; i++) {
            _buckets[i].key[0] = '\0';
        }
        _count = 0;
        _totalAllowed = 0;
        _totalDenied = 0;
        _evictions = 0;
    }

    /** Per-key rate */
    float requestsPerSecond() const { return _rps; }

    /** Per-key burst */
    size_t burstCapacity() const { return _capacity; }

    /** Reconfigure all limits (resets existing buckets) */
    void configure(float requestsPerSecond, size_t burstCapacity) {
        _rps = requestsPerSecond;
        _capacity = burstCapacity;
        // Reset existing buckets to new config
        for (size_t i = 0; i < _count; i++) {
            _buckets[i].tokens = (float)burstCapacity;
        }
    }

    /** Serialize state to JSON string */
    String toJson() const {
        String json = "{";
        json += "\"enabled\":";
        json += _enabled ? "true" : "false";
        json += ",\"requestsPerSecond\":";
        json += String(_rps, 1);
        json += ",\"burstCapacity\":";
        json += String((unsigned long)_capacity);
        json += ",\"activeKeys\":";
        json += String((unsigned long)_count);
        json += ",\"maxKeys\":";
        json += String((unsigned long)_maxKeys);
        json += ",\"totalAllowed\":";
        json += String(_totalAllowed);
        json += ",\"totalDenied\":";
        json += String(_totalDenied);
        json += ",\"evictions\":";
        json += String(_evictions);
        json += "}";
        return json;
    }

private:
    static constexpr size_t KEY_MAX_LEN = 32;

    struct Bucket {
        char key[KEY_MAX_LEN] = {0};
        float tokens = 0;
        unsigned long lastRefill = 0;
        unsigned long lastAccess = 0;
        unsigned long denied = 0;
    };

    float _rps;
    size_t _capacity;
    size_t _maxKeys;
    size_t _count;
    unsigned long _totalAllowed;
    unsigned long _totalDenied;
    unsigned long _evictions;
    bool _enabled;
    Bucket* _buckets;

    Bucket* _findOrCreate(const char* key) {
        // Find existing
        for (size_t i = 0; i < _count; i++) {
            if (std::strcmp(_buckets[i].key, key) == 0) {
                return &_buckets[i];
            }
        }

        // Create new
        Bucket* b = nullptr;
        if (_count < _maxKeys) {
            b = &_buckets[_count++];
        } else {
            // Evict LRU
            size_t lruIdx = 0;
            unsigned long oldest = _buckets[0].lastAccess;
            for (size_t i = 1; i < _count; i++) {
                if (_buckets[i].lastAccess < oldest) {
                    oldest = _buckets[i].lastAccess;
                    lruIdx = i;
                }
            }
            b = &_buckets[lruIdx];
            _evictions++;
        }

        // Initialize bucket
        std::strncpy(b->key, key, KEY_MAX_LEN - 1);
        b->key[KEY_MAX_LEN - 1] = '\0';
        b->tokens = (float)_capacity;
        b->lastRefill = millis();
        b->lastAccess = millis();
        b->denied = 0;
        return b;
    }

    void _refillBucket(Bucket* b) {
        unsigned long now = millis();
        unsigned long elapsed = now - b->lastRefill;
        if (elapsed == 0) return;

        b->lastRefill = now;
        float newTokens = (elapsed / 1000.0f) * _rps;
        b->tokens += newTokens;
        if (b->tokens > (float)_capacity) {
            b->tokens = (float)_capacity;
        }
    }
};

} // namespace mcpd

#endif // MCP_RATE_LIMIT_H
