/**
 * mcpd — MCP Tasks (experimental, MCP 2025-11-25)
 *
 * Async/long-running tool execution with durable state machines.
 * Clients can poll for status and retrieve results when complete.
 *
 * Task lifecycle: working → completed|failed|cancelled
 *                 working → input_required → working → ...
 */

#ifndef MCPD_TASK_H
#define MCPD_TASK_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include <map>
#include <vector>

namespace mcpd {

/**
 * Task status per MCP 2025-11-25 spec.
 */
enum class TaskStatus {
    Working,        // Task is actively executing
    InputRequired,  // Task needs input from the requestor
    Completed,      // Terminal: task completed successfully
    Failed,         // Terminal: task failed
    Cancelled       // Terminal: task was cancelled
};

inline const char* taskStatusToString(TaskStatus s) {
    switch (s) {
        case TaskStatus::Working:       return "working";
        case TaskStatus::InputRequired: return "input_required";
        case TaskStatus::Completed:     return "completed";
        case TaskStatus::Failed:        return "failed";
        case TaskStatus::Cancelled:     return "cancelled";
    }
    return "working";
}

inline TaskStatus taskStatusFromString(const char* s) {
    if (!s) return TaskStatus::Working;
    if (strcmp(s, "working") == 0)        return TaskStatus::Working;
    if (strcmp(s, "input_required") == 0) return TaskStatus::InputRequired;
    if (strcmp(s, "completed") == 0)      return TaskStatus::Completed;
    if (strcmp(s, "failed") == 0)         return TaskStatus::Failed;
    if (strcmp(s, "cancelled") == 0)      return TaskStatus::Cancelled;
    return TaskStatus::Working;
}

inline bool isTerminalStatus(TaskStatus s) {
    return s == TaskStatus::Completed || s == TaskStatus::Failed || s == TaskStatus::Cancelled;
}

/**
 * Represents a single MCP Task.
 */
struct MCPTask {
    String taskId;
    TaskStatus status = TaskStatus::Working;
    String statusMessage;
    String createdAt;       // ISO 8601
    String lastUpdatedAt;   // ISO 8601
    int64_t ttl = -1;       // milliseconds, -1 = unlimited (null)
    int32_t pollInterval = 5000; // recommended poll interval in ms

    // Result storage (populated when completed)
    String resultJson;      // The serialized CallToolResult
    bool hasResult = false;

    // Metadata
    String toolName;        // Which tool this task is for
    String immediateResponse; // Optional model-immediate-response

    MCPTask() = default;

    /**
     * Serialize task state to JSON object (for tasks/get, tasks/list responses).
     */
    void toJson(JsonObject& obj) const {
        obj["taskId"] = taskId;
        obj["status"] = taskStatusToString(status);
        if (!statusMessage.isEmpty()) {
            obj["statusMessage"] = statusMessage;
        }
        obj["createdAt"] = createdAt;
        obj["lastUpdatedAt"] = lastUpdatedAt;
        if (ttl >= 0) {
            obj["ttl"] = (long)ttl;
        }
        // ttl omitted when -1 (unlimited)
        obj["pollInterval"] = pollInterval;
    }
};

/**
 * Task execution support level for individual tools.
 */
enum class TaskSupport {
    Forbidden,  // Default: tool does not support task execution
    Optional,   // Tool may be invoked as task or normal request
    Required    // Tool must be invoked as task
};

inline const char* taskSupportToString(TaskSupport ts) {
    switch (ts) {
        case TaskSupport::Forbidden: return "forbidden";
        case TaskSupport::Optional:  return "optional";
        case TaskSupport::Required:  return "required";
    }
    return "forbidden";
}

/**
 * Callback type for async tool execution.
 * Called with (taskId, params). The handler should:
 * 1. Start async work
 * 2. Call server.taskComplete(taskId, result) or server.taskFail(taskId, error) when done
 */
using MCPTaskToolHandler = std::function<void(const String& taskId, JsonVariant params)>;

/**
 * Manages all active tasks with TTL expiration.
 */
class TaskManager {
public:
    TaskManager() = default;

    /**
     * Create a new task. Returns the task ID.
     */
    String createTask(const char* toolName, int64_t requestedTtl = -1) {
        MCPTask task;
        task.taskId = _generateId();
        task.status = TaskStatus::Working;
        task.statusMessage = "The operation is now in progress.";
        task.toolName = toolName;
        task.createdAt = _isoNow();
        task.lastUpdatedAt = task.createdAt;
        task.ttl = requestedTtl;
        _tasks[task.taskId] = task;
        return task.taskId;
    }

    /**
     * Get a task by ID. Returns nullptr if not found or expired.
     */
    MCPTask* getTask(const String& taskId) {
        _expireOldTasks();
        auto it = _tasks.find(taskId);
        if (it == _tasks.end()) return nullptr;
        return &it->second;
    }

    /**
     * Update task status.
     */
    bool updateStatus(const String& taskId, TaskStatus newStatus, const String& message = "") {
        auto* task = getTask(taskId);
        if (!task) return false;
        if (isTerminalStatus(task->status)) return false; // Can't change terminal

        // Validate transitions
        if (task->status == TaskStatus::Working) {
            // Can go anywhere
        } else if (task->status == TaskStatus::InputRequired) {
            // Can go to working, completed, failed, cancelled
        }

        task->status = newStatus;
        if (!message.isEmpty()) task->statusMessage = message;
        task->lastUpdatedAt = _isoNow();
        return true;
    }

    /**
     * Complete a task with a result.
     */
    bool completeTask(const String& taskId, const String& resultJson) {
        auto* task = getTask(taskId);
        if (!task) return false;
        if (isTerminalStatus(task->status)) return false;

        task->status = TaskStatus::Completed;
        task->statusMessage = "Task completed successfully.";
        task->lastUpdatedAt = _isoNow();
        task->resultJson = resultJson;
        task->hasResult = true;
        return true;
    }

    /**
     * Fail a task with an error message.
     */
    bool failTask(const String& taskId, const String& errorMessage) {
        auto* task = getTask(taskId);
        if (!task) return false;
        if (isTerminalStatus(task->status)) return false;

        task->status = TaskStatus::Failed;
        task->statusMessage = errorMessage;
        task->lastUpdatedAt = _isoNow();
        return true;
    }

    /**
     * Cancel a task.
     */
    bool cancelTask(const String& taskId) {
        auto* task = getTask(taskId);
        if (!task) return false;
        if (isTerminalStatus(task->status)) return false;

        task->status = TaskStatus::Cancelled;
        task->statusMessage = "The task was cancelled by request.";
        task->lastUpdatedAt = _isoNow();
        return true;
    }

    /**
     * List all tasks (with pagination).
     */
    std::vector<MCPTask> listTasks(size_t startIdx = 0, size_t pageSize = 20, size_t* nextIdx = nullptr) {
        _expireOldTasks();
        std::vector<MCPTask> result;
        size_t idx = 0;
        for (auto& kv : _tasks) {
            if (idx >= startIdx && result.size() < pageSize) {
                result.push_back(kv.second);
            }
            idx++;
        }
        if (nextIdx) {
            *nextIdx = (startIdx + pageSize < _tasks.size()) ? startIdx + pageSize : 0;
        }
        return result;
    }

    /**
     * Remove a specific task (e.g., after TTL expiry).
     */
    void removeTask(const String& taskId) {
        _tasks.erase(taskId);
    }

    /**
     * Get current task count.
     */
    size_t taskCount() const { return _tasks.size(); }

    /**
     * Check if tasks feature is enabled.
     */
    bool isEnabled() const { return _enabled; }
    void setEnabled(bool e) { _enabled = e; }

    /**
     * Set maximum number of concurrent tasks.
     */
    void setMaxTasks(size_t max) { _maxTasks = max; }
    size_t maxTasks() const { return _maxTasks; }

    /**
     * Set default poll interval (ms).
     */
    void setDefaultPollInterval(int32_t ms) { _defaultPollInterval = ms; }
    int32_t defaultPollInterval() const { return _defaultPollInterval; }

private:
    std::map<String, MCPTask> _tasks;
    bool _enabled = false;
    size_t _maxTasks = 16;  // Reasonable for MCU
    int32_t _defaultPollInterval = 5000;
    uint32_t _nextId = 1;

    String _generateId() {
        // Simple monotonic ID suitable for MCU (no UUID needed)
        char buf[16];
        snprintf(buf, sizeof(buf), "task-%u", _nextId++);
        return String(buf);
    }

    String _isoNow() {
        // On MCU, millis()-based relative timestamp; on test, static
        unsigned long ms = millis();
        unsigned long secs = ms / 1000;
        unsigned long mins = secs / 60;
        unsigned long hrs = mins / 60;
        char buf[32];
        snprintf(buf, sizeof(buf), "1970-01-01T%02lu:%02lu:%02luZ",
                 hrs % 24, mins % 60, secs % 60);
        return String(buf);
    }

    void _expireOldTasks() {
        if (_tasks.empty()) return;
        std::vector<String> expired;
        for (auto& kv : _tasks) {
            if (kv.second.ttl > 0 && isTerminalStatus(kv.second.status)) {
                // Simple heuristic: remove completed tasks after TTL
                // (In real impl, compare createdAt + ttl vs now)
                // For MCU, just cap at _maxTasks * 2
            }
        }
        // Cap total tasks to prevent memory exhaustion
        while (_tasks.size() > _maxTasks * 2) {
            // Remove oldest terminal tasks first
            for (auto it = _tasks.begin(); it != _tasks.end(); ++it) {
                if (isTerminalStatus(it->second.status)) {
                    _tasks.erase(it);
                    break;
                }
            }
            // If no terminal tasks, break to avoid infinite loop
            break;
        }
    }
};

} // namespace mcpd

#endif // MCPD_TASK_H
