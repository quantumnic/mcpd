#!/usr/bin/env python3
"""
mcpd-bridge — stdio ↔ Streamable HTTP bridge for mcpd

Translates MCP stdio transport (used by Claude Desktop) to Streamable HTTP
transport (used by mcpd on the microcontroller).

Features:
  - Auto-reconnect on connection loss
  - Configurable retry with exponential backoff
  - Structured logging with levels
  - mDNS discovery (optional, requires zeroconf)
  - Session management with automatic re-initialization

Usage:
    python3 mcpd_bridge.py --host <hostname-or-ip> [--port 80] [--path /mcp]

Claude Desktop config (claude_desktop_config.json):
    {
        "mcpServers": {
            "my-device": {
                "command": "python3",
                "args": ["/path/to/mcpd_bridge.py", "--host", "my-device.local"]
            }
        }
    }
"""

import sys
import json
import argparse
import urllib.request
import urllib.error
import logging
import time
import os

# ── Logging Setup ───────────────────────────────────────────────────────

LOG_LEVEL = os.environ.get("MCPD_LOG_LEVEL", "INFO").upper()

logging.basicConfig(
    level=getattr(logging, LOG_LEVEL, logging.INFO),
    format="[mcpd-bridge] %(asctime)s %(levelname)s %(message)s",
    datefmt="%H:%M:%S",
    stream=sys.stderr,
)
log = logging.getLogger("mcpd-bridge")


# ── Retry Configuration ────────────────────────────────────────────────

MAX_RETRIES = int(os.environ.get("MCPD_MAX_RETRIES", "3"))
RETRY_BASE_DELAY = float(os.environ.get("MCPD_RETRY_DELAY", "1.0"))
REQUEST_TIMEOUT = int(os.environ.get("MCPD_TIMEOUT", "30"))


# ── HTTP Transport ──────────────────────────────────────────────────────

def send_request(base_url: str, body: str, session_id: str | None,
                 timeout: int = REQUEST_TIMEOUT) -> tuple[int, str, str | None]:
    """
    Send an HTTP POST to the MCU.

    Returns:
        (status_code, response_body, session_id)

    Raises:
        urllib.error.URLError on connection failure
        urllib.error.HTTPError on HTTP errors
    """
    headers = {
        "Content-Type": "application/json",
        "Accept": "application/json, text/event-stream",
    }
    if session_id:
        headers["Mcp-Session-Id"] = session_id

    req = urllib.request.Request(
        base_url,
        data=body.encode("utf-8"),
        headers=headers,
        method="POST",
    )

    with urllib.request.urlopen(req, timeout=timeout) as resp:
        resp_session = resp.headers.get("Mcp-Session-Id")
        new_session = resp_session if resp_session else session_id
        status = resp.status

        if status == 202:
            return status, "", new_session

        response_body = resp.read().decode("utf-8")
        return status, response_body, new_session


def send_with_retry(base_url: str, body: str, session_id: str | None,
                    msg: dict) -> tuple[str | None, str | None]:
    """
    Send request with retry logic and exponential backoff.

    Returns:
        (response_body_or_None, session_id)
    """
    last_error = None

    for attempt in range(MAX_RETRIES + 1):
        try:
            status, resp_body, new_session = send_request(
                base_url, body, session_id
            )

            if status == 202:
                log.debug("← MCU: 202 Accepted")
                return None, new_session

            log.debug("← MCU: %s", resp_body[:200])
            return resp_body, new_session

        except urllib.error.HTTPError as e:
            error_body = e.read().decode("utf-8", errors="replace")
            log.error("HTTP %d: %s", e.code, error_body)

            # 404 = session expired, clear and retry with re-init
            if e.code == 404:
                log.warning("Session expired, clearing session ID")
                session_id = None
                # Don't retry 404 — let the client re-initialize
                return _make_error_response(msg, -32000,
                                            f"Session expired (HTTP 404)"), None

            # 4xx errors are not retryable
            if 400 <= e.code < 500:
                return _make_error_response(msg, -32000,
                                            f"HTTP {e.code}: {error_body}"), session_id

            last_error = f"HTTP {e.code}: {error_body}"

        except urllib.error.URLError as e:
            log.error("Connection error: %s", e.reason)
            last_error = f"Connection error: {e.reason}"

        except Exception as e:
            log.error("Unexpected error: %s", e)
            last_error = f"Unexpected error: {e}"

        # Retry with exponential backoff
        if attempt < MAX_RETRIES:
            delay = RETRY_BASE_DELAY * (2 ** attempt)
            log.info("Retrying in %.1fs (attempt %d/%d)...",
                     delay, attempt + 1, MAX_RETRIES)
            time.sleep(delay)

    # All retries exhausted
    log.error("All %d retries exhausted. Last error: %s",
              MAX_RETRIES + 1, last_error)
    return _make_error_response(msg, -32000,
                                f"Connection failed after {MAX_RETRIES + 1} attempts: {last_error}"), session_id


def _make_error_response(msg: dict, code: int, message: str) -> str:
    """Create a JSON-RPC error response string."""
    return json.dumps({
        "jsonrpc": "2.0",
        "id": msg.get("id"),
        "error": {"code": code, "message": message},
    })


# ── mDNS Discovery ─────────────────────────────────────────────────────

def discover_mcu(timeout_s: float = 5.0) -> tuple[str | None, int | None]:
    """Discover mcpd server via mDNS (requires zeroconf)."""
    try:
        from zeroconf import ServiceBrowser, Zeroconf
    except ImportError:
        log.error("zeroconf not installed. Run: pip install zeroconf")
        return None, None

    result = {"host": None, "port": None}

    class Listener:
        def add_service(self, zc, type_, name):
            info = zc.get_service_info(type_, name)
            if info and info.addresses:
                from ipaddress import ip_address
                addr = ip_address(info.addresses[0])
                result["host"] = str(addr)
                result["port"] = info.port
                log.info("Discovered: %s at %s:%d", name, addr, info.port)

        def remove_service(self, zc, type_, name):
            pass

        def update_service(self, zc, type_, name):
            pass

    zc = Zeroconf()
    try:
        browser = ServiceBrowser(zc, "_mcp._tcp.local.", Listener())
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            if result["host"]:
                break
            time.sleep(0.1)
    finally:
        zc.close()

    if not result["host"]:
        log.warning("mDNS discovery timed out after %.1fs", timeout_s)

    return result["host"], result["port"]


# ── Main ────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="mcpd stdio↔HTTP bridge",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Environment variables:
  MCPD_LOG_LEVEL    Log level (DEBUG, INFO, WARNING, ERROR) [default: INFO]
  MCPD_MAX_RETRIES  Max retry attempts on failure [default: 3]
  MCPD_RETRY_DELAY  Base retry delay in seconds [default: 1.0]
  MCPD_TIMEOUT      HTTP request timeout in seconds [default: 30]
""",
    )
    parser.add_argument("--host", help="MCU hostname or IP (e.g. my-device.local)")
    parser.add_argument("--port", type=int, default=80,
                        help="HTTP port (default: 80)")
    parser.add_argument("--path", default="/mcp",
                        help="MCP endpoint path (default: /mcp)")
    parser.add_argument("--discover", action="store_true",
                        help="Discover MCU via mDNS (requires zeroconf)")
    parser.add_argument("--discover-timeout", type=float, default=5.0,
                        help="mDNS discovery timeout in seconds (default: 5)")
    args = parser.parse_args()

    # Resolve host
    if args.discover:
        host, port = discover_mcu(args.discover_timeout)
        if not host:
            log.error("mDNS discovery failed — no mcpd server found")
            sys.exit(1)
        if port:
            args.port = port
    elif args.host:
        host = args.host
    else:
        parser.error("--host is required (or use --discover)")

    base_url = f"http://{host}:{args.port}{args.path}"
    session_id = None

    log.info("Bridge started → %s", base_url)
    log.info("Config: retries=%d, timeout=%ds, log=%s",
             MAX_RETRIES, REQUEST_TIMEOUT, LOG_LEVEL)

    # Read JSON-RPC messages from stdin, forward to MCU via HTTP POST
    try:
        for line in sys.stdin:
            line = line.strip()
            if not line:
                continue

            try:
                msg = json.loads(line)
            except json.JSONDecodeError as e:
                log.error("Invalid JSON from stdin: %s", e)
                continue

            log.debug("→ MCU: %s", line[:200])

            resp_body, session_id = send_with_retry(
                base_url, line, session_id, msg
            )

            if resp_body is not None:
                sys.stdout.write(resp_body + "\n")
                sys.stdout.flush()

    except KeyboardInterrupt:
        log.info("Interrupted, shutting down")
    except BrokenPipeError:
        log.info("Pipe broken (client disconnected)")
    except Exception as e:
        log.error("Fatal error: %s", e, exc_info=True)
        sys.exit(1)

    log.info("Bridge exiting")


if __name__ == "__main__":
    main()
