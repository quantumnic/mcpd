# Changelog

All notable changes to this project will be documented in this file.

## [0.4.0] - 2026-02-18

### Added
- **STM32 Platform HAL** (`STM32Platform.h`) — full hardware abstraction for STM32 boards
  - WiFi, GPIO, System HAL implementations for STM32duino framework
  - Supports STM32F1xx (Blue Pill), STM32F4xx (Nucleo), STM32H7xx
  - Hardware RNG support where available, 96-bit unique device ID
  - Configurable analog resolution, PWM via HardwareTimer
- **Resource Subscriptions** (`resources/subscribe`, `resources/unsubscribe`)
  - Clients can subscribe to resource URIs for change notifications
  - `notifyResourceUpdated(uri)` sends `notifications/resources/updated` to subscribers
  - `subscribe: true` advertised in resources capability
  - Idempotent subscribe (no duplicates)
- **Completion/Autocomplete** (`completion/complete`)
  - `CompletionManager` for registering completion providers
  - Supports `ref/prompt` argument completion
  - Supports `ref/resource` template variable completion
  - Prefix filtering and `hasMore` truncation support
  - Capability advertised in `initialize` when providers registered
- **Built-in SPI Tool** (`MCPSPITool.h`)
  - `spi_transfer` — send/receive bytes with configurable CS pin, frequency, mode, bit order
  - `spi_config` — initialize SPI bus with optional custom pins (ESP32)
  - Transfer size limit (256 bytes) for MCU memory safety
- New example: `industrial_monitor` — industrial process monitoring with tank level,
  temperature, valve control, alarms, subscriptions, completion, and prompts
- 13 new unit tests (50 unit + 15 HTTP = 65 total)

### Changed
- Bumped version to 0.4.0
- README: updated feature comparison table, architecture diagram
- Built-in tool count: 9 → 10 (added SPI)
- Platform support: ESP32, RP2040 → ESP32, RP2040, STM32

## [0.3.0] - 2026-02-18

### Added
- **Logging capability** (`MCPLogging.h`) — MCP `logging/setLevel` support
  - 8 log levels (debug through emergency, per RFC 5424)
  - Client-controlled log filtering via `logging/setLevel` method
  - Log notification sink for sending `notifications/message` to clients
  - Convenience methods: `debug()`, `info()`, `warning()`, `error()`, `critical()`
  - Automatic Serial output for local debugging
- **Cursor-based pagination** for all list methods
  - `tools/list`, `resources/list`, `resources/templates/list`, `prompts/list`
  - Configurable page size via `setPageSize()` (0 = disabled)
  - `nextCursor` in response for fetching next page
- **Dynamic tool/resource management** at runtime
  - `removeTool(name)` and `removeResource(uri)` methods
  - `notifyToolsChanged()`, `notifyResourcesChanged()`, `notifyPromptsChanged()` — emit `notifications/*/list_changed`
  - `listChanged: true` advertised in capabilities
- **`notifications/cancelled` handling** — graceful acknowledgment
- New example: `smart_greenhouse` — greenhouse automation with logging, dynamic tools, prompts
- 11 new unit tests (37 unit + 15 HTTP = 52 total)

### Changed
- Bumped version to 0.3.0
- Capabilities now advertise `listChanged: true` for tools, resources, and prompts
- Logging capability advertised in `initialize` response

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
