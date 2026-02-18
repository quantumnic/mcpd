/**
 * mcpd — Progress Notifications
 *
 * Support for notifications/progress (MCP 2025-03-26).
 * Allows long-running tools to report progress to the client.
 *
 * Usage:
 *   // In a tool handler:
 *   server.reportProgress(progressToken, 25, 100, "Reading sensors...");
 */

#ifndef MCPD_PROGRESS_H
#define MCPD_PROGRESS_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <map>

namespace mcpd {

/**
 * A queued progress notification.
 */
struct ProgressNotification {
    String progressToken;  // Can be string or integer (stored as string)
    double progress;       // Current progress value
    double total;          // Total expected (0 = indeterminate)
    String message;        // Optional human-readable status

    String toJsonRpc() const {
        JsonDocument doc;
        doc["jsonrpc"] = "2.0";
        doc["method"] = "notifications/progress";
        JsonObject params = doc["params"].to<JsonObject>();
        params["progressToken"] = progressToken;
        params["progress"] = progress;
        if (total > 0) {
            params["total"] = total;
        }
        if (!message.isEmpty()) {
            params["message"] = message;
        }
        String output;
        serializeJson(doc, output);
        return output;
    }
};

/**
 * Tracks in-flight requests and their progress tokens for cancellation support.
 */
class RequestTracker {
public:
    /**
     * Register an in-flight request with its progress token (if any).
     */
    void trackRequest(const String& requestId, const String& progressToken = "") {
        _inFlight[requestId] = progressToken;
    }

    /**
     * Mark a request as completed (removes from tracking).
     */
    void completeRequest(const String& requestId) {
        _inFlight.erase(requestId);
    }

    /**
     * Mark a request as cancelled. Returns true if it was in-flight.
     */
    bool cancelRequest(const String& requestId) {
        auto it = _inFlight.find(requestId);
        if (it != _inFlight.end()) {
            _cancelled.push_back(requestId);
            _inFlight.erase(it);
            return true;
        }
        return false;
    }

    /**
     * Check if a request has been cancelled.
     */
    bool isCancelled(const String& requestId) const {
        for (const auto& id : _cancelled) {
            if (id == requestId) return true;
        }
        return false;
    }

    /**
     * Clear cancellation record for a request.
     */
    void clearCancelled(const String& requestId) {
        for (auto it = _cancelled.begin(); it != _cancelled.end(); ++it) {
            if (*it == requestId) {
                _cancelled.erase(it);
                return;
            }
        }
    }

    /**
     * Check if any requests are in-flight.
     */
    bool hasInFlight() const { return !_inFlight.empty(); }

    /**
     * Get count of in-flight requests.
     */
    size_t inFlightCount() const { return _inFlight.size(); }

private:
    std::map<String, String> _inFlight;   // requestId → progressToken
    std::vector<String> _cancelled;        // cancelled requestIds
};

} // namespace mcpd

#endif // MCPD_PROGRESS_H
