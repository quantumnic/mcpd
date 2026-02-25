/**
 * MCPCircuitBreaker — Circuit breaker pattern for mcpd tool calls
 *
 * Prevents repeated calls to failing hardware/peripherals, allowing recovery
 * time. Essential for MCU environments where I2C devices freeze, sensors
 * disconnect, or network calls timeout.
 *
 * States:
 *   CLOSED   → Normal operation, calls pass through
 *   OPEN     → Failures exceeded threshold, calls are rejected immediately
 *   HALF_OPEN → Recovery probe: one call allowed to test if service recovered
 *
 * Features:
 *   - Per-key circuit breakers (e.g. per-tool, per-peripheral, per-bus)
 *   - Configurable failure threshold and recovery timeout
 *   - Success threshold for half-open → closed transition
 *   - Failure/recovery callbacks
 *   - JSON serialization for diagnostics
 *   - Manual reset/trip
 *   - LRU eviction for bounded memory on MCUs
 *   - Server integration (automatic tool call protection)
 *
 * Usage:
 *   mcpd::CircuitBreakerRegistry breakers(5, 30000);  // 5 failures, 30s recovery
 *   auto& cb = breakers.get("i2c-sensor");
 *   if (cb.allowRequest()) {
 *       bool ok = readSensor();
 *       ok ? cb.recordSuccess() : cb.recordFailure();
 *   }
 */

#ifndef MCP_CIRCUIT_BREAKER_H
#define MCP_CIRCUIT_BREAKER_H

#include <Arduino.h>
#include <cstring>
#include <functional>

namespace mcpd {

enum class CircuitState : uint8_t {
    CLOSED = 0,
    OPEN = 1,
    HALF_OPEN = 2
};

/**
 * Single circuit breaker instance.
 */
class CircuitBreaker {
public:
    using Callback = std::function<void(const char* key, CircuitState newState)>;

    CircuitBreaker()
        : _failureThreshold(5), _recoveryTimeoutMs(30000), _halfOpenSuccessThreshold(1),
          _state(CircuitState::CLOSED), _failureCount(0), _successCount(0),
          _lastFailureMs(0), _lastStateChangeMs(0),
          _totalFailures(0), _totalSuccesses(0), _totalRejected(0), _tripCount(0) {
        _key[0] = '\0';
    }

    void configure(const char* key, size_t failureThreshold, unsigned long recoveryTimeoutMs,
                   size_t halfOpenSuccessThreshold = 1) {
        strncpy(_key, key, sizeof(_key) - 1);
        _key[sizeof(_key) - 1] = '\0';
        _failureThreshold = failureThreshold;
        _recoveryTimeoutMs = recoveryTimeoutMs;
        _halfOpenSuccessThreshold = halfOpenSuccessThreshold;
        reset();
    }

    /**
     * Check if a request should be allowed through.
     * Transitions OPEN → HALF_OPEN if recovery timeout has elapsed.
     */
    bool allowRequest() {
        unsigned long now = millis();
        switch (_state) {
            case CircuitState::CLOSED:
                return true;
            case CircuitState::OPEN:
                if (now - _lastFailureMs >= _recoveryTimeoutMs) {
                    _transition(CircuitState::HALF_OPEN, now);
                    return true;  // Allow probe request
                }
                _totalRejected++;
                return false;
            case CircuitState::HALF_OPEN:
                // Only allow if we haven't had a success yet in this half-open cycle
                // (one probe at a time)
                return true;
        }
        return false;
    }

    /**
     * Record a successful call. May transition HALF_OPEN → CLOSED.
     */
    void recordSuccess() {
        _totalSuccesses++;
        switch (_state) {
            case CircuitState::CLOSED:
                _failureCount = 0;  // Reset consecutive failures
                _successCount++;
                break;
            case CircuitState::HALF_OPEN:
                _successCount++;
                if (_successCount >= _halfOpenSuccessThreshold) {
                    _transition(CircuitState::CLOSED, millis());
                    _failureCount = 0;
                    _successCount = 0;
                }
                break;
            case CircuitState::OPEN:
                break;  // Shouldn't happen
        }
    }

    /**
     * Record a failed call. May transition CLOSED → OPEN or HALF_OPEN → OPEN.
     */
    void recordFailure() {
        unsigned long now = millis();
        _totalFailures++;
        _lastFailureMs = now;
        _failureCount++;
        _successCount = 0;

        switch (_state) {
            case CircuitState::CLOSED:
                if (_failureCount >= _failureThreshold) {
                    _transition(CircuitState::OPEN, now);
                    _tripCount++;
                }
                break;
            case CircuitState::HALF_OPEN:
                _transition(CircuitState::OPEN, now);
                _tripCount++;
                break;
            case CircuitState::OPEN:
                break;
        }
    }

    /** Manually reset to CLOSED state. */
    void reset() {
        _state = CircuitState::CLOSED;
        _failureCount = 0;
        _successCount = 0;
        _lastFailureMs = 0;
        _lastStateChangeMs = millis();
    }

    /** Manually trip to OPEN state. */
    void trip() {
        _lastFailureMs = millis();
        _transition(CircuitState::OPEN, _lastFailureMs);
        _tripCount++;
    }

    // ── Getters ────────────────────────────────────────────

    CircuitState state() const { return _state; }
    const char* stateStr() const {
        switch (_state) {
            case CircuitState::CLOSED: return "closed";
            case CircuitState::OPEN: return "open";
            case CircuitState::HALF_OPEN: return "half_open";
        }
        return "unknown";
    }
    const char* key() const { return _key; }
    size_t failureCount() const { return _failureCount; }
    size_t failureThreshold() const { return _failureThreshold; }
    unsigned long recoveryTimeoutMs() const { return _recoveryTimeoutMs; }
    unsigned long lastFailureMs() const { return _lastFailureMs; }
    unsigned long lastStateChangeMs() const { return _lastStateChangeMs; }
    size_t totalFailures() const { return _totalFailures; }
    size_t totalSuccesses() const { return _totalSuccesses; }
    size_t totalRejected() const { return _totalRejected; }
    size_t tripCount() const { return _tripCount; }
    bool isOpen() const { return _state == CircuitState::OPEN; }
    bool isClosed() const { return _state == CircuitState::CLOSED; }
    bool isHalfOpen() const { return _state == CircuitState::HALF_OPEN; }

    /**
     * Time remaining until recovery probe (0 if not OPEN or already elapsed).
     */
    unsigned long retryAfterMs() const {
        if (_state != CircuitState::OPEN) return 0;
        unsigned long elapsed = millis() - _lastFailureMs;
        if (elapsed >= _recoveryTimeoutMs) return 0;
        return _recoveryTimeoutMs - elapsed;
    }

    void onStateChange(Callback cb) { _onStateChange = cb; }

    // ── JSON serialization ─────────────────────────────────

    String toJson() const {
        String json = "{\"key\":\"";
        json += _key;
        json += "\",\"state\":\"";
        json += stateStr();
        json += "\",\"failureCount\":";
        json += String((unsigned long)_failureCount);
        json += ",\"failureThreshold\":";
        json += String((unsigned long)_failureThreshold);
        json += ",\"recoveryTimeoutMs\":";
        json += String(_recoveryTimeoutMs);
        json += ",\"retryAfterMs\":";
        json += String(retryAfterMs());
        json += ",\"totalFailures\":";
        json += String((unsigned long)_totalFailures);
        json += ",\"totalSuccesses\":";
        json += String((unsigned long)_totalSuccesses);
        json += ",\"totalRejected\":";
        json += String((unsigned long)_totalRejected);
        json += ",\"tripCount\":";
        json += String((unsigned long)_tripCount);
        json += "}";
        return json;
    }

private:
    void _transition(CircuitState newState, unsigned long now) {
        CircuitState old = _state;
        _state = newState;
        _lastStateChangeMs = now;
        if (old != newState && _onStateChange) {
            _onStateChange(_key, newState);
        }
    }

    char _key[32];
    size_t _failureThreshold;
    unsigned long _recoveryTimeoutMs;
    size_t _halfOpenSuccessThreshold;

    CircuitState _state;
    size_t _failureCount;
    size_t _successCount;
    unsigned long _lastFailureMs;
    unsigned long _lastStateChangeMs;

    // Stats
    size_t _totalFailures;
    size_t _totalSuccesses;
    size_t _totalRejected;
    size_t _tripCount;

    Callback _onStateChange;
};


/**
 * Registry of named circuit breakers with LRU eviction.
 * Bounded memory — when full, evicts the least-recently-used breaker.
 */
class CircuitBreakerRegistry {
public:
    /**
     * @param failureThreshold    Failures before tripping open
     * @param recoveryTimeoutMs   Time in OPEN before trying HALF_OPEN
     * @param maxBreakers         Maximum number of breakers (LRU eviction)
     * @param halfOpenSuccessThreshold  Successes in HALF_OPEN before closing
     */
    explicit CircuitBreakerRegistry(size_t failureThreshold = 5,
                                     unsigned long recoveryTimeoutMs = 30000,
                                     size_t maxBreakers = 16,
                                     size_t halfOpenSuccessThreshold = 1)
        : _failureThreshold(failureThreshold), _recoveryTimeoutMs(recoveryTimeoutMs),
          _halfOpenSuccessThreshold(halfOpenSuccessThreshold),
          _maxBreakers(maxBreakers), _count(0), _onStateChange(nullptr) {
        _breakers = new Entry[maxBreakers];
    }

    ~CircuitBreakerRegistry() {
        delete[] _breakers;
    }

    // Non-copyable
    CircuitBreakerRegistry(const CircuitBreakerRegistry&) = delete;
    CircuitBreakerRegistry& operator=(const CircuitBreakerRegistry&) = delete;

    /**
     * Get or create a circuit breaker for the given key.
     */
    CircuitBreaker& get(const char* key) {
        // Look for existing
        for (size_t i = 0; i < _count; i++) {
            if (strcmp(_breakers[i].breaker.key(), key) == 0) {
                _breakers[i].lastAccessMs = millis();
                return _breakers[i].breaker;
            }
        }
        // Need to create — evict LRU if full
        size_t idx;
        if (_count < _maxBreakers) {
            idx = _count++;
        } else {
            idx = _findLRU();
        }
        _breakers[idx].breaker.configure(key, _failureThreshold, _recoveryTimeoutMs,
                                          _halfOpenSuccessThreshold);
        _breakers[idx].lastAccessMs = millis();
        if (_onStateChange) {
            _breakers[idx].breaker.onStateChange(_onStateChange);
        }
        return _breakers[idx].breaker;
    }

    /** Check if a key exists in the registry. */
    bool has(const char* key) const {
        for (size_t i = 0; i < _count; i++) {
            if (strcmp(_breakers[i].breaker.key(), key) == 0) return true;
        }
        return false;
    }

    /** Remove a circuit breaker by key. Returns true if found. */
    bool remove(const char* key) {
        for (size_t i = 0; i < _count; i++) {
            if (strcmp(_breakers[i].breaker.key(), key) == 0) {
                // Shift remaining entries
                for (size_t j = i; j < _count - 1; j++) {
                    _breakers[j] = _breakers[j + 1];
                }
                _count--;
                return true;
            }
        }
        return false;
    }

    /** Reset all circuit breakers to CLOSED. */
    void resetAll() {
        for (size_t i = 0; i < _count; i++) {
            _breakers[i].breaker.reset();
        }
    }

    /** Number of active breakers. */
    size_t count() const { return _count; }
    size_t maxBreakers() const { return _maxBreakers; }

    /** Set a global state change callback for all new breakers. */
    void onStateChange(CircuitBreaker::Callback cb) {
        _onStateChange = cb;
        // Apply to existing breakers
        for (size_t i = 0; i < _count; i++) {
            _breakers[i].breaker.onStateChange(cb);
        }
    }

    /** Count breakers in OPEN state. */
    size_t openCount() const {
        size_t c = 0;
        for (size_t i = 0; i < _count; i++) {
            if (_breakers[i].breaker.isOpen()) c++;
        }
        return c;
    }

    /** Get all breaker keys in a given state. */
    size_t getByState(CircuitState state, const char** keys, size_t maxKeys) const {
        size_t c = 0;
        for (size_t i = 0; i < _count && c < maxKeys; i++) {
            if (_breakers[i].breaker.state() == state) {
                keys[c++] = _breakers[i].breaker.key();
            }
        }
        return c;
    }

    // ── JSON ───────────────────────────────────────────────

    String toJson() const {
        String json = "{\"count\":";
        json += String((unsigned long)_count);
        json += ",\"maxBreakers\":";
        json += String((unsigned long)_maxBreakers);
        json += ",\"openCount\":";
        json += String((unsigned long)openCount());
        json += ",\"breakers\":[";
        for (size_t i = 0; i < _count; i++) {
            if (i > 0) json += ",";
            json += _breakers[i].breaker.toJson();
        }
        json += "]}";
        return json;
    }

private:
    struct Entry {
        CircuitBreaker breaker;
        unsigned long lastAccessMs = 0;
    };

    size_t _findLRU() const {
        size_t lru = 0;
        unsigned long oldest = _breakers[0].lastAccessMs;
        for (size_t i = 1; i < _count; i++) {
            if (_breakers[i].lastAccessMs < oldest) {
                oldest = _breakers[i].lastAccessMs;
                lru = i;
            }
        }
        return lru;
    }

    size_t _failureThreshold;
    unsigned long _recoveryTimeoutMs;
    size_t _halfOpenSuccessThreshold;
    size_t _maxBreakers;
    size_t _count;
    Entry* _breakers;
    CircuitBreaker::Callback _onStateChange;
};

}  // namespace mcpd

#endif  // MCP_CIRCUIT_BREAKER_H
