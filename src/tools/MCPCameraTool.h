/**
 * mcpd — ESP32-CAM Camera Tool
 *
 * Captures images via ESP32 camera module and returns them as base64-encoded
 * image content via MCP. Supports resolution, quality, and flash control.
 *
 * Requires ESP32 with camera (ESP32-CAM, ESP32-S3-EYE, XIAO ESP32S3 Sense, etc.)
 * and the esp_camera library.
 *
 * Tools registered:
 *   - camera_init      — initialize camera with pin config and resolution
 *   - camera_capture   — take a photo, returns base64 JPEG image content
 *   - camera_status    — camera sensor info (resolution, gain, exposure, etc.)
 *   - camera_configure — adjust settings (brightness, contrast, saturation, etc.)
 *   - camera_flash     — control the onboard LED flash
 *
 * MIT License
 */

#ifndef MCPD_CAMERA_TOOL_H
#define MCPD_CAMERA_TOOL_H

#include "../MCPTool.h"

#ifdef ESP32
#include "esp_camera.h"
#endif

namespace mcpd {

// Common ESP32-CAM (AI-Thinker) pin definitions
struct CameraPins {
    int pwdn   = 32;
    int reset  = -1;
    int xclk   = 0;
    int siod   = 26;
    int sioc   = 27;
    int y9     = 35;
    int y8     = 34;
    int y7     = 39;
    int y6     = 36;
    int y5     = 21;
    int y4     = 19;
    int y3     = 18;
    int y2     = 5;
    int vsync  = 25;
    int href   = 23;
    int pclk   = 22;
    int flash  = 4;   // Flash LED pin
};

namespace {
    bool _cameraInitialized = false;
    CameraPins _cameraPins;
    int _flashPin = 4;
}

/**
 * Base64 encoding for image data.
 */
inline String _base64Encode(const uint8_t* data, size_t len) {
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    String out;
    out.reserve((len + 2) / 3 * 4);

    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = ((uint32_t)data[i]) << 16;
        if (i + 1 < len) n |= ((uint32_t)data[i + 1]) << 8;
        if (i + 2 < len) n |= data[i + 2];

        out += b64[(n >> 18) & 0x3F];
        out += b64[(n >> 12) & 0x3F];
        out += (i + 1 < len) ? b64[(n >> 6) & 0x3F] : '=';
        out += (i + 2 < len) ? b64[n & 0x3F] : '=';
    }
    return out;
}

/**
 * Map resolution string to framesize enum.
 */
#ifdef ESP32
inline framesize_t _parseResolution(const char* res) {
    if (strcmp(res, "QQVGA") == 0)  return FRAMESIZE_QQVGA;   // 160x120
    if (strcmp(res, "QVGA") == 0)   return FRAMESIZE_QVGA;    // 320x240
    if (strcmp(res, "CIF") == 0)    return FRAMESIZE_CIF;      // 400x296
    if (strcmp(res, "VGA") == 0)    return FRAMESIZE_VGA;      // 640x480
    if (strcmp(res, "SVGA") == 0)   return FRAMESIZE_SVGA;     // 800x600
    if (strcmp(res, "XGA") == 0)    return FRAMESIZE_XGA;      // 1024x768
    if (strcmp(res, "SXGA") == 0)   return FRAMESIZE_SXGA;     // 1280x1024
    if (strcmp(res, "UXGA") == 0)   return FRAMESIZE_UXGA;     // 1600x1200
    return FRAMESIZE_VGA;  // default
}
#endif

/**
 * Register camera tools on the server.
 *
 * @param server  The MCP server instance
 * @param pins    Camera pin configuration (defaults to AI-Thinker ESP32-CAM)
 */
inline void addCameraTools(Server& server, CameraPins pins = CameraPins()) {
    _cameraPins = pins;
    _flashPin = pins.flash;

    // ── camera_init ──────────────────────────────────────────────────
    {
        MCPTool tool(
            "camera_init",
            "Initialize the ESP32 camera module. Call once before capturing. "
            "Supports resolutions: QQVGA (160x120), QVGA (320x240), CIF (400x296), "
            "VGA (640x480), SVGA (800x600), XGA (1024x768), SXGA (1280x1024), UXGA (1600x1200). "
            "Default: VGA, quality 12.",
            R"j({"type":"object","properties":{"resolution":{"type":"string","enum":["QQVGA","QVGA","CIF","VGA","SVGA","XGA","SXGA","UXGA"],"description":"Image resolution","default":"VGA"},"quality":{"type":"integer","minimum":4,"maximum":63,"description":"JPEG quality (4=best, 63=worst)","default":12},"xclk_freq":{"type":"integer","description":"XCLK frequency in Hz","default":20000000}}})j",
            [](const JsonObject& args) -> String {
#ifdef ESP32
                if (_cameraInitialized) {
                    return "{\"status\":\"already_initialized\",\"message\":\"Camera already initialized. Use camera_configure to change settings.\"}";
                }

                camera_config_t config;
                config.ledc_channel = LEDC_CHANNEL_0;
                config.ledc_timer = LEDC_TIMER_0;
                config.pin_d0 = _cameraPins.y2;
                config.pin_d1 = _cameraPins.y3;
                config.pin_d2 = _cameraPins.y4;
                config.pin_d3 = _cameraPins.y5;
                config.pin_d4 = _cameraPins.y6;
                config.pin_d5 = _cameraPins.y7;
                config.pin_d6 = _cameraPins.y8;
                config.pin_d7 = _cameraPins.y9;
                config.pin_xclk = _cameraPins.xclk;
                config.pin_pclk = _cameraPins.pclk;
                config.pin_vsync = _cameraPins.vsync;
                config.pin_href = _cameraPins.href;
                config.pin_sccb_sda = _cameraPins.siod;
                config.pin_sccb_scl = _cameraPins.sioc;
                config.pin_pwdn = _cameraPins.pwdn;
                config.pin_reset = _cameraPins.reset;

                int xclkFreq = args["xclk_freq"] | 20000000;
                config.xclk_freq_hz = xclkFreq;

                config.pixel_format = PIXFORMAT_JPEG;
                config.grab_mode = CAMERA_GRAB_LATEST;
                config.fb_location = CAMERA_FB_IN_PSRAM;

                const char* res = args["resolution"] | "VGA";
                config.frame_size = _parseResolution(res);

                int quality = args["quality"] | 12;
                config.jpeg_quality = quality;
                config.fb_count = 2;

                // Check for PSRAM
                if (psramFound()) {
                    config.fb_count = 2;
                    config.grab_mode = CAMERA_GRAB_LATEST;
                } else {
                    config.frame_size = FRAMESIZE_SVGA;
                    config.fb_location = CAMERA_FB_IN_DRAM;
                    config.fb_count = 1;
                }

                esp_err_t err = esp_camera_init(&config);
                if (err != ESP_OK) {
                    return String("{\"error\":\"Camera init failed\",\"code\":\"0x") +
                           String(err, HEX) + "\"}";
                }

                // Set flash pin as output
                if (_flashPin >= 0) {
                    pinMode(_flashPin, OUTPUT);
                    digitalWrite(_flashPin, LOW);
                }

                _cameraInitialized = true;
                return String("{\"status\":\"initialized\",\"resolution\":\"") + res +
                       "\",\"quality\":" + quality +
                       ",\"psram\":" + (psramFound() ? "true" : "false") + "}";
#else
                (void)args;
                return "{\"error\":\"Camera only supported on ESP32\"}";
#endif
            });
        tool.markIdempotent();
        server.addTool(tool);
    }

    // ── camera_capture ───────────────────────────────────────────────
    {
        MCPTool tool(
            "camera_capture",
            "Capture a JPEG photo from the camera. Returns base64-encoded image data. "
            "Camera must be initialized first with camera_init. "
            "Optional flash for low-light conditions.",
            R"j({"type":"object","properties":{"flash":{"type":"boolean","description":"Enable flash LED during capture","default":false},"flash_duration_ms":{"type":"integer","description":"Flash warmup time in ms before capture","default":100}}})j",
            [](const JsonObject& args) -> String {
#ifdef ESP32
                if (!_cameraInitialized) {
                    return "{\"error\":\"Camera not initialized. Call camera_init first.\"}";
                }

                bool useFlash = args["flash"] | false;
                int flashDuration = args["flash_duration_ms"] | 100;

                // Flash on
                if (useFlash && _flashPin >= 0) {
                    digitalWrite(_flashPin, HIGH);
                    delay(flashDuration);
                }

                // Capture frame
                camera_fb_t* fb = esp_camera_fb_get();

                // Flash off
                if (useFlash && _flashPin >= 0) {
                    digitalWrite(_flashPin, LOW);
                }

                if (!fb) {
                    return "{\"error\":\"Camera capture failed\"}";
                }

                // Encode as base64
                String b64 = _base64Encode(fb->buf, fb->len);

                // Build response with image content
                String result = "{\"width\":" + String(fb->width) +
                                ",\"height\":" + String(fb->height) +
                                ",\"size\":" + String(fb->len) +
                                ",\"format\":\"jpeg\"" +
                                ",\"base64\":\"" + b64 + "\"}";

                esp_camera_fb_return(fb);
                return result;
#else
                (void)args;
                return "{\"error\":\"Camera only supported on ESP32\"}";
#endif
            });
        tool.markReadOnly();
        server.addTool(tool);
    }

    // ── camera_status ────────────────────────────────────────────────
    {
        MCPTool tool(
            "camera_status",
            "Get camera sensor status: resolution, gain, exposure, white balance, etc.",
            R"j({"type":"object","properties":{}})j",
            [](const JsonObject& args) -> String {
#ifdef ESP32
                (void)args;
                if (!_cameraInitialized) {
                    return "{\"initialized\":false}";
                }

                sensor_t* s = esp_camera_sensor_get();
                if (!s) {
                    return "{\"error\":\"Failed to get sensor\"}";
                }

                JsonDocument doc;
                doc["initialized"] = true;
                doc["pid"] = s->id.PID;
                doc["framesize"] = s->status.framesize;
                doc["quality"] = s->status.quality;
                doc["brightness"] = s->status.brightness;
                doc["contrast"] = s->status.contrast;
                doc["saturation"] = s->status.saturation;
                doc["sharpness"] = s->status.sharpness;
                doc["denoise"] = s->status.denoise;
                doc["special_effect"] = s->status.special_effect;
                doc["wb_mode"] = s->status.wb_mode;
                doc["awb"] = s->status.awb;
                doc["awb_gain"] = s->status.awb_gain;
                doc["aec"] = s->status.aec;
                doc["aec2"] = s->status.aec2;
                doc["ae_level"] = s->status.ae_level;
                doc["aec_value"] = s->status.aec_value;
                doc["agc"] = s->status.agc;
                doc["agc_gain"] = s->status.agc_gain;
                doc["gainceiling"] = s->status.gainceiling;
                doc["bpc"] = s->status.bpc;
                doc["wpc"] = s->status.wpc;
                doc["raw_gma"] = s->status.raw_gma;
                doc["lenc"] = s->status.lenc;
                doc["hmirror"] = s->status.hmirror;
                doc["vflip"] = s->status.vflip;
                doc["colorbar"] = s->status.colorbar;

                String result;
                serializeJson(doc, result);
                return result;
#else
                (void)args;
                return "{\"initialized\":false,\"error\":\"Camera only supported on ESP32\"}";
#endif
            });
        tool.markReadOnly();
        server.addTool(tool);
    }

    // ── camera_configure ─────────────────────────────────────────────
    {
        MCPTool tool(
            "camera_configure",
            "Adjust camera sensor settings: brightness (-2..2), contrast (-2..2), "
            "saturation (-2..2), resolution, quality, hmirror, vflip, special effects, etc.",
            R"j({"type":"object","properties":{"resolution":{"type":"string","enum":["QQVGA","QVGA","CIF","VGA","SVGA","XGA","SXGA","UXGA"]},"quality":{"type":"integer","minimum":4,"maximum":63},"brightness":{"type":"integer","minimum":-2,"maximum":2},"contrast":{"type":"integer","minimum":-2,"maximum":2},"saturation":{"type":"integer","minimum":-2,"maximum":2},"sharpness":{"type":"integer","minimum":-2,"maximum":2},"hmirror":{"type":"boolean","description":"Horizontal mirror"},"vflip":{"type":"boolean","description":"Vertical flip"},"special_effect":{"type":"integer","minimum":0,"maximum":6,"description":"0=none,1=negative,2=grayscale,3=red,4=green,5=blue,6=sepia"}}})j",
            [](const JsonObject& args) -> String {
#ifdef ESP32
                if (!_cameraInitialized) {
                    return "{\"error\":\"Camera not initialized\"}";
                }

                sensor_t* s = esp_camera_sensor_get();
                if (!s) return "{\"error\":\"Failed to get sensor\"}";

                JsonDocument doc;
                int changes = 0;

                if (args.containsKey("resolution")) {
                    framesize_t fs = _parseResolution(args["resolution"]);
                    s->set_framesize(s, fs);
                    doc["resolution"] = args["resolution"].as<const char*>();
                    changes++;
                }
                if (args.containsKey("quality")) {
                    s->set_quality(s, args["quality"].as<int>());
                    doc["quality"] = args["quality"].as<int>();
                    changes++;
                }
                if (args.containsKey("brightness")) {
                    s->set_brightness(s, args["brightness"].as<int>());
                    doc["brightness"] = args["brightness"].as<int>();
                    changes++;
                }
                if (args.containsKey("contrast")) {
                    s->set_contrast(s, args["contrast"].as<int>());
                    doc["contrast"] = args["contrast"].as<int>();
                    changes++;
                }
                if (args.containsKey("saturation")) {
                    s->set_saturation(s, args["saturation"].as<int>());
                    doc["saturation"] = args["saturation"].as<int>();
                    changes++;
                }
                if (args.containsKey("sharpness")) {
                    s->set_sharpness(s, args["sharpness"].as<int>());
                    doc["sharpness"] = args["sharpness"].as<int>();
                    changes++;
                }
                if (args.containsKey("hmirror")) {
                    s->set_hmirror(s, args["hmirror"].as<bool>() ? 1 : 0);
                    doc["hmirror"] = args["hmirror"].as<bool>();
                    changes++;
                }
                if (args.containsKey("vflip")) {
                    s->set_vflip(s, args["vflip"].as<bool>() ? 1 : 0);
                    doc["vflip"] = args["vflip"].as<bool>();
                    changes++;
                }
                if (args.containsKey("special_effect")) {
                    s->set_special_effect(s, args["special_effect"].as<int>());
                    doc["special_effect"] = args["special_effect"].as<int>();
                    changes++;
                }

                doc["changes"] = changes;
                String result;
                serializeJson(doc, result);
                return result;
#else
                (void)args;
                return "{\"error\":\"Camera only supported on ESP32\"}";
#endif
            });
        tool.markIdempotent();
        server.addTool(tool);
    }

    // ── camera_flash ─────────────────────────────────────────────────
    {
        MCPTool tool(
            "camera_flash",
            "Control the onboard flash LED. Set brightness (0=off, 255=max) or toggle on/off.",
            R"j({"type":"object","properties":{"on":{"type":"boolean","description":"Turn flash on/off"},"brightness":{"type":"integer","minimum":0,"maximum":255,"description":"PWM brightness (0-255, ESP32 only)"}}})j",
            [](const JsonObject& args) -> String {
#ifdef ESP32
                if (_flashPin < 0) {
                    return "{\"error\":\"No flash pin configured\"}";
                }

                if (args.containsKey("brightness")) {
                    int brightness = args["brightness"].as<int>();
                    analogWrite(_flashPin, brightness);
                    return String("{\"flash_pin\":") + _flashPin +
                           ",\"brightness\":" + brightness + "}";
                }

                bool on = args["on"] | false;
                digitalWrite(_flashPin, on ? HIGH : LOW);
                return String("{\"flash_pin\":") + _flashPin +
                       ",\"on\":" + (on ? "true" : "false") + "}";
#else
                (void)args;
                return "{\"error\":\"Flash only supported on ESP32\"}";
#endif
            });
        tool.markIdempotent();
        server.addTool(tool);
    }
}

} // namespace mcpd

#endif // MCPD_CAMERA_TOOL_H
