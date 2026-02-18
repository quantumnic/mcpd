# mcpd API Reference

## Core Classes

### `mcpd::Server`

The main MCP server class. Handles HTTP transport, JSON-RPC dispatch, session management, and mDNS advertisement.

```cpp
#include <mcpd.h>

mcpd::Server mcp("my-device");       // name, port 80
mcpd::Server mcp("my-device", 8080); // name, custom port
```

#### Constructor

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `name` | `const char*` | — | Server name (used in mDNS and `initialize` response) |
| `port` | `uint16_t` | `80` | HTTP port to listen on |

#### Methods

##### `void addTool(name, description, inputSchemaJson, handler)`

Register a callable tool.

```cpp
mcp.addTool(
    "read_sensor",                              // name
    "Read the temperature sensor",              // description
    R"({"type":"object","properties":{          // JSON Schema for input
        "unit":{"type":"string","enum":["C","F"]}
    }})",
    [](const JsonObject& args) -> String {      // handler
        float temp = readTemp();
        return String("{\"temperature\":") + temp + "}";
    }
);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | `const char*` | Tool name (must be unique) |
| `description` | `const char*` | Human-readable description |
| `inputSchemaJson` | `const char*` | JSON Schema string for the tool's parameters |
| `handler` | `MCPToolHandler` | `std::function<String(const JsonObject&)>` — receives parsed arguments, returns result as JSON string |

##### `void addTool(const MCPTool& tool)`

Register a pre-built tool object.

##### `void addResource(uri, name, description, mimeType, handler)`

Register a readable resource.

```cpp
mcp.addResource(
    "sensor://temperature",         // URI
    "Temperature",                  // name
    "Current temperature reading",  // description
    "application/json",             // MIME type
    []() -> String {                // handler
        return String("{\"value\":22.5}");
    }
);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `uri` | `const char*` | Resource URI (e.g., `sensor://temp`) |
| `name` | `const char*` | Human-readable name |
| `description` | `const char*` | Description of the resource |
| `mimeType` | `const char*` | MIME type of the content |
| `handler` | `MCPResourceHandler` | `std::function<String()>` — returns content string |

##### `void addResource(const MCPResource& resource)`

Register a pre-built resource object.

##### `void begin()`

Start the HTTP server and mDNS advertisement. Call after WiFi is connected.

##### `void loop()`

Process incoming HTTP requests. **Must be called in your `loop()` function.**

##### `void stop()`

Stop the server and free resources.

##### `void setEndpoint(const char* path)`

Set the MCP endpoint path. Default: `"/mcp"`.

##### `void setMDNS(bool enabled)`

Enable or disable mDNS advertisement. Default: `true`.

##### `const char* getName()`

Get the server name.

##### `uint16_t getPort()`

Get the server port.

---

## Built-in Tools

All built-in tools follow the same pattern: `ToolClass::attach(server)`.

### GPIO Tools (`tools/MCPGPIOTool.h`)

```cpp
#include <tools/MCPGPIOTool.h>
mcpd::tools::GPIOTool::attach(mcp);
```

| Tool | Description | Parameters |
|------|-------------|------------|
| `pin_mode` | Set GPIO pin mode | `pin` (int), `mode` (INPUT/OUTPUT/INPUT_PULLUP/INPUT_PULLDOWN) |
| `digital_read` | Read digital pin value | `pin` (int) |
| `digital_write` | Write digital pin value | `pin` (int), `value` (0 or 1) |
| `analog_read` | Read analog value (0-4095) | `pin` (int) |

### PWM Tools (`tools/MCPPWMTool.h`)

```cpp
#include <tools/MCPPWMTool.h>
mcpd::tools::PWMTool::attach(mcp);
```

| Tool | Description | Parameters |
|------|-------------|------------|
| `pwm_write` | Set PWM duty cycle | `pin` (int), `duty` (int), `frequency` (int, default 5000), `resolution` (int, default 8), `channel` (int, default 0) |
| `pwm_stop` | Stop PWM on a pin | `pin` (int) |

### Servo Tools (`tools/MCPServoTool.h`)

```cpp
#include <tools/MCPServoTool.h>
mcpd::tools::ServoTool::attach(mcp);        // default LEDC channel 8
mcpd::tools::ServoTool::attach(mcp, 12);    // custom base channel
```

| Tool | Description | Parameters |
|------|-------------|------------|
| `servo_write` | Set servo angle | `pin` (int), `angle` (0-180), `minUs` (int, default 544), `maxUs` (int, default 2400) |
| `servo_detach` | Stop servo signal | `pin` (int) |

### NeoPixel Tools (`tools/MCPNeoPixelTool.h`)

Requires: `adafruit/Adafruit NeoPixel@^1.12`

```cpp
#include <tools/MCPNeoPixelTool.h>
#include <Adafruit_NeoPixel.h>

Adafruit_NeoPixel strip(16, 5, NEO_GRB + NEO_KHZ800);
// In setup():
strip.begin();
mcpd::tools::NeoPixelTool::attach(mcp, strip);
```

| Tool | Description | Parameters |
|------|-------------|------------|
| `neopixel_set` | Set single pixel color | `index` (int), `r` `g` `b` (0-255), `show` (bool, default true) |
| `neopixel_fill` | Fill all pixels | `r` `g` `b` (0-255) |
| `neopixel_clear` | Turn off all pixels | — |
| `neopixel_brightness` | Set global brightness | `brightness` (0-255) |

### DHT Sensor Tool (`tools/MCPDHTTool.h`)

Requires: `adafruit/DHT sensor library@^1.4`

```cpp
#include <tools/MCPDHTTool.h>
#include <DHT.h>

DHT dht(4, DHT22);
// In setup():
dht.begin();
mcpd::tools::DHTTool::attach(mcp, dht);
```

| Tool | Description | Parameters |
|------|-------------|------------|
| `dht_read` | Read temperature & humidity | `fahrenheit` (bool, default false) |

### I2C Tools (`tools/MCPI2CTool.h`)

```cpp
#include <tools/MCPI2CTool.h>
Wire.begin();
mcpd::tools::I2CTool::attach(mcp);        // default Wire
mcpd::tools::I2CTool::attach(mcp, Wire1); // custom Wire instance
```

| Tool | Description | Parameters |
|------|-------------|------------|
| `i2c_scan` | Scan I2C bus | — |
| `i2c_read` | Read bytes from device | `address` (int), `count` (int, default 1, max 32) |
| `i2c_write` | Write bytes to device | `address` (int), `bytes` (int[]) |

### System Info (`tools/MCPSystemTool.h`)

```cpp
#include <tools/MCPSystemTool.h>
mcpd::tools::SystemTool::attach(mcp);
```

| Tool | Description | Returns |
|------|-------------|---------|
| `system_info` | Get system information | freeHeap, heapSize, uptimeMs, chipModel, cpuFreqMHz, flashSize, ip, mac, rssi |

### WiFi Tools (`tools/MCPWiFiTool.h`)

```cpp
#include <tools/MCPWiFiTool.h>
mcpd::tools::WiFiTool::attach(mcp);
```

| Tool | Description | Returns |
|------|-------------|---------|
| `wifi_status` | Current WiFi info | connected, ssid, ip, gateway, rssi, mac, channel |
| `wifi_scan` | Scan networks | Array of {ssid, rssi, channel, encryption} |

---

## mcpd-bridge (Python)

The bridge translates MCP stdio transport (Claude Desktop) ↔ Streamable HTTP (mcpd on MCU).

### Usage

```bash
python3 mcpd_bridge.py --host my-device.local [--port 80] [--path /mcp]
python3 mcpd_bridge.py --discover  # auto-discover via mDNS
```

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `MCPD_LOG_LEVEL` | `INFO` | Logging level (DEBUG, INFO, WARNING, ERROR) |
| `MCPD_MAX_RETRIES` | `3` | Max retry attempts on connection failure |
| `MCPD_RETRY_DELAY` | `1.0` | Base retry delay in seconds (exponential backoff) |
| `MCPD_TIMEOUT` | `30` | HTTP request timeout in seconds |

### Claude Desktop Configuration

```jsonc
// ~/Library/Application Support/Claude/claude_desktop_config.json
{
    "mcpServers": {
        "my-device": {
            "command": "python3",
            "args": ["/path/to/mcpd/host/mcpd_bridge.py", "--host", "my-device.local"]
        }
    }
}
```

---

## MCP Protocol Details

mcpd implements MCP specification 2025-03-26 via Streamable HTTP transport.

### Supported Methods

| Method | Description |
|--------|-------------|
| `initialize` | Start session, exchange capabilities |
| `notifications/initialized` | Client confirms initialization |
| `ping` | Health check |
| `tools/list` | List available tools |
| `tools/call` | Execute a tool |
| `resources/list` | List available resources |
| `resources/read` | Read a resource |
| `resources/templates/list` | List resource templates |
| `prompts/list` | List available prompts |
| `prompts/get` | Get a prompt with arguments filled in |
| `logging/setLevel` | Set minimum log level for notifications |
| `completion/complete` | Get autocomplete suggestions for prompt args or resource template vars |
| `resources/subscribe` | Subscribe to change notifications for a resource URI |
| `resources/unsubscribe` | Unsubscribe from resource change notifications |

### Session Management

- Session ID sent via `Mcp-Session-Id` header
- Generated on `initialize`, validated on subsequent requests
- Session terminated via `DELETE /mcp`

### Error Codes

| Code | Meaning |
|------|---------|
| `-32700` | Parse error (invalid JSON) |
| `-32600` | Invalid request (missing jsonrpc version or method) |
| `-32601` | Method not found |
| `-32602` | Invalid params (missing tool name, tool not found, etc.) |
