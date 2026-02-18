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
#include "MCPPrompt.h"
#include "MCPLogging.h"
#include "MCPCompletion.h"
#include "MCPTransport.h"

#define MCPD_VERSION "0.4.0"
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

    // ── Prompt registration ────────────────────────────────────────────

    /**
     * Register a prompt with arguments and a handler that returns messages.
     */
    void addPrompt(const char* name, const char* description,
                   std::vector<MCPPromptArgument> arguments,
                   MCPPromptHandler handler);

    void addPrompt(const MCPPrompt& prompt);

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

    /** Access the logging subsystem */
    Logging& logging() { return _logging; }

    /** Access the completion manager for registering autocomplete providers */
    CompletionManager& completions() { return _completions; }

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
    size_t _pageSize = 0;  // 0 = no pagination

    WebServer* _httpServer = nullptr;
    Logging _logging;
    CompletionManager _completions;

    std::vector<MCPTool> _tools;
    std::vector<MCPResource> _resources;
    std::vector<MCPResourceTemplate> _resourceTemplates;
    std::vector<MCPPrompt> _prompts;

    // Pending notifications to send
    std::vector<String> _pendingNotifications;

    // Resource subscriptions: URI → subscribed
    std::vector<String> _subscribedResources;

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

    // ── Helpers ────────────────────────────────────────────────────────

    String _jsonRpcResult(JsonVariant id, const String& resultJson);
    String _jsonRpcError(JsonVariant id, int code, const char* message);
    String _generateSessionId();
};

} // namespace mcpd

#endif // MCPD_H
