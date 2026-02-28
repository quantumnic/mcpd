/**
 * MCPRetry — Configurable retry policies for mcpd tool calls
 *
 * Provides retry logic with exponential backoff and jitter for unreliable
 * MCU peripherals (I2C devices that NAK, flaky sensors, network timeouts).
 *
 * Features:
 *   - Exponential backoff with configurable base delay and multiplier
 *   - Optional jitter (full, equal, decorrelated) to avoid thundering herd
 *   - Maximum retry count and total timeout
 *   - Per-operation retry policies
 *   - Retry predicates (decide if error is retryable)
 *   - Callbacks (onRetry, onGiveUp)
 *   - Policy registry with LRU eviction for bounded memory
 *   - JSON serialization for diagnostics
 *   - Synchronous execute-with-retry helper
 *
 * Usage:
 *   mcpd::RetryPolicy policy;
 *   policy.maxRetries = 3;
 *   policy.baseDelayMs = 100;
 *
 *   mcpd::RetryExecutor executor(policy);
 *   auto result = executor.execute([]() -> mcpd::RetryResult {
 *       int val = readSensor();
 *       if (val < 0) return mcpd::RetryResult::retryable("sensor NAK");
 *       return mcpd::RetryResult::success(String(val));
 *   });
 */

#ifndef MCP_RETRY_H
#define MCP_RETRY_H

#include <Arduino.h>
#include <cstring>
#include <functional>

namespace mcpd {

// ── Jitter strategies ──────────────────────────────────────

enum class JitterMode : uint8_t {
    NONE = 0,       // No jitter — pure exponential backoff
    FULL = 1,       // [0, calculatedDelay)
    EQUAL = 2,      // calculatedDelay/2 + [0, calculatedDelay/2)
    DECORRELATED = 3 // min(maxDelay, random(baseDelay, lastDelay * 3))
};

// ── RetryResult ────────────────────────────────────────────

/**
 * Result of a retryable operation.
 */
class RetryResult {
public:
    bool succeeded;
    bool canRetry;
    char error[64];
    char value[128];

    static RetryResult success(const char* val = "") {
        RetryResult r;
        r.succeeded = true;
        r.canRetry = false;
        r.error[0] = '\0';
        strncpy(r.value, val, sizeof(r.value) - 1);
        r.value[sizeof(r.value) - 1] = '\0';
        return r;
    }

    static RetryResult retryable(const char* err = "error") {
        RetryResult r;
        r.succeeded = false;
        r.canRetry = true;
        strncpy(r.error, err, sizeof(r.error) - 1);
        r.error[sizeof(r.error) - 1] = '\0';
        r.value[0] = '\0';
        return r;
    }

    static RetryResult fatal(const char* err = "fatal error") {
        RetryResult r;
        r.succeeded = false;
        r.canRetry = false;
        strncpy(r.error, err, sizeof(r.error) - 1);
        r.error[sizeof(r.error) - 1] = '\0';
        r.value[0] = '\0';
        return r;
    }

private:
    RetryResult() : succeeded(false), canRetry(false) {
        error[0] = '\0';
        value[0] = '\0';
    }
};

// ── RetryPolicy ────────────────────────────────────────────

struct RetryPolicy {
    size_t maxRetries = 3;
    unsigned long baseDelayMs = 100;
    float multiplier = 2.0f;
    unsigned long maxDelayMs = 10000;
    unsigned long totalTimeoutMs = 0;  // 0 = no total timeout
    JitterMode jitter = JitterMode::NONE;

    RetryPolicy() = default;

    RetryPolicy(size_t retries, unsigned long baseMs, float mult = 2.0f,
                unsigned long maxMs = 10000, JitterMode j = JitterMode::NONE)
        : maxRetries(retries), baseDelayMs(baseMs), multiplier(mult),
          maxDelayMs(maxMs), totalTimeoutMs(0), jitter(j) {}

    /**
     * Calculate delay for a given attempt (0-based).
     */
    unsigned long delayForAttempt(size_t attempt, unsigned long lastDelayMs = 0) const {
        // Exponential backoff
        unsigned long d = baseDelayMs;
        for (size_t i = 0; i < attempt; i++) {
            d = (unsigned long)(d * multiplier);
            if (d > maxDelayMs) { d = maxDelayMs; break; }
        }
        if (d > maxDelayMs) d = maxDelayMs;

        // Apply jitter
        switch (jitter) {
            case JitterMode::NONE:
                return d;
            case JitterMode::FULL:
                return d > 0 ? (unsigned long)(random(0, (long)d)) : 0;
            case JitterMode::EQUAL: {
                unsigned long half = d / 2;
                return half + (half > 0 ? (unsigned long)(random(0, (long)half)) : 0);
            }
            case JitterMode::DECORRELATED: {
                unsigned long prev = lastDelayMs > 0 ? lastDelayMs : baseDelayMs;
                unsigned long upper = prev * 3;
                if (upper > maxDelayMs) upper = maxDelayMs;
                unsigned long lower = baseDelayMs;
                if (lower > upper) lower = upper;
                return lower + (upper > lower ? (unsigned long)(random(0, (long)(upper - lower))) : 0);
            }
        }
        return d;
    }

    const char* jitterStr() const {
        switch (jitter) {
            case JitterMode::NONE: return "none";
            case JitterMode::FULL: return "full";
            case JitterMode::EQUAL: return "equal";
            case JitterMode::DECORRELATED: return "decorrelated";
        }
        return "none";
    }

    String toJson() const {
        String json = "{\"maxRetries\":";
        json += String((unsigned long)maxRetries);
        json += ",\"baseDelayMs\":";
        json += String(baseDelayMs);
        json += ",\"multiplier\":";
        json += String(multiplier, 1);
        json += ",\"maxDelayMs\":";
        json += String(maxDelayMs);
        json += ",\"totalTimeoutMs\":";
        json += String(totalTimeoutMs);
        json += ",\"jitter\":\"";
        json += jitterStr();
        json += "\"}";
        return json;
    }
};

// ── RetryStats ─────────────────────────────────────────────

struct RetryStats {
    size_t totalAttempts = 0;
    size_t totalSuccesses = 0;
    size_t totalRetries = 0;
    size_t totalFailures = 0;    // gave up after max retries
    size_t totalFatalErrors = 0; // non-retryable errors
    size_t totalTimeouts = 0;    // total timeout exceeded
    unsigned long totalDelayMs = 0;

    void reset() {
        totalAttempts = 0;
        totalSuccesses = 0;
        totalRetries = 0;
        totalFailures = 0;
        totalFatalErrors = 0;
        totalTimeouts = 0;
        totalDelayMs = 0;
    }

    String toJson() const {
        String json = "{\"totalAttempts\":";
        json += String((unsigned long)totalAttempts);
        json += ",\"totalSuccesses\":";
        json += String((unsigned long)totalSuccesses);
        json += ",\"totalRetries\":";
        json += String((unsigned long)totalRetries);
        json += ",\"totalFailures\":";
        json += String((unsigned long)totalFailures);
        json += ",\"totalFatalErrors\":";
        json += String((unsigned long)totalFatalErrors);
        json += ",\"totalTimeouts\":";
        json += String((unsigned long)totalTimeouts);
        json += ",\"totalDelayMs\":";
        json += String(totalDelayMs);
        json += "}";
        return json;
    }
};

// ── RetryExecutor ──────────────────────────────────────────

/**
 * Executes an operation with retry logic.
 */
class RetryExecutor {
public:
    using Operation = std::function<RetryResult()>;
    using RetryCallback = std::function<void(size_t attempt, const char* error, unsigned long delayMs)>;
    using GiveUpCallback = std::function<void(size_t attempts, const char* lastError)>;

    explicit RetryExecutor(const RetryPolicy& policy = RetryPolicy())
        : _policy(policy) {}

    void setPolicy(const RetryPolicy& policy) { _policy = policy; }
    const RetryPolicy& policy() const { return _policy; }
    const RetryStats& stats() const { return _stats; }
    void resetStats() { _stats.reset(); }

    void onRetry(RetryCallback cb) { _onRetry = cb; }
    void onGiveUp(GiveUpCallback cb) { _onGiveUp = cb; }

    /**
     * Execute an operation with retry.
     * Blocks for the duration of retries (uses delay()).
     */
    RetryResult execute(Operation op) {
        unsigned long startMs = millis();
        unsigned long lastDelayMs = 0;

        for (size_t attempt = 0; attempt <= _policy.maxRetries; attempt++) {
            _stats.totalAttempts++;

            // Check total timeout before attempt
            if (_policy.totalTimeoutMs > 0 && attempt > 0) {
                unsigned long elapsed = millis() - startMs;
                if (elapsed >= _policy.totalTimeoutMs) {
                    _stats.totalTimeouts++;
                    return RetryResult::fatal("total timeout exceeded");
                }
            }

            RetryResult result = op();

            if (result.succeeded) {
                _stats.totalSuccesses++;
                return result;
            }

            if (!result.canRetry) {
                _stats.totalFatalErrors++;
                return result;
            }

            // Can retry — check if we have attempts left
            if (attempt >= _policy.maxRetries) {
                _stats.totalFailures++;
                if (_onGiveUp) _onGiveUp(attempt + 1, result.error);
                return result;
            }

            // Calculate and apply delay
            unsigned long d = _policy.delayForAttempt(attempt, lastDelayMs);

            // Clamp delay to remaining total timeout
            if (_policy.totalTimeoutMs > 0) {
                unsigned long elapsed = millis() - startMs;
                unsigned long remaining = _policy.totalTimeoutMs > elapsed ?
                                          _policy.totalTimeoutMs - elapsed : 0;
                if (d > remaining) d = remaining;
            }

            _stats.totalRetries++;
            _stats.totalDelayMs += d;
            lastDelayMs = d;

            if (_onRetry) _onRetry(attempt, result.error, d);

            if (d > 0) delay(d);
        }

        // Should not reach here, but safety
        _stats.totalFailures++;
        return RetryResult::fatal("max retries exceeded");
    }

private:
    RetryPolicy _policy;
    RetryStats _stats;
    RetryCallback _onRetry;
    GiveUpCallback _onGiveUp;
};

// ── RetryPolicyRegistry ────────────────────────────────────

/**
 * Named registry of retry policies with LRU eviction.
 * Assign per-tool or per-peripheral retry configurations.
 */
class RetryPolicyRegistry {
public:
    explicit RetryPolicyRegistry(size_t maxPolicies = 16)
        : _maxPolicies(maxPolicies), _count(0) {
        _entries = new Entry[maxPolicies];
    }

    ~RetryPolicyRegistry() { delete[] _entries; }

    // Non-copyable
    RetryPolicyRegistry(const RetryPolicyRegistry&) = delete;
    RetryPolicyRegistry& operator=(const RetryPolicyRegistry&) = delete;

    /**
     * Set a retry policy for a named key.
     */
    void set(const char* key, const RetryPolicy& policy) {
        // Check if key exists
        for (size_t i = 0; i < _count; i++) {
            if (strcmp(_entries[i].key, key) == 0) {
                _entries[i].policy = policy;
                _entries[i].lastAccessMs = millis();
                _entries[i].stats.reset();
                return;
            }
        }
        // Add new or evict LRU
        size_t idx;
        if (_count < _maxPolicies) {
            idx = _count++;
        } else {
            idx = _findLRU();
        }
        strncpy(_entries[idx].key, key, sizeof(_entries[idx].key) - 1);
        _entries[idx].key[sizeof(_entries[idx].key) - 1] = '\0';
        _entries[idx].policy = policy;
        _entries[idx].stats.reset();
        _entries[idx].lastAccessMs = millis();
    }

    /**
     * Get a retry policy for a key (or nullptr if not found).
     */
    const RetryPolicy* get(const char* key) const {
        for (size_t i = 0; i < _count; i++) {
            if (strcmp(_entries[i].key, key) == 0) {
                return &_entries[i].policy;
            }
        }
        return nullptr;
    }

    /**
     * Check if a key has a policy registered.
     */
    bool has(const char* key) const {
        return get(key) != nullptr;
    }

    /**
     * Execute an operation using the registered policy for a key.
     * Falls back to defaultPolicy if key not found.
     */
    RetryResult execute(const char* key, RetryExecutor::Operation op,
                        const RetryPolicy& defaultPolicy = RetryPolicy()) {
        const RetryPolicy* p = get(key);
        RetryPolicy policy = p ? *p : defaultPolicy;

        // Find or create stats entry
        Entry* entry = _findEntry(key);
        if (!entry) {
            // Create a temporary executor
            RetryExecutor executor(policy);
            RetryResult result = executor.execute(op);
            return result;
        }

        entry->lastAccessMs = millis();
        RetryExecutor executor(policy);
        RetryResult result = executor.execute(op);

        // Accumulate stats
        const RetryStats& es = executor.stats();
        entry->stats.totalAttempts += es.totalAttempts;
        entry->stats.totalSuccesses += es.totalSuccesses;
        entry->stats.totalRetries += es.totalRetries;
        entry->stats.totalFailures += es.totalFailures;
        entry->stats.totalFatalErrors += es.totalFatalErrors;
        entry->stats.totalTimeouts += es.totalTimeouts;
        entry->stats.totalDelayMs += es.totalDelayMs;

        return result;
    }

    /**
     * Get accumulated stats for a key.
     */
    const RetryStats* stats(const char* key) const {
        for (size_t i = 0; i < _count; i++) {
            if (strcmp(_entries[i].key, key) == 0) {
                return &_entries[i].stats;
            }
        }
        return nullptr;
    }

    /**
     * Remove a policy by key.
     */
    bool remove(const char* key) {
        for (size_t i = 0; i < _count; i++) {
            if (strcmp(_entries[i].key, key) == 0) {
                // Shift remaining
                for (size_t j = i; j + 1 < _count; j++) {
                    _entries[j] = _entries[j + 1];
                }
                _count--;
                return true;
            }
        }
        return false;
    }

    /**
     * Reset stats for all entries.
     */
    void resetAllStats() {
        for (size_t i = 0; i < _count; i++) {
            _entries[i].stats.reset();
        }
    }

    void clear() { _count = 0; }
    size_t count() const { return _count; }
    size_t maxPolicies() const { return _maxPolicies; }

    /**
     * Get all registered keys.
     */
    size_t keys(const char** out, size_t maxKeys) const {
        size_t n = maxKeys < _count ? maxKeys : _count;
        for (size_t i = 0; i < n; i++) {
            out[i] = _entries[i].key;
        }
        return n;
    }

    // ── JSON ───────────────────────────────────────────────

    String toJson() const {
        String json = "{\"count\":";
        json += String((unsigned long)_count);
        json += ",\"maxPolicies\":";
        json += String((unsigned long)_maxPolicies);
        json += ",\"entries\":[";
        for (size_t i = 0; i < _count; i++) {
            if (i > 0) json += ",";
            json += "{\"key\":\"";
            json += _entries[i].key;
            json += "\",\"policy\":";
            json += _entries[i].policy.toJson();
            json += ",\"stats\":";
            json += _entries[i].stats.toJson();
            json += "}";
        }
        json += "]}";
        return json;
    }

private:
    struct Entry {
        char key[32] = {0};
        RetryPolicy policy;
        RetryStats stats;
        unsigned long lastAccessMs = 0;
    };

    Entry* _findEntry(const char* key) {
        for (size_t i = 0; i < _count; i++) {
            if (strcmp(_entries[i].key, key) == 0) {
                return &_entries[i];
            }
        }
        return nullptr;
    }

    size_t _findLRU() const {
        size_t lru = 0;
        unsigned long oldest = _entries[0].lastAccessMs;
        for (size_t i = 1; i < _count; i++) {
            if (_entries[i].lastAccessMs < oldest) {
                oldest = _entries[i].lastAccessMs;
                lru = i;
            }
        }
        return lru;
    }

    size_t _maxPolicies;
    size_t _count;
    Entry* _entries;
};

}  // namespace mcpd

#endif  // MCP_RETRY_H
