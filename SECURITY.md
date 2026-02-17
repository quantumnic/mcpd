# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 0.1.x   | ✅ Yes             |

## Reporting a Vulnerability

If you discover a security vulnerability in mcpd, please report it responsibly.

**Do NOT open a public GitHub issue for security vulnerabilities.**

Instead, please email: **redbasecap-buiss@users.noreply.github.com**

Include:
- Description of the vulnerability
- Steps to reproduce
- Potential impact
- Suggested fix (if any)

We will acknowledge receipt within 48 hours and aim to provide a fix within 7 days for critical issues.

## Security Considerations for mcpd

Since mcpd runs on microcontrollers exposing network services, please consider:

1. **Always use API key authentication** in production (`MCPAuth`)
2. **Use HTTPS** where possible (via reverse proxy, since MCUs typically don't support TLS natively)
3. **Limit network exposure** — don't expose mcpd directly to the internet without a firewall
4. **Keep firmware updated** — use the OTA feature to deploy security patches
5. **Change default credentials** — always set a unique server name and API key

## Scope

This policy applies to the mcpd library code. Security issues in dependencies (ArduinoJson, ESP32 Arduino core, etc.) should be reported to their respective maintainers.
