/**
 * mcpd — MCP Server implementation
 *
 * Implements MCP 2025-03-26 with Streamable HTTP transport.
 */

#include "mcpd.h"

namespace mcpd {

// ════════════════════════════════════════════════════════════════════════
// Constructor / Destructor
// ════════════════════════════════════════════════════════════════════════

Server::Server(const char* name, uint16_t port)
    : _name(name), _port(port) {}

Server::~Server() {
    stop();
}

// ════════════════════════════════════════════════════════════════════════
// Tool & Resource Registration
// ════════════════════════════════════════════════════════════════════════

void Server::addTool(const char* name, const char* description,
                     const char* inputSchemaJson, MCPToolHandler handler) {
    _tools.emplace_back(name, description, inputSchemaJson, handler);
}

void Server::addTool(const MCPTool& tool) {
    _tools.push_back(tool);
}

void Server::addRichTool(const char* name, const char* description,
                         const char* inputSchemaJson, MCPRichToolHandler handler) {
    // Register as a normal tool with a wrapper handler
    _tools.emplace_back(name, description, inputSchemaJson,
        [](const JsonObject&) -> String { return "{}"; });  // placeholder
    // Store the rich handler separately
    _richTools.emplace_back(String(name), handler);
}

void Server::addResource(const char* uri, const char* name,
                         const char* description, const char* mimeType,
                         MCPResourceHandler handler) {
    _resources.emplace_back(uri, name, description, mimeType, handler);
}

void Server::addResource(const MCPResource& resource) {
    _resources.push_back(resource);
}

void Server::addResourceTemplate(const char* uriTemplate, const char* name,
                                 const char* description, const char* mimeType,
                                 MCPResourceTemplateHandler handler) {
    _resourceTemplates.emplace_back(uriTemplate, name, description, mimeType, handler);
}

void Server::addResourceTemplate(const MCPResourceTemplate& tmpl) {
    _resourceTemplates.push_back(tmpl);
}

void Server::addPrompt(const char* name, const char* description,
                       std::vector<MCPPromptArgument> arguments,
                       MCPPromptHandler handler) {
    _prompts.emplace_back(name, description, std::move(arguments), handler);
}

void Server::addPrompt(const MCPPrompt& prompt) {
    _prompts.push_back(prompt);
}

void Server::addRoot(const char* uri, const char* name) {
    _roots.emplace_back(uri, name);
}

// ════════════════════════════════════════════════════════════════════════
// Configuration
// ════════════════════════════════════════════════════════════════════════

void Server::setEndpoint(const char* path) { _endpoint = path; }
void Server::setMDNS(bool enabled) { _mdnsEnabled = enabled; }

bool Server::enableTool(const char* name, bool enabled) {
    // Verify tool exists
    bool found = false;
    for (const auto& t : _tools) {
        if (t.name == name) { found = true; break; }
    }
    if (!found) return false;

    if (enabled) {
        _disabledTools.erase(String(name));
    } else {
        _disabledTools.insert(String(name));
    }
    notifyToolsChanged();
    return true;
}

bool Server::isToolEnabled(const char* name) const {
    return _disabledTools.find(String(name)) == _disabledTools.end();
}

bool Server::removeTool(const char* name) {
    for (auto it = _tools.begin(); it != _tools.end(); ++it) {
        if (it->name == name) {
            _tools.erase(it);
            return true;
        }
    }
    return false;
}

bool Server::removeResource(const char* uri) {
    for (auto it = _resources.begin(); it != _resources.end(); ++it) {
        if (it->uri == uri) {
            _resources.erase(it);
            return true;
        }
    }
    return false;
}

bool Server::removeResourceTemplate(const char* uriTemplate) {
    for (auto it = _resourceTemplates.begin(); it != _resourceTemplates.end(); ++it) {
        if (it->uriTemplate == uriTemplate) {
            _resourceTemplates.erase(it);
            return true;
        }
    }
    return false;
}

bool Server::removePrompt(const char* name) {
    for (auto it = _prompts.begin(); it != _prompts.end(); ++it) {
        if (it->name == name) {
            _prompts.erase(it);
            return true;
        }
    }
    return false;
}

bool Server::removeRoot(const char* uri) {
    for (auto it = _roots.begin(); it != _roots.end(); ++it) {
        if (it->uri == uri) {
            _roots.erase(it);
            return true;
        }
    }
    return false;
}

void Server::notifyToolsChanged() {
    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["method"] = "notifications/tools/list_changed";
    String output;
    serializeJson(doc, output);
    _pendingNotifications.push_back(output);
}

void Server::notifyResourcesChanged() {
    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["method"] = "notifications/resources/list_changed";
    String output;
    serializeJson(doc, output);
    _pendingNotifications.push_back(output);
}

void Server::notifyPromptsChanged() {
    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["method"] = "notifications/prompts/list_changed";
    String output;
    serializeJson(doc, output);
    _pendingNotifications.push_back(output);
}

// ════════════════════════════════════════════════════════════════════════
// Lifecycle
// ════════════════════════════════════════════════════════════════════════

void Server::begin() {
    // Seed random for session ID generation
    randomSeed(esp_random());

    _httpServer = new WebServer(_port);

    // Collect headers we need to read
    const char* headerKeys[] = { transport::HEADER_SESSION_ID };
    _httpServer->collectHeaders(headerKeys, 1);

    // Register MCP endpoint handlers
    _httpServer->on(_endpoint, HTTP_POST, [this]() { _handleMCPPost(); });
    _httpServer->on(_endpoint, HTTP_GET, [this]() { _handleMCPGet(); });
    _httpServer->on(_endpoint, HTTP_DELETE, [this]() { _handleMCPDelete(); });
    _httpServer->on(_endpoint, HTTP_OPTIONS, [this]() {
        transport::setCORSHeaders(*_httpServer);
        _httpServer->send(204);
    });

    // Register Prometheus metrics endpoint
    _metrics.begin(*_httpServer);

    _httpServer->begin();

    // mDNS advertisement
    if (_mdnsEnabled) {
        if (MDNS.begin(_name)) {
            // Advertise as _mcp._tcp service
            MDNS.addService("mcp", "tcp", _port);
            MDNS.addServiceTxt("mcp", "tcp", "path", _endpoint);
            MDNS.addServiceTxt("mcp", "tcp", "version", MCPD_MCP_PROTOCOL_VERSION);
            Serial.printf("[mcpd] mDNS: %s.local, service _mcp._tcp\n", _name);
        } else {
            Serial.println("[mcpd] mDNS failed to start");
        }
    }

    // Start WebSocket transport if enabled
    if (_wsPort > 0) {
        _wsTransport = new WebSocketTransport(_wsPort);
        _wsTransport->onMessage([this](const String& msg) -> String {
            return _processJsonRpc(msg);
        });
        _wsTransport->begin();
        if (_mdnsEnabled) {
            char wsPortStr[8];
            snprintf(wsPortStr, sizeof(wsPortStr), "%d", _wsPort);
            MDNS.addServiceTxt("mcp", "tcp", "ws_port", wsPortStr);
        }
        Serial.printf("[mcpd] WebSocket transport on port %d\n", _wsPort);
    }

    // Start BLE transport if enabled
#ifdef ESP32
    if (_bleName) {
        _bleTransport = new BLETransport(_bleName, _bleMtu);
        _bleTransport->onMessage([this](const String& msg) -> String {
            return _processJsonRpc(msg);
        });
        _bleTransport->onConnection([this](bool connected) {
            if (connected && _onConnectCb) _onConnectCb();
            if (!connected && _onDisconnectCb) _onDisconnectCb();
        });
        _bleTransport->begin();
        Serial.printf("[mcpd] BLE transport enabled: %s\n", _bleName);
    }
#endif

    Serial.printf("[mcpd] Server '%s' started on port %d, endpoint %s\n",
                  _name, _port, _endpoint);
}

void Server::loop() {
    if (_httpServer) {
        _httpServer->handleClient();
    }

    // Manage SSE connections (keepalive, prune)
    _sseManager.loop();

    // Send any pending notifications via SSE
    if (_sseManager.hasClients(_sessionId) && !_pendingNotifications.empty()) {
        for (const auto& notif : _pendingNotifications) {
            _sseManager.broadcast(_sessionId, notif);
        }
        _pendingNotifications.clear();
    }

    // Send any pending sampling requests via SSE
    auto outgoing = _samplingManager.drainOutgoing();
    for (const auto& msg : outgoing) {
        if (_sseManager.hasClients(_sessionId)) {
            _sseManager.broadcast(_sessionId, msg);
        }
    }

    // Send any pending elicitation requests via SSE
    auto elicitOutgoing = _elicitationManager.drainOutgoing();
    for (const auto& msg : elicitOutgoing) {
        if (_sseManager.hasClients(_sessionId)) {
            _sseManager.broadcast(_sessionId, msg);
        }
    }

    // Prune expired requests
    _samplingManager.pruneExpired();
    _elicitationManager.pruneExpired();

    // Process WebSocket transport if enabled
    if (_wsTransport) {
        _wsTransport->loop();
    }

    // Process BLE transport if enabled
#ifdef ESP32
    if (_bleTransport) {
        _bleTransport->loop();

        // Forward pending notifications via BLE too
        if (_bleTransport->isConnected() && !_pendingNotifications.empty()) {
            for (const auto& notif : _pendingNotifications) {
                _bleTransport->sendNotification(notif);
            }
            // Note: don't clear here — SSE might still need them
            // They're cleared after SSE broadcast above
        }
    }
#endif
}

void Server::enableWebSocket(uint16_t port) {
    _wsPort = port;
}

#ifdef ESP32
void Server::enableBLE(const char* deviceName, uint16_t mtu) {
    _bleName = deviceName;
    _bleMtu = mtu;
}
#endif

void Server::setRateLimit(float requestsPerSecond, size_t burstCapacity) {
    _rateLimiter.configure(requestsPerSecond, burstCapacity);
}

void Server::stop() {
    if (_httpServer) {
        _httpServer->stop();
        delete _httpServer;
        _httpServer = nullptr;
    }
    if (_wsTransport) {
        _wsTransport->stop();
        delete _wsTransport;
        _wsTransport = nullptr;
    }
#ifdef ESP32
    if (_bleTransport) {
        _bleTransport->stop();
        delete _bleTransport;
        _bleTransport = nullptr;
    }
#endif
    _initialized = false;
    _sessionId = "";
}

// ════════════════════════════════════════════════════════════════════════
// HTTP Handlers (Streamable HTTP Transport)
// ════════════════════════════════════════════════════════════════════════

void Server::_handleMCPPost() {
    transport::setCORSHeaders(*_httpServer);

    // Authentication check
    if (_auth.isEnabled() && !_auth.authenticate(*_httpServer)) {
        Auth::sendUnauthorized(*_httpServer);
        return;
    }

    // Rate limit check
    if (_rateLimiter.isEnabled() && !_rateLimiter.tryAcquire()) {
        _httpServer->send(429, transport::CONTENT_TYPE_JSON,
                          _jsonRpcError(JsonVariant(), -32000, "Rate limit exceeded"));
        return;
    }

    String body = _httpServer->arg("plain");
    if (body.isEmpty()) {
        _httpServer->send(400, transport::CONTENT_TYPE_JSON,
                          _jsonRpcError(JsonVariant(), -32700, "Parse error: empty body"));
        return;
    }

    // Check session ID for post-initialization requests
    if (_initialized && !_sessionId.isEmpty()) {
        String clientSession = _httpServer->header(transport::HEADER_SESSION_ID);
        // Allow initialize requests without session ID
        if (clientSession.length() > 0 && clientSession != _sessionId) {
            _httpServer->send(404, transport::CONTENT_TYPE_JSON,
                              _jsonRpcError(JsonVariant(), -32600, "Invalid session"));
            return;
        }
    }

    String response = _processJsonRpc(body);

    // For notifications (no id), return 202 Accepted
    if (response.isEmpty()) {
        _httpServer->send(202);
        return;
    }

    // Return as application/json (simple mode, no SSE needed for single responses)
    if (!_sessionId.isEmpty()) {
        _httpServer->sendHeader(transport::HEADER_SESSION_ID, _sessionId);
    }
    _httpServer->send(200, transport::CONTENT_TYPE_JSON, response);
}

void Server::_handleMCPGet() {
    transport::setCORSHeaders(*_httpServer);

    // Authentication check
    if (_auth.isEnabled() && !_auth.authenticate(*_httpServer)) {
        Auth::sendUnauthorized(*_httpServer);
        return;
    }

    // Check that the client wants SSE
    // Note: on ESP32 WebServer, we need to take over the client socket
    if (!_initialized || _sessionId.isEmpty()) {
        _httpServer->send(400, transport::CONTENT_TYPE_JSON,
                          _jsonRpcError(JsonVariant(), -32600, "Not initialized — call initialize first"));
        return;
    }

    // Validate session ID
    String clientSession = _httpServer->header(transport::HEADER_SESSION_ID);
    if (clientSession.length() > 0 && clientSession != _sessionId) {
        _httpServer->send(404, transport::CONTENT_TYPE_JSON,
                          _jsonRpcError(JsonVariant(), -32600, "Invalid session"));
        return;
    }

    // Take over the raw client socket for SSE
    WiFiClient client = _httpServer->client();
    if (_sseManager.addClient(client, _sessionId, _endpoint)) {
        Serial.println("[mcpd] SSE stream opened");
        // Prevent WebServer from sending its own response
        // (addClient already sent headers)
    } else {
        _httpServer->send(503, transport::CONTENT_TYPE_JSON,
                          _jsonRpcError(JsonVariant(), -32000, "Too many SSE connections"));
    }
}

void Server::_handleMCPDelete() {
    transport::setCORSHeaders(*_httpServer);

    // Authentication check
    if (_auth.isEnabled() && !_auth.authenticate(*_httpServer)) {
        Auth::sendUnauthorized(*_httpServer);
        return;
    }

    String clientSession = _httpServer->header(transport::HEADER_SESSION_ID);
    if (clientSession == _sessionId) {
        _initialized = false;
        _sessionId = "";
        _httpServer->send(200, transport::CONTENT_TYPE_JSON, "{}");
    } else {
        _httpServer->send(404);
    }
}

// ════════════════════════════════════════════════════════════════════════
// JSON-RPC Processing
// ════════════════════════════════════════════════════════════════════════

String Server::_processJsonRpc(const String& body) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
        return _jsonRpcError(JsonVariant(), -32700,
                             (String("Parse error: ") + err.c_str()).c_str());
    }

    // Check if it's a batch (array)
    if (doc.is<JsonArray>()) {
        JsonArray arr = doc.as<JsonArray>();
        String batchResponse = "[";
        bool first = true;
        bool hasRequests = false;

        for (JsonVariant item : arr) {
            JsonVariant id = item["id"];
            if (!id.isNull()) {
                hasRequests = true;
                if (!first) batchResponse += ",";
                batchResponse += _dispatch(item["method"], item["params"], id);
                first = false;
            }
            // Notifications (no id) are processed but don't produce responses
            else {
                const char* method = item["method"];
                if (method) _dispatch(method, item["params"], JsonVariant());
            }
        }

        if (!hasRequests) return ""; // All notifications → 202
        batchResponse += "]";
        return batchResponse;
    }

    // Single message
    const char* method = doc["method"];
    JsonVariant id = doc["id"];

    // Validate JSON-RPC version
    const char* version = doc["jsonrpc"];
    if (!version || strcmp(version, "2.0") != 0) {
        return _jsonRpcError(id, -32600, "Invalid Request: missing or wrong jsonrpc version");
    }

    // Check if this is a response to a server-initiated request (sampling or elicitation)
    if (!method && !id.isNull() && !doc["result"].isNull()) {
        int respId = id.as<int>();
        JsonObject result = doc["result"].as<JsonObject>();
        if (_samplingManager.handleResponse(respId, result)) {
            Serial.printf("[mcpd] Sampling response received (id: %d)\n", respId);
        } else if (_elicitationManager.handleResponse(respId, result)) {
            Serial.printf("[mcpd] Elicitation response received (id: %d)\n", respId);
        }
        return "";  // No response needed for responses
    }

    if (!method) {
        return _jsonRpcError(id, -32600, "Invalid Request: missing method");
    }

    String result = _dispatch(method, doc["params"], id);

    // If it's a notification (no id), return empty to trigger 202
    if (id.isNull()) return "";

    return result;
}

String Server::_dispatch(const char* method, JsonVariant params, JsonVariant id) {
    if (!method) {
        return _jsonRpcError(id, -32600, "Invalid Request");
    }

    String m(method);

    // Record metrics for each dispatched method
    unsigned long _dispatchStart = millis();

    auto _recordAndReturn = [&](const String& result) -> String {
        _metrics.recordRequest(m, millis() - _dispatchStart);
        return result;
    };

    if (m == "initialize")       return _recordAndReturn(_handleInitialize(params, id));
    if (m == "ping")             return _recordAndReturn(_handlePing(id));
    if (m == "tools/list")       return _recordAndReturn(_handleToolsList(params, id));
    if (m == "tools/call")       return _recordAndReturn(_handleToolsCall(params, id));
    if (m == "resources/list")   return _recordAndReturn(_handleResourcesList(params, id));
    if (m == "resources/read")   return _recordAndReturn(_handleResourcesRead(params, id));
    if (m == "resources/templates/list") return _recordAndReturn(_handleResourcesTemplatesList(params, id));
    if (m == "prompts/list")           return _recordAndReturn(_handlePromptsList(params, id));
    if (m == "prompts/get")            return _recordAndReturn(_handlePromptsGet(params, id));
    if (m == "logging/setLevel")       return _recordAndReturn(_handleLoggingSetLevel(params, id));
    if (m == "completion/complete")     return _recordAndReturn(_handleCompletionComplete(params, id));
    if (m == "resources/subscribe")     return _recordAndReturn(_handleResourcesSubscribe(params, id));
    if (m == "resources/unsubscribe")   return _recordAndReturn(_handleResourcesUnsubscribe(params, id));
    if (m == "roots/list")              return _recordAndReturn(_handleRootsList(params, id));

    // Tasks (experimental, MCP 2025-11-25)
    if (m == "tasks/get")               return _recordAndReturn(_handleTasksGet(params, id));
    if (m == "tasks/result")            return _recordAndReturn(_handleTasksResult(params, id));
    if (m == "tasks/list")              return _recordAndReturn(_handleTasksList(params, id));
    if (m == "tasks/cancel")            return _recordAndReturn(_handleTasksCancel(params, id));

    // notifications/initialized — no response needed
    if (m == "notifications/initialized") { _metrics.recordRequest(m, millis() - _dispatchStart); return ""; }
    // notifications/cancelled — cancel in-flight request
    if (m == "notifications/cancelled") {
        _metrics.recordRequest(m, millis() - _dispatchStart);
        if (!params.isNull()) {
            const char* rid = params["requestId"].as<const char*>();
            String reqId = rid ? rid : "";
            if (!reqId.isEmpty()) {
                _requestTracker.cancelRequest(reqId);
                Serial.printf("[mcpd] Request cancelled: %s\n", reqId.c_str());
            }
        }
        return "";
    }

    _metrics.recordError();
    return _jsonRpcError(id, -32601, "Method not found");
}

// ════════════════════════════════════════════════════════════════════════
// MCP Method Handlers
// ════════════════════════════════════════════════════════════════════════

String Server::_handleInitialize(JsonVariant params, JsonVariant id) {
    _sessionId = _generateSessionId();
    _initialized = true;

    // Extract client info and call lifecycle hook
    if (_onInitializeCb) {
        String clientName = "unknown";
        if (!params.isNull() && !params["clientInfo"].isNull()) {
            const char* cn = params["clientInfo"]["name"].as<const char*>();
            if (cn) clientName = cn;
        }
        _onInitializeCb(clientName);
    }

    JsonDocument result;
    result["protocolVersion"] = MCPD_MCP_PROTOCOL_VERSION;

    JsonObject serverInfo = result["serverInfo"].to<JsonObject>();
    serverInfo["name"] = _name;
    serverInfo["version"] = _version ? _version : MCPD_VERSION;
    if (_description) serverInfo["description"] = _description;
    if (_websiteUrl) serverInfo["websiteUrl"] = _websiteUrl;
    if (!_icons.empty()) {
        JsonArray iconsArr = serverInfo["icons"].to<JsonArray>();
        for (const auto& icon : _icons) {
            JsonObject iconObj = iconsArr.add<JsonObject>();
            icon.toJson(iconObj);
        }
    }

    // MCP 2025-03-26: instructions guide the LLM's behavior with this server
    if (_instructions) {
        result["instructions"] = _instructions;
    }

    JsonObject capabilities = result["capabilities"].to<JsonObject>();

    // Advertise tools capability with listChanged support
    if (!_tools.empty()) {
        JsonObject toolsCap = capabilities["tools"].to<JsonObject>();
        toolsCap["listChanged"] = true;
    }

    // Advertise resources capability with listChanged and subscribe support
    if (!_resources.empty() || !_resourceTemplates.empty()) {
        JsonObject resCap = capabilities["resources"].to<JsonObject>();
        resCap["listChanged"] = true;
        resCap["subscribe"] = true;
    }

    // Advertise prompts capability with listChanged support
    if (!_prompts.empty()) {
        JsonObject promptsCap = capabilities["prompts"].to<JsonObject>();
        promptsCap["listChanged"] = true;
    }

    // Advertise roots capability if roots are registered
    if (!_roots.empty()) {
        JsonObject rootsCap = capabilities["roots"].to<JsonObject>();
        rootsCap["listChanged"] = true;
    }

    // Advertise logging capability
    capabilities["logging"].to<JsonObject>();

    // Advertise sampling capability (server can request LLM inference from client)
    capabilities["sampling"].to<JsonObject>();

    // Advertise elicitation capability (server can request user input from client)
    capabilities["elicitation"].to<JsonObject>();

    // Advertise completion capability if providers are registered
    if (_completions.hasProviders()) {
        capabilities["completion"].to<JsonObject>();
    }

    // Advertise tasks capability (experimental, MCP 2025-11-25)
    if (_taskManager.isEnabled()) {
        JsonObject tasksCap = capabilities["tasks"].to<JsonObject>();
        tasksCap["list"].to<JsonObject>();
        tasksCap["cancel"].to<JsonObject>();
        JsonObject tasksReqs = tasksCap["requests"].to<JsonObject>();
        JsonObject tasksTools = tasksReqs["tools"].to<JsonObject>();
        tasksTools["call"].to<JsonObject>();
    }

    // Include rate limit info in server info if enabled
    if (_rateLimiter.isEnabled()) {
        JsonObject rateLimit = serverInfo["rateLimit"].to<JsonObject>();
        rateLimit["requestsPerSecond"] = _rateLimiter.requestsPerSecond();
        rateLimit["burstCapacity"] = (int)_rateLimiter.burstCapacity();
    }

    String resultStr;
    serializeJson(result, resultStr);
    return _jsonRpcResult(id, resultStr);
}

String Server::_handlePing(JsonVariant id) {
    return _jsonRpcResult(id, "{}");
}

String Server::_handleToolsList(JsonVariant params, JsonVariant id) {
    JsonDocument result;
    JsonArray tools = result["tools"].to<JsonArray>();

    // Cursor-based pagination
    size_t startIdx = 0;
    if (!params.isNull() && params["cursor"].is<const char*>()) {
        startIdx = (size_t)atoi(params["cursor"].as<const char*>());
    }

    size_t endIdx = _tools.size();
    if (_pageSize > 0 && (startIdx + _pageSize) < endIdx) {
        endIdx = startIdx + _pageSize;
        // Set nextCursor
        result["nextCursor"] = String(endIdx);
    }

    for (size_t i = startIdx; i < endIdx; i++) {
        // Skip disabled tools
        if (_disabledTools.count(_tools[i].name)) continue;
        JsonObject obj = tools.add<JsonObject>();
        _tools[i].toJson(obj);

        // Add execution.taskSupport if this tool has task support configured
        auto tsIt = _taskToolSupport.find(_tools[i].name);
        if (tsIt != _taskToolSupport.end() && tsIt->second != TaskSupport::Forbidden) {
            JsonObject execution = obj["execution"].to<JsonObject>();
            execution["taskSupport"] = taskSupportToString(tsIt->second);
        }
    }

    String resultStr;
    serializeJson(result, resultStr);
    return _jsonRpcResult(id, resultStr);
}

String Server::_handleToolsCall(JsonVariant params, JsonVariant id) {
    const char* toolName = params["name"];
    if (!toolName) {
        return _jsonRpcError(id, -32602, "Missing tool name");
    }

    // Extract progress token from _meta if present
    String progressToken;
    if (!params["_meta"].isNull() && !params["_meta"]["progressToken"].isNull()) {
        const char* pt = params["_meta"]["progressToken"].as<const char*>();
        if (pt) progressToken = pt;
    }

    // Track the request for cancellation support
    String requestId;
    if (!id.isNull()) {
        if (id.is<const char*>()) {
            requestId = id.as<const char*>();
        } else {
            requestId = String(id.as<long>());
        }
    }
    if (!requestId.isEmpty()) {
        _requestTracker.trackRequest(requestId, progressToken);
    }

    // Check for task-augmented request (MCP 2025-11-25)
    bool isTaskRequest = !params["task"].isNull();
    if (isTaskRequest && !_taskManager.isEnabled()) {
        if (!requestId.isEmpty()) _requestTracker.completeRequest(requestId);
        return _jsonRpcError(id, -32601, "Tasks not supported");
    }

    // Reject disabled tools
    if (_disabledTools.count(String(toolName))) {
        if (!requestId.isEmpty()) {
            _requestTracker.completeRequest(requestId);
        }
        return _jsonRpcError(id, -32602, "Tool not found");
    }

    // Find the tool
    for (const auto& tool : _tools) {
        if (tool.name == toolName) {
            JsonObject arguments = params["arguments"].as<JsonObject>();

            // Handle task-augmented request
            if (isTaskRequest) {
                // Check tool-level task support
                auto tsIt = _taskToolSupport.find(String(toolName));
                TaskSupport ts = (tsIt != _taskToolSupport.end()) ? tsIt->second : TaskSupport::Forbidden;
                if (ts == TaskSupport::Forbidden) {
                    if (!requestId.isEmpty()) _requestTracker.completeRequest(requestId);
                    return _jsonRpcError(id, -32601, "Tool does not support task execution");
                }

                // Check for async handler
                auto handlerIt = _taskToolHandlers.find(String(toolName));
                if (handlerIt == _taskToolHandlers.end()) {
                    if (!requestId.isEmpty()) _requestTracker.completeRequest(requestId);
                    return _jsonRpcError(id, -32601, "No async handler for tool");
                }

                // Extract requested TTL
                int64_t ttl = -1;
                if (!params["task"]["ttl"].isNull()) {
                    ttl = (int64_t)params["task"]["ttl"].as<long>();
                }

                // Create the task
                String taskId = _taskManager.createTask(toolName, ttl);
                MCPTask* task = _taskManager.getTask(taskId);

                // Before-call hook for task
                if (_beforeToolCallHook) {
                    ToolCallContext ctx;
                    ctx.toolName = toolName;
                    ctx.args = &arguments;
                    ctx.startMs = millis();
                    ctx.durationMs = 0;
                    ctx.isError = false;
                    if (!_beforeToolCallHook(ctx)) {
                        _taskManager.cancelTask(taskId);
                        if (!requestId.isEmpty()) _requestTracker.completeRequest(requestId);
                        return _jsonRpcError(id, -32600, "Tool call rejected");
                    }
                }

                // Invoke async handler
                handlerIt->second(taskId, params["arguments"]);

                // Return CreateTaskResult
                JsonDocument result;
                JsonObject taskObj = result["task"].to<JsonObject>();
                task->toJson(taskObj);

                if (!requestId.isEmpty()) _requestTracker.completeRequest(requestId);

                String resultStr;
                serializeJson(result, resultStr);
                return _jsonRpcResult(id, resultStr);
            }

            // Check if tool requires task execution
            auto tsIt = _taskToolSupport.find(String(toolName));
            if (tsIt != _taskToolSupport.end() && tsIt->second == TaskSupport::Required) {
                if (!requestId.isEmpty()) _requestTracker.completeRequest(requestId);
                return _jsonRpcError(id, -32601, "Tool requires task execution");
            }

            // Before-call hook: allow rejection
            if (_beforeToolCallHook) {
                ToolCallContext ctx;
                ctx.toolName = toolName;
                ctx.args = &arguments;
                ctx.startMs = millis();
                ctx.durationMs = 0;
                ctx.isError = false;
                if (!_beforeToolCallHook(ctx)) {
                    if (!requestId.isEmpty()) {
                        _requestTracker.completeRequest(requestId);
                    }
                    return _jsonRpcError(id, -32600, "Tool call rejected");
                }
            }

            unsigned long callStartMs = millis();

            // Check if this tool has a rich handler
            MCPRichToolHandler richHandler = nullptr;
            for (const auto& rh : _richTools) {
                if (rh.first == toolName) {
                    richHandler = rh.second;
                    break;
                }
            }

            String resultStr;
            bool callIsError = false;

            if (richHandler) {
                // Use rich handler for structured content
                MCPToolResult toolResult;
                try {
                    toolResult = richHandler(arguments);
                } catch (...) {
                    toolResult = MCPToolResult::error("Internal tool error");
                    callIsError = true;
                }

                JsonDocument result;
                JsonObject resultObj = result.to<JsonObject>();
                toolResult.toJson(resultObj);

                // If tool has outputSchema and first content is text, include structuredContent
                if (!tool.outputSchemaJson.isEmpty() && !toolResult.isError &&
                    !toolResult.content.empty() && toolResult.content[0].type == MCPContent::TEXT) {
                    JsonDocument structured;
                    DeserializationError err = deserializeJson(structured, toolResult.content[0].text);
                    if (!err) {
                        resultObj["structuredContent"] = structured.as<JsonVariant>();
                    }
                }

                serializeJson(result, resultStr);
                if (toolResult.isError) callIsError = true;
            } else {
                // Use simple handler (backward compatible)
                String handlerResult;
                try {
                    handlerResult = tool.handler(arguments);
                } catch (...) {
                    handlerResult = "Internal tool error";
                    callIsError = true;
                }

                JsonDocument result;
                JsonArray content = result["content"].to<JsonArray>();
                JsonObject textContent = content.add<JsonObject>();
                textContent["type"] = "text";
                textContent["text"] = handlerResult;
                if (callIsError) {
                    result["isError"] = true;
                }

                // If tool has outputSchema, include structuredContent
                if (!tool.outputSchemaJson.isEmpty() && !callIsError) {
                    JsonDocument structured;
                    DeserializationError err = deserializeJson(structured, handlerResult);
                    if (!err) {
                        result["structuredContent"] = structured.as<JsonVariant>();
                    }
                }

                serializeJson(result, resultStr);
            }

            // After-call hook: logging/metrics
            if (_afterToolCallHook) {
                ToolCallContext ctx;
                ctx.toolName = toolName;
                ctx.args = &arguments;
                ctx.startMs = callStartMs;
                ctx.durationMs = millis() - callStartMs;
                ctx.isError = callIsError;
                _afterToolCallHook(ctx);
            }

            // Complete request tracking
            if (!requestId.isEmpty()) {
                _requestTracker.completeRequest(requestId);
            }

            return _jsonRpcResult(id, resultStr);
        }
    }

    if (!requestId.isEmpty()) {
        _requestTracker.completeRequest(requestId);
    }

    return _jsonRpcError(id, -32602,
        (String("Tool not found: ") + toolName).c_str());
}

String Server::_handleResourcesList(JsonVariant params, JsonVariant id) {
    JsonDocument result;
    JsonArray resources = result["resources"].to<JsonArray>();

    size_t startIdx = 0;
    if (!params.isNull() && params["cursor"].is<const char*>()) {
        startIdx = (size_t)atoi(params["cursor"].as<const char*>());
    }

    size_t endIdx = _resources.size();
    if (_pageSize > 0 && (startIdx + _pageSize) < endIdx) {
        endIdx = startIdx + _pageSize;
        result["nextCursor"] = String(endIdx);
    }

    for (size_t i = startIdx; i < endIdx; i++) {
        JsonObject obj = resources.add<JsonObject>();
        _resources[i].toJson(obj);
    }

    String resultStr;
    serializeJson(result, resultStr);
    return _jsonRpcResult(id, resultStr);
}

String Server::_handleResourcesRead(JsonVariant params, JsonVariant id) {
    const char* uri = params["uri"];
    if (!uri) {
        return _jsonRpcError(id, -32602, "Missing resource URI");
    }

    for (const auto& res : _resources) {
        if (res.uri == uri) {
            String content = res.handler();

            JsonDocument result;
            JsonArray contents = result["contents"].to<JsonArray>();
            JsonObject item = contents.add<JsonObject>();
            item["uri"] = res.uri;
            item["mimeType"] = res.mimeType;
            item["text"] = content;

            String resultStr;
            serializeJson(result, resultStr);
            return _jsonRpcResult(id, resultStr);
        }
    }

    // Try matching against resource templates
    for (const auto& tmpl : _resourceTemplates) {
        std::map<String, String> templateVars;
        if (tmpl.match(String(uri), templateVars)) {
            String content = tmpl.handler(templateVars);

            JsonDocument result;
            JsonArray contents = result["contents"].to<JsonArray>();
            JsonObject item = contents.add<JsonObject>();
            item["uri"] = uri;
            item["mimeType"] = tmpl.mimeType;
            item["text"] = content;

            String resultStr;
            serializeJson(result, resultStr);
            return _jsonRpcResult(id, resultStr);
        }
    }

    return _jsonRpcError(id, -32602, "Resource not found");
}

String Server::_handleResourcesTemplatesList(JsonVariant params, JsonVariant id) {
    JsonDocument result;
    JsonArray templates = result["resourceTemplates"].to<JsonArray>();

    size_t startIdx = 0;
    if (!params.isNull() && params["cursor"].is<const char*>()) {
        startIdx = (size_t)atoi(params["cursor"].as<const char*>());
    }

    size_t endIdx = _resourceTemplates.size();
    if (_pageSize > 0 && (startIdx + _pageSize) < endIdx) {
        endIdx = startIdx + _pageSize;
        result["nextCursor"] = String(endIdx);
    }

    for (size_t i = startIdx; i < endIdx; i++) {
        JsonObject obj = templates.add<JsonObject>();
        _resourceTemplates[i].toJson(obj);
    }

    String resultStr;
    serializeJson(result, resultStr);
    return _jsonRpcResult(id, resultStr);
}

String Server::_handlePromptsList(JsonVariant params, JsonVariant id) {
    JsonDocument result;
    JsonArray prompts = result["prompts"].to<JsonArray>();

    size_t startIdx = 0;
    if (!params.isNull() && params["cursor"].is<const char*>()) {
        startIdx = (size_t)atoi(params["cursor"].as<const char*>());
    }

    size_t endIdx = _prompts.size();
    if (_pageSize > 0 && (startIdx + _pageSize) < endIdx) {
        endIdx = startIdx + _pageSize;
        result["nextCursor"] = String(endIdx);
    }

    for (size_t i = startIdx; i < endIdx; i++) {
        JsonObject obj = prompts.add<JsonObject>();
        _prompts[i].toJson(obj);
    }

    String resultStr;
    serializeJson(result, resultStr);
    return _jsonRpcResult(id, resultStr);
}

String Server::_handlePromptsGet(JsonVariant params, JsonVariant id) {
    const char* promptName = params["name"];
    if (!promptName) {
        return _jsonRpcError(id, -32602, "Missing prompt name");
    }

    for (const auto& prompt : _prompts) {
        if (prompt.name == promptName) {
            // Extract arguments from params
            std::map<String, String> arguments;
            JsonObject argsObj = params["arguments"].as<JsonObject>();
            if (!argsObj.isNull()) {
                for (JsonPair kv : argsObj) {
                    const char* val = kv.value().as<const char*>();
                    arguments[String(kv.key().c_str())] = val ? val : "";
                }
            }

            // Check required arguments
            for (const auto& argDef : prompt.arguments) {
                if (argDef.required && arguments.find(argDef.name) == arguments.end()) {
                    return _jsonRpcError(id, -32602,
                        (String("Missing required argument: ") + argDef.name).c_str());
                }
            }

            // Call the handler
            std::vector<MCPPromptMessage> messages = prompt.handler(arguments);

            // Build response
            JsonDocument result;
            result["description"] = prompt.description;
            JsonArray msgsArr = result["messages"].to<JsonArray>();

            for (const auto& msg : messages) {
                JsonObject msgObj = msgsArr.add<JsonObject>();
                msg.toJson(msgObj);
            }

            String resultStr;
            serializeJson(result, resultStr);
            return _jsonRpcResult(id, resultStr);
        }
    }

    return _jsonRpcError(id, -32602,
        (String("Prompt not found: ") + promptName).c_str());
}

String Server::_handleLoggingSetLevel(JsonVariant params, JsonVariant id) {
    const char* level = params["level"];
    if (!level) {
        return _jsonRpcError(id, -32602, "Missing level parameter");
    }

    _logging.setLevel(logLevelFromString(level));
    Serial.printf("[mcpd] Log level set to: %s\n", level);

    return _jsonRpcResult(id, "{}");
}

String Server::_handleCompletionComplete(JsonVariant params, JsonVariant id) {
    // Extract ref (what we're completing for)
    JsonObject ref = params["ref"].as<JsonObject>();
    if (ref.isNull()) {
        return _jsonRpcError(id, -32602, "Missing ref parameter");
    }

    const char* refType = ref["type"];
    if (!refType) {
        return _jsonRpcError(id, -32602, "Missing ref.type");
    }

    const char* argumentName = params["argument"]["name"];
    const char* argumentValue = params["argument"]["value"];
    if (!argumentName || !argumentValue) {
        return _jsonRpcError(id, -32602, "Missing argument.name or argument.value");
    }

    String partial(argumentValue);
    bool hasMore = false;
    std::vector<String> values;

    String type(refType);
    if (type == "ref/prompt") {
        const char* promptName = ref["name"];
        if (!promptName) {
            return _jsonRpcError(id, -32602, "Missing ref.name for prompt completion");
        }
        values = _completions.completePrompt(String(promptName),
                                              String(argumentName), partial, hasMore);
    } else if (type == "ref/resource") {
        const char* uri = ref["uri"];
        if (!uri) {
            return _jsonRpcError(id, -32602, "Missing ref.uri for resource template completion");
        }
        values = _completions.completeResourceTemplate(String(uri),
                                                        String(argumentName), partial, hasMore);
    } else {
        return _jsonRpcError(id, -32602, "Unknown ref type");
    }

    JsonDocument result;
    JsonObject completion = result["completion"].to<JsonObject>();
    JsonArray valuesArr = completion["values"].to<JsonArray>();
    for (const auto& v : values) {
        valuesArr.add(v);
    }
    completion["total"] = values.size();
    completion["hasMore"] = hasMore;

    String resultStr;
    serializeJson(result, resultStr);
    return _jsonRpcResult(id, resultStr);
}

String Server::_handleResourcesSubscribe(JsonVariant params, JsonVariant id) {
    const char* uri = params["uri"];
    if (!uri) {
        return _jsonRpcError(id, -32602, "Missing resource URI");
    }

    // Add to subscriptions (set handles dedup)
    _subscribedResources.insert(String(uri));

    return _jsonRpcResult(id, "{}");
}

String Server::_handleResourcesUnsubscribe(JsonVariant params, JsonVariant id) {
    const char* uri = params["uri"];
    if (!uri) {
        return _jsonRpcError(id, -32602, "Missing resource URI");
    }

    _subscribedResources.erase(String(uri));

    return _jsonRpcResult(id, "{}");
}

void Server::notifyResourceUpdated(const char* uri) {
    // Only notify if this resource is subscribed
    if (_subscribedResources.find(String(uri)) == _subscribedResources.end()) return;

    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["method"] = "notifications/resources/updated";
    JsonObject params = doc["params"].to<JsonObject>();
    params["uri"] = uri;

    String output;
    serializeJson(doc, output);
    _pendingNotifications.push_back(output);
}

String Server::_handleRootsList(JsonVariant /* params */, JsonVariant id) {
    JsonDocument result;
    JsonArray roots = result["roots"].to<JsonArray>();

    for (const auto& root : _roots) {
        JsonObject obj = roots.add<JsonObject>();
        root.toJson(obj);
    }

    String resultStr;
    serializeJson(result, resultStr);
    return _jsonRpcResult(id, resultStr);
}

// ════════════════════════════════════════════════════════════════════════
// Tasks (experimental, MCP 2025-11-25)
// ════════════════════════════════════════════════════════════════════════

void Server::addTaskTool(const char* name, const char* description,
                         const char* inputSchemaJson, MCPTaskToolHandler handler,
                         TaskSupport support) {
    // Register as a normal tool with a placeholder handler
    _tools.emplace_back(name, description, inputSchemaJson,
        [](const JsonObject&) -> String { return "{}"; });
    _taskToolHandlers[String(name)] = handler;
    _taskToolSupport[String(name)] = support;
}

bool Server::taskComplete(const String& taskId, const String& resultJson) {
    bool ok = _taskManager.completeTask(taskId, resultJson);
    if (ok) {
        // Send status notification
        MCPTask* task = _taskManager.getTask(taskId);
        if (task) {
            JsonDocument doc;
            doc["jsonrpc"] = "2.0";
            doc["method"] = "notifications/tasks/status";
            JsonObject params = doc["params"].to<JsonObject>();
            task->toJson(params);
            String output;
            serializeJson(doc, output);
            _pendingNotifications.push_back(output);
        }
    }
    return ok;
}

bool Server::taskFail(const String& taskId, const String& errorMessage) {
    bool ok = _taskManager.failTask(taskId, errorMessage);
    if (ok) {
        MCPTask* task = _taskManager.getTask(taskId);
        if (task) {
            JsonDocument doc;
            doc["jsonrpc"] = "2.0";
            doc["method"] = "notifications/tasks/status";
            JsonObject params = doc["params"].to<JsonObject>();
            task->toJson(params);
            String output;
            serializeJson(doc, output);
            _pendingNotifications.push_back(output);
        }
    }
    return ok;
}

bool Server::taskCancel(const String& taskId) {
    bool ok = _taskManager.cancelTask(taskId);
    if (ok) {
        MCPTask* task = _taskManager.getTask(taskId);
        if (task) {
            JsonDocument doc;
            doc["jsonrpc"] = "2.0";
            doc["method"] = "notifications/tasks/status";
            JsonObject params = doc["params"].to<JsonObject>();
            task->toJson(params);
            String output;
            serializeJson(doc, output);
            _pendingNotifications.push_back(output);
        }
    }
    return ok;
}

String Server::_handleTasksGet(JsonVariant params, JsonVariant id) {
    const char* taskId = params["taskId"];
    if (!taskId) {
        return _jsonRpcError(id, -32602, "Missing taskId");
    }

    MCPTask* task = _taskManager.getTask(String(taskId));
    if (!task) {
        return _jsonRpcError(id, -32602, "Task not found");
    }

    JsonDocument result;
    JsonObject taskObj = result.to<JsonObject>(); task->toJson(taskObj);

    String resultStr;
    serializeJson(result, resultStr);
    return _jsonRpcResult(id, resultStr);
}

String Server::_handleTasksResult(JsonVariant params, JsonVariant id) {
    const char* taskId = params["taskId"];
    if (!taskId) {
        return _jsonRpcError(id, -32602, "Missing taskId");
    }

    MCPTask* task = _taskManager.getTask(String(taskId));
    if (!task) {
        return _jsonRpcError(id, -32602, "Task not found");
    }

    if (!isTerminalStatus(task->status)) {
        // Task not yet complete — return current status as error
        return _jsonRpcError(id, -32002, "Task not yet complete");
    }

    if (task->status == TaskStatus::Failed) {
        return _jsonRpcError(id, -32000, task->statusMessage.c_str());
    }

    if (task->status == TaskStatus::Cancelled) {
        return _jsonRpcError(id, -32000, "Task was cancelled");
    }

    // Return the stored result with related-task metadata
    if (task->hasResult) {
        // Parse and augment with _meta
        JsonDocument resultDoc;
        deserializeJson(resultDoc, task->resultJson);
        JsonObject meta = resultDoc["_meta"].to<JsonObject>();
        JsonObject relatedTask = meta["io.modelcontextprotocol/related-task"].to<JsonObject>();
        relatedTask["taskId"] = task->taskId;

        String resultStr;
        serializeJson(resultDoc, resultStr);
        return _jsonRpcResult(id, resultStr);
    }

    return _jsonRpcResult(id, "{}");
}

String Server::_handleTasksList(JsonVariant params, JsonVariant id) {
    size_t startIdx = 0;
    if (!params.isNull() && params["cursor"].is<const char*>()) {
        startIdx = (size_t)atoi(params["cursor"].as<const char*>());
    }

    size_t nextIdx = 0;
    auto tasks = _taskManager.listTasks(startIdx, 20, &nextIdx);

    JsonDocument result;
    JsonArray tasksArr = result["tasks"].to<JsonArray>();
    for (const auto& task : tasks) {
        JsonObject obj = tasksArr.add<JsonObject>();
        task.toJson(obj);
    }

    if (nextIdx > 0) {
        result["nextCursor"] = String(nextIdx);
    }

    String resultStr;
    serializeJson(result, resultStr);
    return _jsonRpcResult(id, resultStr);
}

String Server::_handleTasksCancel(JsonVariant params, JsonVariant id) {
    const char* taskIdStr = params["taskId"];
    if (!taskIdStr) {
        return _jsonRpcError(id, -32602, "Missing taskId");
    }

    String taskId(taskIdStr);
    if (!_taskManager.cancelTask(taskId)) {
        return _jsonRpcError(id, -32602, "Task not found or already terminal");
    }

    MCPTask* task = _taskManager.getTask(taskId);
    if (!task) {
        return _jsonRpcError(id, -32602, "Task not found");
    }

    JsonDocument result;
    JsonObject taskObj = result.to<JsonObject>(); task->toJson(taskObj);

    String resultStr;
    serializeJson(result, resultStr);
    return _jsonRpcResult(id, resultStr);
}

// ════════════════════════════════════════════════════════════════════════
// Progress Notifications
// ════════════════════════════════════════════════════════════════════════

int Server::requestSampling(const MCPSamplingRequest& request, MCPSamplingCallback callback) {
    // Sampling requires an SSE connection for server-to-client messages
    if (!_sseManager.hasClients(_sessionId)) {
        Serial.println("[mcpd] Cannot send sampling request: no SSE clients connected");
        return -1;
    }

    int id = _samplingManager.queueRequest(request, callback);
    Serial.printf("[mcpd] Sampling request queued (id: %d)\n", id);
    return id;
}

int Server::requestElicitation(const MCPElicitationRequest& request,
                               MCPElicitationCallback callback) {
    if (!_sseManager.hasClients(_sessionId)) {
        Serial.println("[mcpd] Cannot send elicitation request: no SSE clients connected");
        return -1;
    }

    int id = _elicitationManager.queueRequest(request, callback);
    Serial.printf("[mcpd] Elicitation request queued (id: %d)\n", id);
    return id;
}

void Server::reportProgress(const String& progressToken, double progress,
                            double total, const String& message) {
    if (progressToken.isEmpty()) return;

    ProgressNotification pn;
    pn.progressToken = progressToken;
    pn.progress = progress;
    pn.total = total;
    pn.message = message;
    _pendingNotifications.push_back(pn.toJsonRpc());
}

// ════════════════════════════════════════════════════════════════════════
// Helpers
// ════════════════════════════════════════════════════════════════════════

String Server::_jsonRpcResult(JsonVariant id, const String& resultJson) {
    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    if (!id.isNull()) {
        doc["id"] = id;
    }
    // Parse result JSON and embed it
    JsonDocument resultDoc;
    deserializeJson(resultDoc, resultJson);
    doc["result"] = resultDoc.as<JsonVariant>();

    String output;
    serializeJson(doc, output);
    return output;
}

String Server::jsonRpcErrorWithData(JsonVariant id, int code, const char* message,
                                    const String& dataJson) {
    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    if (!id.isNull()) {
        doc["id"] = id;
    } else {
        doc["id"] = nullptr;
    }
    JsonObject error = doc["error"].to<JsonObject>();
    error["code"] = code;
    error["message"] = message;

    if (!dataJson.isEmpty()) {
        JsonDocument dataDoc;
        deserializeJson(dataDoc, dataJson);
        error["data"] = dataDoc.as<JsonVariant>();
    }

    String output;
    serializeJson(doc, output);
    return output;
}

String Server::_jsonRpcError(JsonVariant id, int code, const char* message) {
    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    if (!id.isNull()) {
        doc["id"] = id;
    } else {
        doc["id"] = nullptr;
    }
    JsonObject error = doc["error"].to<JsonObject>();
    error["code"] = code;
    error["message"] = message;

    String output;
    serializeJson(doc, output);
    return output;
}

String Server::_generateSessionId() {
    // Generate a simple session ID from random bytes
    String sid = "";
    for (int i = 0; i < 16; i++) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02x", (uint8_t)random(256));
        sid += hex;
    }
    return sid;
}

} // namespace mcpd
