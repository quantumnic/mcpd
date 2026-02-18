/**
 * mcpd — Completion/Autocomplete Support
 *
 * MCP completion capability: provides autocomplete suggestions for
 * resource template URIs and prompt arguments.
 *
 * Implements: completion/complete (MCP 2025-03-26)
 */

#ifndef MCP_COMPLETION_H
#define MCP_COMPLETION_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include <vector>
#include <map>

namespace mcpd {

/**
 * Completion provider function.
 * Given a partial value for a named argument, returns possible completions.
 *
 * @param argumentName  The argument being completed (e.g. "sensor_id")
 * @param partialValue  What the user has typed so far
 * @return Vector of completion strings
 */
using CompletionProvider = std::function<std::vector<String>(
    const String& argumentName, const String& partialValue)>;

/**
 * Manages completion providers for prompts and resource templates.
 */
class CompletionManager {
public:
    /**
     * Register a completion provider for a specific prompt argument.
     *
     * @param promptName    Name of the prompt
     * @param argumentName  Name of the argument to complete
     * @param provider      Function returning completion suggestions
     */
    void addPromptCompletion(const String& promptName,
                             const String& argumentName,
                             CompletionProvider provider) {
        String key = "prompt:" + promptName + ":" + argumentName;
        _providers[key] = provider;
    }

    /**
     * Register a completion provider for a resource template variable.
     *
     * @param uriTemplate   The resource template URI
     * @param variableName  Name of the template variable to complete
     * @param provider      Function returning completion suggestions
     */
    void addResourceTemplateCompletion(const String& uriTemplate,
                                        const String& variableName,
                                        CompletionProvider provider) {
        String key = "template:" + uriTemplate + ":" + variableName;
        _providers[key] = provider;
    }

    /**
     * Get completions for a prompt argument.
     *
     * @param promptName    Name of the prompt
     * @param argumentName  Argument being completed
     * @param partialValue  Partial text typed so far
     * @param hasMore       [out] Set to true if results are truncated
     * @param maxResults    Maximum completions to return
     * @return Vector of completion strings
     */
    std::vector<String> completePrompt(const String& promptName,
                                        const String& argumentName,
                                        const String& partialValue,
                                        bool& hasMore,
                                        size_t maxResults = 100) {
        String key = "prompt:" + promptName + ":" + argumentName;
        return _complete(key, argumentName, partialValue, hasMore, maxResults);
    }

    /**
     * Get completions for a resource template variable.
     */
    std::vector<String> completeResourceTemplate(const String& uriTemplate,
                                                   const String& variableName,
                                                   const String& partialValue,
                                                   bool& hasMore,
                                                   size_t maxResults = 100) {
        String key = "template:" + uriTemplate + ":" + variableName;
        return _complete(key, variableName, partialValue, hasMore, maxResults);
    }

    /** Check if any completion providers are registered */
    bool hasProviders() const { return !_providers.empty(); }

private:
    std::map<String, CompletionProvider> _providers;

    std::vector<String> _complete(const String& key,
                                   const String& argumentName,
                                   const String& partialValue,
                                   bool& hasMore,
                                   size_t maxResults) {
        hasMore = false;
        auto it = _providers.find(key);
        if (it == _providers.end()) {
            return {};
        }

        std::vector<String> all = it->second(argumentName, partialValue);

        // Filter by prefix match
        std::vector<String> filtered;
        for (const auto& s : all) {
            if (partialValue.length() == 0 || s.startsWith(partialValue)) {
                filtered.push_back(s);
            }
        }

        // Truncate if needed
        if (filtered.size() > maxResults) {
            hasMore = true;
            filtered.resize(maxResults);
        }

        return filtered;
    }
};

} // namespace mcpd

#endif // MCP_COMPLETION_H
