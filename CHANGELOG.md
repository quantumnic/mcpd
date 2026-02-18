# Changelog

All notable changes to this project will be documented in this file.

## [0.21.0] - 2026-02-18

### Added
- **Modbus RTU/TCP Tool** (`tools/MCPModbusTool.h`) — industrial protocol support for PLCs, sensors, and equipment:
  - `modbus_init` — initialize as RTU master (serial/RS-485) or TCP master (Ethernet/WiFi)
  - `modbus_read_coils` — read coil status (FC 01), up to 2000 coils
  - `modbus_read_discrete` — read discrete inputs (FC 02)
  - `modbus_read_holding` — read holding registers (FC 03) with format options (uint16, int16, uint32, int32, float32, hex)
  - `modbus_read_input` — read input registers (FC 04)
  - `modbus_write_coil` — write single coil (FC 05)
  - `modbus_write_register` — write single holding register (FC 06)
  - `modbus_write_coils` — write multiple coils (FC 15)
  - `modbus_write_registers` — write multiple holding registers (FC 16)
  - `modbus_scan` — scan bus for responding devices (addresses 1-247)
  - `modbus_status` — connection status and error counters (requests, responses, timeouts, CRC errors, exceptions)
  - CRC-16/Modbus implementation
  - RS-485 direction enable (DE) pin support
  - Modbus exception code decoding
- **Modbus Gateway Example** (`examples/modbus_gateway/`) — full industrial gateway with:
  - RS-485 Modbus RTU master setup
  - Device map resource and register map resource template
  - Diagnostic prompt for bus health checks
  - WiFi + System tool integration

### Tests
- Added 20 Modbus tool tests (CRC verification, init modes, read/write operations, validation, scanning, status counters, tool registration)
- Added 30 Camera tool tests (base64 encoding, non-ESP32 fallback, tool registration)
- Added 11 ESP-NOW tool tests (MAC parsing, non-ESP32 fallback, tool registration)
- Added 8 LoRa tool tests (non-ESP32 fallback, tool registration)
- Added 10 I2S Audio tool tests (base64 encoding, non-ESP32 fallback, tool registration)
- Total test count: 323 (was 304)

## [0.20.0] - 2026-02-18

### Added
- **LoRa Radio Tool** (`tools/MCPLoRaTool.h`) — SX1276/SX1278 long-range radio via SPI:
  - `lora_init` — initialize radio with configurable frequency (433/868/915 MHz), spreading factor, bandwidth, coding rate, sync word, preamble, CRC
  - `lora_send` — transmit packets (text or hex), auto-returns to receive mode
  - `lora_receive` — read received packets from 32-slot ring buffer with RSSI/SNR metadata
  - `lora_configure` — change radio parameters on the fly without re-init
  - `lora_status` — full radio status: config, TX/RX counts, buffered packets, signal quality
  - `lora_sleep` — put radio in sleep mode for power saving, or wake back up
  - `lora_cad` — channel activity detection to check if frequency is in use
  - ISR-safe receive callback with ring buffer
  - Delivery statistics tracking
  - `addLoRaTools(server, pins)` — single-call registration
- **I2S Audio Tool** (`tools/MCPI2SAudioTool.h`) — microphone input and speaker output via I2S:
  - `i2s_init` — configure I2S for mic, speaker, or duplex mode with sample rate (8-48kHz), bit depth (16/24/32), mono/stereo
  - `i2s_record` — record audio, returns base64-encoded PCM or WAV (with proper RIFF header)
  - `i2s_play` — play base64-encoded audio with auto WAV header detection and volume scaling
  - `i2s_volume` — software volume control (0-100%)
  - `i2s_status` — interface stats: sample rate, bit depth, volume, total samples recorded/played
  - `i2s_stop` — clean shutdown and driver uninstall
  - Built-in base64 encoder/decoder for audio data
  - Supports INMP441, SPH0645, ICS-43434 mics and MAX98357A, PCM5102 DACs
  - `addI2SAudioTools(server, pins)` — single-call registration
- **LoRa Smart Farm Example** (`examples/lora_farm/`) — demonstrates:
  - ESP32 + LoRa radio for long-range mesh communication between farm nodes
  - DHT22 temperature/humidity + soil moisture sensor + relay irrigation control
  - Optional I2S microphone for acoustic pest detection
  - Custom `soil_read` tool with multi-sample averaging and qualitative assessment
  - AI-guided farming prompt and soil moisture resource
- 23 new unit tests:
  - LoRa tools: non-ESP32 fallback (7), registration (1)
  - I2S audio tools: base64 encoding (4), non-ESP32 fallback (6), registration (1)
  - Fixed version assertions in existing tests
- Version test assertions updated from 0.18.0 to 0.20.0

### Changed
- Bumped version to 0.20.0
- Total test count: **304** (222 JSON-RPC + 67 tool tests + 15 HTTP integration)
- Total built-in tools: **138** (125 + 7 LoRa + 6 I2S)

## [0.19.0] - 2026-02-18

### Added
- **Camera Tool** (`tools/MCPCameraTool.h`) — ESP32-CAM image capture via MCP:
  - `camera_init` — initialize camera with configurable resolution (QQVGA to UXGA), quality, and pin mapping
  - `camera_capture` — take a JPEG photo, returns base64-encoded image data with optional flash control
  - `camera_status` — full sensor info (brightness, contrast, saturation, gain, exposure, white balance, etc.)
  - `camera_configure` — adjust sensor settings: resolution, quality, brightness, contrast, saturation, sharpness, mirror, flip, special effects
  - `camera_flash` — control onboard LED flash with on/off toggle or PWM brightness (0-255)
  - Supports AI-Thinker ESP32-CAM, ESP32-S3-EYE, XIAO ESP32S3 Sense (configurable pins)
  - Built-in base64 encoder for image data
  - `addCameraTools(server, pins)` — single-call registration
- **ESP-NOW Tool** (`tools/MCPESPNowTool.h`) — peer-to-peer mesh communication:
  - `espnow_init` — initialize ESP-NOW subsystem with optional WiFi channel selection
  - `espnow_add_peer` — register a peer by MAC address with optional encryption
  - `espnow_send` — send up to 250 bytes to a specific peer
  - `espnow_receive` — read received messages from ring buffer (32-message capacity)
  - `espnow_peers` — list peers with delivery statistics (sent/delivered/failed/rate)
  - `espnow_broadcast` — send to all peers via broadcast address (FF:FF:FF:FF:FF:FF)
  - ISR-safe ring buffer for received messages
  - Delivery callback tracking per peer
  - `addESPNowTools(server)` — single-call registration
- **Wireless Camera Example** (`examples/wireless_camera/`) — demonstrates:
  - ESP32-CAM as MCP server with full camera control
  - ESP-NOW mesh for coordination with other MCUs
  - AI-guided security monitoring prompt
  - Camera status resource
- 20 new unit tests:
  - Camera tools: base64 encoding (4), non-ESP32 fallback (5), registration (1)
  - ESP-NOW tools: MAC parsing/formatting (3), non-ESP32 fallback (6), registration (1)
- Build system: `test_tools` now included in native test suite (`test/native/Makefile`)

### Changed
- Bumped version to 0.19.0
- Total test count: **285** (222 JSON-RPC + 48 tool tests + 15 HTTP integration)
- Total built-in tools: **125** (114 + 5 Camera + 6 ESP-NOW)

## [0.18.0] - 2026-02-18

### Added
- **SD Card Tool** (`tools/MCPSDCardTool.h`) — file storage on SD/microSD cards:
  - `sd_mount` — initialize SD card with configurable CS pin
  - `sd_info` — card type, capacity, usage statistics, read/write counters
  - `sd_list` — list files and directories at a given path
  - `sd_read` — read file contents with offset and length support (max 4096 bytes)
  - `sd_write` — write/overwrite file content (destructive)
  - `sd_append` — append to file, creates if not exists
  - `sd_delete` — delete a file (destructive)
  - Emulated filesystem for testing on non-SD platforms
  - `addSDCardTools(server, csPin)` — single-call registration
- **Battery Monitor Tool** (`tools/MCPBatteryTool.h`) — battery voltage and charge monitoring:
  - `battery_read` — current voltage, percentage, charge level (good/moderate/low/critical), charging state
  - `battery_status` — detailed status with voltage trend (rising/stable/falling), estimated runtime, configuration
  - `battery_calibrate` — update voltage mapping: vFull, vEmpty, divider ratio, vRef, chemistry, charging pin
  - `battery_history` — recent reading history for trend analysis (up to 100 readings)
  - 16-sample ADC averaging on ESP32 for stable readings
  - Non-linear percentage mapping, configurable for LiPo/LiFePO4/NiMH/custom
  - `addBatteryTools(server, adcPin, dividerRatio)` — single-call registration
- **Portable Data Logger Example** (`examples/portable_logger/`) — demonstrates:
  - SD card + battery + DHT sensor integration for portable environmental logging
  - Custom resource for live logger status
  - AI-driven data logging prompt with power management awareness
- **MCPTool::annotate()** — builder-style method for setting tool annotations inline
- **MCPToolAnnotations builder methods** — `setReadOnlyHint()`, `setDestructiveHint()`, `setIdempotentHint()`, `setOpenWorldHint()` for fluent annotation construction
- **Arduino mock improvements** — `String(float, decimals)`, `String::replace()`, `operator+(unsigned long)` for better host-side test coverage
- 12 new unit tests:
  - SD Card tools: mount, write+read, append, read-not-mounted, delete, delete-nonexistent, list, info (8)
  - Battery tools: read, status, calibrate, history (4)

### Changed
- Bumped version to 0.18.0
- Total test count: **250** (222 JSON-RPC + 28 tool tests)
- Total built-in tools: **114** (103 + 7 SD Card + 4 Battery)

## [0.17.0] - 2026-02-18

### Added
- **Interrupt Monitor Tool** (`tools/MCPInterruptTool.h`) — GPIO edge-event detection:
  - `interrupt_attach` — configure a pin for interrupt monitoring (rising/falling/change edges, optional pull-up)
  - `interrupt_read` — read event count, rate (Hz), and last trigger time with optional counter reset
  - `interrupt_detach` — stop monitoring a pin and get final count
  - `interrupt_list` — list all pins with active interrupt monitoring
  - ISR-safe counting with `IRAM_ATTR`, supports up to 8 simultaneous pins
  - `addInterruptTools(server)` — single-call registration
- **Analog Watchdog Tool** (`tools/MCPAnalogWatchTool.h`) — threshold monitoring:
  - `analog_watch_set` — configure low/high thresholds on ADC pins with optional labels
  - `analog_watch_status` — check all watches with current readings, trigger state, alert count
  - `analog_watch_clear` — remove a watch from a pin
  - `updateAnalogWatches()` — call in loop() for 100ms polling (non-blocking)
  - Supports up to 8 simultaneous watches
  - `addAnalogWatchTools(server)` — single-call registration
- **New test suite** `test/test_tools.cpp` — 16 dedicated tests for tool registration, resources, prompts, rich tools, error handling, and batch operations

### Fixed
- Fixed deprecated `containsKey()` usage in tests — migrated to `obj[key].is<T>()`
- Fixed `as<String>()` compilation error with ArduinoJson v7 — use `as<const char*>()` instead
- Fixed unused variable warning in `MCPResourceTemplate.h`
- Fixed version string assertions in tests (were checking 0.15.0 instead of current version)

### Changed
- Total test count: **238** (222 JSON-RPC + 16 tool tests)
- Total built-in tools: **103** (was 99)

## [0.16.0] - 2026-02-18

### Added
- **NVS (Non-Volatile Storage) Tool** (`tools/MCPNVSTool.h`) — persistent key-value storage:
  - `nvs_set` — store string, int, float, or boolean values with auto type detection, key length validation (max 15 chars for ESP32 NVS)
  - `nvs_get` — retrieve value by key with type-aware formatting
  - `nvs_delete` — remove a key from persistent storage
  - `nvs_list` — list all stored entries with types and values
  - `nvs_status` — storage statistics (used/free entries, total operations, platform info)
  - Uses ESP32 Preferences library natively, emulated store on other platforms
  - `addNVSTools(server, namespace)` — single-call registration
- **GPS Tool** (`tools/MCPGPSTool.h`) — NMEA GPS module support (NEO-6M, BN-220, etc.):
  - `gps_read` — current position (lat/lon/alt), UTC time, satellite count, HDOP, fix age
  - `gps_satellites` — satellite count, fix quality assessment (excellent/good/moderate/fair/poor)
  - `gps_speed` — speed in km/h, m/s, mph with course heading and cardinal direction
  - `gps_distance` — haversine distance to target coordinates or saved waypoint, with persistent waypoint support
  - `gps_status` — module status, fix info, statistics, serial connection state
  - NMEA GGA + RMC sentence parsing with temperature-compensated coordinate conversion
  - `addGPSTools(server, serial, baud)` — single-call registration
- **Relay Tool** (`tools/MCPRelayTool.h`) — multi-channel relay control with safety features:
  - `relay_set` — turn relay ON/OFF by channel index or label
  - `relay_get` — read current state of a relay channel
  - `relay_toggle` — toggle relay state (ON→OFF or OFF→ON)
  - `relay_pulse` — briefly activate relay for 50-30000ms then auto-OFF
  - `relay_all_off` — emergency all-off for all channels
  - `relay_status` — overview of all channels with on-time tracking and switch counts
  - Interlock groups for mutually exclusive relays (e.g., heater + cooler)
  - Maximum-on timers for safety auto-off
  - Active-low/high configuration per channel
  - `addRelayChannel()` + `addRelayTools(server)` — flexible registration
- **Asset Tracker Example** (`examples/asset_tracker/`) — demonstrates:
  - GPS + NVS + Relay integration for IoT asset tracking
  - Switchable GPS power via relay for battery management
  - Persistent waypoint and settings storage
  - Custom resource for last known position
  - AI-driven tracking and power management prompt
- 22 new unit tests:
  - NVS tools: set string, set integer, reject long key, get existing, get missing, delete, list, status (8)
  - GPS tools: read with fix, read no fix, satellites, speed, distance, status (6)
  - Relay tools: set on, set by label, toggle, pulse, reject invalid duration, all off, status, interlock (8)

### Changed
- Bumped version to 0.16.0
- Total tests: 202 → 222 unit tests + 15 HTTP integration tests = 237 total
- Built-in tools now total 99 (83 + 5 NVS + 5 GPS + 6 Relay)
- README comparison table updated to reflect 99 built-in tools

## [0.15.0] - 2026-02-18

### Added
- **LCD Display Tool** (`tools/MCPLCDTool.h`) — I2C LCD control (HD44780 + PCF8574 backpack):
  - `lcd_print` — print text at row/col position, optional clear-before-print, tracks display buffer
  - `lcd_clear` — clear display and internal buffer
  - `lcd_setCursor` — move cursor to specific position with bounds validation
  - `lcd_backlight` — toggle backlight on/off
  - `lcd_createChar` — define custom 5x8 characters in 8 CGRAM slots (0-7)
  - `lcd_status` — read display config, backlight state, and current buffer content
  - Supports 16x2 and 20x4 displays
  - `addLCDTools(server, address, cols, rows)` — single-call registration
- **IR Remote Tool** (`tools/MCPIRTool.h`) — infrared remote control:
  - `ir_send` — send IR code using standard protocols (NEC, Sony, RC5, Samsung, LG) with configurable bit length and repeat count
  - `ir_send_raw` — send raw mark/space timing sequences with configurable carrier frequency (30-56kHz), max 256 timings
  - `ir_status` — transmitter status, pin config, total codes sent, last protocol/code
  - `addIRTools(server, sendPin, recvPin)` — single-call registration
- **RTC Tool** (`tools/MCPRTCTool.h`) — DS3231/DS1307 real-time clock:
  - `rtc_get` — read date/time in ISO or component format, includes day-of-week
  - `rtc_set` — set date/time with full validation (leap years, month lengths), auto day-of-week calculation via Zeller's congruence
  - `rtc_alarm` — set/clear/check two independent alarms (DS3231 alarm1 supports seconds)
  - `rtc_temperature` — read DS3231 built-in temperature sensor (°C and °F)
  - `rtc_status` — chip type, I2C address, current time, alarm states, temperature
  - `addRTCTools(server, address, chipType)` — single-call registration
- **Smart Display Example** (`examples/smart_display/`) — demonstrates:
  - LCD + RTC + IR + DHT integration on shared I2C bus
  - AI-driven room control: display messages, read clock, send IR commands, monitor environment
  - Custom prompt for natural language room control
- 14 new unit tests:
  - LCD tools: print, clear, backlight toggle, createChar, status (5)
  - IR tools: send NEC code, send raw timings, status (3)
  - RTC tools: get datetime, set datetime, reject invalid date, alarm set/check, temperature (5)
  - Additional pulse counter config test (1)

### Changed
- Bumped version to 0.15.0
- Total tests: 188 → 202 unit tests + 15 HTTP integration tests = 217 total
- Built-in tools now total 83 (69 + 6 LCD + 3 IR + 5 RTC)
- README comparison table updated to reflect 83 built-in tools

## [0.14.0] - 2026-02-18

### Added
- **DAC (Digital-to-Analog Converter) Tool** (`tools/MCPDACTool.h`) — true analog output on ESP32:
  - `dac_write` — write raw 8-bit value (0-255) to DAC pin (GPIO 25/26)
  - `dac_write_voltage` — write desired voltage (0-3.3V) with automatic conversion and resolution step reporting
  - `dac_status` — read current state of both DAC channels (value, voltage, enabled)
  - `addDACTools(server)` — single-call registration
- **Ultrasonic Distance Sensor Tool** (`tools/MCPUltrasonicTool.h`) — HC-SR04/JSN-SR04T support:
  - `distance_read` — single reading with multi-sample averaging, outlier filtering, temperature-compensated speed of sound; returns cm, inches, and meters
  - `distance_read_multi` — read all registered sensors at once
  - `distance_config` — update temperature compensation and max range at runtime
  - Supports up to 4 sensors, configurable labels for human-readable identification
  - `addUltrasonicTools(server, trigPin, echoPin, maxDistanceCm, label)` — single-call registration
- **Buzzer/Tone Tool** (`tools/MCPBuzzerTool.h`) — piezo buzzer with melodies:
  - `buzzer_tone` — play tone at specific frequency (20-20kHz) and duration, with duty cycle control
  - `buzzer_melody` — play predefined melodies (alert, success, error, startup, doorbell, siren) or custom note sequences (max 32 notes) with tempo control
  - `buzzer_stop` — immediate stop with active state reporting
  - Uses LEDC PWM on ESP32 for precise tone generation
  - `addBuzzerTools(server, pin, ledcChannel)` — single-call registration
- **Smart Parking Example** (`examples/smart_parking/`) — demonstrates:
  - 2x ultrasonic sensors for parking bay occupancy detection
  - NeoPixel LED status indicators
  - Buzzer proximity warnings with predefined melodies
  - DAC output for analog occupancy gauge
  - Custom resources for real-time parking status and per-bay monitoring
  - AI-driven parking analysis prompt
- 13 new unit tests:
  - DAC tools: write, invalid pin rejection, voltage conversion, status (4)
  - Ultrasonic tools: read measurement, read multi, config, no-echo error (4)
  - Buzzer tools: tone playback, melody, stop, invalid frequency rejection (4)
  - Additional pulse counter config test (1)

### Changed
- Bumped version to 0.14.0
- Total tests: 175 → 188 unit tests + 15 HTTP integration tests = 203 total
- Built-in tools now total 69 (60 + 3 DAC + 3 ultrasonic + 3 buzzer)
- README comparison table updated to reflect 69 built-in tools

## [0.13.0] - 2026-02-18

### Added
- **Stepper Motor Tool** (`tools/MCPStepperTool.h`) — DIR/STEP stepper motor control with acceleration profiles:
  - `stepper_move` — move to absolute position or relative steps, non-blocking with trapezoidal acceleration
  - `stepper_status` — read position, speed, running state, endstop status
  - `stepper_config` — set max speed, acceleration, microstepping (1-32), direction inversion, enable/disable
  - `stepper_home` — homing sequence toward endstop at reduced speed, auto-zero on trigger
  - `stepper_stop` — emergency stop (instant) or decelerated stop
  - `stepperLoop()` — non-blocking motion update, call from `loop()`
  - Supports up to 4 steppers, compatible with A4988/DRV8825/TMC2208 drivers
  - `addStepperTools(server, stepPin, dirPin, enablePin, endstopPin)` — single-call registration
- **Capacitive Touch Sensor Tool** (`tools/MCPTouchTool.h`) — ESP32 touch pin support:
  - `touch_read` — read raw touch value, threshold, baseline, touch strength percentage
  - `touch_read_all` — read all registered touch pads at once with touched count
  - `touch_calibrate` — auto-calibrate baselines and thresholds with configurable sensitivity and sample count
  - Supports all 10 ESP32 touch pins (T0-T9), automatic GPIO-to-touch mapping
  - `addTouchTools(server, gpios, count, labels)` — registration with optional human-readable labels
- **Pulse Counter Tool** (`tools/MCPPulseCounterTool.h`) — hardware interrupt-driven pulse counting:
  - `pulse_read` — read count, instantaneous frequency (Hz), RPM, scaled unit value, average frequency
  - `pulse_reset` — reset counter to zero with previous count returned
  - `pulse_config` — configure scaling (pulses/unit), unit name, glitch filter, edge mode (rising/falling/both)
  - IRAM_ATTR ISR handlers with configurable glitch filtering
  - Ideal for flow meters, tachometers, anemometers, event counting
  - `addPulseCounterTools(server, pin, pulsesPerUnit, unitName, pulsesPerRev)` — single-call registration
- **CNC Controller Example** (`examples/cnc_controller/`) — demonstrates:
  - 3-axis stepper motor control with homing sequences
  - Spindle RPM monitoring via pulse counter
  - Capacitive touch probes for tool length and surface detection
  - Machine position resource for real-time XYZ readout
  - AI-driven machining prompt for status analysis
- 11 new unit tests:
  - Stepper tools: status, move validation, config, emergency stop, home without endstop (5)
  - Touch tools: read, read all, calibrate (3)
  - Pulse counter tools: read with frequency/RPM, reset, config with scaling (3)

### Changed
- Bumped version to 0.13.0
- Total tests: 164 → 175 unit tests + 15 HTTP integration tests = 190 total
- Built-in tools now total 60 (49 + 5 stepper + 3 touch + 3 pulse counter)
- README comparison table updated to reflect 60 built-in tools

## [0.12.0] - 2026-02-18

### Added
- **CAN Bus Tool** (`tools/MCPCANTool.h`) — Controller Area Network support for ESP32 via TWAI driver:
  - `can_init` — initialize CAN bus with configurable bitrate (125K/250K/500K/1M) and mode (normal/listen-only/no-ack)
  - `can_send` — send CAN frames with standard (11-bit) or extended (29-bit) IDs, RTR support
  - `can_receive` — read pending frames from receive buffer with configurable timeout and max count
  - `can_filter` — set acceptance filter for selective frame reception
  - `can_status` — get bus status, error counters, bus-off state, message queue depths
  - Proper MCP tool annotations (readOnly, destructive, title)
  - `addCANTools(server, txPin, rxPin)` — single-call registration
- **Rotary Encoder Tool** (`tools/MCPEncoderTool.h`) — hardware interrupt-driven encoder input:
  - `encoder_read` — read position, idle time, button state, optional revolution/degree tracking
  - `encoder_reset` — reset position to zero or specified value
  - `encoder_config` — configure steps per revolution, min/max position limits
  - Supports up to 4 simultaneous encoders via ISR multiplexing
  - IRAM_ATTR ISR handlers for reliable counting at high speeds
  - `addEncoderTools(server, pinA, pinB, pinButton)` — registration with auto-indexing
- **Server Diagnostics Tool** (`MCPDiagnostics.h`) — comprehensive server self-inspection:
  - `server_diagnostics` — version info, uptime, memory usage, network status, rate limiter stats, session summary
  - Optional detailed listing of registered tools and resources
  - Low-memory warning integration with HeapMonitor
  - `addDiagnosticsTool(server)` — single-call registration
- **Industrial CAN Bus Example** (`examples/industrial_canbus/`) — demonstrates:
  - CAN bus monitoring with traffic logging ring buffer
  - Rotary encoder for physical parameter adjustment
  - Server diagnostics for remote health monitoring
  - AI-driven CAN traffic analysis prompt
  - Multi-operator session management
- 15 new unit tests:
  - CAN bus tools: status, send validation, receive empty (3)
  - Encoder tools: read, reset, config (3)
  - Diagnostics tool: basic output, version info (2)
  - Batch JSON-RPC: all-notifications, mixed requests (2)
  - Error handling: missing tool name, nonexistent tool, invalid version, method not found, tools_call missing name (5)

### Changed
- Bumped version to 0.12.0
- Total tests: 149 → 164 unit tests + 15 HTTP integration tests = 179 total
- `mcpd.h` now includes `MCPDiagnostics.h`
- Built-in tools now total 49 (40 + 5 CAN + 3 encoder + 1 diagnostics)
- README comparison table updated to reflect 49 built-in tools

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
