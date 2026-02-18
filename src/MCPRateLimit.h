/**
 * MCPRateLimit — Request rate limiting for mcpd
 *
 * Protects embedded devices from being overwhelmed by too many requests.
 * Uses a token bucket algorithm — efficient, constant memory, O(1) per check.
 *
 * Usage:
 *   server.setRateLimit(10, 5);  // 10 requests/sec, burst of 5
 *   // Requests exceeding the limit get a JSON-RPC -32000 error
 */

#ifndef MCP_RATE_LIMIT_H
#define MCP_RATE_LIMIT_H

#include <Arduino.h>

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
    void disable() { _enabled = false; }

    /** Check if rate limiting is enabled */
    bool isEnabled() const { return _enabled; }

    /**
     * Try to consume a token. Returns true if allowed, false if rate limited.
     */
    bool tryAcquire() {
        if (!_enabled) return true;

        _refill();

        if (_tokens >= 1.0f) {
            _tokens -= 1.0f;
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
    void resetStats() { _totalAllowed = 0; _totalDenied = 0; }

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

} // namespace mcpd

#endif // MCP_RATE_LIMIT_H
