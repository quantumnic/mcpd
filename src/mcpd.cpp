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

    Serial.printf("[mcpd] Server '%s' started on port %d, endpoint %s\n",
                  _name, _port, _endpoint);
}

void Server::loop() {
    if (_httpServer) {
        _httpServer->handleClient();
    }
}

void Server::stop() {
    if (_httpServer) {
        _httpServer->stop();
        delete _httpServer;
        _httpServer = nullptr;
    }
    _initialized = false;
    _sessionId = "";
}

// ════════════════════════════════════════════════════════════════════════
// HTTP Handlers (Streamable HTTP Transport)
// ════════════════════════════════════════════════════════════════════════

void Server::_handleMCPPost() {
    transport::setCORSHeaders(*_httpServer);

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
    // GET endpoint for SSE stream — not yet implemented (returns 405)
    // For basic servers, POST-only is sufficient per spec
    _httpServer->send(405, transport::CONTENT_TYPE_JSON,
                      _jsonRpcError(JsonVariant(), -32601, "SSE stream not supported yet"));
}

void Server::_handleMCPDelete() {
    transport::setCORSHeaders(*_httpServer);

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

    if (m == "initialize")       return _handleInitialize(params, id);
    if (m == "ping")             return _handlePing(id);
    if (m == "tools/list")       return _handleToolsList(params, id);
    if (m == "tools/call")       return _handleToolsCall(params, id);
    if (m == "resources/list")   return _handleResourcesList(params, id);
    if (m == "resources/read")   return _handleResourcesRead(params, id);
    if (m == "resources/templates/list") return _handleResourcesTemplatesList(params, id);
    if (m == "prompts/list")           return _handlePromptsList(params, id);
    if (m == "prompts/get")            return _handlePromptsGet(params, id);
    if (m == "logging/setLevel")       return _handleLoggingSetLevel(params, id);
    if (m == "completion/complete")     return _handleCompletionComplete(params, id);
    if (m == "resources/subscribe")     return _handleResourcesSubscribe(params, id);
    if (m == "resources/unsubscribe")   return _handleResourcesUnsubscribe(params, id);
    if (m == "roots/list")              return _handleRootsList(params, id);

    // notifications/initialized — no response needed
    if (m == "notifications/initialized") return "";
    // notifications/cancelled — cancel in-flight request
    if (m == "notifications/cancelled") {
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

    return _jsonRpcError(id, -32601, "Method not found");
}

// ════════════════════════════════════════════════════════════════════════
// MCP Method Handlers
// ════════════════════════════════════════════════════════════════════════

String Server::_handleInitialize(JsonVariant params, JsonVariant id) {
    _sessionId = _generateSessionId();
    _initialized = true;

    JsonDocument result;
    result["protocolVersion"] = MCPD_MCP_PROTOCOL_VERSION;

    JsonObject serverInfo = result["serverInfo"].to<JsonObject>();
    serverInfo["name"] = _name;
    serverInfo["version"] = MCPD_VERSION;

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

    // Advertise completion capability if providers are registered
    if (_completions.hasProviders()) {
        capabilities["completion"].to<JsonObject>();
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
        JsonObject obj = tools.add<JsonObject>();
        _tools[i].toJson(obj);
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

    // Find the tool
    for (const auto& tool : _tools) {
        if (tool.name == toolName) {
            JsonObject arguments = params["arguments"].as<JsonObject>();

            // Check if this tool has a rich handler
            MCPRichToolHandler richHandler = nullptr;
            for (const auto& rh : _richTools) {
                if (rh.first == toolName) {
                    richHandler = rh.second;
                    break;
                }
            }

            String resultStr;

            if (richHandler) {
                // Use rich handler for structured content
                MCPToolResult toolResult;
                try {
                    toolResult = richHandler(arguments);
                } catch (...) {
                    toolResult = MCPToolResult::error("Internal tool error");
                }

                JsonDocument result;
                JsonObject resultObj = result.to<JsonObject>();
                toolResult.toJson(resultObj);
                serializeJson(result, resultStr);
            } else {
                // Use simple handler (backward compatible)
                String handlerResult;
                bool isError = false;
                try {
                    handlerResult = tool.handler(arguments);
                } catch (...) {
                    handlerResult = "Internal tool error";
                    isError = true;
                }

                JsonDocument result;
                JsonArray content = result["content"].to<JsonArray>();
                JsonObject textContent = content.add<JsonObject>();
                textContent["type"] = "text";
                textContent["text"] = handlerResult;
                if (isError) {
                    result["isError"] = true;
                }
                serializeJson(result, resultStr);
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
        std::map<String, String> params;
        if (tmpl.match(String(uri), params)) {
            String content = tmpl.handler(params);

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

    // Add to subscriptions if not already present
    String uriStr(uri);
    bool found = false;
    for (const auto& sub : _subscribedResources) {
        if (sub == uriStr) { found = true; break; }
    }
    if (!found) {
        _subscribedResources.push_back(uriStr);
    }

    return _jsonRpcResult(id, "{}");
}

String Server::_handleResourcesUnsubscribe(JsonVariant params, JsonVariant id) {
    const char* uri = params["uri"];
    if (!uri) {
        return _jsonRpcError(id, -32602, "Missing resource URI");
    }

    String uriStr(uri);
    for (auto it = _subscribedResources.begin(); it != _subscribedResources.end(); ++it) {
        if (*it == uriStr) {
            _subscribedResources.erase(it);
            break;
        }
    }

    return _jsonRpcResult(id, "{}");
}

void Server::notifyResourceUpdated(const char* uri) {
    // Only notify if this resource is subscribed
    String uriStr(uri);
    bool subscribed = false;
    for (const auto& sub : _subscribedResources) {
        if (sub == uriStr) { subscribed = true; break; }
    }
    if (!subscribed) return;

    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["method"] = "notifications/resources/updated";
    JsonObject params = doc["params"].to<JsonObject>();
    params["uri"] = uri;

    String output;
    serializeJson(doc, output);
    _pendingNotifications.push_back(output);
}

String Server::_handleRootsList(JsonVariant params, JsonVariant id) {
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
// Progress Notifications
// ════════════════════════════════════════════════════════════════════════

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
