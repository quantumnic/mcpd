# Upgrading mcpd

## From 0.16.x to 0.17.0

**No breaking changes.** Drop-in upgrade.

### New tools (opt-in)

```cpp
#include <mcpd.h>
#include <tools/MCPInterruptTool.h>
#include <tools/MCPAnalogWatchTool.h>

mcpd::Server mcp("my-device");
mcpd::addInterruptTools(mcp);
mcpd::addAnalogWatchTools(mcp);

void loop() {
    mcp.loop();
    mcpd::updateAnalogWatches();  // Required for analog watchdog polling
}
```

### ArduinoJson v7 compatibility

If you were using `containsKey()` in custom tool handlers, replace with:
```cpp
// Before (deprecated):
if (params.containsKey("key")) { ... }

// After:
if (params["key"].is<JsonVariant>()) { ... }
```

Similarly, replace `params["x"].as<String>()` with:
```cpp
const char* val = params["x"].as<const char*>();
String str = val ? val : "";
```

## From 0.15.x to 0.16.0

**No breaking changes.** New tools: NVS storage, GPS, relay control.

## From 0.14.x to 0.15.0

**No breaking changes.** New tools: LCD display, IR remote, RTC clock.

## General upgrade steps

1. Update `library.json` / `library.properties` version
2. Run `pio lib update` or replace the library folder
3. Recompile — no API changes between minor versions
4. New tools are always opt-in via `#include` and explicit registration

## Version policy

- **Major** (1.x): Breaking API changes (none yet)
- **Minor** (0.x): New features, new tools, backward compatible
- **Patch** (0.x.y): Bug fixes only
