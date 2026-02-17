/**
 * mcpd — Interactive Serial Setup CLI
 *
 * Provides a serial-based setup menu for first-boot configuration.
 * Users can configure WiFi credentials, server name, API key, etc.
 * via the Arduino Serial Monitor.
 *
 * Usage:
 *   mcpd::SetupCLI cli;
 *   if (cli.shouldRun()) {  // no config or user pressed Enter during boot
 *       cli.run();          // blocks until setup complete
 *   }
 */

#ifndef MCPD_SETUP_CLI_H
#define MCPD_SETUP_CLI_H

#include <Arduino.h>
#include "MCPConfig.h"

namespace mcpd {

class SetupCLI {
public:
    static constexpr unsigned long BOOT_WAIT_MS = 3000;

    explicit SetupCLI(Config& config) : _config(config) {}

    /**
     * Check if setup should run:
     * - No WiFi credentials stored, OR
     * - User presses Enter within BOOT_WAIT_MS
     */
    bool shouldRun() {
        if (!_config.hasWiFiCredentials()) {
            Serial.println("\n[mcpd] No WiFi config found. Starting setup...");
            return true;
        }

        Serial.println("\n[mcpd] Press ENTER within 3s to enter setup...");
        unsigned long start = millis();
        while (millis() - start < BOOT_WAIT_MS) {
            if (Serial.available()) {
                char c = Serial.read();
                if (c == '\n' || c == '\r') {
                    return true;
                }
            }
            delay(50);
        }
        return false;
    }

    /**
     * Run the interactive setup menu. Blocks until complete.
     */
    void run() {
        _printBanner();

        bool running = true;
        while (running) {
            _printMenu();
            String choice = _readLine("Choice: ");

            if (choice == "1")      _setupWiFi();
            else if (choice == "2") _setupServerName();
            else if (choice == "3") _setupAPIKey();
            else if (choice == "4") _setupEndpoint();
            else if (choice == "5") _showConfig();
            else if (choice == "6") _clearConfig();
            else if (choice == "7") {
                _config.save();
                Serial.println("\n  ✓ Config saved! Starting mcpd...\n");
                running = false;
            }
            else if (choice == "0") {
                Serial.println("\n  Exiting setup without saving.\n");
                running = false;
            }
            else {
                Serial.println("  Invalid choice.");
            }
        }
    }

private:
    Config& _config;

    void _printBanner() {
        Serial.println();
        Serial.println("  ╔══════════════════════════════════╗");
        Serial.println("  ║     ⚡ mcpd Setup Wizard ⚡     ║");
        Serial.println("  ║  MCP Server for Microcontrollers ║");
        Serial.println("  ╚══════════════════════════════════╝");
        Serial.println();
    }

    void _printMenu() {
        Serial.println("  ┌─────────────────────────────┐");
        Serial.println("  │  1. WiFi Credentials        │");
        Serial.println("  │  2. Server Name              │");
        Serial.println("  │  3. API Key                  │");
        Serial.println("  │  4. MCP Endpoint             │");
        Serial.println("  │  5. Show Current Config      │");
        Serial.println("  │  6. Clear All Config         │");
        Serial.println("  │  7. Save & Start             │");
        Serial.println("  │  0. Exit (no save)           │");
        Serial.println("  └─────────────────────────────┘");
    }

    void _setupWiFi() {
        Serial.println("\n  -- WiFi Setup --");
        String ssid = _readLine("  SSID: ");
        String pass = _readLine("  Password: ");

        if (ssid.length() > 0) {
            _config.data().wifiSSID = ssid;
            _config.data().wifiPassword = pass;
            Serial.println("  ✓ WiFi credentials set.");
        } else {
            Serial.println("  ✗ SSID cannot be empty.");
        }
    }

    void _setupServerName() {
        Serial.println("\n  -- Server Name --");
        Serial.println("  (Used for mDNS: <name>.local)");
        String name = _readLine("  Name [" + _config.data().serverName + "]: ");
        if (name.length() > 0) {
            _config.data().serverName = name;
            Serial.println("  ✓ Server name set to: " + name);
        }
    }

    void _setupAPIKey() {
        Serial.println("\n  -- API Key --");
        Serial.println("  (Leave empty to disable authentication)");
        String key = _readLine("  API Key: ");
        _config.data().apiKey = key;
        if (key.length() > 0) {
            Serial.println("  ✓ API key set.");
        } else {
            Serial.println("  ✓ Authentication disabled.");
        }
    }

    void _setupEndpoint() {
        Serial.println("\n  -- MCP Endpoint --");
        String ep = _readLine("  Endpoint [" + _config.data().mcpEndpoint + "]: ");
        if (ep.length() > 0) {
            if (!ep.startsWith("/")) ep = "/" + ep;
            _config.data().mcpEndpoint = ep;
            Serial.println("  ✓ Endpoint set to: " + ep);
        }
    }

    void _showConfig() {
        Serial.println("\n  -- Current Configuration --");
        Serial.println("  WiFi SSID:    " + (_config.data().wifiSSID.length() > 0 ? _config.data().wifiSSID : "(not set)"));
        Serial.println("  WiFi Pass:    " + String(_config.data().wifiPassword.length() > 0 ? "****" : "(not set)"));
        Serial.println("  Server Name:  " + _config.data().serverName);
        Serial.println("  Server Port:  " + String(_config.data().serverPort));
        Serial.println("  API Key:      " + String(_config.data().apiKey.length() > 0 ? "****" : "(disabled)"));
        Serial.println("  MCP Endpoint: " + _config.data().mcpEndpoint);
        Serial.println();
    }

    void _clearConfig() {
        String confirm = _readLine("  Are you sure? (y/N): ");
        if (confirm == "y" || confirm == "Y") {
            _config.clear();
            Serial.println("  ✓ Config cleared.");
        }
    }

    /**
     * Read a line from Serial with echo.
     */
    String _readLine(const String& prompt) {
        Serial.print(prompt);
        String line = "";
        while (true) {
            if (Serial.available()) {
                char c = Serial.read();
                if (c == '\n' || c == '\r') {
                    Serial.println();
                    return line;
                } else if (c == 0x08 || c == 0x7F) { // Backspace
                    if (line.length() > 0) {
                        line.remove(line.length() - 1);
                        Serial.print("\b \b");
                    }
                } else if (c >= 32) {
                    line += c;
                    Serial.print(c);
                }
            }
            delay(10);
        }
    }
};

} // namespace mcpd

#endif // MCPD_SETUP_CLI_H
