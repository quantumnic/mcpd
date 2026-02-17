/**
 * mcpd — MCP Prompt definition
 *
 * Prompts are reusable prompt templates that clients can discover and use.
 * MCP Spec 2025-03-26: prompts/list, prompts/get
 */

#ifndef MCPD_PROMPT_H
#define MCPD_PROMPT_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include <vector>
#include <map>

namespace mcpd {

/**
 * A single argument definition for a prompt.
 */
struct MCPPromptArgument {
    String name;
    String description;
    bool required;

    MCPPromptArgument() : required(false) {}

    MCPPromptArgument(const char* name, const char* description, bool required = false)
        : name(name), description(description), required(required) {}
};

/**
 * A single message in a prompt's output.
 */
struct MCPPromptMessage {
    String role;     // "user" or "assistant"
    String type;     // "text" or "resource"
    String text;     // For text content
    String uri;      // For resource content (embedded resource)
    String mimeType; // For resource content

    MCPPromptMessage() = default;

    // Text message constructor
    MCPPromptMessage(const char* role, const char* text)
        : role(role), type("text"), text(text) {}

    // Resource message constructor
    static MCPPromptMessage resource(const char* role, const char* uri,
                                      const char* text, const char* mimeType = "text/plain") {
        MCPPromptMessage msg;
        msg.role = role;
        msg.type = "resource";
        msg.uri = uri;
        msg.text = text;
        msg.mimeType = mimeType;
        return msg;
    }

    void toJson(JsonObject& obj) const {
        obj["role"] = role;
        JsonObject content = obj["content"].to<JsonObject>();
        content["type"] = type;
        if (type == "text") {
            content["text"] = text;
        } else if (type == "resource") {
            JsonObject res = content["resource"].to<JsonObject>();
            res["uri"] = uri;
            res["mimeType"] = mimeType;
            res["text"] = text;
        }
    }
};

/**
 * Prompt handler function.
 * Receives argument values and returns a vector of messages.
 */
using MCPPromptHandler = std::function<std::vector<MCPPromptMessage>(
    const std::map<String, String>& arguments)>;

/**
 * Represents a single MCP prompt.
 */
struct MCPPrompt {
    String name;
    String description;
    std::vector<MCPPromptArgument> arguments;
    MCPPromptHandler handler;

    MCPPrompt() = default;

    MCPPrompt(const char* name, const char* description,
              std::vector<MCPPromptArgument> arguments,
              MCPPromptHandler handler)
        : name(name), description(description),
          arguments(std::move(arguments)), handler(handler) {}

    /**
     * Serialize for prompts/list response.
     */
    void toJson(JsonObject& obj) const {
        obj["name"] = name;
        obj["description"] = description;

        if (!arguments.empty()) {
            JsonArray args = obj["arguments"].to<JsonArray>();
            for (const auto& arg : arguments) {
                JsonObject argObj = args.add<JsonObject>();
                argObj["name"] = arg.name;
                argObj["description"] = arg.description;
                argObj["required"] = arg.required;
            }
        }
    }
};

} // namespace mcpd

#endif // MCPD_PROMPT_H
