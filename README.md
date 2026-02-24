<div align="center">

```
                              ___
   _ __ ___   ___ _ __   __| |
  | '_ ` _ \ / __| '_ \ / _` |
  | | | | | | (__| |_) | (_| |
  |_| |_| |_|\___| .__/ \__,_|
                  |_|
  ⚡ MCP Server SDK for Microcontrollers
```

**Expose ESP32/RP2040 hardware as AI-accessible tools via [Model Context Protocol](https://modelcontextprotocol.io)**

[![Native Tests](https://github.com/redbasecap-buiss/mcpd/actions/workflows/test.yml/badge.svg)](https://github.com/redbasecap-buiss/mcpd/actions/workflows/test.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-compatible-orange.svg)](https://platformio.org)
[![Arduino](https://img.shields.io/badge/Arduino-compatible-teal.svg)](https://www.arduino.cc)
[![MCP](https://img.shields.io/badge/MCP-2025--11--25-purple.svg)](https://modelcontextprotocol.io)
[![Works with Claude Desktop](https://img.shields.io/badge/Works%20with-Claude%20Desktop-cc785c.svg)](https://claude.ai/download)

</div>

---

**mcpd** turns your microcontroller into a standards-compliant MCP server. Claude Desktop, Cursor, or any MCP client can discover and interact with your hardware — read sensors, toggle GPIOs, control servos — using the same protocol they use for any other tool.

> 💡 **30-second pitch:** Flash an ESP32, connect to WiFi, and Claude can read your temperature sensor. No cloud. No custom API. Just MCP.

## Why mcpd?

| Feature | mcpd | ESP32MCPServer | esp-mcp |
|---|:---:|:---:|:---:|
| Runs on the MCU | ✅ | ✅ | ❌ CLI tool |
| MCP spec compliant | ✅ 2025-11-25 | ❌ custom WS | ❌ |
| Actually compiles | ✅ 1545 tests | ❌ self-described | N/A |
| Streamable HTTP + SSE | ✅ | ❌ | ❌ |
| WebSocket transport | ✅ | ✅ | ❌ |
| Claude Desktop bridge | ✅ | ❌ | ❌ |
| mDNS discovery | ✅ | ❌ | ❌ |
| Resource Templates (RFC 6570) | ✅ | ❌ | ❌ |
| Built-in tools (GPIO, I2C, SPI, ADC, UART, CAN, Modbus, FS, Power, Timer, LCD, IR, RTC, Camera, ESP-NOW…) | ✅ 106 tools | ❌ | ❌ |
| Sampling (server requests LLM from client) | ✅ | ❌ | ❌ |
| Elicitation (server requests user input) | ✅ | ❌ | ❌ |
| Audio content type | ✅ | ❌ | ❌ |
| Tool Annotations (readOnly, destructive hints) | ✅ | ❌ | ❌ |
| Server Instructions (LLM guidance) | ✅ | ❌ | ❌ |
| Runtime Tool Enable/Disable | ✅ | ❌ | ❌ |
| Structured Content (text, image, resource) | ✅ | ❌ | ❌ |
| Icons (server, tools, resources, prompts) | ✅ | ❌ | ❌ |
| ResourceLink content type | ✅ | ❌ | ❌ |
| Progress Notifications | ✅ | ❌ | ❌ |
| Request Cancellation | ✅ | ❌ | ❌ |
| Prompts support | ✅ | ❌ | ❌ |
| Authentication | ✅ | ❌ | ❌ |
| Role-Based Access Control (RBAC) | ✅ | ❌ | ❌ |
| OTA Updates | ✅ | ❌ | ❌ |
| Prometheus Metrics | ✅ | ❌ | ❌ |
| Captive Portal + Setup CLI | ✅ | ❌ | ❌ |
| Hardware Abstraction Layer | ✅ | ❌ | ❌ |
| Resource Subscriptions | ✅ | ❌ | ❌ |
| Completion/Autocomplete | ✅ | ❌ | ❌ |
| Roots | ✅ | ❌ | ❌ |
| Multi-platform (ESP32, RP2040, STM32) | ✅ | ESP32 only | ESP32 only |

## Architecture

```
┌──────────────────────┐         ┌─────────────────────────────┐
│   Claude Desktop /   │  stdio  │      mcpd-bridge            │
│   Cursor / any MCP   │◄───────►│   (Python, runs on host)    │
│   Client             │         │   Auto-discovers via mDNS   │
└──────────────────────┘         └──────────┬──────────────────┘
                                            │
                              ┌─────────────┼─────────────┐
                              │ HTTP POST   │ SSE GET     │ WebSocket
                              │ (Streamable HTTP)         │
                              ▼             ▼             ▼
                   ┌──────────────────────────────────────────┐
                   │            ESP32 / RP2040                 │
                   │  ┌──────────────────────────────────────┐│
                   │  │           mcpd::Server                ││
                   │  │  ┌────────────┐  ┌────────────────┐  ││
                   │  │  │ Transport  │  │   Dispatch      │  ││
                   │  │  │ HTTP/SSE/WS│  │ JSON-RPC 2.0   │  ││
                   │  │  └────────────┘  └───────┬────────┘  ││
                   │  │          ┌───────────────┬┼──────┐   ││
                   │  │          ▼               ▼▼      ▼   ││
                   │  │  ┌─────────────┐ ┌──────────┐ ┌───┐ ││
                   │  │  │Tools (22)   │ │Resources │ │Tpl│ ││
                   │  │  │GPIO PWM I2C │ │Readings  │ │URI│ ││
                   │  │  │SPI ADC UART │ │Status    │ │   │ ││
                   │  │  │Servo DHT NP │ │Custom    │ │   │ ││
                   │  │  │WiFi Sys MQTT│ │          │ │   │ ││
                   │  │  │Power Timer  │ │          │ │   │ ││
                   │  │  └─────────────┘ └──────────┘ └───┘ ││
                   │  └──────────────────────────────────────┘│
                   │  ┌──────────────────────────────────────┐│
                   │  │ Platform HAL │ Auth │ OTA │ Metrics  ││
                   │  └──────────────────────────────────────┘│
                   │  mDNS: _mcp._tcp    /metrics (Prometheus)│
                   └──────────────────────────────────────────┘
```

## Quick Start

### 1. Install

**PlatformIO** (recommended):
```ini
; platformio.ini
[env:esp32]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps =
    mcpd
    bblanchon/ArduinoJson@^7
```

**Arduino IDE**: Download as ZIP → Sketch → Include Library → Add .ZIP Library

### 2. Minimal Firmware (5 minutes)

```cpp
#include <mcpd.h>

mcpd::Server mcp("my-device");

void setup() {
    Serial.begin(115200);
    WiFi.begin("MySSID", "MyPassword");
    while (WiFi.status() != WL_CONNECTED) delay(500);

    mcp.addTool("read_temperature", "Read the temperature sensor", R"({
        "type": "object",
        "properties": {}
    })", [](const JsonObject& params) -> String {
        float temp = analogRead(34) * 0.1; // your sensor logic
        return String("{\"temperature_c\":") + temp + "}";
    });

    mcp.begin();
    Serial.printf("MCP server at http://%s/mcp\n", WiFi.localIP().toString().c_str());
}

void loop() {
    mcp.loop();
}
```

### 3. Connect Claude Desktop

Add to your `claude_desktop_config.json`:

```jsonc
{
  "mcpServers": {
    "my-device": {
      "command": "python3",
      "args": ["/path/to/mcpd/host/mcpd_bridge.py", "--host", "my-device.local"]
    }
  }
}
```

The bridge discovers your MCU via mDNS and translates stdio ↔ Streamable HTTP.

### 4. Talk to your hardware!

> **You:** "What's the temperature?"
>
> **Claude:** *calls `read_temperature`* → "The sensor reads 23.5°C"

<!-- TODO: Screenshot of Claude Desktop using mcpd tool -->
<!-- ![Claude Desktop screenshot](docs/claude-desktop-screenshot.png) -->

## Features

### 🔐 Authentication

Protect your MCP server with API key authentication:

```cpp
#include <mcpd.h>
#include <MCPAuth.h>

mcpd::Server mcp("my-device");
mcpd::Auth auth;

void setup() {
    // ...WiFi setup...
    auth.setApiKey("your-secret-key-here");
    // Auth integrates with the server — see examples for full setup
    mcp.begin();
}
```

Supports Bearer tokens, `X-API-Key` header, and `?key=` query parameter.

### 📡 SSE Transport

Server-Sent Events for streaming responses (MCP Streamable HTTP spec):

```cpp
#include <MCPTransportSSE.h>

mcpd::SSEManager sse;
// Clients connect via GET /mcp with Accept: text/event-stream
// Server pushes events in real-time
```

### 📶 Captive Portal Setup

No hardcoded WiFi credentials — configure via captive portal:

```cpp
#include <MCPConfig.h>

mcpd::Config config;

void setup() {
    if (!config.load()) {
        // First boot: start WiFi setup portal
        config.startCaptivePortal("mcpd-setup");
    }
    config.connectWiFi();
}
```

Connect to the `mcpd-setup` WiFi network and a setup page opens automatically.

### 🔄 OTA Updates

Update firmware over WiFi — no USB cable needed:

```cpp
#include <MCPOTA.h>

mcpd::OTA ota;

void setup() {
    // ...WiFi setup...
    ota.begin("my-device");
}

void loop() {
    ota.loop();  // handles OTA in background
    mcp.loop();
}
```

Then: `pio run -t upload --upload-port my-device.local`

### 💬 Prompts

Expose reusable prompt templates that MCP clients can discover and use:

```cpp
#include <mcpd.h>

mcp.addPrompt("diagnose_sensor",
    "Diagnose a sensor issue",
    {
        mcpd::MCPPromptArgument("sensor_id", "Sensor to diagnose", true),
        mcpd::MCPPromptArgument("symptom", "Observed symptom", false)
    },
    [](const std::map<String, String>& args) -> std::vector<mcpd::MCPPromptMessage> {
        String sensor = args.at("sensor_id");
        return {
            mcpd::MCPPromptMessage("user",
                (String("Please diagnose sensor '") + sensor +
                 "'. Read its current value, check the last 5 readings, "
                 "and tell me if anything looks wrong.").c_str())
        };
    });
```

Clients call `prompts/list` to discover available prompts and `prompts/get` to retrieve them with arguments filled in.

### 📝 Logging

MCP-native logging — clients control the log level, server sends structured log notifications:

```cpp
#include <mcpd.h>

mcpd::Server mcp("my-device");

void setup() {
    // ... WiFi + tools setup ...
    mcp.begin();

    // Log at various levels — only messages >= client's level are sent
    mcp.logging().info("sensors", "Temperature sensor initialized");
    mcp.logging().warning("battery", "Battery below 20%");
    mcp.logging().error("network", "MQTT connection failed");
}
```

Clients call `logging/setLevel` to control verbosity (debug → emergency). Log messages arrive as `notifications/message`.

### 📄 Pagination

For devices with many tools/resources, enable cursor-based pagination:

```cpp
mcp.setPageSize(5);  // 5 items per page
// Clients receive nextCursor in response to fetch more
```

### 🔄 Dynamic Tools

Add or remove tools at runtime (e.g., when peripherals are connected/disconnected):

```cpp
mcp.addTool("new_sensor", "Read new sensor", schema, handler);
mcp.notifyToolsChanged();  // Notify connected clients

mcp.removeTool("old_sensor");
mcp.notifyToolsChanged();

// Enable/disable tools without removing them
mcp.disableTool("maintenance_tool");   // Hidden from clients
mcp.enableTool("maintenance_tool");    // Visible again
```

### 📋 Server Instructions

Guide the LLM's behavior with your device using server instructions (MCP 2025-11-25):

```cpp
mcp.setInstructions(
    "This device controls a greenhouse. Always read temperature "
    "before adjusting fans. Never set irrigation above 80%."
);
```

Instructions are sent in the `initialize` response and help the model understand the context and constraints of your hardware.

### 🏷️ Custom Version

Override the version string in server info (useful for firmware versioning):

```cpp
mcp.setVersion("2.1.0-greenhouse");
```

### 📊 Prometheus Metrics

Monitor your device with `/metrics` endpoint:

```cpp
#include <MCPMetrics.h>

mcpd::Metrics metrics;
// After server starts:
// metrics.begin(httpServer);
```

Exposes: `mcpd_uptime_seconds`, `mcpd_free_heap_bytes`, `mcpd_requests_total`, `mcpd_request_latency_ms_avg`, `mcpd_wifi_rssi_dbm`, and more.

```bash
curl http://my-device.local/metrics
# mcpd_uptime_seconds 3421
# mcpd_free_heap_bytes 142680
# mcpd_requests_total 847
# mcpd_wifi_rssi_dbm -42
```

## Built-in Tools

mcpd ships with optional built-in tools:

| Tool | Functions | Library Required |
|------|-----------|-----------------|
| **GPIO** | `digital_read`, `digital_write`, `analog_read`, `pin_mode` | — |
| **PWM** | `pwm_write`, `pwm_stop` | — |
| **Servo** | `servo_write`, `servo_detach` | ESP32Servo |
| **NeoPixel** | `neopixel_set`, `neopixel_fill`, `neopixel_clear` | Adafruit NeoPixel |
| **DHT** | `dht_read` (temperature & humidity) | DHT |
| **WiFi** | `wifi_status`, `wifi_scan` | — |
| **I2C** | `i2c_scan`, `i2c_read`, `i2c_write` | — |
| **MQTT** | `mqtt_connect`, `mqtt_publish`, `mqtt_subscribe`, `mqtt_messages`, `mqtt_status` | PubSubClient |
| **System** | `system_info` (heap, uptime, chip) | — |
| **OTA** | `ota_info`, `ota_partitions`, `ota_rollback`, `ota_mark_valid` | — |

```cpp
#include <tools/MCPGPIOTool.h>
#include <tools/MCPSystemTool.h>
#include <tools/MCPMQTTTool.h>

mcpd::tools::GPIOTool::attach(mcp);
mcpd::tools::SystemTool::attach(mcp);

mcpd::tools::MQTTTool mqtt;
mqtt.attach(mcp);
// In loop(): mqtt.loop();
```

For full API documentation, see [docs/API.md](docs/API.md). For a technical overview of the codebase, see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Examples

| Example | Description | Hardware |
|---------|-------------|----------|
| [`basic_server`](examples/basic_server/) | Minimal MCP server with one custom tool | ESP32 only |
| [`sensor_hub`](examples/sensor_hub/) | Multi-sensor setup with resources | ESP32 + sensors |
| [`home_automation`](examples/home_automation/) | GPIO + MQTT home automation | ESP32 + relays |
| [`weather_station`](examples/weather_station/) | Temperature, humidity, pressure as MCP resources | ESP32 + DHT22 + BMP280 |
| [`mqtt_bridge`](examples/mqtt_bridge/) | MQTT pub/sub bridge — AI talks to IoT | ESP32 + MQTT broker |
| [`robot_arm`](examples/robot_arm/) | Claude controls a 4-DOF servo robot arm | ESP32 + 4× servos |
| [`smart_greenhouse`](examples/smart_greenhouse/) | Greenhouse automation with logging & dynamic tools | ESP32 + DHT22 + relays |
| [`async_tasks`](examples/async_tasks/) | Async tool execution with progress tracking & input flow | ESP32 only |

## Supported Platforms

| Platform | Status | WiFi | Flash | RAM | Notes |
|----------|--------|------|-------|-----|-------|
| **ESP32** | ✅ Stable | Built-in | 4 MB | 520 KB | Recommended for beginners |
| **ESP32-S2** | ✅ Stable | Built-in | 4 MB | 320 KB | Single-core, USB native |
| **ESP32-S3** | ✅ Stable | Built-in | 8 MB | 512 KB | Dual-core, AI acceleration |
| **ESP32-C3** | ✅ Stable | Built-in | 4 MB | 400 KB | RISC-V, low power |
| **RP2040 (Pico W)** | 🧪 HAL ready | CYW43 | 2 MB | 264 KB | Platform HAL implemented |
| **STM32 + Ethernet** | 🔜 Planned | External | Varies | Varies | Requires Ethernet shield |

## MCP Compliance

Implements [MCP specification 2025-11-25](https://modelcontextprotocol.io/specification/2025-11-25):

- ✅ JSON-RPC 2.0 message format
- ✅ `initialize` / `initialized` lifecycle
- ✅ `tools/list` and `tools/call`
- ✅ `resources/list` and `resources/read`
- ✅ Capability negotiation
- ✅ Streamable HTTP transport (POST + SSE)
- ✅ Session management (`Mcp-Session-Id`)
- ✅ mDNS service advertisement (`_mcp._tcp`)
- ✅ `prompts/list` and `prompts/get`
- ✅ Batch request support
- ✅ CORS headers for browser clients
- ✅ `logging/setLevel` and `notifications/message` (logging capability)
- ✅ Cursor-based pagination for list methods
- ✅ `notifications/*/list_changed` (dynamic tool/resource/prompt changes)
- ✅ `notifications/cancelled` handling

## Native Testing

Test on macOS/Linux **without any hardware**:

```bash
make test
```

- **222 JSON-RPC tests** — Protocol parsing, dispatch, error handling, batch requests, prompts, logging, pagination
- **202 tool tests** — All built-in tools: GPIO, PWM, Servo, NeoPixel, DHT, I2C, SPI, ADC, UART, CAN, Modbus, LoRa, camera, ESP-NOW, and more
- **38 HTTP integration tests** — Real HTTP requests against a POSIX socket MCP server: full lifecycle, CORS, prompts, completion, subscriptions, concurrency

## Roadmap

- [x] Core MCP server with Streamable HTTP
- [x] Built-in GPIO, PWM, Servo, NeoPixel, DHT, I2C, System tools
- [x] Host bridge (Python stdio ↔ HTTP)
- [x] mDNS discovery
- [x] SSE transport
- [x] Authentication (API key / Bearer token)
- [x] Configuration persistence (NVS + Captive Portal)
- [x] OTA updates
- [x] Prometheus metrics
- [x] WebSocket transport
- [x] Hardware Abstraction Layer (ESP32 + RP2040)
- [x] Interactive serial setup CLI
- [x] Resource Templates (RFC 6570 Level 1)
- [ ] RP2040 (Pico W) full platform testing
- [x] Built-in MQTT tool
- [x] Prompts support
- [x] BLE transport (ESP32)
- [x] Rate limiting
- [x] Connection lifecycle hooks
- [x] Watchdog tool
- [ ] mTLS authentication

## Testing

For host-side testing without hardware, see [**esp32emu**](https://github.com/redbasecap-buiss/esp32emu) — a lightweight ESP32/Arduino emulator with real network sockets.

## License

[MIT](LICENSE) — Nicola Spieser
