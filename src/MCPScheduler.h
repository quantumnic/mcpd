/**
 * mcpd — Lightweight Task Scheduler
 *
 * Cron-like periodic execution for microcontrollers.
 * Schedule callbacks or tool invocations at fixed intervals.
 *
 * Usage:
 *   mcpd::Scheduler scheduler;
 *   scheduler.every(5000, []() { readSensor(); });              // every 5s
 *   scheduler.every(60000, []() { checkBattery(); }, "battery"); // named task
 *   scheduler.at(millis() + 10000, []() { calibrate(); });      // one-shot
 *   // In loop():
 *   scheduler.loop();
 *
 * Features:
 *   - Named tasks for management (pause/resume/remove/reschedule)
 *   - One-shot and repeating schedules
 *   - Execution count tracking
 *   - Jitter compensation (catches up missed intervals)
 *   - Max concurrent task limit (memory-safe for MCUs)
 *   - Optional MCP tool integration (expose scheduler status as a resource)
 */

#ifndef MCPD_SCHEDULER_H
#define MCPD_SCHEDULER_H

#include <Arduino.h>
#include <functional>
#include <vector>
#include <algorithm>

namespace mcpd {

/**
 * A scheduled task entry.
 */
struct ScheduledTask {
    String name;                         ///< Optional name for management
    std::function<void()> callback;      ///< Function to execute
    unsigned long intervalMs = 0;        ///< Repeat interval (0 = one-shot)
    unsigned long nextRunMs = 0;         ///< Next scheduled execution (millis)
    unsigned long lastRunMs = 0;         ///< Last execution time
    unsigned long execCount = 0;         ///< Number of times executed
    unsigned long maxExecutions = 0;     ///< Max runs before auto-remove (0 = unlimited)
    bool paused = false;                 ///< Paused tasks are skipped
    bool oneShot = false;                ///< Remove after first execution
    bool active = true;                  ///< Internal: false = pending removal
};

/**
 * Lightweight task scheduler for microcontrollers.
 */
class Scheduler {
public:
    static constexpr size_t DEFAULT_MAX_TASKS = 32;

    explicit Scheduler(size_t maxTasks = DEFAULT_MAX_TASKS)
        : _maxTasks(maxTasks) {}

    /**
     * Schedule a repeating task.
     * @param intervalMs  Interval in milliseconds between executions
     * @param callback    Function to call
     * @param name        Optional name for management
     * @return Index of the task, or -1 if full
     */
    int every(unsigned long intervalMs, std::function<void()> callback,
              const char* name = "") {
        if (_tasks.size() >= _maxTasks) return -1;
        if (intervalMs == 0) return -1;

        ScheduledTask task;
        task.name = name ? name : "";
        task.callback = callback;
        task.intervalMs = intervalMs;
        task.nextRunMs = millis() + intervalMs;
        task.oneShot = false;

        _tasks.push_back(task);
        return (int)_tasks.size() - 1;
    }

    /**
     * Schedule a one-shot task.
     * @param atMs      Absolute millis() time to execute
     * @param callback  Function to call
     * @param name      Optional name
     * @return Index of the task, or -1 if full
     */
    int at(unsigned long atMs, std::function<void()> callback,
           const char* name = "") {
        if (_tasks.size() >= _maxTasks) return -1;

        ScheduledTask task;
        task.name = name ? name : "";
        task.callback = callback;
        task.intervalMs = 0;
        task.nextRunMs = atMs;
        task.oneShot = true;

        _tasks.push_back(task);
        return (int)_tasks.size() - 1;
    }

    /**
     * Schedule a task that runs N times then auto-removes.
     * @param intervalMs     Interval between executions
     * @param maxExecutions  Number of times to run
     * @param callback       Function to call
     * @param name           Optional name
     * @return Index of the task, or -1 if full
     */
    int times(unsigned long intervalMs, unsigned long maxExecutions,
              std::function<void()> callback, const char* name = "") {
        int idx = every(intervalMs, callback, name);
        if (idx >= 0) {
            _tasks[(size_t)idx].maxExecutions = maxExecutions;
        }
        return idx;
    }

    /**
     * Process pending tasks. Call this in loop().
     * @return Number of tasks executed this cycle
     */
    int loop() {
        unsigned long now = millis();
        int executed = 0;

        for (size_t i = 0; i < _tasks.size(); i++) {
            auto& task = _tasks[i];
            if (!task.active || task.paused) continue;

            if (now >= task.nextRunMs) {
                // Execute
                if (task.callback) {
                    task.callback();
                }
                task.lastRunMs = now;
                task.execCount++;
                executed++;

                if (task.oneShot) {
                    task.active = false;
                } else if (task.maxExecutions > 0 && task.execCount >= task.maxExecutions) {
                    task.active = false;
                } else {
                    // Schedule next run (with jitter compensation)
                    task.nextRunMs = now + task.intervalMs;
                }
            }
        }

        // Garbage collect inactive tasks
        if (executed > 0) {
            _tasks.erase(
                std::remove_if(_tasks.begin(), _tasks.end(),
                    [](const ScheduledTask& t) { return !t.active; }),
                _tasks.end()
            );
        }

        return executed;
    }

    // ── Task management ────────────────────────────────────────────

    /**
     * Pause a task by name.
     * @return true if found and paused
     */
    bool pause(const char* name) {
        auto* t = _findByName(name);
        if (!t) return false;
        t->paused = true;
        return true;
    }

    /**
     * Resume a paused task by name.
     * @return true if found and resumed
     */
    bool resume(const char* name) {
        auto* t = _findByName(name);
        if (!t) return false;
        t->paused = false;
        return true;
    }

    /**
     * Remove a task by name.
     * @return true if found and removed
     */
    bool remove(const char* name) {
        auto* t = _findByName(name);
        if (!t) return false;
        t->active = false;
        return true;
    }

    /**
     * Remove a task by index.
     * @return true if valid index and removed
     */
    bool removeByIndex(int index) {
        if (index < 0 || (size_t)index >= _tasks.size()) return false;
        _tasks[(size_t)index].active = false;
        return true;
    }

    /**
     * Reschedule a task to a new interval.
     * @return true if found and rescheduled
     */
    bool reschedule(const char* name, unsigned long newIntervalMs) {
        auto* t = _findByName(name);
        if (!t || newIntervalMs == 0) return false;
        t->intervalMs = newIntervalMs;
        t->nextRunMs = millis() + newIntervalMs;
        return true;
    }

    /**
     * Check if a named task exists and is active.
     */
    bool exists(const char* name) const {
        for (const auto& t : _tasks) {
            if (t.active && t.name == name) return true;
        }
        return false;
    }

    /**
     * Get execution count for a named task.
     * @return execution count, or 0 if not found
     */
    unsigned long execCount(const char* name) const {
        for (const auto& t : _tasks) {
            if (t.active && t.name == name) return t.execCount;
        }
        return 0;
    }

    /**
     * Get number of active tasks.
     */
    size_t count() const {
        size_t n = 0;
        for (const auto& t : _tasks) {
            if (t.active) n++;
        }
        return n;
    }

    /**
     * Get total number of tasks (including paused).
     */
    size_t size() const { return _tasks.size(); }

    /**
     * Get maximum allowed tasks.
     */
    size_t maxTasks() const { return _maxTasks; }

    /**
     * Remove all tasks.
     */
    void clear() { _tasks.clear(); }

    /**
     * Get task info by name (const access).
     * @return pointer to task or nullptr
     */
    const ScheduledTask* get(const char* name) const {
        for (const auto& t : _tasks) {
            if (t.active && t.name == name) return &t;
        }
        return nullptr;
    }

    /**
     * Get task info by index (const access).
     * @return pointer to task or nullptr
     */
    const ScheduledTask* getByIndex(int index) const {
        if (index < 0 || (size_t)index >= _tasks.size()) return nullptr;
        return _tasks[(size_t)index].active ? &_tasks[(size_t)index] : nullptr;
    }

    /**
     * Serialize scheduler status to JSON string.
     * Useful for exposing as an MCP resource.
     */
    String toJSON() const {
        String json = "{\"taskCount\":";
        json += String((unsigned long)count());
        json += ",\"maxTasks\":";
        json += String((unsigned long)_maxTasks);
        json += ",\"tasks\":[";

        bool first = true;
        for (const auto& t : _tasks) {
            if (!t.active) continue;
            if (!first) json += ",";
            first = false;

            json += "{\"name\":\"";
            json += t.name;
            json += "\",\"intervalMs\":";
            json += String(t.intervalMs);
            json += ",\"execCount\":";
            json += String(t.execCount);
            json += ",\"paused\":";
            json += t.paused ? "true" : "false";
            json += ",\"oneShot\":";
            json += t.oneShot ? "true" : "false";
            if (t.maxExecutions > 0) {
                json += ",\"maxExecutions\":";
                json += String(t.maxExecutions);
            }
            json += "}";
        }

        json += "]}";
        return json;
    }

private:
    std::vector<ScheduledTask> _tasks;
    size_t _maxTasks;

    ScheduledTask* _findByName(const char* name) {
        if (!name || strlen(name) == 0) return nullptr;
        for (auto& t : _tasks) {
            if (t.active && t.name == name) return &t;
        }
        return nullptr;
    }
};

} // namespace mcpd

#endif // MCPD_SCHEDULER_H
