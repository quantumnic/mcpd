# Changelog

All notable changes to this project will be documented in this file.

## [0.7.0] - 2026-02-18

### Added
- **Structured Content Types** (`MCPContent.h`) — rich tool responses beyond plain text:
  - `MCPContent::makeText()` — text content
  - `MCPContent::makeImage()` — base64-encoded image content with MIME type
  - `MCPContent::makeResource()` — embedded resource content (text)
  - `MCPContent::makeResourceBlob()` — embedded resource content (binary/base64)
  - `MCPToolResult` — multi-part tool results combining text, images, and resources
  - `MCPToolResult::text()`, `::error()`, `::image()` convenience factories
- **Rich Tool Handler** (`MCPRichToolHandler`) — new handler type for tools returning structured content:
  - `server.addRichTool(name, desc, schema, handler)` — register tools that return `MCPToolResult`
  - Backward-compatible: existing `addTool()` with string handlers still works unchanged
- **Progress Notifications** (`MCPProgress.h`) — MCP `notifications/progress` support:
  - `server.reportProgress(token, progress, total, message)` — report progress for long-running tools
  - Progress token extraction from `_meta.progressToken` in `tools/call` requests
  - `ProgressNotification` struct with JSON-RPC serialization
- **Request Cancellation** — proper `notifications/cancelled` handling:
  - `RequestTracker` class for tracking in-flight requests
  - `server.requests()` accessor for cancellation checking in tool handlers
  - Cancelled requests are tracked and queryable via `isCancelled(requestId)`
- 8 new unit tests for structured content (factories, serialization, JSON output)
- 2 new unit tests for rich tool handler (call, error result)
- 4 new unit tests for progress notifications (JSON, no-total, queue, empty-token)
- 4 new unit tests for request tracking/cancellation (basic, cancel, unknown, via notification)
- 1 new unit test for progress token extraction in tools/call
- Fixed pre-existing compilation issue with `as<String>()` in tool handlers (replaced with `as<const char*>()`)

### Changed
- Bumped version to 0.7.0
- Total tests: 88 → 96 (19 new tests)
- `_handleToolsCall` now extracts `_meta.progressToken` and tracks requests
- `notifications/cancelled` now properly cancels tracked in-flight requests
- Fixed `_handlePromptsGet` and built-in tool handlers to use `as<const char*>()` for ArduinoJson v7 compatibility

## [0.6.0] - 2026-02-18

### Added
- **Tool Annotations** (MCP 2025-03-26 spec) — `MCPToolAnnotations` struct with hints:
  - `readOnlyHint`, `destructiveHint`, `idempotentHint`, `openWorldHint`, `title`
  - Builder-style API: `tool.markReadOnly()`, `tool.markIdempotent()`, `tool.markLocalOnly()`
  - `tool.setAnnotations(ann)` for custom annotation objects
  - Annotations serialized in `tools/list` responses when explicitly set
- **Built-in Power Management Tool** (`MCPPowerTool.h`) — 5 tools for battery MCU projects:
  - `power_info` — uptime, reset reason, free heap, CPU freq, chip info, wakeup cause
  - `power_deep_sleep` — enter deep sleep with timer or ext pin wakeup
  - `power_light_sleep` — enter light sleep, returns after waking
  - `power_restart` — software reboot with configurable delay
  - `power_watchdog` — enable/feed/disable task watchdog timer (TWDT)
- **Built-in Timer Tool** (`MCPTimerTool.h`) — 5 tools for hardware timing:
  - `timer_start` — start hardware timer (periodic or one-shot), fire count tracking
  - `timer_stop` — stop timer, return total fires
  - `timer_status` — read timer state and fire count
  - `timer_millis` — precise millis/micros timestamps
  - `timer_pulse_in` — measure pulse width (pulseIn), includes HC-SR04 distance calc
- Added annotations to all GPIO built-in tools (digital_read=readOnly, digital_write=idempotent, etc.)
- 11 new unit tests (73 unit + 15 HTTP = 88 total)

### Changed
- Bumped version to 0.6.0
- Built-in tool count: 12 → 22 (added Power ×5, Timer ×5)
- `MCPTool` struct now includes optional `annotations` field
- Fixed raw string delimiter issue in GPIO tool schemas (`R"j(...)j"`)

## [0.5.0] - 2026-02-18

### Added
- **Built-in ADC Tool** (`MCPADCTool.h`) — advanced analog-to-digital converter tools
  - `adc_read` — single pin reading with configurable sample averaging (1-64 samples), min/max stats
  - `adc_read_voltage` — read and convert to voltage with configurable Vref and resolution
  - `adc_read_multi` — read up to 8 analog pins in one call with averaging
  - `adc_config` — ESP32-specific attenuation (0/2.5/6/11 dB) and resolution (9-12 bit) config
- **Built-in UART Tool** (`MCPUARTTool.h`) — serial communication for peripherals
  - `uart_config` — initialize Serial1/Serial2 with baud rate and optional pin remapping
  - `uart_write` — send text or hex-encoded binary data with optional newline
  - `uart_read` — read with timeout, max bytes limit, text or hex output mode
  - `uart_available` — check bytes available in receive buffer
- **Roots support** (`MCPRoots.h`) — MCP `roots/list` method
  - `addRoot(uri, name)` for registering server context roots
  - `roots` capability with `listChanged: true` in initialize
  - Roots describe the server's data domains (e.g. `sensor://`, `gpio://`)
- New example: `data_logger` — multi-channel ADC logging with UART peripherals,
  ring buffer storage, configurable sampling, resources, roots, prompts, completions
- 12 new unit tests (62 unit + 15 HTTP = 77 total)

### Changed
- Bumped version to 0.5.0
- Built-in tool count: 10 → 12 (added ADC, UART)
- library.json: added `ststm32` platform, updated description

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
