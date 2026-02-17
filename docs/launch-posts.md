# mcpd Launch Posts

## Reddit r/esp32

**Title:** I built an open-source MCP server SDK that runs directly on ESP32 — Claude/Cursor can now talk to your hardware

Hey r/esp32! 👋

I've been working on **mcpd** — an open-source library that turns your ESP32 into a [Model Context Protocol](https://modelcontextprotocol.io) server. This means Claude Desktop, Cursor, or any MCP client can discover and interact with your hardware directly.

**What it does:**
- Implements MCP spec 2025-03-26 with Streamable HTTP, SSE, and WebSocket transports
- Ships with 8 built-in tools: GPIO, PWM, Servo, I2C, DHT, NeoPixel, WiFi scan, System info
- mDNS auto-discovery (`_mcp._tcp`) — Claude finds your device automatically
- Python bridge for Claude Desktop (stdio ↔ HTTP)
- Auth, OTA updates, Prometheus metrics, captive portal setup

**Quick example:**
```cpp
#include <mcpd.h>
mcpd::Server mcp("my-sensor");

void setup() {
    WiFi.begin("SSID", "pass");
    mcp.addTool("read_temp", "Read temperature", schema, handler);
    mcp.begin();
}
void loop() { mcp.loop(); }
```

Then in Claude: "What's the temperature?" → Claude calls your tool → gets real sensor data.

35 native tests, runs on all ESP32 variants (S2, S3, C3). RP2040 HAL is ready too.

🔗 GitHub: https://github.com/redbasecap-buiss/mcpd
📄 MIT Licensed

Would love feedback! What hardware would you want to expose to AI first?

---

## Reddit r/arduino

**Title:** mcpd — MCP Server SDK that lets AI assistants (Claude, Cursor) control your Arduino-compatible boards

Hey r/arduino!

I built **mcpd**, an Arduino-compatible library that implements the [Model Context Protocol](https://modelcontextprotocol.io) on microcontrollers. It lets AI assistants like Claude Desktop discover and use your hardware as tools.

**Why this matters:** Instead of building custom APIs or MQTT bridges, you implement MCP — the same standard protocol that Claude, Cursor, and other AI tools already speak. Your ESP32 becomes just another tool the AI can use.

**Features:**
- Arduino IDE and PlatformIO compatible
- 8 built-in tools (GPIO, I2C, Servo, NeoPixel, DHT, PWM, WiFi, System)
- Streamable HTTP + SSE + WebSocket transports
- mDNS discovery, authentication, OTA updates
- Captive portal for WiFi setup (no hardcoded credentials)
- 35 unit + integration tests

Install via PlatformIO: `lib_deps = mcpd` or download ZIP for Arduino IDE.

Currently supports ESP32 family, RP2040 HAL is implemented and coming next.

GitHub: https://github.com/redbasecap-buiss/mcpd | MIT License

---

## Reddit r/homeassistant

**Title:** mcpd: Turn ESP32 sensors into AI-accessible MCP tools — works with Claude Desktop alongside Home Assistant

Hi r/homeassistant!

If you're running ESP32-based sensors and want to give AI assistants direct access to your hardware, check out **mcpd**.

It's an open-source library that implements the Model Context Protocol (MCP) directly on the ESP32. This means Claude Desktop can read your sensors, toggle relays, or scan I2C buses — without going through HA or a cloud service.

**Use case:** You have a weather station ESP32 with DHT22 + BMP280. With mcpd, Claude can directly query: "What's the temperature and humidity in the greenhouse?" — and get real-time readings.

It's complementary to HA, not a replacement. Think of it as giving your AI assistant direct hardware access for prototyping, debugging, or specialized queries.

- mDNS auto-discovery
- Built-in tools for GPIO, I2C, DHT, and more
- Auth support (API key / Bearer token)
- Prometheus `/metrics` endpoint

GitHub: https://github.com/redbasecap-buiss/mcpd | MIT License

---

## Reddit r/selfhosted

**Title:** mcpd — Self-hosted MCP server on ESP32, lets AI assistants interact with your hardware. No cloud required.

For the self-hosting crowd: **mcpd** runs an MCP server directly on your ESP32. No cloud, no subscriptions, no data leaving your network.

Claude Desktop connects to your ESP32 via a local Python bridge. mDNS handles discovery. Everything stays on your LAN.

Features: Streamable HTTP/SSE/WebSocket transports, API key auth, OTA updates, Prometheus metrics, captive portal setup.

35 tests. MIT licensed. Single dependency (ArduinoJson).

https://github.com/redbasecap-buiss/mcpd

---

## Twitter/X Thread

**🧵 Thread:**

1/ Introducing **mcpd** — an open-source MCP Server SDK that runs directly on ESP32 and RP2040.

Your microcontroller becomes a tool that Claude, Cursor, or any MCP client can discover and use. No cloud. No custom API.

🔗 https://github.com/redbasecap-buiss/mcpd

2/ How it works:

```
Claude Desktop ↔ stdio ↔ mcpd-bridge (Python) ↔ HTTP ↔ ESP32 (mcpd)
```

mDNS auto-discovery. Streamable HTTP + SSE + WebSocket. The full MCP 2025-03-26 spec.

3/ Ships with 8 built-in tools:
⚡ GPIO read/write
🔧 PWM & Servo control
🌡️ DHT temperature/humidity
🔌 I2C scan/read/write
💡 NeoPixel control
📡 WiFi scanning
📊 System info

4/ But the real power is custom tools. 10 lines of code:

```cpp
mcp.addTool("read_temp", "Read temperature", schema, [](auto& p) {
    return String(analogRead(34) * 0.1);
});
```

Now Claude can ask: "What's the temperature?" and get real data.

5/ Also includes:
🔐 API key / Bearer token auth
📡 Captive portal (no hardcoded WiFi)
🔄 OTA firmware updates
📊 Prometheus /metrics
🧪 35 native tests (no hardware needed)

MIT licensed. One dependency. Works today.

Try it: https://github.com/redbasecap-buiss/mcpd ⭐
