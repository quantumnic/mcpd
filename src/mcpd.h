/**
 * mcpd — MCP Server SDK for Microcontrollers
 *
 * Implements Model Context Protocol (2025-03-26) with Streamable HTTP transport.
 * https://github.com/redbasecap-buiss/mcpd
 *
 * MIT License — Nicola Spieser
 */

#ifndef MCPD_H
#define MCPD_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <functional>
#include <vector>
#include <map>
#include <set>

#include "MCPIcon.h"
#include "MCPTool.h"
#include "MCPResource.h"
#include "MCPResourceTemplate.h"
#include "MCPPrompt.h"
#include "MCPLogging.h"
#include "MCPCompletion.h"
#include "MCPRoots.h"
#include "MCPToolGroup.h"
#include "MCPContent.h"
#include "MCPProgress.h"
#include "MCPTransport.h"
#include "MCPTransportSSE.h"
#include "MCPSampling.h"
#include "MCPElicitation.h"
#include "MCPTransportWS.h"
#include "MCPRateLimit.h"
#include "MCPSession.h"
#include "MCPHeap.h"
#include "MCPAuth.h"
#include "MCPMetrics.h"
#include "MCPTask.h"
#include "MCPValidation.h"
#include "MCPCache.h"
#include "MCPScheduler.h"
#include "MCPEventStore.h"
#include "MCPStateStore.h"
#include "MCPAccessControl.h"
#include "MCPAuditLog.h"
#include "MCPWatchdog.h"
#include "MCPHealthCheck.h"
#include "MCPCircuitBreaker.h"
#include "MCPRetry.h"

#ifdef ESP32
#include "MCPTransportBLE.h"
#endif

#define MCPD_VERSION "0.49.0"
#define MCPD_MCP_PROTOCOL_VERSION "2025-11-25"
#define MCPD_MCP_PROTOCOL_VERSION_COMPAT "2025-03-26"

namespace mcpd {

/**
 * MCP Server — the main class.
 *
 * Usage:
 *   mcpd::Server mcp("my-device");
 *   mcp.addTool("name", "desc", schema, handler);
 *   mcp.begin();         // starts HTTP server + mDNS
 *   mcp.loop();          // call in loop()
 */
class Server {
public:
    /**
     * @param name  Server name advertised via MCP initialize and mDNS
     * @param port  HTTP port (default 80)
     */
    explicit Server(const char* name, uint16_t port = 80);
    ~Server();

    // ── Tool registration ──────────────────────────────────────────────

    /**
     * Register a tool with a JSON Schema string for parameters.
     * The handler receives parsed params and returns a JSON string result.
     */
    void addTool(const char* name, const char* description,
                 const char* inputSchemaJson, MCPToolHandler handler);

    /**
     * Register a tool with a pre-built MCPTool object.
     */
    void addTool(const MCPTool& tool);

    /**
     * Register a tool with a rich handler that returns structured content
     * (images, embedded resources, multi-part responses).
     */
    void addRichTool(const char* name, const char* description,
                     const char* inputSchemaJson, MCPRichToolHandler handler);

    // ── Resource registration ──────────────────────────────────────────

    /**
     * Register a resource.
     */
    void addResource(const char* uri, const char* name,
                     const char* description, const char* mimeType,
                     MCPResourceHandler handler);

    void addResource(const MCPResource& resource);

    // ── Resource Template registration ─────────────────────────────────

    /**
     * Register a resource template with URI template (RFC 6570 Level 1).
     * E.g. "sensor://{sensor_id}/reading"
     */
    void addResourceTemplate(const char* uriTemplate, const char* name,
                             const char* description, const char* mimeType,
                             MCPResourceTemplateHandler handler);

    void addResourceTemplate(const MCPResourceTemplate& tmpl);

    // ── Prompt registration ────────────────────────────────────────────

    /**
     * Register a prompt with arguments and a handler that returns messages.
     */
    void addPrompt(const char* name, const char* description,
                   std::vector<MCPPromptArgument> arguments,
                   MCPPromptHandler handler);

    void addPrompt(const MCPPrompt& prompt);

    // ── Root registration ──────────────────────────────────────────────

    /**
     * Register a root URI that describes the server's context.
     */
    void addRoot(const char* uri, const char* name = "");

    // ── Lifecycle ──────────────────────────────────────────────────────

    /**
     * Start the MCP server (HTTP + mDNS).
     * Call after WiFi is connected.
     */
    void begin();

    /**
     * Process incoming requests. Call in loop().
     */
    void loop();

    /**
     * Stop the server.
     */
    void stop();

    // ── Configuration ──────────────────────────────────────────────────

    /** Set the MCP endpoint path (default: "/mcp") */
    void setEndpoint(const char* path);

    /** Enable/disable mDNS advertisement (default: enabled) */
    void setMDNS(bool enabled);

    /**
     * Set server instructions for the LLM (MCP 2025-03-26 spec).
     * Included in the initialize response to guide the model's behavior.
     * Example: "This device controls a greenhouse. Use temperature_read before adjusting fans."
     */
    void setInstructions(const char* instructions) { _instructions = instructions; }

    /**
     * Set a custom version string (overrides MCPD_VERSION in server info).
     */
    void setVersion(const char* version) { _version = version; }

    /**
     * Set a human-readable description of this server (MCP 2025-11-25).
     */
    void setDescription(const char* desc) { _description = desc; }

    /**
     * Set the server's website URL (MCP 2025-11-25).
     */
    void setWebsiteUrl(const char* url) { _websiteUrl = url; }

    /**
     * Add an icon for this server (MCP 2025-11-25).
     * Icons are included in serverInfo during initialize.
     */
    void addIcon(const MCPIcon& icon) { _icons.push_back(icon); }

    /**
     * Enable or disable a tool at runtime by name.
     * Disabled tools are hidden from tools/list and cannot be called.
     * Emits tools/list_changed notification.
     * @return true if the tool was found
     */
    bool enableTool(const char* name, bool enabled = true);
    bool disableTool(const char* name) { return enableTool(name, false); }
    bool isToolEnabled(const char* name) const;

    // ── Tool Groups ────────────────────────────────────────────────────

    /** Access the tool group manager */
    ToolGroupManager& toolGroups() { return _toolGroups; }
    const ToolGroupManager& toolGroups() const { return _toolGroups; }

    /**
     * Create a tool group and optionally assign tools.
     * Convenience wrapper around toolGroups().createGroup().
     */
    bool createToolGroup(const char* name, const char* description = "") {
        return _toolGroups.createGroup(name, description);
    }

    /**
     * Add a tool to a group (creates the group if needed).
     */
    bool addToolToGroup(const char* toolName, const char* groupName) {
        return _toolGroups.addToolToGroup(toolName, groupName);
    }

    /**
     * Enable or disable a tool group. Disabled groups hide their tools
     * from tools/list and prevent calls (unless the tool belongs to
     * another enabled group). Emits tools/list_changed notification.
     * @return true if group exists
     */
    bool enableToolGroup(const char* name, bool enabled = true);
    bool disableToolGroup(const char* name) { return enableToolGroup(name, false); }

    /** Get server name */
    const char* getName() const { return _name; }

    /** Get port */
    uint16_t getPort() const { return _port; }

    /** Access the logging subsystem */
    Logging& logging() { return _logging; }

    /** Access the completion manager for registering autocomplete providers */
    CompletionManager& completions() { return _completions; }

    /** Access the request tracker for cancellation checking */
    RequestTracker& requests() { return _requestTracker; }

    /** Access the sampling manager for server-initiated LLM requests */
    SamplingManager& sampling() { return _samplingManager; }

    /** Access the SSE manager for server-push connections */
    SSEManager& sse() { return _sseManager; }

    /** Access the elicitation manager for requesting user input */
    ElicitationManager& elicitation() { return _elicitationManager; }

    /**
     * Request LLM sampling from the connected client.
     * The request is queued and sent via SSE; the response arrives asynchronously.
     * @param request   Sampling request with messages and parameters
     * @param callback  Called when the client responds
     * @return Request ID, or -1 if no SSE client is connected
     */
    int requestSampling(const MCPSamplingRequest& request, MCPSamplingCallback callback);

    /**
     * Report progress for a long-running tool.
     * @param progressToken  Token from the original request's _meta.progressToken
     * @param progress       Current progress value
     * @param total          Total expected value (0 = indeterminate)
     * @param message        Optional human-readable status message
     */
    void reportProgress(const String& progressToken, double progress,
                        double total = 0, const String& message = "");

    /**
     * Request user input (elicitation) from the connected client.
     * The request is queued and sent via SSE; the response arrives asynchronously.
     * @param request   Elicitation request with message and field definitions
     * @param callback  Called when the client responds (accept/decline/cancel)
     * @return Request ID, or -1 if no SSE client is connected
     */
    int requestElicitation(const MCPElicitationRequest& request,
                           MCPElicitationCallback callback);

    /**
     * Enable WebSocket transport alongside HTTP.
     * Call before begin(). The WS server runs on a separate port.
     * @param port  WebSocket port (default 8081)
     */
    void enableWebSocket(uint16_t port = 8081);

#ifdef ESP32
    /**
     * Enable BLE transport alongside HTTP.
     * Call before begin(). Uses ESP32 BLE GATT server.
     * @param deviceName  BLE device name (default: server name)
     * @param mtu         Negotiated MTU size (default 512)
     */
    void enableBLE(const char* deviceName = nullptr, uint16_t mtu = 512);
#endif

    // ── Rate Limiting ──────────────────────────────────────────────────

    /**
     * Enable request rate limiting to protect the device.
     * @param requestsPerSecond  Sustained rate
     * @param burstCapacity      Maximum burst size
     */
    void setRateLimit(float requestsPerSecond, size_t burstCapacity);

    /** Access the rate limiter for stats or manual control */
    RateLimiter& rateLimiter() { return _rateLimiter; }

    // ── Access Control (RBAC) ──────────────────────────────────────────
    /** Access the RBAC controller for role-based tool restrictions. */
    AccessControl& accessControl() { return _accessControl; }
    HealthCheck& healthCheck() { return _healthCheck; }

    /**
     * Access the audit log for configuration and queries.
     */
    AuditLog& auditLog() { return _auditLog; }

    // ── Session Management ─────────────────────────────────────────────

    /** Access session manager for multi-client session control */
    SessionManager& sessions() { return _sessionManager; }

    /** Set max concurrent sessions (default: 4, 0 = unlimited) */
    void setMaxSessions(size_t max) { _sessionManager.setMaxSessions(max); }

    /** Set session idle timeout in ms (default: 30 min, 0 = no timeout) */
    void setSessionTimeout(unsigned long timeoutMs) { _sessionManager.setIdleTimeout(timeoutMs); }

    // ── Heap Monitoring ────────────────────────────────────────────────

    /** Access heap monitor for memory diagnostics */
    HeapMonitor& heap() { return _heapMonitor; }

    // ── Authentication ─────────────────────────────────────────────────

    /** Access the authentication module */
    Auth& auth() { return _auth; }

    // ── Metrics ────────────────────────────────────────────────────────

    /** Access the Prometheus metrics module */
    Metrics& metrics() { return _metrics; }

    // ── Tool Call Hooks ─────────────────────────────────────────────────

    /**
     * Context passed to tool call hooks.
     */
    struct ToolCallContext {
        String toolName;          // Name of the tool being called
        const JsonObject* args;   // Tool arguments (read-only in before hook)
        unsigned long startMs;    // millis() when call started (after hook only)
        unsigned long durationMs; // Call duration in ms (after hook only)
        bool isError;             // Whether the tool returned an error (after hook only)
    };

    /**
     * Before-call hook. Return true to proceed, false to reject the call.
     * When rejected, the server returns a "Tool call rejected" error.
     */
    using BeforeToolCallHook = std::function<bool(const ToolCallContext& ctx)>;

    /**
     * After-call hook. Called after tool execution completes.
     * Useful for logging, metrics, auditing.
     */
    using AfterToolCallHook = std::function<void(const ToolCallContext& ctx)>;

    /** Set a hook called before every tool call. Return false to reject. */
    void onBeforeToolCall(BeforeToolCallHook hook) { _beforeToolCallHook = hook; }

    /** Set a hook called after every tool call completes. */
    void onAfterToolCall(AfterToolCallHook hook) { _afterToolCallHook = hook; }

    // ── Tasks (experimental, MCP 2025-11-25) ────────────────────────────

    /** Access the task manager for async tool execution */
    TaskManager& tasks() { return _taskManager; }

    /**
     * Enable task support for tools/call.
     * When enabled, clients can send task-augmented requests.
     */
    void enableTasks(bool enable = true) { _taskManager.setEnabled(enable); }

    /**
     * Register an async tool handler for task-based execution.
     * The handler is called when a tool is invoked as a task.
     * It should call taskComplete() or taskFail() when done.
     */
    void addTaskTool(const char* name, const char* description,
                     const char* inputSchemaJson, MCPTaskToolHandler handler,
                     TaskSupport support = TaskSupport::Optional);

    /**
     * Complete a task with a result JSON string.
     * Call from within async task handlers.
     */
    bool taskComplete(const String& taskId, const String& resultJson);

    /**
     * Fail a task with an error message.
     */
    bool taskFail(const String& taskId, const String& errorMessage);

    /**
     * Cancel a task.
     */
    bool taskCancel(const String& taskId);

    // ── Input Validation ────────────────────────────────────────────────

    /**
     * Enable automatic validation of tool call arguments against inputSchema.
     * When enabled, the server validates required fields, types, enums,
     * numeric ranges, string lengths, and array sizes before invoking handlers.
     *
     * Invalid calls receive a JSON-RPC error (-32602) with detailed messages.
     * Disabled by default for backward compatibility.
     */
    void enableInputValidation(bool enable = true) { _inputValidation = enable; }

    /** Check if input validation is enabled */
    bool isInputValidationEnabled() const { return _inputValidation; }

    /**
     * Enable output validation: after a tool with outputSchema executes,
     * validate its structured output against the schema. If validation
     * fails, the tool result is replaced with an error containing
     * validation details. Useful for development and debugging.
     *
     * Disabled by default. Requires enableInputValidation() pattern.
     */
    void enableOutputValidation(bool enable = true) { _outputValidation = enable; }

    /** Check if output validation is enabled */
    bool isOutputValidationEnabled() const { return _outputValidation; }

    // ── Tool Result Caching ─────────────────────────────────────────────

    /**
     * Access the tool result cache for configuration.
     * Use to set per-tool TTLs before enabling.
     */
    ToolResultCache& cache() { return _cache; }

    /**
     * Enable tool result caching. Only tools with explicit TTL are cached.
     * @see ToolResultCache::setToolTTL
     */
    void enableCache(bool enable = true) { _cache.setEnabled(enable); }

    /** Check if caching is enabled */
    bool isCacheEnabled() const { return _cache.isEnabled(); }

    // ── JSON-RPC Error Data ────────────────────────────────────────────

    /**
     * Create a JSON-RPC error response with optional structured data.
     * Per JSON-RPC spec, errors may include a "data" field with additional info.
     */
    String jsonRpcErrorWithData(JsonVariant id, int code, const char* message,
                                const String& dataJson);

    // ── Lifecycle Hooks ────────────────────────────────────────────────

    using LifecycleCallback = std::function<void()>;
    using InitCallback = std::function<void(const String& clientName)>;

    /** Called when a new session is initialized (receives client info) */
    void onInitialize(InitCallback cb) { _onInitializeCb = cb; }

    /** Called when a client connects (HTTP/SSE/WS/BLE) */
    void onConnect(LifecycleCallback cb) { _onConnectCb = cb; }

    /** Called when a client disconnects */
    void onDisconnect(LifecycleCallback cb) { _onDisconnectCb = cb; }

    // ── Resource Subscriptions ─────────────────────────────────────────

    /**
     * Notify that a subscribed resource has been updated.
     * Queues a notifications/resources/updated notification for subscribers.
     */
    void notifyResourceUpdated(const char* uri);

    /** Set pagination page size for list methods (0 = no pagination) */
    void setPageSize(size_t pageSize) { _pageSize = pageSize; }

    /**
     * Notify clients that the tool list has changed.
     * Call after dynamically adding/removing tools at runtime.
     */
    void notifyToolsChanged();

    /**
     * Notify clients that the resource list has changed.
     */
    void notifyResourcesChanged();

    /**
     * Notify clients that the prompt list has changed.
     */
    void notifyPromptsChanged();

    /**
     * Remove a tool by name. Returns true if found and removed.
     */
    bool removeTool(const char* name);

    /**
     * Remove a resource by URI. Returns true if found and removed.
     */
    bool removeResource(const char* uri);

    /**
     * Remove a resource template by URI template. Returns true if found and removed.
     */
    bool removeResourceTemplate(const char* uriTemplate);

    /**
     * Remove a prompt by name. Returns true if found and removed.
     */
    bool removePrompt(const char* name);

    /**
     * Remove a root by URI. Returns true if found and removed.
     */
    bool removeRoot(const char* uri);

#ifdef MCPD_TEST
public:  // Allow test access to internals
#else
private:
#endif
    const char* _name;
    uint16_t _port;
    const char* _endpoint = "/mcp";
    const char* _instructions = nullptr;
    const char* _version = nullptr;
    const char* _description = nullptr;
    const char* _websiteUrl = nullptr;
    std::vector<MCPIcon> _icons;
    bool _mdnsEnabled = true;
    std::set<String> _disabledTools;  // tools hidden from listing/calling
    String _sessionId;
    bool _initialized = false;
    size_t _pageSize = 0;  // 0 = no pagination

    WebServer* _httpServer = nullptr;
    Logging _logging;
    CompletionManager _completions;
    RequestTracker _requestTracker;
    SamplingManager _samplingManager;
    ElicitationManager _elicitationManager;
    SSEManager _sseManager;
    WebSocketTransport* _wsTransport = nullptr;
    uint16_t _wsPort = 0;

#ifdef ESP32
    BLETransport* _bleTransport = nullptr;
    const char* _bleName = nullptr;
    uint16_t _bleMtu = 512;
#endif

    RateLimiter _rateLimiter;
    AccessControl _accessControl;
    HealthCheck _healthCheck;
    AuditLog _auditLog;
    SessionManager _sessionManager;
    HeapMonitor _heapMonitor;
    Auth _auth;
    Metrics _metrics;
    TaskManager _taskManager;
    bool _inputValidation = false;
    bool _outputValidation = false;
    ToolResultCache _cache;
    ToolGroupManager _toolGroups;
    std::map<String, MCPTaskToolHandler> _taskToolHandlers;
    std::map<String, TaskSupport> _taskToolSupport;

    // Lifecycle callbacks
    InitCallback _onInitializeCb;
    LifecycleCallback _onConnectCb;
    LifecycleCallback _onDisconnectCb;

    // Tool call hooks
    BeforeToolCallHook _beforeToolCallHook;
    AfterToolCallHook _afterToolCallHook;

    std::vector<MCPTool> _tools;
    std::vector<std::pair<String, MCPRichToolHandler>> _richTools;  // name → handler
    std::vector<MCPResource> _resources;
    std::vector<MCPResourceTemplate> _resourceTemplates;
    std::vector<MCPPrompt> _prompts;
    std::vector<MCPRoot> _roots;

    // Pending notifications to send
    std::vector<String> _pendingNotifications;

    // Resource subscriptions: set of subscribed URIs (O(log n) lookup)
    std::set<String> _subscribedResources;

    // ── JSON-RPC dispatch ──────────────────────────────────────────────

    void _handleMCPPost();
    void _handleMCPGet();
    void _handleMCPDelete();

    String _processJsonRpc(const String& body);
    String _dispatch(const char* method, JsonVariant params, JsonVariant id);

    // ── MCP method handlers ────────────────────────────────────────────

    String _handleInitialize(JsonVariant params, JsonVariant id);
    String _handleToolsList(JsonVariant params, JsonVariant id);
    String _handleToolsCall(JsonVariant params, JsonVariant id);
    String _handleResourcesList(JsonVariant params, JsonVariant id);
    String _handleResourcesRead(JsonVariant params, JsonVariant id);
    String _handleResourcesTemplatesList(JsonVariant params, JsonVariant id);
    String _handlePromptsList(JsonVariant params, JsonVariant id);
    String _handlePromptsGet(JsonVariant params, JsonVariant id);
    String _handlePing(JsonVariant id);
    String _handleLoggingSetLevel(JsonVariant params, JsonVariant id);
    String _handleCompletionComplete(JsonVariant params, JsonVariant id);
    String _handleResourcesSubscribe(JsonVariant params, JsonVariant id);
    String _handleResourcesUnsubscribe(JsonVariant params, JsonVariant id);
    String _handleRootsList(JsonVariant params, JsonVariant id);
    String _handleTasksGet(JsonVariant params, JsonVariant id);
    String _handleTasksResult(JsonVariant params, JsonVariant id);
    String _handleTasksList(JsonVariant params, JsonVariant id);
    String _handleTasksCancel(JsonVariant params, JsonVariant id);

    // ── Helpers ────────────────────────────────────────────────────────

    String _jsonRpcResult(JsonVariant id, const String& resultJson);
    String _jsonRpcError(JsonVariant id, int code, const char* message);
    String _generateSessionId();
};

} // namespace mcpd

#endif // MCPD_H
