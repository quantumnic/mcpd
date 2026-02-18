/**
 * mcpd — MCP Content Types
 *
 * Structured content types for tool results: text, image, and embedded resource.
 * Allows tools to return rich responses beyond plain text.
 *
 * MCP 2025-03-26 spec defines three content types:
 *   - TextContent:     { type: "text", text: "..." }
 *   - ImageContent:    { type: "image", data: "base64...", mimeType: "image/png" }
 *   - AudioContent:    { type: "audio", data: "base64...", mimeType: "audio/wav" }
 *   - ResourceContent: { type: "resource", resource: { uri, mimeType, text|blob } }
 */

#ifndef MCPD_CONTENT_H
#define MCPD_CONTENT_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

namespace mcpd {

/**
 * A single content item in a tool result.
 */
struct MCPContent {
    enum Type { TEXT, IMAGE, AUDIO, RESOURCE };

    Type type;
    String text;        // For TEXT: the text content
    String data;        // For IMAGE: base64-encoded image data
    String mimeType;    // For IMAGE or RESOURCE
    String uri;         // For RESOURCE: resource URI
    String blob;        // For RESOURCE: base64-encoded binary (alternative to text)

    // Factory: text content
    static MCPContent makeText(const String& text) {
        MCPContent c;
        c.type = TEXT;
        c.text = text;
        return c;
    }

    // Factory: image content
    static MCPContent makeImage(const String& base64Data, const String& mimeType) {
        MCPContent c;
        c.type = IMAGE;
        c.data = base64Data;
        c.mimeType = mimeType;
        return c;
    }

    // Factory: audio content
    static MCPContent makeAudio(const String& base64Data, const String& mimeType) {
        MCPContent c;
        c.type = AUDIO;
        c.data = base64Data;
        c.mimeType = mimeType;
        return c;
    }

    // Factory: embedded resource (text)
    static MCPContent makeResource(const String& uri, const String& mimeType,
                                   const String& text) {
        MCPContent c;
        c.type = RESOURCE;
        c.uri = uri;
        c.mimeType = mimeType;
        c.text = text;
        return c;
    }

    // Factory: embedded resource (blob)
    static MCPContent makeResourceBlob(const String& uri, const String& mimeType,
                                       const String& base64Blob) {
        MCPContent c;
        c.type = RESOURCE;
        c.uri = uri;
        c.mimeType = mimeType;
        c.blob = base64Blob;
        return c;
    }

    void toJson(JsonObject obj) const {
        switch (type) {
            case TEXT:
                obj["type"] = "text";
                obj["text"] = text;
                break;
            case IMAGE:
                obj["type"] = "image";
                obj["data"] = data;
                obj["mimeType"] = mimeType;
                break;
            case AUDIO:
                obj["type"] = "audio";
                obj["data"] = data;
                obj["mimeType"] = mimeType;
                break;
            case RESOURCE: {
                obj["type"] = "resource";
                JsonObject res = obj["resource"].to<JsonObject>();
                res["uri"] = uri;
                res["mimeType"] = mimeType;
                if (!blob.isEmpty()) {
                    res["blob"] = blob;
                } else {
                    res["text"] = text;
                }
                break;
            }
        }
    }
};

/**
 * Tool result with structured content.
 * Can hold multiple content items (e.g., text + image).
 */
struct MCPToolResult {
    std::vector<MCPContent> content;
    bool isError = false;

    MCPToolResult() = default;

    // Convenience: single text result
    static MCPToolResult text(const String& text) {
        MCPToolResult r;
        r.content.push_back(MCPContent::makeText(text));
        return r;
    }

    // Convenience: error result
    static MCPToolResult error(const String& message) {
        MCPToolResult r;
        r.content.push_back(MCPContent::makeText(message));
        r.isError = true;
        return r;
    }

    // Convenience: single image result
    static MCPToolResult image(const String& base64Data, const String& mimeType,
                               const String& altText = "") {
        MCPToolResult r;
        if (!altText.isEmpty()) {
            r.content.push_back(MCPContent::makeText(altText));
        }
        r.content.push_back(MCPContent::makeImage(base64Data, mimeType));
        return r;
    }

    // Convenience: single audio result
    static MCPToolResult audio(const String& base64Data, const String& mimeType,
                               const String& description = "") {
        MCPToolResult r;
        if (!description.isEmpty()) {
            r.content.push_back(MCPContent::makeText(description));
        }
        r.content.push_back(MCPContent::makeAudio(base64Data, mimeType));
        return r;
    }

    // Add content item
    MCPToolResult& add(const MCPContent& c) {
        content.push_back(c);
        return *this;
    }

    void toJson(JsonObject obj) const {
        JsonArray arr = obj["content"].to<JsonArray>();
        for (const auto& c : content) {
            JsonObject item = arr.add<JsonObject>();
            c.toJson(item);
        }
        if (isError) {
            obj["isError"] = true;
        }
    }
};

// ── Rich Tool Handler ──────────────────────────────────────────────────

/**
 * Tool handler that returns structured content.
 * Use this instead of MCPToolHandler for tools that return images, resources, etc.
 */
using MCPRichToolHandler = std::function<MCPToolResult(const JsonObject& arguments)>;

} // namespace mcpd

#endif // MCPD_CONTENT_H
