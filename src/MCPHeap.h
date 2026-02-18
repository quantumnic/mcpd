/**
 * mcpd — Heap / Memory Monitoring
 *
 * Tracks heap usage and provides tools for memory diagnostics.
 * Critical for embedded systems where OOM = crash/reboot.
 *
 * MIT License
 */

#ifndef MCPD_HEAP_H
#define MCPD_HEAP_H

#include <Arduino.h>
#include <ArduinoJson.h>

namespace mcpd {

/**
 * HeapMonitor — tracks memory statistics over time.
 */
class HeapMonitor {
public:
    HeapMonitor() = default;

    /**
     * Sample current heap state. Call periodically (e.g., in loop()).
     */
    void sample() {
#ifdef ESP32
        size_t free = ESP.getFreeHeap();
        size_t total = ESP.getHeapSize();

        if (free < _minFreeEver || _minFreeEver == 0) {
            _minFreeEver = free;
        }
        _lastFree = free;
        _lastTotal = total;
        _lastMaxAlloc = ESP.getMaxAllocHeap();
        _sampleCount++;
#endif
    }

    /** Set low-memory warning threshold (bytes). Default: 10KB. */
    void setWarningThreshold(size_t bytes) { _warningThreshold = bytes; }

    /** Check if heap is critically low. */
    bool isLow() const {
#ifdef ESP32
        return ESP.getFreeHeap() < _warningThreshold;
#else
        return false;
#endif
    }

    size_t lastFree() const { return _lastFree; }
    size_t lastTotal() const { return _lastTotal; }
    size_t minFreeEver() const { return _minFreeEver; }
    size_t lastMaxAlloc() const { return _lastMaxAlloc; }
    size_t sampleCount() const { return _sampleCount; }

    float usagePercent() const {
        if (_lastTotal == 0) return 0;
        return 100.0f * (1.0f - (float)_lastFree / (float)_lastTotal);
    }

private:
    size_t _lastFree = 0;
    size_t _lastTotal = 0;
    size_t _lastMaxAlloc = 0;
    size_t _minFreeEver = 0;
    size_t _sampleCount = 0;
    size_t _warningThreshold = 10240;
};

namespace tools {

/**
 * HeapTool — register heap monitoring tools on the MCP server.
 * Uses template to avoid circular dependency with Server.
 */
class HeapTool {
public:
    template<typename ServerT>
    static void registerAll(ServerT& server) {
        // heap_status
        {
            MCPTool tool;
            tool.name = "heap_status";
            tool.description = "Get current heap/memory status: free memory, fragmentation, largest allocatable block, PSRAM. Critical for monitoring embedded device health.";
            tool.inputSchemaJson = R"({"type":"object","properties":{}})";
            tool.annotations.readOnlyHint = true;
            tool.annotations.title = "Heap Status";
            tool.handler = [](const JsonObject&) -> String {
                JsonDocument doc;
#ifdef ESP32
                doc["freeHeap"] = ESP.getFreeHeap();
                doc["totalHeap"] = ESP.getHeapSize();
                doc["minFreeHeap"] = ESP.getMinFreeHeap();
                doc["maxAllocBlock"] = ESP.getMaxAllocHeap();
                size_t free = ESP.getFreeHeap();
                size_t total = ESP.getHeapSize();
                size_t maxAlloc = ESP.getMaxAllocHeap();
                if (total > 0) {
                    float usage = 100.0f * (1.0f - (float)free / (float)total);
                    doc["usagePercent"] = serialized(String(usage, 1));
                }
                doc["isLow"] = (free < 10240);
                if (free > 0) {
                    float frag = 100.0f * (1.0f - (float)maxAlloc / (float)free);
                    doc["fragmentationPercent"] = serialized(String(frag, 1));
                }
#else
                doc["note"] = "Heap monitoring only available on ESP32";
#endif
                String result;
                serializeJson(doc, result);
                return result;
            };
            server.addTool(tool);
        }

        // heap_history
        {
            MCPTool tool;
            tool.name = "heap_history";
            tool.description = "Get heap monitoring statistics since boot: min/max/trend.";
            tool.inputSchemaJson = R"({"type":"object","properties":{}})";
            tool.annotations.readOnlyHint = true;
            tool.annotations.title = "Heap History";
            tool.handler = [](const JsonObject&) -> String {
                JsonDocument doc;
#ifdef ESP32
                doc["freeHeap"] = ESP.getFreeHeap();
                doc["totalHeap"] = ESP.getHeapSize();
                doc["minFreeHeap"] = ESP.getMinFreeHeap();
#endif
                doc["uptimeMs"] = millis();
                String result;
                serializeJson(doc, result);
                return result;
            };
            server.addTool(tool);
        }
    }
};

} // namespace tools
} // namespace mcpd

#endif // MCPD_HEAP_H
