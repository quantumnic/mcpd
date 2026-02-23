/**
 * mcpd — Event Store (Ring-Buffer Event Log)
 *
 * Lightweight, bounded event log for microcontrollers. Tools and user code
 * can emit events (sensor readings, state changes, errors) which are stored
 * in a fixed-size ring buffer. Clients can query events by tag, time range,
 * or sequence number.
 *
 * Memory-safe: the ring buffer has a compile-time or runtime capacity and
 * automatically evicts the oldest events when full.
 *
 * Usage:
 *   mcpd::EventStore events(64);  // keep last 64 events
 *   events.emit("temperature", "{\"value\":22.5,\"unit\":\"C\"}");
 *   events.emit("gpio", "{\"pin\":2,\"state\":1}", mcpd::EventSeverity::Info);
 *
 *   auto recent = events.since(millis() - 60000);  // last 60s
 *   auto temps  = events.byTag("temperature");
 *   String json = events.toJSON();                  // all events as JSON array
 */

#ifndef MCPD_EVENT_STORE_H
#define MCPD_EVENT_STORE_H

#ifdef MCPD_TEST
#include <Arduino.h>
#else
#include <Arduino.h>
#endif

#include <vector>
#include <functional>

namespace mcpd {

enum class EventSeverity : uint8_t {
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3,
    Critical = 4
};

inline const char* severityToString(EventSeverity s) {
    switch (s) {
        case EventSeverity::Debug:    return "debug";
        case EventSeverity::Info:     return "info";
        case EventSeverity::Warning:  return "warning";
        case EventSeverity::Error:    return "error";
        case EventSeverity::Critical: return "critical";
        default:                      return "unknown";
    }
}

inline EventSeverity severityFromString(const char* s) {
    if (!s) return EventSeverity::Info;
    if (strcmp(s, "debug") == 0)    return EventSeverity::Debug;
    if (strcmp(s, "info") == 0)     return EventSeverity::Info;
    if (strcmp(s, "warning") == 0)  return EventSeverity::Warning;
    if (strcmp(s, "error") == 0)    return EventSeverity::Error;
    if (strcmp(s, "critical") == 0) return EventSeverity::Critical;
    return EventSeverity::Info;
}

struct Event {
    uint32_t seq;              ///< Monotonic sequence number
    unsigned long timestampMs; ///< millis() when emitted
    String tag;                ///< Category/type tag (e.g. "temperature", "gpio")
    String data;               ///< Payload (typically JSON)
    EventSeverity severity;    ///< Severity level
};

/**
 * Listener callback: called on every emit().
 * Receives a const reference to the newly stored event.
 */
using EventListener = std::function<void(const Event&)>;

class EventStore {
public:
    /**
     * @param capacity Maximum number of events to retain (ring buffer size).
     *                 Must be >= 1; clamped to 1 if 0.
     */
    explicit EventStore(size_t capacity = 64)
        : _capacity(capacity < 1 ? 1 : capacity), _seq(0), _count(0), _head(0) {
        _events.resize(_capacity);
    }

    /**
     * Emit (store) a new event.
     * @param tag       Category tag
     * @param data      Payload string (JSON recommended)
     * @param severity  Severity level (default: Info)
     * @return Sequence number of the stored event
     */
    uint32_t emit(const String& tag, const String& data,
                  EventSeverity severity = EventSeverity::Info) {
        uint32_t s = _seq++;
        size_t idx = _head;
        _events[idx] = Event{s, millis(), tag, data, severity};
        _head = (_head + 1) % _capacity;
        if (_count < _capacity) _count++;

        // Notify listeners
        for (auto& listener : _listeners) {
            listener(_events[idx]);
        }

        return s;
    }

    /**
     * Get all stored events (oldest first).
     */
    std::vector<Event> all() const {
        return _collect([](const Event&) { return true; });
    }

    /**
     * Get events matching a specific tag.
     */
    std::vector<Event> byTag(const String& tag) const {
        return _collect([&tag](const Event& e) { return e.tag == tag; });
    }

    /**
     * Get events with severity >= minSeverity.
     */
    std::vector<Event> bySeverity(EventSeverity minSeverity) const {
        return _collect([minSeverity](const Event& e) {
            return static_cast<uint8_t>(e.severity) >= static_cast<uint8_t>(minSeverity);
        });
    }

    /**
     * Get events emitted since a given millis() timestamp.
     */
    std::vector<Event> since(unsigned long sinceMs) const {
        return _collect([sinceMs](const Event& e) { return e.timestampMs >= sinceMs; });
    }

    /**
     * Get events with sequence number >= sinceSeq.
     */
    std::vector<Event> sinceSeq(uint32_t sinceSeq) const {
        return _collect([sinceSeq](const Event& e) { return e.seq >= sinceSeq; });
    }

    /**
     * Get the last N events (oldest first).
     */
    std::vector<Event> last(size_t n) const {
        auto result = all();
        if (result.size() > n) {
            result.erase(result.begin(), result.begin() + (result.size() - n));
        }
        return result;
    }

    /**
     * Combined filter: tag (empty = any) + severity + since timestamp.
     */
    std::vector<Event> query(const String& tag = "",
                             EventSeverity minSeverity = EventSeverity::Debug,
                             unsigned long sinceMs = 0) const {
        return _collect([&](const Event& e) {
            if (tag.length() > 0 && e.tag != tag) return false;
            if (static_cast<uint8_t>(e.severity) < static_cast<uint8_t>(minSeverity)) return false;
            if (sinceMs > 0 && e.timestampMs < sinceMs) return false;
            return true;
        });
    }

    /**
     * Serialize all stored events to a JSON array string.
     */
    String toJSON() const {
        return _toJSON(all());
    }

    /**
     * Serialize a filtered set to JSON.
     */
    String toJSON(const std::vector<Event>& events) const {
        return _toJSON(events);
    }

    /**
     * Get distinct tags currently in the store.
     */
    std::vector<String> tags() const {
        std::vector<String> result;
        auto events = all();
        for (auto& e : events) {
            bool found = false;
            for (auto& t : result) {
                if (t == e.tag) { found = true; break; }
            }
            if (!found) result.push_back(e.tag);
        }
        return result;
    }

    /**
     * Register a listener called on every emit().
     */
    void onEvent(EventListener listener) {
        _listeners.push_back(listener);
    }

    /**
     * Clear all listeners.
     */
    void clearListeners() {
        _listeners.clear();
    }

    /**
     * Clear all events, reset sequence counter.
     */
    void clear() {
        _count = 0;
        _head = 0;
        _seq = 0;
        for (size_t i = 0; i < _capacity; i++) {
            _events[i] = Event{};
        }
    }

    /** Number of events currently stored. */
    size_t count() const { return _count; }

    /** Maximum capacity of the ring buffer. */
    size_t capacity() const { return _capacity; }

    /** Next sequence number that will be assigned. */
    uint32_t nextSeq() const { return _seq; }

    /** True if the buffer is at capacity (oldest events are being evicted). */
    bool isFull() const { return _count >= _capacity; }

    /**
     * Get summary statistics as JSON.
     */
    String statsJSON() const {
        String s = "{\"count\":";
        s += String(_count);
        s += ",\"capacity\":";
        s += String(_capacity);
        s += ",\"nextSeq\":";
        s += String(_seq);
        s += ",\"full\":";
        s += isFull() ? "true" : "false";
        s += ",\"evicted\":";
        s += String(_seq > _capacity ? _seq - _capacity : 0);

        // Count per severity
        size_t counts[5] = {0, 0, 0, 0, 0};
        auto events = all();
        for (auto& e : events) {
            uint8_t idx = static_cast<uint8_t>(e.severity);
            if (idx < 5) counts[idx]++;
        }
        s += ",\"bySeverity\":{";
        s += "\"debug\":" + String(counts[0]);
        s += ",\"info\":" + String(counts[1]);
        s += ",\"warning\":" + String(counts[2]);
        s += ",\"error\":" + String(counts[3]);
        s += ",\"critical\":" + String(counts[4]);
        s += "}}";
        return s;
    }

private:
    size_t _capacity;
    uint32_t _seq;
    size_t _count;
    size_t _head;
    std::vector<Event> _events;
    std::vector<EventListener> _listeners;

    /** Collect events matching a predicate, oldest first. */
    std::vector<Event> _collect(std::function<bool(const Event&)> pred) const {
        std::vector<Event> result;
        if (_count == 0) return result;

        // Start from oldest event
        size_t start = (_count < _capacity) ? 0 : _head;
        for (size_t i = 0; i < _count; i++) {
            size_t idx = (start + i) % _capacity;
            if (pred(_events[idx])) {
                result.push_back(_events[idx]);
            }
        }
        return result;
    }

    static String _toJSON(const std::vector<Event>& events) {
        String json = "[";
        for (size_t i = 0; i < events.size(); i++) {
            if (i > 0) json += ",";
            json += "{\"seq\":";
            json += String(events[i].seq);
            json += ",\"ts\":";
            json += String(events[i].timestampMs);
            json += ",\"tag\":\"";
            json += events[i].tag;
            json += "\",\"severity\":\"";
            json += severityToString(events[i].severity);
            json += "\",\"data\":";
            // If data looks like JSON object/array, include raw; otherwise quote it
            if (events[i].data.length() > 0 &&
                (events[i].data[0] == '{' || events[i].data[0] == '[')) {
                json += events[i].data;
            } else {
                json += "\"";
                json += events[i].data;
                json += "\"";
            }
            json += "}";
        }
        json += "]";
        return json;
    }
};

} // namespace mcpd

#endif // MCPD_EVENT_STORE_H
