/**
 * mcpd — MCP Resource Templates (RFC 6570 Level 1)
 *
 * Supports URI templates like "sensor://{sensor_id}/reading" for dynamic resources.
 * Implements resources/templates/list and resolves templates on resources/read.
 *
 * MCP Spec: Resource Templates allow servers to expose parameterized resources.
 */

#ifndef MCPD_RESOURCE_TEMPLATE_H
#define MCPD_RESOURCE_TEMPLATE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include <map>
#include <vector>

namespace mcpd {

/**
 * Template handler receives extracted URI parameters and returns content.
 */
using MCPResourceTemplateHandler = std::function<String(const std::map<String, String>& params)>;

/**
 * Represents an MCP Resource Template.
 *
 * Example:
 *   uriTemplate: "sensor://{sensor_id}/reading"
 *   name: "Sensor Reading"
 */
struct MCPResourceTemplate {
    String uriTemplate;     // RFC 6570 Level 1 template
    String name;
    String description;
    String mimeType;
    MCPResourceTemplateHandler handler;

    MCPResourceTemplate() = default;

    MCPResourceTemplate(const char* uriTemplate, const char* name,
                        const char* description, const char* mimeType,
                        MCPResourceTemplateHandler handler)
        : uriTemplate(uriTemplate), name(name), description(description),
          mimeType(mimeType), handler(handler) {}

    /**
     * Serialize for resources/templates/list response.
     */
    void toJson(JsonObject& obj) const {
        obj["uriTemplate"] = uriTemplate;
        obj["name"] = name;
        obj["description"] = description;
        obj["mimeType"] = mimeType;
    }

    /**
     * Extract template variable names from the URI template.
     * E.g. "sensor://{sensor_id}/reading" → ["sensor_id"]
     */
    std::vector<String> variables() const {
        std::vector<String> vars;
        int i = 0;
        while (i < (int)uriTemplate.length()) {
            int start = uriTemplate.indexOf('{', i);
            if (start < 0) break;
            int end = uriTemplate.indexOf('}', start);
            if (end < 0) break;
            vars.push_back(uriTemplate.substring(start + 1, end));
            i = end + 1;
        }
        return vars;
    }

    /**
     * Try to match a concrete URI against this template.
     * Returns true and fills params if it matches.
     *
     * E.g. template "sensor://{sensor_id}/reading"
     *      URI      "sensor://temp1/reading"
     *      → params["sensor_id"] = "temp1"
     */
    bool match(const String& uri, std::map<String, String>& params) const {
        params.clear();

        // Build a regex-like matcher by splitting template on {var} segments
        int tPos = 0, uPos = 0;

        while (tPos < (int)uriTemplate.length() && uPos < (int)uri.length()) {
            int varStart = uriTemplate.indexOf('{', tPos);

            if (varStart < 0) {
                // No more variables — rest must match literally
                String rest = uriTemplate.substring(tPos);
                String uriRest = uri.substring(uPos);
                return rest == uriRest;
            }

            // Literal prefix before the variable must match
            String prefix = uriTemplate.substring(tPos, varStart);
            if (prefix.length() > 0) {
                if (uri.substring(uPos, uPos + prefix.length()) != prefix) {
                    return false;
                }
                uPos += prefix.length();
            }

            int varEnd = uriTemplate.indexOf('}', varStart);
            if (varEnd < 0) return false;

            String varName = uriTemplate.substring(varStart + 1, varEnd);
            tPos = varEnd + 1;

            // Find the next literal segment after this variable
            String nextLiteral;
            int nextBrace = uriTemplate.indexOf('{', tPos);
            if (tPos < (int)uriTemplate.length() && nextBrace != tPos) {
                // There's a literal segment after the variable
                int litEnd = (nextBrace >= 0) ? nextBrace : uriTemplate.length();
                nextLiteral = uriTemplate.substring(tPos, litEnd);
            }

            // Extract the variable value from the URI
            String value;
            if (nextLiteral.length() > 0) {
                int nextLitPos = uri.indexOf(nextLiteral, uPos);
                if (nextLitPos < 0) return false;
                value = uri.substring(uPos, nextLitPos);
                uPos = nextLitPos;
            } else if (tPos >= (int)uriTemplate.length()) {
                // Variable is at the end of the template
                value = uri.substring(uPos);
                uPos = uri.length();
            } else {
                // Variable followed by another variable — grab until next '/'
                int slashPos = uri.indexOf('/', uPos);
                if (slashPos < 0) slashPos = uri.length();
                value = uri.substring(uPos, slashPos);
                uPos = slashPos;
            }

            if (value.length() == 0) return false;
            params[varName] = value;
        }

        return tPos >= (int)uriTemplate.length() && uPos >= (int)uri.length();
    }
};

} // namespace mcpd

#endif // MCPD_RESOURCE_TEMPLATE_H
