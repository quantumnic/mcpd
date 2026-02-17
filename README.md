<![CDATA[<div align="center">

# mcpd

**MCP Server SDK for Microcontrollers**

Expose ESP32/RP2040 hardware as AI-accessible tools via [Model Context Protocol](https://modelcontextprotocol.io)

[![Native Tests](https://github.com/redbasecap-buiss/mcpd/actions/workflows/test.yml/badge.svg)](https://github.com/redbasecap-buiss/mcpd/actions/workflows/test.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-compatible-orange.svg)](https://platformio.org)
[![Arduino](https://img.shields.io/badge/Arduino-compatible-teal.svg)](https://www.arduino.cc)
[![MCP](https://img.shields.io/badge/MCP-2025--03--26-purple.svg)](https://modelcontextprotocol.io)

</div>

---

**mcpd** turns your microcontroller into a standards-compliant MCP server. Claude Desktop, Cursor, or any MCP client can discover and interact with your hardware — read sensors, toggle GPIOs, publish MQTT messages — using the same protocol they use for any other tool.

## Why mcpd?

| | mcpd | ESP32MCPServer | esp-mcp |
|---|---|---|---|
| Runs on the MCU | ✅ | ✅ | ❌ (CLI tool) |
| MCP spec compliant | ✅ (2025-03-26) | ❌ (custom WebSocket) | ❌ |
| Compiles | ✅ | ❌ (self-described) | N/A |
| Streamable HTTP transport | ✅ | ❌ | ❌ |
| Claude Desktop bridge | ✅ | ❌ | ❌ |
| mDNS discovery | ✅ | ❌ | ❌ |
| Built-in tools (GPIO, I2C, MQTT…) | ✅ | ❌ | ❌ |
| Multi-platform (ESP32, RP2040) | ✅ | ESP32 only | ESP32 only |

## Architecture

```
┌──────────────────────┐         ┌─────────────────────────────┐
│   Claude Desktop /   │  stdio  │      mcpd-bridge            │
│   Cursor / any MCP   │◄───────►│   (Python, runs on host)    │
│   Client             │         │                             │
└──────────────────────┘         └──────────┬──────────────────┘
                                            │ HTTP POST/SSE
                                            │ (Streamable HTTP)
                                            ▼
                                 ┌─────────────────────────────┐
                                 │        ESP32 / RP2040       │
                                 │    ┌─────────────────────┐  │
                                 │    │      mcpd server     │  │
                                 │    │  ┌────┐ ┌────┐      │  │
                                 │    │  │GPIO│ │I2C │ ...   │  │
                                 │    │  └────┘ └────┘      │  │
                                 │    └─────────────────────┘  │
                                 │    mDNS: _mcp._tcp          │
                                 └─────────────────────────────┘
```

## Quick Start

### 1. Install the library

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

**Arduino IDE**: Download this repository as ZIP → Sketch → Include Library → Add .ZIP Library

### 2. Write your firmware

```cpp
#include <mcpd.h>

mcpd::Server mcp("my-sensor-hub");

void setup() {
    Serial.begin(115200);
    WiFi.begin("MySSID", "MyPassword");
    while (WiFi.status() != WL_CONNECTED) delay(500);

    // Register a custom tool
    mcp.addTool("read_temperature", "Read the DS18B20 temperature sensor", R"({
        "type": "object",
        "properties": {}
    })", [](const JsonObject& params) -> String {
        float temp = readDS18B20(); // your function
        return String("{\"temperature\":") + temp + "}";
    });

    mcp.begin();
    Serial.printf("MCP server at http://%s/mcp\n", WiFi.localIP().toString().c_str());
}

void loop() {
    mcp.loop();
}
```

### 3. Connect to Claude Desktop

```jsonc
// claude_desktop_config.json
{
  "mcpServers": {
    "my-sensor-hub": {
      "command": "python3",
      "args": ["/path/to/mcpd/host/mcpd_bridge.py", "--host", "my-sensor-hub.local"]
    }
  }
}
```

The bridge discovers your MCU via mDNS and translates stdio ↔ Streamable HTTP.

## Supported Hardware

| Platform | Status | Notes |
|----------|--------|-------|
| ESP32 / ESP32-S2 / S3 / C3 | ✅ Supported | WiFi built-in |
| RP2040 (Pico W) | 🔜 Planned | WiFi via CYW43 |
| STM32 + Ethernet | 🔜 Planned | Requires Ethernet shield |

## Built-in Tools

mcpd ships with optional built-in tools you can enable:

- **GPIO** — `digital_read`, `digital_write`, `analog_read`, `pin_mode`
- **PWM** — `pwm_write`, `pwm_stop` (hardware PWM via LEDC)
- **Servo** — `servo_write`, `servo_detach` (hobby servo control)
- **NeoPixel** — `neopixel_set`, `neopixel_fill`, `neopixel_clear`, `neopixel_brightness` (requires Adafruit NeoPixel library)
- **DHT Sensor** — `dht_read` (temperature & humidity, requires DHT library)
- **WiFi Info** — `wifi_status`, `wifi_scan`
- **I2C** — `i2c_scan`, `i2c_read`, `i2c_write`
- **System** — `system_info` (free heap, uptime, chip model)

For full API documentation, see [docs/API.md](docs/API.md).

```cpp
#include <mcpd.h>
#include <tools/MCPGPIOTool.h>
#include <tools/MCPSystemTool.h>

mcpd::Server mcp("my-board");

void setup() {
    // ...WiFi setup...
    mcpd::tools::GPIOTool::attach(mcp);
    mcpd::tools::SystemTool::attach(mcp);
    mcp.begin();
}
```

## MCP Compliance

mcpd implements the [MCP specification 2025-03-26](https://modelcontextprotocol.io/specification/2025-03-26):

- ✅ JSON-RPC 2.0 message format
- ✅ `initialize` / `initialized` lifecycle
- ✅ `tools/list` and `tools/call`
- ✅ `resources/list` and `resources/read`
- ✅ Capability negotiation
- ✅ Streamable HTTP transport (POST + SSE)
- ✅ Session management (`Mcp-Session-Id`)
- ✅ mDNS service advertisement (`_mcp._tcp`)

## Roadmap

- [x] Core MCP server with Streamable HTTP
- [x] Built-in GPIO, System, WiFi tools
- [x] Host bridge (Python stdio ↔ HTTP)
- [x] mDNS discovery
- [ ] Built-in I2C/SPI tools
- [ ] Built-in MQTT tool
- [ ] Resources: live sensor readings as MCP resources
- [ ] RP2040 (Pico W) support
- [ ] OTA updates via MCP tool
- [ ] WebSocket transport option
- [ ] Authentication (API key / mTLS)
- [ ] Prompts support

## Native Testing

mcpd can be compiled and tested on macOS/Linux **without any hardware**. The test harness includes Arduino API mocks and a real POSIX socket HTTP server, so tests send actual HTTP requests against the MCP server running on localhost.

```bash
# Run all tests (unit + HTTP integration)
make test
```

**What gets tested:**
- **20 unit tests** — JSON-RPC parsing, dispatch, error handling, batch requests
- **15 HTTP integration tests** — Real HTTP requests against a POSIX socket MCP server: initialize, tools/list, tools/call, resources, sessions, CORS, batch, notifications

**Test architecture:**
- `test/arduino_mock.h` — Arduino API stubs (String, Serial, GPIO, WiFi, mDNS, I2C)
- `test/mocks/` — Per-header mock files including EEPROM (RAM-backed)
- `test/native/test_mcp_http.cpp` — HTTP integration tests with embedded POSIX socket server
- `test/test_jsonrpc.cpp` — Unit tests calling JSON-RPC layer directly

## Examples

- [`basic_server`](examples/basic_server/) — Minimal MCP server with one custom tool
- [`sensor_hub`](examples/sensor_hub/) — Multi-sensor setup with resources
- [`home_automation`](examples/home_automation/) — GPIO + MQTT home automation

## License

[MIT](LICENSE) — Nicola Spieser
]]>