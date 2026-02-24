/**
 * mcpd — Audit Log (Security Event Trail)
 *
 * Bounded ring-buffer audit log for security-relevant events on MCUs.
 * Tracks tool calls, access denied events, authentication attempts,
 * session lifecycle, and custom audit events. Complements RBAC by
 * providing a tamper-evident trail of who did what.
 *
 * Memory-safe: fixed-capacity ring buffer evicts oldest entries.
 * Optional listener callback for real-time alerting / forwarding.
 *
 * Usage:
 *   mcpd::AuditLog audit(128);  // keep last 128 entries
 *   audit.logToolCall("admin", "gpio_write", "{\"pin\":2,\"state\":1}", true);
 *   audit.logAccessDenied("guest", "gpio_write");
 *   audit.logAuth("key-abc", true);
 *
 *   auto denied = audit.byAction(mcpd::AuditAction::AccessDenied);
 *   auto recent = audit.since(millis() - 60000);
 *   String json = audit.toJSON();  // all entries as JSON array
 */

#ifndef MCPD_AUDIT_LOG_H
#define MCPD_AUDIT_LOG_H

#include <Arduino.h>
#include <vector>
#include <functional>
#include <string>
#include <cstring>

namespace mcpd {

// ── Audit Action Types ──────────────────────────────────────────────

enum class AuditAction : uint8_t {
    ToolCall      = 0,  // Tool was invoked
    AccessDenied  = 1,  // Tool call blocked by RBAC
    AuthSuccess   = 2,  // Successful authentication
    AuthFailure   = 3,  // Failed authentication attempt
    SessionStart  = 4,  // New session established
    SessionEnd    = 5,  // Session closed
    RoleChange    = 6,  // Role or mapping modified
    Custom        = 7   // User-defined audit event
};

inline const char* auditActionToString(AuditAction a) {
    switch (a) {
        case AuditAction::ToolCall:     return "tool_call";
        case AuditAction::AccessDenied: return "access_denied";
        case AuditAction::AuthSuccess:  return "auth_success";
        case AuditAction::AuthFailure:  return "auth_failure";
        case AuditAction::SessionStart: return "session_start";
        case AuditAction::SessionEnd:   return "session_end";
        case AuditAction::RoleChange:   return "role_change";
        case AuditAction::Custom:       return "custom";
        default:                        return "unknown";
    }
}

// ── Audit Entry ─────────────────────────────────────────────────────

struct AuditEntry {
    uint32_t    seq;           // Monotonic sequence number
    unsigned long timestamp;   // millis() at time of event
    AuditAction action;        // What happened
    String      actor;         // Who did it (role, key, session id)
    String      target;        // What was acted on (tool name, etc.)
    String      detail;        // Additional context (params, reason)
    bool        success;       // Did it succeed?

    /** Serialize a single entry to JSON string. */
    String toJSON() const {
        String json = "{";
        json += "\"seq\":" + String(seq);
        json += ",\"time\":" + String(timestamp);
        json += ",\"action\":\"" + String(auditActionToString(action)) + "\"";
        json += ",\"actor\":\"" + actor + "\"";
        if (target.length() > 0) {
            json += ",\"target\":\"" + target + "\"";
        }
        if (detail.length() > 0) {
            json += ",\"detail\":\"" + detail + "\"";
        }
        json += ",\"success\":" + String(success ? "true" : "false");
        json += "}";
        return json;
    }
};

// ── Audit Log ───────────────────────────────────────────────────────

class AuditLog {
public:
    using Listener = std::function<void(const AuditEntry&)>;

    /** Create an audit log with the given ring buffer capacity. */
    explicit AuditLog(size_t capacity = 64)
        : _capacity(capacity > 0 ? capacity : 1), _seq(0), _enabled(true) {
        _entries.reserve(_capacity);
    }

    // ── Enable / Disable ────────────────────────────────────────────

    void setEnabled(bool enabled) { _enabled = enabled; }
    bool isEnabled() const { return _enabled; }

    // ── Capacity ────────────────────────────────────────────────────

    size_t capacity() const { return _capacity; }
    size_t count() const { return _entries.size(); }

    /** Resize the ring buffer. Entries beyond new capacity are evicted (oldest first). */
    void setCapacity(size_t cap) {
        if (cap == 0) cap = 1;
        _capacity = cap;
        while (_entries.size() > _capacity) {
            _entries.erase(_entries.begin());
        }
    }

    // ── Convenience Logging Methods ─────────────────────────────────

    /** Log a tool call. */
    void logToolCall(const char* actor, const char* tool,
                     const char* params = "", bool success = true) {
        _append(AuditAction::ToolCall, actor, tool, params, success);
    }

    /** Log an access denied event. */
    void logAccessDenied(const char* actor, const char* tool,
                         const char* reason = "") {
        _append(AuditAction::AccessDenied, actor, tool, reason, false);
    }

    /** Log an authentication attempt. */
    void logAuth(const char* identifier, bool success,
                 const char* detail = "") {
        _append(success ? AuditAction::AuthSuccess : AuditAction::AuthFailure,
                identifier, "", detail, success);
    }

    /** Log session lifecycle. */
    void logSession(const char* sessionId, bool start,
                    const char* detail = "") {
        _append(start ? AuditAction::SessionStart : AuditAction::SessionEnd,
                sessionId, "", detail, true);
    }

    /** Log a role or mapping change. */
    void logRoleChange(const char* actor, const char* detail) {
        _append(AuditAction::RoleChange, actor, "", detail, true);
    }

    /** Log a custom audit event. */
    void logCustom(const char* actor, const char* target,
                   const char* detail, bool success = true) {
        _append(AuditAction::Custom, actor, target, detail, success);
    }

    // ── Generic log ─────────────────────────────────────────────────

    /** Log any audit action directly. */
    void log(AuditAction action, const char* actor, const char* target = "",
             const char* detail = "", bool success = true) {
        _append(action, actor, target, detail, success);
    }

    // ── Query Methods ───────────────────────────────────────────────

    /** Get all entries (oldest first). */
    const std::vector<AuditEntry>& entries() const { return _entries; }

    /** Get entries filtered by action type. */
    std::vector<AuditEntry> byAction(AuditAction action) const {
        std::vector<AuditEntry> result;
        for (auto& e : _entries) {
            if (e.action == action) result.push_back(e);
        }
        return result;
    }

    /** Get entries filtered by actor. */
    std::vector<AuditEntry> byActor(const char* actor) const {
        std::vector<AuditEntry> result;
        String a(actor);
        for (auto& e : _entries) {
            if (e.actor == a) result.push_back(e);
        }
        return result;
    }

    /** Get entries filtered by target (e.g. tool name). */
    std::vector<AuditEntry> byTarget(const char* target) const {
        std::vector<AuditEntry> result;
        String t(target);
        for (auto& e : _entries) {
            if (e.target == t) result.push_back(e);
        }
        return result;
    }

    /** Get entries since a given timestamp (millis). */
    std::vector<AuditEntry> since(unsigned long ts) const {
        std::vector<AuditEntry> result;
        for (auto& e : _entries) {
            if (e.timestamp >= ts) result.push_back(e);
        }
        return result;
    }

    /** Get entries since a given sequence number (exclusive). */
    std::vector<AuditEntry> sinceSeq(uint32_t afterSeq) const {
        std::vector<AuditEntry> result;
        for (auto& e : _entries) {
            if (e.seq > afterSeq) result.push_back(e);
        }
        return result;
    }

    /** Get only failed entries. */
    std::vector<AuditEntry> failures() const {
        std::vector<AuditEntry> result;
        for (auto& e : _entries) {
            if (!e.success) result.push_back(e);
        }
        return result;
    }

    /** Get the last N entries (most recent). */
    std::vector<AuditEntry> last(size_t n) const {
        if (n >= _entries.size()) return _entries;
        return std::vector<AuditEntry>(_entries.end() - n, _entries.end());
    }

    // ── Stats ───────────────────────────────────────────────────────

    /** Count entries matching an action type. */
    size_t countByAction(AuditAction action) const {
        size_t c = 0;
        for (auto& e : _entries) {
            if (e.action == action) c++;
        }
        return c;
    }

    /** Count failed entries. */
    size_t countFailures() const {
        size_t c = 0;
        for (auto& e : _entries) {
            if (!e.success) c++;
        }
        return c;
    }

    /** Current sequence number (total events ever logged). */
    uint32_t currentSeq() const { return _seq; }

    // ── Serialization ───────────────────────────────────────────────

    /** Serialize all entries to a JSON array string. */
    String toJSON() const {
        String json = "[";
        for (size_t i = 0; i < _entries.size(); i++) {
            if (i > 0) json += ",";
            json += _entries[i].toJSON();
        }
        json += "]";
        return json;
    }

    /** Serialize stats summary to JSON. */
    String statsJSON() const {
        String json = "{";
        json += "\"total\":" + String(_seq);
        json += ",\"buffered\":" + String((unsigned long)_entries.size());
        json += ",\"capacity\":" + String((unsigned long)_capacity);
        json += ",\"tool_calls\":" + String((unsigned long)countByAction(AuditAction::ToolCall));
        json += ",\"access_denied\":" + String((unsigned long)countByAction(AuditAction::AccessDenied));
        json += ",\"auth_success\":" + String((unsigned long)countByAction(AuditAction::AuthSuccess));
        json += ",\"auth_failure\":" + String((unsigned long)countByAction(AuditAction::AuthFailure));
        json += ",\"failures\":" + String((unsigned long)countFailures());
        json += "}";
        return json;
    }

    // ── Listener ────────────────────────────────────────────────────

    /** Set a listener called on every new entry (for real-time alerting). */
    void setListener(Listener fn) { _listener = fn; }

    /** Remove the listener. */
    void clearListener() { _listener = nullptr; }

    // ── Clear ───────────────────────────────────────────────────────

    /** Clear all entries (sequence counter is NOT reset). */
    void clear() { _entries.clear(); }

    /** Full reset including sequence counter. */
    void reset() {
        _entries.clear();
        _seq = 0;
    }

private:
    void _append(AuditAction action, const char* actor, const char* target,
                 const char* detail, bool success) {
        if (!_enabled) return;

        AuditEntry entry;
        entry.seq = ++_seq;
        entry.timestamp = millis();
        entry.action = action;
        entry.actor = actor ? actor : "";
        entry.target = target ? target : "";
        entry.detail = detail ? detail : "";
        entry.success = success;

        if (_entries.size() >= _capacity) {
            _entries.erase(_entries.begin());
        }
        _entries.push_back(entry);

        if (_listener) {
            _listener(entry);
        }
    }

    size_t _capacity;
    uint32_t _seq;
    bool _enabled;
    std::vector<AuditEntry> _entries;
    Listener _listener;
};

}  // namespace mcpd

#endif  // MCPD_AUDIT_LOG_H
