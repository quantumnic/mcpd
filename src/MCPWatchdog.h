/**
 * mcpd — Software Watchdog (Task Health Monitor)
 *
 * Lightweight software watchdog for microcontrollers. Register named tasks
 * with timeout deadlines. Tasks must periodically "kick" (feed) the watchdog
 * to prove they're alive. If a task misses its deadline, the watchdog fires
 * a timeout callback, enabling recovery actions (restart task, log error,
 * reboot device, etc.).
 *
 * Designed for embedded systems where hardware watchdogs are too coarse
 * (single system-wide timer) and you need per-task health monitoring.
 *
 * Features:
 *   - Named watchdog entries with configurable timeout (ms)
 *   - Kick (feed) to reset the deadline
 *   - Per-task timeout callbacks
 *   - Global timeout listener
 *   - Pause/resume individual watchdogs
 *   - Task state tracking (healthy, expired, paused)
 *   - Timeout count statistics
 *   - Bounded number of entries (MCU-friendly)
 *   - JSON serialization of all watchdog state
 *   - check() scans all entries and fires callbacks for expired ones
 *
 * Usage:
 *   mcpd::Watchdog wd(16);
 *
 *   wd.add("sensor_loop", 5000);   // 5s timeout
 *   wd.add("comms", 10000);        // 10s timeout
 *
 *   wd.onTimeout([](const char* name, uint32_t count) {
 *       Serial.printf("WATCHDOG: %s timed out (%u times)\n", name, count);
 *   });
 *
 *   // In sensor loop:
 *   wd.kick("sensor_loop");
 *
 *   // In main loop:
 *   wd.check(millis());
 */

#ifndef MCPD_WATCHDOG_H
#define MCPD_WATCHDOG_H

#include <cstdint>
#include <cstring>
#include <cstdio>

namespace mcpd {

enum class WatchdogState : uint8_t {
    Healthy,
    Expired,
    Paused
};

inline const char* watchdogStateToString(WatchdogState s) {
    switch (s) {
        case WatchdogState::Healthy: return "healthy";
        case WatchdogState::Expired: return "expired";
        case WatchdogState::Paused:  return "paused";
        default: return "unknown";
    }
}

class Watchdog {
public:
    using TimeoutCallback = void (*)(const char* name, uint32_t timeoutCount);
    using PerTaskCallback = void (*)(const char* name);

    explicit Watchdog(size_t maxEntries = 16)
        : maxEntries_(maxEntries), count_(0), globalCb_(nullptr) {
        entries_ = new Entry[maxEntries_];
    }

    ~Watchdog() {
        delete[] entries_;
    }

    // Non-copyable
    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;

    /**
     * Add a watchdog entry.
     * @param name   Unique task name (max 31 chars)
     * @param timeoutMs  Timeout in milliseconds
     * @param cb     Optional per-task timeout callback
     * @return true if added successfully
     */
    bool add(const char* name, uint32_t timeoutMs, PerTaskCallback cb = nullptr) {
        if (!name || timeoutMs == 0) return false;
        if (find(name) >= 0) return false;  // duplicate
        if (count_ >= maxEntries_) return false;

        Entry& e = entries_[count_++];
        strncpy(e.name, name, sizeof(e.name) - 1);
        e.name[sizeof(e.name) - 1] = '\0';
        e.timeoutMs = timeoutMs;
        e.lastKickMs = 0;
        e.started = false;
        e.state = WatchdogState::Healthy;
        e.timeoutCount = 0;
        e.callback = cb;
        return true;
    }

    /**
     * Remove a watchdog entry by name.
     * @return true if removed
     */
    bool remove(const char* name) {
        int idx = find(name);
        if (idx < 0) return false;
        // Shift remaining entries
        for (size_t i = (size_t)idx; i + 1 < count_; i++) {
            entries_[i] = entries_[i + 1];
        }
        count_--;
        return true;
    }

    /**
     * Kick (feed) a watchdog to reset its deadline.
     * @param name   Task name
     * @param nowMs  Current time in milliseconds
     * @return true if found and kicked
     */
    bool kick(const char* name, uint32_t nowMs) {
        int idx = find(name);
        if (idx < 0) return false;
        Entry& e = entries_[idx];
        if (e.state == WatchdogState::Paused) return false;
        e.lastKickMs = nowMs;
        e.started = true;
        e.state = WatchdogState::Healthy;
        return true;
    }

    /**
     * Check all entries for timeouts. Call periodically from main loop.
     * @param nowMs  Current time in milliseconds
     * @return Number of entries that timed out in this check
     */
    size_t check(uint32_t nowMs) {
        size_t fired = 0;
        for (size_t i = 0; i < count_; i++) {
            Entry& e = entries_[i];
            if (e.state == WatchdogState::Paused) continue;
            if (!e.started) continue;

            uint32_t elapsed = nowMs - e.lastKickMs;
            if (elapsed >= e.timeoutMs) {
                if (e.state != WatchdogState::Expired) {
                    e.state = WatchdogState::Expired;
                    e.timeoutCount++;
                    fired++;
                    if (e.callback) e.callback(e.name);
                    if (globalCb_) globalCb_(e.name, e.timeoutCount);
                }
            }
        }
        return fired;
    }

    /**
     * Pause a watchdog entry (won't fire while paused).
     */
    bool pause(const char* name) {
        int idx = find(name);
        if (idx < 0) return false;
        entries_[idx].state = WatchdogState::Paused;
        return true;
    }

    /**
     * Resume a paused watchdog entry.
     * @param nowMs  Current time (resets the deadline)
     */
    bool resume(const char* name, uint32_t nowMs) {
        int idx = find(name);
        if (idx < 0) return false;
        Entry& e = entries_[idx];
        if (e.state != WatchdogState::Paused) return false;
        e.state = WatchdogState::Healthy;
        e.lastKickMs = nowMs;
        e.started = true;
        return true;
    }

    /**
     * Get the state of a watchdog entry.
     */
    WatchdogState state(const char* name) const {
        int idx = find(name);
        if (idx < 0) return WatchdogState::Expired;
        return entries_[idx].state;
    }

    /**
     * Get timeout count for a watchdog entry.
     */
    uint32_t timeoutCount(const char* name) const {
        int idx = find(name);
        if (idx < 0) return 0;
        return entries_[idx].timeoutCount;
    }

    /**
     * Get the timeout (ms) for a watchdog entry.
     */
    uint32_t timeout(const char* name) const {
        int idx = find(name);
        if (idx < 0) return 0;
        return entries_[idx].timeoutMs;
    }

    /**
     * Update the timeout for an existing entry.
     */
    bool setTimeout(const char* name, uint32_t timeoutMs) {
        if (timeoutMs == 0) return false;
        int idx = find(name);
        if (idx < 0) return false;
        entries_[idx].timeoutMs = timeoutMs;
        return true;
    }

    /**
     * Reset timeout count for an entry.
     */
    bool resetCount(const char* name) {
        int idx = find(name);
        if (idx < 0) return false;
        entries_[idx].timeoutCount = 0;
        return true;
    }

    /**
     * Set global timeout listener.
     */
    void onTimeout(TimeoutCallback cb) {
        globalCb_ = cb;
    }

    /**
     * Check if a named entry exists.
     */
    bool exists(const char* name) const {
        return find(name) >= 0;
    }

    /**
     * Number of registered entries.
     */
    size_t count() const { return count_; }

    /**
     * Maximum capacity.
     */
    size_t capacity() const { return maxEntries_; }

    /**
     * Number of currently expired entries.
     */
    size_t expiredCount() const {
        size_t n = 0;
        for (size_t i = 0; i < count_; i++) {
            if (entries_[i].state == WatchdogState::Expired) n++;
        }
        return n;
    }

    /**
     * Number of healthy entries.
     */
    size_t healthyCount() const {
        size_t n = 0;
        for (size_t i = 0; i < count_; i++) {
            if (entries_[i].state == WatchdogState::Healthy) n++;
        }
        return n;
    }

    /**
     * Number of paused entries.
     */
    size_t pausedCount() const {
        size_t n = 0;
        for (size_t i = 0; i < count_; i++) {
            if (entries_[i].state == WatchdogState::Paused) n++;
        }
        return n;
    }

    /**
     * Remove all entries.
     */
    void clear() {
        count_ = 0;
    }

    /**
     * Serialize all watchdog state to JSON.
     * @param buf    Output buffer
     * @param bufLen Buffer size
     * @return Number of characters written (excluding null terminator)
     */
    int toJson(char* buf, size_t bufLen) const {
        if (!buf || bufLen == 0) return 0;

        int pos = 0;
        auto append = [&](const char* s) {
            while (*s && pos + 1 < (int)bufLen) {
                buf[pos++] = *s++;
            }
        };

        append("{\"entries\":[");

        for (size_t i = 0; i < count_; i++) {
            if (i > 0) append(",");

            const Entry& e = entries_[i];
            char tmp[256];
            snprintf(tmp, sizeof(tmp),
                "{\"name\":\"%s\",\"timeoutMs\":%u,\"state\":\"%s\","
                "\"timeoutCount\":%u,\"started\":%s}",
                e.name, (unsigned)e.timeoutMs,
                watchdogStateToString(e.state),
                (unsigned)e.timeoutCount,
                e.started ? "true" : "false");
            append(tmp);
        }

        append("],\"count\":");
        char countBuf[16];
        snprintf(countBuf, sizeof(countBuf), "%u", (unsigned)count_);
        append(countBuf);

        append(",\"capacity\":");
        snprintf(countBuf, sizeof(countBuf), "%u", (unsigned)maxEntries_);
        append(countBuf);

        append("}");
        buf[pos] = '\0';
        return pos;
    }

private:
    struct Entry {
        char name[32];
        uint32_t timeoutMs;
        uint32_t lastKickMs;
        bool started;
        WatchdogState state;
        uint32_t timeoutCount;
        PerTaskCallback callback;

        Entry() : timeoutMs(0), lastKickMs(0), started(false),
                  state(WatchdogState::Healthy), timeoutCount(0), callback(nullptr) {
            name[0] = '\0';
        }
    };

    int find(const char* name) const {
        if (!name) return -1;
        for (size_t i = 0; i < count_; i++) {
            if (strcmp(entries_[i].name, name) == 0) return (int)i;
        }
        return -1;
    }

    Entry* entries_;
    size_t maxEntries_;
    size_t count_;
    TimeoutCallback globalCb_;
};

} // namespace mcpd

#endif // MCPD_WATCHDOG_H
