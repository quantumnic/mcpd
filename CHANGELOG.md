# Changelog

All notable changes to this project will be documented in this file.

## [0.11.0] - 2026-02-18

### Added
- **OneWire / DS18B20 Temperature Tool** (`tools/MCPOneWireTool.h`) — popular temperature sensor support:
  - `onewire_scan` — scan bus for connected devices, identify sensor family (DS18B20/DS18S20/DS1822/DS1825)
  - `onewire_read_temp` — read temperature from sensor by index or address (°C and °F)
  - `onewire_read_all` — read all sensors on the bus in one call
  - `onewire_set_resolution` — configure 9-12 bit resolution per sensor (accuracy vs speed tradeoff)
  - Proper MCP tool annotations (readOnly, title)
  - `addOneWireTools(server, pin)` — single-call registration
- **Session Management** (`MCPSession.h`) — multi-client session tracking:
  - `SessionManager` — tracks concurrent MCP sessions with configurable limits
  - `server.setMaxSessions(n)` — limit concurrent AI clients (default: 4)
  - `server.setSessionTimeout(ms)` — auto-expire idle sessions (default: 30 min)
  - `Session` struct with client name, creation time, last activity tracking
  - Automatic eviction of oldest idle session when limit reached
  - `sessions().summary()` — JSON diagnostic overview of all active sessions
  - `validateSession()`, `removeSession()`, `pruneExpired()` for lifecycle management
- **Heap / Memory Monitor** (`MCPHeap.h`) — embedded memory diagnostics:
  - `HeapMonitor` class — tracks free heap, fragmentation, min-ever, PSRAM
  - `heap_status` tool — current memory state, usage %, fragmentation %, low-memory warning
  - `heap_history` tool — memory statistics since boot, uptime
  - `server.heap().sample()` — periodic sampling for trend tracking
  - `server.heap().isLow()` — quick low-memory check
  - Configurable warning threshold (default 10KB)
- **Temperature Monitor Example** (`examples/temperature_monitor/`) — demonstrates:
  - Multi-sensor DS18B20 monitoring with OneWire tools
  - Heap monitoring for device health
  - Session management with limits and timeouts
  - Temperature history resource with ring buffer
  - Diagnostic prompt for AI-driven analysis
- 13 new unit tests:
  - Session manager: create, validate, remove, max limit, get info, summary, timeout config (8)
  - Heap monitor: initial state, warning threshold, usage percent (3)
  - Server integration: session manager access, heap monitor access (2)

### Changed
- Bumped version to 0.11.0
- Total tests: 136 → 149 unit tests + 15 HTTP integration tests = 164 total
- `Server` class now includes `SessionManager` and `HeapMonitor` members
- `mcpd.h` now includes `MCPSession.h` and `MCPHeap.h`
- Built-in tools now total 38 (34 + 4 OneWire + 2 heap monitoring = 40 tools available)

## [0.10.0] - 2026-02-18

### Added
- **BLE Transport** (`MCPTransportBLE.h`) — Bluetooth Low Energy GATT server for ESP32:
  - Custom MCP BLE service with RX/TX/Status characteristics
  - Automatic message chunking for large payloads (configurable MTU)
  - Chunk framing protocol: single/first/continue/final headers
  - Connect/disconnect event callbacks
  - Auto-restart advertising after client disconnects
  - `server.enableBLE("device-name")` — enable before `begin()`
  - Enables phone/tablet MCP access without WiFi infrastructure
- **Rate Limiting** (`MCPRateLimit.h`) — Token bucket rate limiter for device protection:
  - `server.setRateLimit(rps, burst)` — configure sustained rate and burst capacity
  - HTTP 429 response with JSON-RPC error when limit exceeded
  - Rate limit info advertised in `initialize` response (`serverInfo.rateLimit`)
  - Stats tracking: `totalAllowed()`, `totalDenied()`, `resetStats()`
  - O(1) per-request check, constant memory
- **Connection Lifecycle Hooks** — callbacks for session events:
  - `server.onInitialize(callback)` — called with client name on new session
  - `server.onConnect(callback)` — called on transport connect (SSE/WS/BLE)
  - `server.onDisconnect(callback)` — called on transport disconnect
- **Watchdog Tool** (`tools/MCPWatchdogTool.h`) — hardware watchdog management:
  - `watchdog_status` — get enabled state, timeout, time since last feed, reset reason
  - `watchdog_enable` — enable with configurable timeout (1-120s) and panic mode
  - `watchdog_feed` — feed/reset the watchdog timer
  - `watchdog_disable` — disable the watchdog
  - Proper MCP tool annotations (readOnly, destructive, title)
  - ESP32 `esp_task_wdt` integration
- **BLE Gateway Example** (`examples/ble_gateway/`) — demonstrates:
  - Dual WiFi + BLE MCP server
  - Rate limiting for device protection
  - Lifecycle hooks for status LED
  - Watchdog tool for production reliability
- 13 new unit tests:
  - Rate limiter: default disabled, configure, burst capacity, stats, disable, reset (6)
  - Lifecycle hooks: onInitialize called, unknown client (2)
  - Rate limit integration: in server info, not when disabled (2)
  - Version: 0.10.0 check (1)
  - Watchdog: default state, tool registration (2)

### Changed
- Bumped version to 0.10.0
- Total tests: 123 → 136 (13 new unit tests) + 15 HTTP integration tests
- `server.loop()` now processes BLE transport and forwards notifications via BLE
- `_handleMCPPost()` checks rate limiter before processing requests
- `_handleInitialize()` calls lifecycle hook and includes rate limit info
- `server.stop()` cleans up BLE transport
- Built-in tools now total 34 (30 + 4 watchdog tools)

## [0.9.0] - 2026-02-18

### Added
- **Elicitation Support** (`MCPElicitation.h`) — server can request structured user input from the client:
  - `MCPElicitationRequest` — build form-like input requests with typed fields
  - Field types: text, number, integer, boolean, enum/select
  - Field constraints: required, min/max, default values, enum options
  - `MCPElicitationResponse` — parse user responses with typed getters
  - `ElicitationManager` — queues requests, drains via SSE, handles async responses
  - `server.requestElicitation(request, callback)` — high-level API
  - Three response actions: accept (with content), decline, cancel
  - Server advertises `elicitation` capability in `initialize` response
  - Client responses to elicitation requests are automatically routed to callbacks
  - 120s default timeout (generous for user form-filling)
- **Audio Content Type** (`MCPContent.h`) — tools can now return audio data:
  - `MCPContent::makeAudio(base64Data, mimeType)` — create audio content
  - `MCPToolResult::audio(data, mimeType, description)` — convenience factory
  - Serializes as `{ type: "audio", data: "...", mimeType: "audio/wav" }`
  - Useful for microphone recordings, alert sounds, sensor sonification
- **I2C Bus Scanner Tool** (`tools/MCPI2CScannerTool.h`) — hardware debugging utility:
  - `i2c_scan` — scan entire I2C bus, identify 30+ common sensors/ICs by address
  - `i2c_probe` — probe a specific I2C address with detailed error reporting
  - Configurable bus (0/1), SDA/SCL pins, bus speed
  - Device identification database: BME280, SSD1306, MPU6050, DS3231, AHT20, etc.
  - Both tools marked readOnly + localOnly
- **Server-Integrated WebSocket Transport** — use WS alongside HTTP:
  - `server.enableWebSocket(port)` — enable WS transport before `begin()`
  - WebSocket server starts automatically alongside HTTP
  - WS port advertised via mDNS service TXT record
  - JSON-RPC messages processed through same dispatch pipeline
  - Enables clients that prefer WebSocket (Cline, Continue, etc.)
- **Interactive Config Example** (`examples/interactive_config/`) — demonstrates:
  - Elicitation for runtime configuration wizard
  - I2C scanner for hardware discovery
  - WebSocket transport alongside HTTP
  - Custom tools for config management
- 14 new unit tests:
  - Elicitation: request serialization, integer fields, response accept/decline/cancel,
    manager queue/drain/response/unknown, server capability, server response handling (10)
  - Audio content: factory, serialization, tool result with/without description (4)

### Changed
- Bumped version to 0.9.0
- Total tests: 109 → 123 (14 new unit tests) + 15 HTTP integration tests
- `server.loop()` now manages elicitation outgoing, pruning, and WebSocket transport
- JSON-RPC processor handles elicitation responses alongside sampling responses
- Built-in tools now total 30 (28 + 2 I2C scanner tools)
- WiFiClient mock expanded with read/write/available/operator bool
- WiFiServer mock added for test compilation
- String mock expanded with `endsWith()` method

## [0.8.0] - 2026-02-18

### Added
- **SSE Transport (GET endpoint)** — Server-Sent Events now fully wired into the server:
  - GET `/mcp` with `Accept: text/event-stream` opens an SSE stream
  - Server-push for notifications (progress, resource updates, log messages)
  - SSE Manager handles multiple clients with keepalive and pruning
  - Pending notifications automatically broadcast to connected SSE clients
- **Sampling Support** (`MCPSampling.h`) — server can request LLM inference from the client:
  - `MCPSamplingRequest` — build multi-turn sampling requests with model preferences
  - `MCPSamplingResponse` — parse client responses with text, model, and stop reason
  - `SamplingManager` — queues requests, drains via SSE, handles async responses
  - `server.requestSampling(request, callback)` — high-level API
  - Model preference hints (cost/speed/intelligence priority, model name hints)
  - Stop sequences, system prompt, temperature, includeContext support
  - Server advertises `sampling` capability in `initialize` response
  - Client responses to sampling requests are automatically routed to callbacks
- **Filesystem Tool** (`MCPFilesystemTool.h`) — 6 tools for on-chip storage:
  - `fs_list` — List files in a directory (readOnly)
  - `fs_read` — Read file contents with optional byte limit (readOnly)
  - `fs_write` — Write/create files, append mode supported (destructive)
  - `fs_delete` — Delete files (destructive)
  - `fs_info` — Filesystem usage stats: total/used/free bytes (readOnly)
  - `fs_exists` — Check file/directory existence with metadata (readOnly)
  - Works with SPIFFS, LittleFS, or any `fs::FS` implementation
  - All tools include proper MCP annotations
- **Smart Thermostat Example** (`examples/smart_thermostat/`) — demonstrates:
  - Filesystem tools for temperature logging
  - Sampling: MCU asks AI to analyze temperature patterns
  - Custom tools for thermostat control (status, set target/mode)
  - Prompt for comfort optimization
- 13 new unit tests for sampling (request serialization, model preferences, stop sequences,
  context, response parsing, empty content, manager queue/drain/response/unknown, multiple
  messages, SSE manager state, server sampling capability, server response handling)

### Changed
- Bumped version to 0.8.0
- Total tests: 96 → 109 (13 new unit tests) + 15 HTTP integration tests
- GET `/mcp` now opens SSE stream instead of returning 405
- `server.loop()` now manages SSE keepalive, notification broadcasting, and sampling outgoing
- JSON-RPC processor now handles server-initiated response messages (for sampling callbacks)
- Built-in tools now total 28 (22 + 6 filesystem tools)

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
