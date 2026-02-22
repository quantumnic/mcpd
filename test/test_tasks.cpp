/**
 * mcpd — Tasks test suite (MCP 2025-11-25 experimental)
 *
 * Tests for async task creation, polling, completion, cancellation,
 * task-augmented tool calls, and tasks/list, tasks/get, tasks/result, tasks/cancel.
 */

#include "test_framework.h"
#include "mcpd.h"
#include "mcpd.cpp"

// ════════════════════════════════════════════════════════════════════════
// TaskManager unit tests
// ════════════════════════════════════════════════════════════════════════

TEST(task_create_and_get) {
    mcpd::TaskManager mgr;
    String id = mgr.createTask("my_tool");
    ASSERT_TRUE(!id.isEmpty());
    auto* task = mgr.getTask(id);
    ASSERT_TRUE(task != nullptr);
    ASSERT_EQ(task->status, mcpd::TaskStatus::Working);
    ASSERT_EQ(task->toolName, String("my_tool"));
    ASSERT_TRUE(!task->createdAt.isEmpty());
    ASSERT_TRUE(!task->lastUpdatedAt.isEmpty());
}

TEST(task_complete) {
    mcpd::TaskManager mgr;
    String id = mgr.createTask("tool");
    bool ok = mgr.completeTask(id, "{\"content\":[{\"type\":\"text\",\"text\":\"done\"}]}");
    ASSERT_TRUE(ok);
    auto* task = mgr.getTask(id);
    ASSERT_EQ(task->status, mcpd::TaskStatus::Completed);
    ASSERT_TRUE(task->hasResult);
    ASSERT_STR_CONTAINS(task->resultJson.c_str(), "done");
}

TEST(task_fail) {
    mcpd::TaskManager mgr;
    String id = mgr.createTask("tool");
    bool ok = mgr.failTask(id, "something went wrong");
    ASSERT_TRUE(ok);
    auto* task = mgr.getTask(id);
    ASSERT_EQ(task->status, mcpd::TaskStatus::Failed);
    ASSERT_EQ(task->statusMessage, String("something went wrong"));
}

TEST(task_cancel) {
    mcpd::TaskManager mgr;
    String id = mgr.createTask("tool");
    bool ok = mgr.cancelTask(id);
    ASSERT_TRUE(ok);
    auto* task = mgr.getTask(id);
    ASSERT_EQ(task->status, mcpd::TaskStatus::Cancelled);
}

TEST(task_terminal_no_transition) {
    mcpd::TaskManager mgr;
    String id = mgr.createTask("tool");
    mgr.completeTask(id, "{}");
    // Can't change completed task
    ASSERT_FALSE(mgr.failTask(id, "error"));
    ASSERT_FALSE(mgr.cancelTask(id));
    ASSERT_FALSE(mgr.updateStatus(id, mcpd::TaskStatus::Working));
    ASSERT_EQ(mgr.getTask(id)->status, mcpd::TaskStatus::Completed);
}

TEST(task_input_required_to_working) {
    mcpd::TaskManager mgr;
    String id = mgr.createTask("tool");
    ASSERT_TRUE(mgr.updateStatus(id, mcpd::TaskStatus::InputRequired, "need input"));
    ASSERT_EQ(mgr.getTask(id)->status, mcpd::TaskStatus::InputRequired);
    ASSERT_TRUE(mgr.updateStatus(id, mcpd::TaskStatus::Working, "resuming"));
    ASSERT_EQ(mgr.getTask(id)->status, mcpd::TaskStatus::Working);
}

TEST(task_list) {
    mcpd::TaskManager mgr;
    mgr.createTask("a");
    mgr.createTask("b");
    mgr.createTask("c");
    auto tasks = mgr.listTasks();
    ASSERT_EQ(tasks.size(), (size_t)3);
}

TEST(task_list_pagination) {
    mcpd::TaskManager mgr;
    for (int i = 0; i < 5; i++) mgr.createTask("tool");
    size_t next = 0;
    auto page1 = mgr.listTasks(0, 2, &next);
    ASSERT_EQ(page1.size(), (size_t)2);
    ASSERT_EQ(next, (size_t)2);
    auto page2 = mgr.listTasks(2, 2, &next);
    ASSERT_EQ(page2.size(), (size_t)2);
}

TEST(task_not_found) {
    mcpd::TaskManager mgr;
    ASSERT_TRUE(mgr.getTask("nonexistent") == nullptr);
    ASSERT_FALSE(mgr.completeTask("nonexistent", "{}"));
    ASSERT_FALSE(mgr.failTask("nonexistent", "err"));
    ASSERT_FALSE(mgr.cancelTask("nonexistent"));
}

TEST(task_count) {
    mcpd::TaskManager mgr;
    ASSERT_EQ(mgr.taskCount(), (size_t)0);
    mgr.createTask("a");
    mgr.createTask("b");
    ASSERT_EQ(mgr.taskCount(), (size_t)2);
}

TEST(task_remove) {
    mcpd::TaskManager mgr;
    String id = mgr.createTask("tool");
    mgr.removeTask(id);
    ASSERT_TRUE(mgr.getTask(id) == nullptr);
    ASSERT_EQ(mgr.taskCount(), (size_t)0);
}

TEST(task_ttl) {
    mcpd::TaskManager mgr;
    String id = mgr.createTask("tool", 60000);
    auto* task = mgr.getTask(id);
    ASSERT_EQ(task->ttl, (int64_t)60000);
}

TEST(task_ttl_unlimited) {
    mcpd::TaskManager mgr;
    String id = mgr.createTask("tool", -1);
    auto* task = mgr.getTask(id);
    ASSERT_EQ(task->ttl, (int64_t)-1);
}

TEST(task_to_json) {
    mcpd::TaskManager mgr;
    String id = mgr.createTask("tool", 30000);
    auto* task = mgr.getTask(id);

    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    task->toJson(obj);

    String json;
    serializeJson(doc, json);
    ASSERT_STR_CONTAINS(json.c_str(), "\"taskId\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"status\":\"working\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"createdAt\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"ttl\":30000");
    ASSERT_STR_CONTAINS(json.c_str(), "\"pollInterval\"");
}

TEST(task_to_json_null_ttl) {
    mcpd::TaskManager mgr;
    String id = mgr.createTask("tool", -1);
    auto* task = mgr.getTask(id);

    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    task->toJson(obj);

    String json;
    serializeJson(doc, json);
    // ttl omitted when unlimited (-1)
    ASSERT_STR_NOT_CONTAINS(json.c_str(), "\"ttl\"");
}

TEST(task_status_strings) {
    ASSERT_EQ(String(mcpd::taskStatusToString(mcpd::TaskStatus::Working)), String("working"));
    ASSERT_EQ(String(mcpd::taskStatusToString(mcpd::TaskStatus::InputRequired)), String("input_required"));
    ASSERT_EQ(String(mcpd::taskStatusToString(mcpd::TaskStatus::Completed)), String("completed"));
    ASSERT_EQ(String(mcpd::taskStatusToString(mcpd::TaskStatus::Failed)), String("failed"));
    ASSERT_EQ(String(mcpd::taskStatusToString(mcpd::TaskStatus::Cancelled)), String("cancelled"));
}

TEST(task_status_from_string) {
    ASSERT_EQ(mcpd::taskStatusFromString("working"), mcpd::TaskStatus::Working);
    ASSERT_EQ(mcpd::taskStatusFromString("input_required"), mcpd::TaskStatus::InputRequired);
    ASSERT_EQ(mcpd::taskStatusFromString("completed"), mcpd::TaskStatus::Completed);
    ASSERT_EQ(mcpd::taskStatusFromString("failed"), mcpd::TaskStatus::Failed);
    ASSERT_EQ(mcpd::taskStatusFromString("cancelled"), mcpd::TaskStatus::Cancelled);
    ASSERT_EQ(mcpd::taskStatusFromString("unknown"), mcpd::TaskStatus::Working);
    ASSERT_EQ(mcpd::taskStatusFromString(nullptr), mcpd::TaskStatus::Working);
}

TEST(task_is_terminal) {
    ASSERT_FALSE(mcpd::isTerminalStatus(mcpd::TaskStatus::Working));
    ASSERT_FALSE(mcpd::isTerminalStatus(mcpd::TaskStatus::InputRequired));
    ASSERT_TRUE(mcpd::isTerminalStatus(mcpd::TaskStatus::Completed));
    ASSERT_TRUE(mcpd::isTerminalStatus(mcpd::TaskStatus::Failed));
    ASSERT_TRUE(mcpd::isTerminalStatus(mcpd::TaskStatus::Cancelled));
}

TEST(task_support_strings) {
    ASSERT_EQ(String(mcpd::taskSupportToString(mcpd::TaskSupport::Forbidden)), String("forbidden"));
    ASSERT_EQ(String(mcpd::taskSupportToString(mcpd::TaskSupport::Optional)), String("optional"));
    ASSERT_EQ(String(mcpd::taskSupportToString(mcpd::TaskSupport::Required)), String("required"));
}

TEST(task_manager_enabled) {
    mcpd::TaskManager mgr;
    ASSERT_FALSE(mgr.isEnabled());
    mgr.setEnabled(true);
    ASSERT_TRUE(mgr.isEnabled());
}

TEST(task_manager_config) {
    mcpd::TaskManager mgr;
    mgr.setMaxTasks(32);
    ASSERT_EQ(mgr.maxTasks(), (size_t)32);
    mgr.setDefaultPollInterval(10000);
    ASSERT_EQ(mgr.defaultPollInterval(), (int32_t)10000);
}

// ════════════════════════════════════════════════════════════════════════
// Server integration tests
// ════════════════════════════════════════════════════════════════════════

TEST(server_tasks_capability_disabled) {
    mcpd::Server server("test", 80);
    server.addTool("dummy", "test tool", "{}", [](const JsonObject&) { return "ok"; });

    String req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"test\"}}}";
    String resp = server._processJsonRpc(req);
    ASSERT_STR_NOT_CONTAINS(resp.c_str(), "\"tasks\"");
}

TEST(server_tasks_capability_enabled) {
    mcpd::Server server("test", 80);
    server.enableTasks();
    server.addTool("dummy", "test tool", "{}", [](const JsonObject&) { return "ok"; });

    String req = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"test\"}}}";
    String resp = server._processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"tasks\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"list\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"cancel\"");
}

TEST(server_task_augmented_call) {
    mcpd::Server server("test", 80);
    server.enableTasks();

    String capturedTaskId;
    server.addTaskTool("slow_sensor", "slow sensor read", "{}",
        [&capturedTaskId, &server](const String& taskId, JsonVariant params) {
            capturedTaskId = taskId;
            // Simulate async completion
            server.taskComplete(taskId, "{\"content\":[{\"type\":\"text\",\"text\":\"42\"}]}");
        }, mcpd::TaskSupport::Optional);

    // Initialize
    server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"test\"}}}");

    // Task-augmented call
    String req = "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"slow_sensor\",\"arguments\":{},\"task\":{\"ttl\":60000}}}";
    String resp = server._processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"task\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"taskId\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"status\":\"completed\""); // completed synchronously in test
    ASSERT_TRUE(!capturedTaskId.isEmpty());
}

TEST(server_tasks_get) {
    mcpd::Server server("test", 80);
    server.enableTasks();

    String capturedTaskId;
    server.addTaskTool("sensor", "test", "{}",
        [&capturedTaskId](const String& taskId, JsonVariant) {
            capturedTaskId = taskId;
            // Don't complete - leave as working
        }, mcpd::TaskSupport::Optional);

    server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"test\"}}}");

    // Create task
    server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"sensor\",\"arguments\":{},\"task\":{}}}");

    // Get task
    String getReq = "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tasks/get\",\"params\":{\"taskId\":\"" + capturedTaskId + "\"}}";
    String resp = server._processJsonRpc(getReq);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"status\":\"working\"");
    ASSERT_STR_CONTAINS(resp.c_str(), capturedTaskId.c_str());
}

TEST(server_tasks_result_completed) {
    mcpd::Server server("test", 80);
    server.enableTasks();

    String capturedTaskId;
    server.addTaskTool("sensor", "test", "{}",
        [&capturedTaskId, &server](const String& taskId, JsonVariant) {
            capturedTaskId = taskId;
            server.taskComplete(taskId, "{\"content\":[{\"type\":\"text\",\"text\":\"result data\"}]}");
        }, mcpd::TaskSupport::Optional);

    server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"test\"}}}");
    server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"sensor\",\"arguments\":{},\"task\":{}}}");

    // Get result
    String req = "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tasks/result\",\"params\":{\"taskId\":\"" + capturedTaskId + "\"}}";
    String resp = server._processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "result data");
    ASSERT_STR_CONTAINS(resp.c_str(), "io.modelcontextprotocol/related-task");
}

TEST(server_tasks_result_not_complete) {
    mcpd::Server server("test", 80);
    server.enableTasks();

    String capturedTaskId;
    server.addTaskTool("sensor", "test", "{}",
        [&capturedTaskId](const String& taskId, JsonVariant) {
            capturedTaskId = taskId;
        }, mcpd::TaskSupport::Optional);

    server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"test\"}}}");
    server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"sensor\",\"arguments\":{},\"task\":{}}}");

    String req = "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tasks/result\",\"params\":{\"taskId\":\"" + capturedTaskId + "\"}}";
    String resp = server._processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "not yet complete");
}

TEST(server_tasks_cancel) {
    mcpd::Server server("test", 80);
    server.enableTasks();

    String capturedTaskId;
    server.addTaskTool("sensor", "test", "{}",
        [&capturedTaskId](const String& taskId, JsonVariant) {
            capturedTaskId = taskId;
        }, mcpd::TaskSupport::Optional);

    server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"test\"}}}");
    server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"sensor\",\"arguments\":{},\"task\":{}}}");

    String req = "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tasks/cancel\",\"params\":{\"taskId\":\"" + capturedTaskId + "\"}}";
    String resp = server._processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"status\":\"cancelled\"");
}

TEST(server_tasks_list) {
    mcpd::Server server("test", 80);
    server.enableTasks();

    server.addTaskTool("sensor", "test", "{}",
        [](const String&, JsonVariant) {}, mcpd::TaskSupport::Optional);

    server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"test\"}}}");
    server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"sensor\",\"arguments\":{},\"task\":{}}}");
    server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"sensor\",\"arguments\":{},\"task\":{}}}");

    String resp = server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tasks/list\",\"params\":{}}");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"tasks\"");
    // Should contain 2 tasks
    ASSERT_STR_CONTAINS(resp.c_str(), "task-1");
    ASSERT_STR_CONTAINS(resp.c_str(), "task-2");
}

TEST(server_task_forbidden_tool) {
    mcpd::Server server("test", 80);
    server.enableTasks();
    server.addTool("normal_tool", "no tasks", "{}", [](const JsonObject&) { return "ok"; });

    server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"test\"}}}");

    // Try task-augmented call on non-task tool
    String resp = server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"normal_tool\",\"arguments\":{},\"task\":{}}}");
    ASSERT_STR_CONTAINS(resp.c_str(), "does not support task");
}

TEST(server_task_required_tool_no_task) {
    mcpd::Server server("test", 80);
    server.enableTasks();

    server.addTaskTool("slow", "slow tool", "{}",
        [](const String&, JsonVariant) {}, mcpd::TaskSupport::Required);

    server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"test\"}}}");

    // Normal (non-task) call to required-task tool
    String resp = server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"slow\",\"arguments\":{}}}");
    ASSERT_STR_CONTAINS(resp.c_str(), "requires task execution");
}

TEST(server_task_disabled_globally) {
    mcpd::Server server("test", 80);
    // Tasks NOT enabled
    server.addTool("tool", "test", "{}", [](const JsonObject&) { return "ok"; });

    server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"test\"}}}");

    String resp = server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"tool\",\"arguments\":{},\"task\":{}}}");
    ASSERT_STR_CONTAINS(resp.c_str(), "Tasks not supported");
}

TEST(server_tasks_get_not_found) {
    mcpd::Server server("test", 80);
    server.enableTasks();
    server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"test\"}}}");

    String resp = server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tasks/get\",\"params\":{\"taskId\":\"nonexistent\"}}");
    ASSERT_STR_CONTAINS(resp.c_str(), "Task not found");
}

TEST(server_tasks_get_missing_id) {
    mcpd::Server server("test", 80);
    server.enableTasks();
    String resp = server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tasks/get\",\"params\":{}}");
    ASSERT_STR_CONTAINS(resp.c_str(), "Missing taskId");
}

TEST(server_tasks_cancel_not_found) {
    mcpd::Server server("test", 80);
    server.enableTasks();
    String resp = server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tasks/cancel\",\"params\":{\"taskId\":\"nonexistent\"}}");
    ASSERT_STR_CONTAINS(resp.c_str(), "not found");
}

TEST(server_task_fail_via_server) {
    mcpd::Server server("test", 80);
    server.enableTasks();

    String capturedTaskId;
    server.addTaskTool("sensor", "test", "{}",
        [&capturedTaskId, &server](const String& taskId, JsonVariant) {
            capturedTaskId = taskId;
            server.taskFail(taskId, "sensor offline");
        }, mcpd::TaskSupport::Optional);

    server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"test\"}}}");
    server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"sensor\",\"arguments\":{},\"task\":{}}}");

    // tasks/result for failed task should return error
    String req = "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tasks/result\",\"params\":{\"taskId\":\"" + capturedTaskId + "\"}}";
    String resp = server._processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "sensor offline");
}

TEST(server_task_tool_shows_in_list) {
    mcpd::Server server("test", 80);
    server.enableTasks();

    server.addTaskTool("async_tool", "async", "{}",
        [](const String&, JsonVariant) {}, mcpd::TaskSupport::Optional);

    server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"test\"}}}");

    String resp = server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}");
    ASSERT_STR_CONTAINS(resp.c_str(), "async_tool");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"taskSupport\":\"optional\"");
}

TEST(server_task_with_ttl) {
    mcpd::Server server("test", 80);
    server.enableTasks();

    server.addTaskTool("sensor", "test", "{}",
        [](const String&, JsonVariant) {}, mcpd::TaskSupport::Optional);

    server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"test\"}}}");

    String resp = server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"sensor\",\"arguments\":{},\"task\":{\"ttl\":120000}}}");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"ttl\":120000");
}

TEST(server_task_cancel_via_method) {
    mcpd::Server server("test", 80);
    server.enableTasks();

    String capturedTaskId;
    server.addTaskTool("sensor", "test", "{}",
        [&capturedTaskId](const String& taskId, JsonVariant) {
            capturedTaskId = taskId;
        }, mcpd::TaskSupport::Optional);

    server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"test\"}}}");
    server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"sensor\",\"arguments\":{},\"task\":{}}}");

    // Cancel via server method
    ASSERT_TRUE(server.taskCancel(capturedTaskId));

    // Verify via tasks/get
    String req = "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tasks/get\",\"params\":{\"taskId\":\"" + capturedTaskId + "\"}}";
    String resp = server._processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"status\":\"cancelled\"");
}

TEST(server_tasks_result_cancelled) {
    mcpd::Server server("test", 80);
    server.enableTasks();

    String capturedTaskId;
    server.addTaskTool("sensor", "test", "{}",
        [&capturedTaskId, &server](const String& taskId, JsonVariant) {
            capturedTaskId = taskId;
            server.taskCancel(taskId);
        }, mcpd::TaskSupport::Optional);

    server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"test\"}}}");
    server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"sensor\",\"arguments\":{},\"task\":{}}}");

    String req = "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tasks/result\",\"params\":{\"taskId\":\"" + capturedTaskId + "\"}}";
    String resp = server._processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "cancelled");
}

TEST(server_task_before_hook_rejects) {
    mcpd::Server server("test", 80);
    server.enableTasks();

    server.addTaskTool("sensor", "test", "{}",
        [](const String&, JsonVariant) {}, mcpd::TaskSupport::Optional);

    server.onBeforeToolCall([](const mcpd::Server::ToolCallContext&) { return false; });

    server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"test\"}}}");

    String resp = server._processJsonRpc("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"sensor\",\"arguments\":{},\"task\":{}}}");
    ASSERT_STR_CONTAINS(resp.c_str(), "rejected");
}

// ════════════════════════════════════════════════════════════════════════
// Main
// ════════════════════════════════════════════════════════════════════════

int main() {
    printf("\n  mcpd — Tasks Tests (MCP 2025-11-25)\n");
    printf("  ════════════════════════════════════════\n\n");

    // Tests run via static initialization above

    TEST_SUMMARY();
    return _tests_failed > 0 ? 1 : 0;
}
