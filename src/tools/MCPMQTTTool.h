/**
 * mcpd — Built-in MQTT Tools
 *
 * Provides: mqtt_connect, mqtt_publish, mqtt_subscribe, mqtt_status
 *
 * Requires PubSubClient library:
 *   lib_deps = knolleary/PubSubClient@^2.8
 */

#ifndef MCPD_MQTT_TOOL_H
#define MCPD_MQTT_TOOL_H

#include "../mcpd.h"
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <vector>
#include <functional>

namespace mcpd {
namespace tools {

/**
 * Callback for received MQTT messages.
 * Override MQTTTool::onMessage for custom handling.
 */
using MQTTMessageCallback = std::function<void(const char* topic, const char* payload)>;

/**
 * Built-in MQTT tool — publish/subscribe to MQTT brokers.
 *
 * Usage:
 *   mcpd::tools::MQTTTool mqtt;
 *   mqtt.attach(mcp);
 *   // In loop(): mqtt.loop();
 *
 * AI can then:
 *   - Connect to an MQTT broker
 *   - Publish messages to topics
 *   - Subscribe to topics and read last messages
 *   - Check connection status
 */
class MQTTTool {
public:
    static constexpr size_t MAX_RETAINED_MESSAGES = 50;
    static constexpr size_t MAX_PAYLOAD_SIZE = 1024;

    MQTTTool() : _mqttClient(_wifiClient) {}

    /**
     * Attach MQTT tools to an MCP server.
     */
    void attach(Server& server) {
        _instance = this;

        // mqtt_connect — Connect to an MQTT broker
        server.addTool("mqtt_connect",
            "Connect to an MQTT broker. Supports optional authentication.",
            R"({
                "type": "object",
                "properties": {
                    "host": {
                        "type": "string",
                        "description": "MQTT broker hostname or IP"
                    },
                    "port": {
                        "type": "integer",
                        "description": "MQTT broker port (default: 1883)",
                        "default": 1883
                    },
                    "client_id": {
                        "type": "string",
                        "description": "MQTT client ID (default: device name)"
                    },
                    "username": {
                        "type": "string",
                        "description": "MQTT username (optional)"
                    },
                    "password": {
                        "type": "string",
                        "description": "MQTT password (optional)"
                    }
                },
                "required": ["host"]
            })",
            [this](const JsonObject& args) -> String {
                return _connect(args);
            });

        // mqtt_publish — Publish a message to an MQTT topic
        server.addTool("mqtt_publish",
            "Publish a message to an MQTT topic. Must be connected first.",
            R"({
                "type": "object",
                "properties": {
                    "topic": {
                        "type": "string",
                        "description": "MQTT topic to publish to"
                    },
                    "payload": {
                        "type": "string",
                        "description": "Message payload"
                    },
                    "retained": {
                        "type": "boolean",
                        "description": "Set retained flag (default: false)",
                        "default": false
                    },
                    "qos": {
                        "type": "integer",
                        "description": "QoS level 0 or 1 (default: 0)",
                        "default": 0
                    }
                },
                "required": ["topic", "payload"]
            })",
            [this](const JsonObject& args) -> String {
                return _publish(args);
            });

        // mqtt_subscribe — Subscribe to an MQTT topic
        server.addTool("mqtt_subscribe",
            "Subscribe to an MQTT topic. Messages are buffered and can be read with mqtt_messages.",
            R"({
                "type": "object",
                "properties": {
                    "topic": {
                        "type": "string",
                        "description": "MQTT topic or pattern (supports +/# wildcards)"
                    },
                    "qos": {
                        "type": "integer",
                        "description": "QoS level 0 or 1 (default: 0)",
                        "default": 0
                    }
                },
                "required": ["topic"]
            })",
            [this](const JsonObject& args) -> String {
                return _subscribe(args);
            });

        // mqtt_messages — Read buffered messages from subscribed topics
        server.addTool("mqtt_messages",
            "Read recently received MQTT messages. Optionally filter by topic.",
            R"({
                "type": "object",
                "properties": {
                    "topic": {
                        "type": "string",
                        "description": "Filter by topic (optional, exact match)"
                    },
                    "limit": {
                        "type": "integer",
                        "description": "Max messages to return (default: 10)",
                        "default": 10
                    },
                    "clear": {
                        "type": "boolean",
                        "description": "Clear messages after reading (default: false)",
                        "default": false
                    }
                }
            })",
            [this](const JsonObject& args) -> String {
                return _messages(args);
            });

        // mqtt_status — Check MQTT connection status
        server.addTool("mqtt_status",
            "Get current MQTT connection status and subscriptions.",
            R"({"type": "object", "properties": {}})",
            [this](const JsonObject& args) -> String {
                return _status();
            });

        // Set up the message callback
        _mqttClient.setCallback([](char* topic, byte* payload, unsigned int length) {
            if (_instance) {
                // Null-terminate payload
                char buf[MAX_PAYLOAD_SIZE + 1];
                size_t copyLen = (length < MAX_PAYLOAD_SIZE) ? length : MAX_PAYLOAD_SIZE;
                memcpy(buf, payload, copyLen);
                buf[copyLen] = '\0';
                _instance->_onMessage(topic, buf);
            }
        });

        _mqttClient.setBufferSize(MAX_PAYLOAD_SIZE + 128);
    }

    /**
     * Call in loop() to maintain MQTT connection and process messages.
     */
    void loop() {
        if (_mqttClient.connected()) {
            _mqttClient.loop();
        }
    }

    /**
     * Set a custom message callback (in addition to internal buffering).
     */
    void onMessage(MQTTMessageCallback cb) {
        _userCallback = cb;
    }

    /**
     * Check if connected.
     */
    bool connected() const {
        return _mqttClient.connected();
    }

private:
    static MQTTTool* _instance;

    WiFiClient _wifiClient;
    PubSubClient _mqttClient;
    MQTTMessageCallback _userCallback;

    String _brokerHost;
    uint16_t _brokerPort = 1883;
    String _clientId;

    struct Message {
        String topic;
        String payload;
        unsigned long timestamp;
    };

    std::vector<Message> _messages_buf;
    std::vector<String> _subscriptions;

    // ── Tool implementations ───────────────────────────────────────────

    String _connect(const JsonObject& args) {
        _brokerHost = args["host"].as<String>();
        _brokerPort = args["port"] | 1883;
        _clientId = args["client_id"] | String("mcpd-") + String(random(10000));

        const char* username = args["username"];
        const char* password = args["password"];

        _mqttClient.setServer(_brokerHost.c_str(), _brokerPort);

        bool ok;
        if (username && password) {
            ok = _mqttClient.connect(_clientId.c_str(), username, password);
        } else if (username) {
            ok = _mqttClient.connect(_clientId.c_str(), username, "");
        } else {
            ok = _mqttClient.connect(_clientId.c_str());
        }

        if (ok) {
            // Re-subscribe to any previous subscriptions
            for (const auto& sub : _subscriptions) {
                _mqttClient.subscribe(sub.c_str());
            }
            return String("{\"connected\":true,\"broker\":\"") + _brokerHost +
                   ":" + _brokerPort + "\",\"client_id\":\"" + _clientId + "\"}";
        }

        int state = _mqttClient.state();
        return String("{\"connected\":false,\"error\":\"Connection failed\",\"state\":") +
               state + "}";
    }

    String _publish(const JsonObject& args) {
        if (!_mqttClient.connected()) {
            return "{\"error\":\"Not connected to MQTT broker\"}";
        }

        const char* topic = args["topic"];
        const char* payload = args["payload"];
        bool retained = args["retained"] | false;
        int qos = args["qos"] | 0;

        if (!topic || !payload) {
            return "{\"error\":\"Missing topic or payload\"}";
        }

        bool ok;
        if (qos == 0) {
            ok = _mqttClient.publish(topic, payload, retained);
        } else {
            // PubSubClient publish_P for QoS>0 not directly supported,
            // use publish with retained flag
            ok = _mqttClient.publish(topic, payload, retained);
        }

        if (ok) {
            return String("{\"published\":true,\"topic\":\"") + topic + "\"}";
        }
        return "{\"published\":false,\"error\":\"Publish failed\"}";
    }

    String _subscribe(const JsonObject& args) {
        if (!_mqttClient.connected()) {
            return "{\"error\":\"Not connected to MQTT broker\"}";
        }

        const char* topic = args["topic"];
        int qos = args["qos"] | 0;

        if (!topic) {
            return "{\"error\":\"Missing topic\"}";
        }

        bool ok = _mqttClient.subscribe(topic, qos);
        if (ok) {
            // Track subscription
            String topicStr(topic);
            bool found = false;
            for (const auto& sub : _subscriptions) {
                if (sub == topicStr) { found = true; break; }
            }
            if (!found) _subscriptions.push_back(topicStr);

            return String("{\"subscribed\":true,\"topic\":\"") + topic + "\"}";
        }
        return "{\"subscribed\":false,\"error\":\"Subscribe failed\"}";
    }

    String _messages(const JsonObject& args) {
        const char* topicFilter = args["topic"];
        int limit = args["limit"] | 10;
        bool clear = args["clear"] | false;

        JsonDocument doc;
        JsonArray msgs = doc["messages"].to<JsonArray>();
        int count = 0;

        // Iterate from newest to oldest
        for (int i = (int)_messages_buf.size() - 1; i >= 0 && count < limit; i--) {
            const auto& msg = _messages_buf[i];
            if (topicFilter && msg.topic != String(topicFilter)) continue;

            JsonObject obj = msgs.add<JsonObject>();
            obj["topic"] = msg.topic;
            obj["payload"] = msg.payload;
            obj["age_ms"] = millis() - msg.timestamp;
            count++;
        }

        doc["total_buffered"] = (int)_messages_buf.size();
        doc["returned"] = count;

        if (clear) {
            if (topicFilter) {
                // Only clear matching messages
                String filter(topicFilter);
                _messages_buf.erase(
                    std::remove_if(_messages_buf.begin(), _messages_buf.end(),
                        [&filter](const Message& m) { return m.topic == filter; }),
                    _messages_buf.end());
            } else {
                _messages_buf.clear();
            }
            doc["cleared"] = true;
        }

        String result;
        serializeJson(doc, result);
        return result;
    }

    String _status() {
        JsonDocument doc;
        doc["connected"] = _mqttClient.connected();
        doc["broker"] = _brokerHost + ":" + String(_brokerPort);
        doc["client_id"] = _clientId;
        doc["state"] = _mqttClient.state();
        doc["buffered_messages"] = (int)_messages_buf.size();

        JsonArray subs = doc["subscriptions"].to<JsonArray>();
        for (const auto& sub : _subscriptions) {
            subs.add(sub);
        }

        String result;
        serializeJson(doc, result);
        return result;
    }

    // ── Internal callback ──────────────────────────────────────────────

    void _onMessage(const char* topic, const char* payload) {
        // Buffer the message
        if (_messages_buf.size() >= MAX_RETAINED_MESSAGES) {
            _messages_buf.erase(_messages_buf.begin());
        }
        _messages_buf.push_back({String(topic), String(payload), millis()});

        // Forward to user callback if set
        if (_userCallback) {
            _userCallback(topic, payload);
        }
    }
};

// Static instance pointer for PubSubClient callback
MQTTTool* MQTTTool::_instance = nullptr;

} // namespace tools
} // namespace mcpd

#endif // MCPD_MQTT_TOOL_H
