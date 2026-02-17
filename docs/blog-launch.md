---
title: "mcpd: Teaching Microcontrollers to Speak AI"
published: false
description: "How I built an open-source MCP Server SDK that runs on ESP32, letting Claude Desktop control real hardware"
tags: esp32, ai, opensource, iot
cover_image: ""
---

# mcpd: Teaching Microcontrollers to Speak AI

What if Claude could read your temperature sensor? Not through a cloud API, not via Home Assistant — directly from the ESP32, over your local network?

That's what **mcpd** does. It's an open-source library that implements the [Model Context Protocol](https://modelcontextprotocol.io) (MCP) directly on microcontrollers. Flash it to an ESP32, and suddenly Claude Desktop, Cursor, or any MCP client can discover and use your hardware as tools.

## The Problem

The AI tool ecosystem is exploding. MCP has become the standard for connecting AI assistants to external capabilities. But embedded hardware was left out of the conversation.

Existing approaches:
- **Custom REST APIs** — every device is a snowflake
- **MQTT bridges** — another layer of indirection
- **Cloud platforms** — your sensor data leaves your network
- **ESP32MCPServer** — exists but doesn't compile and uses a non-standard WebSocket protocol

I wanted something that *just works* with Claude Desktop, follows the actual MCP spec, and runs entirely on your LAN.

## The Solution

mcpd implements MCP 2025-03-26 with three transport options:

```
Claude Desktop ←stdio→ mcpd-bridge (Python) ←HTTP→ ESP32 (mcpd)
```

The Python bridge handles the stdio↔HTTP translation. mDNS (`_mcp._tcp`) handles discovery automatically.

### Hello World in 15 Lines

```cpp
#include <mcpd.h>

mcpd::Server mcp("my-sensor");

void setup() {
    Serial.begin(115200);
    WiFi.begin("MySSID", "MyPassword");
    while (WiFi.status() != WL_CONNECTED) delay(500);

    mcp.addTool("read_temperature", "Read the temperature sensor", R"({
        "type": "object", "properties": {}
    })", [](const JsonObject& params) -> String {
        float temp = analogRead(34) * 0.1;
        return String("{\"temperature_c\":") + temp + "}";
    });

    mcp.begin();
}

void loop() { mcp.loop(); }
```

Configure Claude Desktop:
```json
{
  "mcpServers": {
    "my-sensor": {
      "command": "python3",
      "args": ["mcpd_bridge.py", "--host", "my-sensor.local"]
    }
  }
}
```

Done. Ask Claude "What's the temperature?" and it calls your tool.

## What's Included

### 8 Built-in Tools

No need to write GPIO handlers from scratch:

- **GPIO** — digital_read, digital_write, analog_read, pin_mode
- **PWM** — pwm_write, pwm_stop
- **Servo** — servo_write, servo_detach
- **I2C** — i2c_scan, i2c_read, i2c_write
- **DHT** — dht_read (temperature + humidity)
- **NeoPixel** — neopixel_set, neopixel_fill, neopixel_clear
- **WiFi** — wifi_status, wifi_scan
- **System** — system_info (heap, uptime, chip model)

```cpp
#include <tools/MCPGPIOTool.h>
mcpd::tools::GPIOTool::attach(mcp);
// Claude can now: "Set pin 13 high" or "Read analog pin 34"
```

### Three Transport Options

1. **Streamable HTTP** (default) — POST to `/mcp`, standard MCP
2. **SSE** — GET with `Accept: text/event-stream` for real-time streaming
3. **WebSocket** — for clients that prefer persistent connections

### Production Features

- **Authentication** — API key or Bearer token
- **Captive Portal** — WiFi setup without hardcoded credentials
- **OTA Updates** — firmware updates over WiFi
- **Prometheus Metrics** — `/metrics` endpoint for monitoring
- **Resource Templates** — RFC 6570 Level 1 URI templates
- **Hardware Abstraction** — ESP32 and RP2040 platform HAL

## Testing Without Hardware

mcpd has 35 native tests that run on macOS/Linux — no ESP32 needed:

```bash
git clone https://github.com/redbasecap-buiss/mcpd
cd mcpd && make test
```

20 unit tests cover JSON-RPC parsing and dispatch. 15 integration tests spin up a real HTTP server and make actual requests against it.

## Real-World Examples

The repo includes 5 complete examples:

| Example | What It Does |
|---------|-------------|
| `basic_server` | Minimal MCP server with one custom tool |
| `sensor_hub` | Multi-sensor setup with MCP resources |
| `home_automation` | GPIO + relay control |
| `weather_station` | DHT22 + BMP280 as MCP resources |
| `robot_arm` | Claude controls a 4-DOF servo arm |

The robot arm example is my favorite. Ask Claude to "pick up the object at position (10, 5)" and it calculates inverse kinematics, moves four servos, and reports the result.

## What's Next

- Full RP2040/Pico W testing (HAL is ready)
- MQTT tool for bridging to existing IoT setups
- MCP Prompts support
- More community examples

## Try It

```bash
# PlatformIO
pio pkg install --library "mcpd"

# Or clone
git clone https://github.com/redbasecap-buiss/mcpd
```

MIT licensed. One dependency (ArduinoJson). Star it if you find it useful: **[github.com/redbasecap-buiss/mcpd](https://github.com/redbasecap-buiss/mcpd)**

I'd love to hear what hardware you'd expose to AI first. Drop a comment or open a discussion on GitHub!
