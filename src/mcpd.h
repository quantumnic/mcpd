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

#include "MCPTool.h"
#include "MCPResource.h"
#include "MCPResourceTemplate.h"
#include "MCPTransport.h"

#define MCPD_VERSION "0.1.0"
#define MCPD_MCP_PROTOCOL_VERSION "2025-03-26"

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

    /** Get server name */
    const char* getName() const { return _name; }

    /** Get port */
    uint16_t getPort() const { return _port; }

#ifdef MCPD_TEST
public:  // Allow test access to internals
#else
private:
#endif
    const char* _name;
    uint16_t _port;
    const char* _endpoint = "/mcp";
    bool _mdnsEnabled = true;
    String _sessionId;
    bool _initialized = false;

    WebServer* _httpServer = nullptr;

    std::vector<MCPTool> _tools;
    std::vector<MCPResource> _resources;
    std::vector<MCPResourceTemplate> _resourceTemplates;

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
    String _handlePing(JsonVariant id);

    // ── Helpers ────────────────────────────────────────────────────────

    String _jsonRpcResult(JsonVariant id, const String& resultJson);
    String _jsonRpcError(JsonVariant id, int code, const char* message);
    String _generateSessionId();
};

} // namespace mcpd

#endif // MCPD_H
