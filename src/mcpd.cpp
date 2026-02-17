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

// ════════════════════════════════════════════════════════════════════════
// Configuration
// ════════════════════════════════════════════════════════════════════════

void Server::setEndpoint(const char* path) { _endpoint = path; }
void Server::setMDNS(bool enabled) { _mdnsEnabled = enabled; }

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

    // notifications/initialized — no response needed
    if (m == "notifications/initialized") return "";

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

    // Advertise tools capability
    if (!_tools.empty()) {
        capabilities["tools"].to<JsonObject>();
    }

    // Advertise resources capability
    if (!_resources.empty() || !_resourceTemplates.empty()) {
        capabilities["resources"].to<JsonObject>();
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

    for (const auto& tool : _tools) {
        JsonObject obj = tools.add<JsonObject>();
        tool.toJson(obj);
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

    // Find the tool
    for (const auto& tool : _tools) {
        if (tool.name == toolName) {
            JsonObject arguments = params["arguments"].as<JsonObject>();

            // Call the handler with error catching
            String handlerResult;
            bool isError = false;
            try {
                handlerResult = tool.handler(arguments);
            } catch (...) {
                handlerResult = "Internal tool error";
                isError = true;
            }

            // Build MCP result with content array
            JsonDocument result;
            JsonArray content = result["content"].to<JsonArray>();
            JsonObject textContent = content.add<JsonObject>();
            textContent["type"] = "text";
            textContent["text"] = handlerResult;
            if (isError) {
                result["isError"] = true;
            }

            String resultStr;
            serializeJson(result, resultStr);
            return _jsonRpcResult(id, resultStr);
        }
    }

    return _jsonRpcError(id, -32602,
        (String("Tool not found: ") + toolName).c_str());
}

String Server::_handleResourcesList(JsonVariant params, JsonVariant id) {
    JsonDocument result;
    JsonArray resources = result["resources"].to<JsonArray>();

    for (const auto& res : _resources) {
        JsonObject obj = resources.add<JsonObject>();
        res.toJson(obj);
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

    for (const auto& tmpl : _resourceTemplates) {
        JsonObject obj = templates.add<JsonObject>();
        tmpl.toJson(obj);
    }

    String resultStr;
    serializeJson(result, resultStr);
    return _jsonRpcResult(id, resultStr);
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
