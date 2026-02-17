# Changelog

All notable changes to this project will be documented in this file.

## [0.2.0] - 2026-02-17

### Added
- **Built-in MQTT Tool** (`MCPMQTTTool.h`) — connect, publish, subscribe, read messages, check status
  - Message buffering with configurable limit (50 messages)
  - Automatic re-subscription on reconnect
  - Custom message callback support
  - Requires PubSubClient library
- **Prompts support** (`MCPPrompt.h`) — MCP `prompts/list` and `prompts/get` methods
  - Define reusable prompt templates with typed arguments (required/optional)
  - Handlers return structured messages (text or embedded resources)
  - Full argument validation with error reporting
  - Capability negotiation: prompts advertised in `initialize` response
- New example: `mqtt_bridge` — MQTT pub/sub bridge for IoT integration
- 6 new unit tests for prompts (26 total unit tests, 41 total)

## [0.1.0] - 2026-02-17

### Added
- Initial release of mcpd — MCP Server SDK for Microcontrollers
- MCP Server core with Streamable HTTP transport (spec 2025-03-26)
- JSON-RPC 2.0 message handling via ArduinoJson
- `initialize`, `tools/list`, `tools/call`, `resources/list`, `resources/read`, `ping`
- `resources/templates/list` — MCP Resource Templates with URI template matching (RFC 6570 Level 1)
- Capability negotiation & session management (`Mcp-Session-Id`)
- mDNS service advertisement (`_mcp._tcp`)
- Built-in tools: GPIO, PWM, Servo, DHT, I2C, NeoPixel, System, WiFi
- Python stdio↔HTTP bridge for Claude Desktop integration
- SSE transport for streaming responses
- WebSocket transport (`MCPTransportWS.h`) for clients that prefer WebSocket
- Hardware Abstraction Layer (`src/platform/`) — ESP32 and RP2040/Pico W support
- Interactive serial setup CLI (`MCPSetupCLI.h`) for first-boot configuration
- Captive portal for WiFi provisioning
- Bearer token / API key authentication
- Prometheus-compatible `/metrics` endpoint
- OTA update support
- Five examples: basic_server, sensor_hub, home_automation, weather_station, robot_arm
- Community files: CODE_OF_CONDUCT.md, SECURITY.md, CONTRIBUTING.md
- CI: GitHub Actions test workflow
