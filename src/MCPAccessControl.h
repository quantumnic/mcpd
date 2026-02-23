/**
 * mcpd — Role-Based Access Control (RBAC) for tools
 *
 * Associates API keys with roles, and restricts tool access by role.
 * When enabled, tool calls are checked against the caller's role.
 * Unauthenticated callers get the "guest" role (configurable).
 *
 * Usage:
 *   auto& ac = server.accessControl();
 *   ac.addRole("admin");
 *   ac.addRole("viewer");
 *   ac.mapKeyToRole("secret-admin-key", "admin");
 *   ac.mapKeyToRole("read-only-key", "viewer");
 *   ac.restrictTool("gpio_write", {"admin"});
 *   ac.restrictTool("gpio_read", {"admin", "viewer"});
 *   // Tools without restrictions are accessible to all roles
 */

#ifndef MCPD_ACCESS_CONTROL_H
#define MCPD_ACCESS_CONTROL_H

#include <Arduino.h>
#include <functional>
#include <map>
#include <set>
#include <vector>
#include <string>

namespace mcpd {

class AccessControl {
public:
    AccessControl() = default;

    // ── Role Management ──────────────────────────────────────────────

    /** Add a role definition. */
    void addRole(const char* role) {
        _roles.insert(String(role));
    }

    /** Remove a role definition and all its associations. */
    void removeRole(const char* role) {
        String r(role);
        _roles.erase(r);
        // Remove key→role mappings for this role
        for (auto it = _keyToRole.begin(); it != _keyToRole.end(); ) {
            if (it->second == r) {
                it = _keyToRole.erase(it);
            } else {
                ++it;
            }
        }
        // Remove from tool restrictions
        for (auto& pair : _toolRoles) {
            pair.second.erase(r);
        }
    }

    /** Check if a role exists. */
    bool hasRole(const char* role) const {
        return _roles.count(String(role)) > 0;
    }

    /** Get all defined roles. */
    std::set<String> roles() const { return _roles; }

    // ── Key-to-Role Mapping ──────────────────────────────────────────

    /** Map an API key to a role. A key can only have one role. */
    void mapKeyToRole(const char* apiKey, const char* role) {
        _keyToRole[String(apiKey)] = String(role);
        _roles.insert(String(role)); // Auto-add role if not yet defined
    }

    /** Remove a key mapping. */
    void unmapKey(const char* apiKey) {
        _keyToRole.erase(String(apiKey));
    }

    /** Get the role for a key, or empty string if not mapped. */
    String roleForKey(const char* apiKey) const {
        auto it = _keyToRole.find(String(apiKey));
        if (it != _keyToRole.end()) return it->second;
        return String();
    }

    // ── Tool Restrictions ────────────────────────────────────────────

    /**
     * Restrict a tool to specific roles.
     * Only callers with one of the listed roles can call this tool.
     * An empty set means the tool is restricted to no one (effectively disabled).
     */
    void restrictTool(const char* toolName, const std::vector<const char*>& allowedRoles) {
        std::set<String> roleSet;
        for (auto r : allowedRoles) roleSet.insert(String(r));
        _toolRoles[String(toolName)] = roleSet;
    }

    /** Overload accepting String set. */
    void restrictToolSet(const char* toolName, const std::set<String>& allowedRoles) {
        _toolRoles[String(toolName)] = allowedRoles;
    }

    /** Remove restrictions from a tool (makes it accessible to all). */
    void unrestrictTool(const char* toolName) {
        _toolRoles.erase(String(toolName));
    }

    /** Check if a tool has restrictions. */
    bool isToolRestricted(const char* toolName) const {
        return _toolRoles.count(String(toolName)) > 0;
    }

    /** Get allowed roles for a tool (empty set = unrestricted). */
    std::set<String> toolAllowedRoles(const char* toolName) const {
        auto it = _toolRoles.find(String(toolName));
        if (it != _toolRoles.end()) return it->second;
        return {};
    }

    // ── Access Checks ────────────────────────────────────────────────

    /** Set the default role for unauthenticated/unmapped callers. */
    void setDefaultRole(const char* role) {
        _defaultRole = String(role);
        _roles.insert(String(role));
    }

    /** Get the default role. */
    String defaultRole() const { return _defaultRole; }

    /**
     * Check if a caller with the given API key can access a tool.
     * - If the tool has no restrictions, returns true.
     * - If the tool is restricted, checks the key's role (or default role).
     * - If RBAC is not enabled, always returns true.
     */
    bool canAccess(const char* toolName, const char* apiKey = nullptr) const {
        if (!_enabled) return true;

        // Tool not restricted → allow
        auto toolIt = _toolRoles.find(String(toolName));
        if (toolIt == _toolRoles.end()) return true;

        // Determine caller's role
        String callerRole = _defaultRole;
        if (apiKey && apiKey[0] != '\0') {
            auto keyIt = _keyToRole.find(String(apiKey));
            if (keyIt != _keyToRole.end()) {
                callerRole = keyIt->second;
            }
        }

        // Check if caller's role is in allowed set
        if (callerRole.isEmpty()) return false;
        return toolIt->second.count(callerRole) > 0;
    }

    /** Enable/disable RBAC checking. When disabled, canAccess always returns true. */
    void enable(bool v = true) { _enabled = v; }
    void disable() { _enabled = false; }
    bool isEnabled() const { return _enabled; }

    // ── Bulk Operations ──────────────────────────────────────────────

    /**
     * Restrict all tools with a given annotation to specific roles.
     * Useful for restricting all destructive tools to admin only.
     */
    void restrictDestructiveTools(const std::vector<const char*>& toolNames,
                                  const std::vector<const char*>& allowedRoles) {
        for (auto name : toolNames) {
            restrictTool(name, allowedRoles);
        }
    }

    /**
     * Get the list of tools accessible to a given role.
     * Returns tool names that are either unrestricted or explicitly allow this role.
     */
    std::vector<String> toolsForRole(const char* role, const std::vector<String>& allTools) const {
        String r(role);
        std::vector<String> result;
        for (const auto& tool : allTools) {
            auto it = _toolRoles.find(tool);
            if (it == _toolRoles.end()) {
                // Unrestricted
                result.push_back(tool);
            } else if (it->second.count(r) > 0) {
                result.push_back(tool);
            }
        }
        return result;
    }

    // ── JSON Serialization ───────────────────────────────────────────

    /** Serialize RBAC config to JSON string. */
    String toJSON() const {
        String json = "{\"enabled\":";
        json += _enabled ? "true" : "false";
        json += ",\"defaultRole\":\"";
        json += _defaultRole;
        json += "\",\"roles\":[";
        bool first = true;
        for (const auto& r : _roles) {
            if (!first) json += ",";
            json += "\"" + r + "\"";
            first = false;
        }
        json += "],\"toolRestrictions\":{";
        first = true;
        for (const auto& pair : _toolRoles) {
            if (!first) json += ",";
            json += "\"" + pair.first + "\":[";
            bool f2 = true;
            for (const auto& r : pair.second) {
                if (!f2) json += ",";
                json += "\"" + r + "\"";
                f2 = false;
            }
            json += "]";
            first = false;
        }
        json += "},\"keyMappings\":";
        json += String((int)_keyToRole.size());
        json += "}";
        return json;
    }

    /** Get stats as JSON. */
    String statsJSON() const {
        String json = "{\"enabled\":";
        json += _enabled ? "true" : "false";
        json += ",\"roles\":";
        json += String((int)_roles.size());
        json += ",\"keyMappings\":";
        json += String((int)_keyToRole.size());
        json += ",\"restrictedTools\":";
        json += String((int)_toolRoles.size());
        json += "}";
        return json;
    }

private:
    bool _enabled = false;
    String _defaultRole = "guest";
    std::set<String> _roles;
    std::map<String, String> _keyToRole;       // API key → role
    std::map<String, std::set<String>> _toolRoles; // tool name → allowed roles
};

} // namespace mcpd

#endif // MCPD_ACCESS_CONTROL_H
