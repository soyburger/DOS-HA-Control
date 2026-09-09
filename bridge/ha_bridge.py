#!/usr/bin/env python3
"""Bridge between a DOS 6.22 text client and Home Assistant.

Speaks the plain-text line protocol documented in docs/PROTOCOL.md over
TCP and/or a serial port, and translates it into real Home Assistant REST
API calls (HTTPS + bearer token) that DOS could never make itself.

Usage:
    python3 ha_bridge.py [config.ini]
"""
from __future__ import annotations

import configparser
import logging
import socket
import socketserver
import sys
import threading
from dataclasses import dataclass

import requests

try:
    import serial  # pyserial
except ImportError:
    serial = None

LOG = logging.getLogger("ha_bridge")

LINE_END = "\r\n"
MAX_LINE = 200


@dataclass
class Entity:
    entity_id: str
    friendly_name: str


class HomeAssistantClient:
    def __init__(self, base_url: str, token: str, timeout: float = 5.0):
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout
        self.session = requests.Session()
        self.session.headers.update(
            {
                "Authorization": f"Bearer {token}",
                "Content-Type": "application/json",
            }
        )

    def get_state(self, entity_id: str) -> str:
        return self.get_full_state(entity_id)["state"]

    def get_full_state(self, entity_id: str) -> dict:
        resp = self.session.get(
            f"{self.base_url}/api/states/{entity_id}", timeout=self.timeout
        )
        resp.raise_for_status()
        return resp.json()

    def call_service(
        self, domain: str, service: str, entity_id: str, extra: dict | None = None
    ) -> None:
        payload = {"entity_id": entity_id}
        if extra:
            payload.update(extra)
        resp = self.session.post(
            f"{self.base_url}/api/services/{domain}/{service}",
            json=payload,
            timeout=self.timeout,
        )
        resp.raise_for_status()

    def ping(self) -> bool:
        try:
            resp = self.session.get(f"{self.base_url}/api/", timeout=self.timeout)
            return resp.ok
        except requests.RequestException:
            return False


def domain_of(entity_id: str) -> str:
    return entity_id.split(".", 1)[0]


COLOR_MODES = {"hs", "rgb", "rgbw", "rgbww", "xy"}


def light_fields(full_state: dict) -> dict:
    """Returns brightness_pct, r/g/b, and temp_k/min_k/max_k -- each -1 (r/g/b
    and temp_k/min_k/max_k always together as a group) if the entity doesn't
    support it. Derived from HA's own supported_color_modes attribute, not
    configured by hand, so a plain switch reports -1 across the board."""
    attrs = full_state.get("attributes", {})
    modes = set(attrs.get("supported_color_modes") or [])

    brightness_pct = -1
    if modes & COLOR_MODES or "brightness" in modes or "color_temp" in modes:
        raw = attrs.get("brightness")
        if raw is not None:
            brightness_pct = round(raw / 255 * 100)

    r = g = b = -1
    if modes & COLOR_MODES:
        rgb = attrs.get("rgb_color")
        if rgb is not None:
            r, g, b = rgb[0], rgb[1], rgb[2]

    temp_k = min_k = max_k = -1
    if "color_temp" in modes:
        min_k = attrs.get("min_color_temp_kelvin", -1)
        max_k = attrs.get("max_color_temp_kelvin", -1)
        temp_k = attrs.get("color_temp_kelvin", -1)
        if min_k is None:
            min_k = -1
        if max_k is None:
            max_k = -1
        if temp_k is None:
            temp_k = -1

    return {
        "brightness_pct": brightness_pct,
        "r": r, "g": g, "b": b,
        "temp_k": temp_k, "min_k": min_k, "max_k": max_k,
    }


class CommandHandler:
    """Parses one protocol line and returns the reply text (no line endings)."""

    def __init__(self, ha: HomeAssistantClient, entities: dict[str, Entity]):
        self.ha = ha
        self.entities = entities

    def handle(self, line: str) -> list[str]:
        line = line.strip()
        if not line:
            return []
        parts = line.split(None, 1)
        cmd = parts[0].upper()
        arg = parts[1].strip() if len(parts) > 1 else ""

        try:
            if cmd == "PING":
                return ["PONG"]
            if cmd == "LIST":
                return self._list()
            if cmd == "GET":
                return self._get(arg)
            if cmd in ("ON", "OFF", "TOGGLE"):
                return self._service(cmd, arg)
            if cmd == "SETBRIGHT":
                return self._setbright(arg)
            if cmd == "SETRGB":
                return self._setrgb(arg)
            if cmd == "SETTEMP":
                return self._settemp(arg)
            return [f"ERR|unknown command {cmd}"]
        except requests.HTTPError as exc:
            return [f"ERR|HA HTTP {exc.response.status_code}"]
        except requests.RequestException as exc:
            return [f"ERR|HA unreachable: {exc.__class__.__name__}"]

    def _list(self) -> list[str]:
        out = []
        for ent in self.entities.values():
            try:
                full = self.ha.get_full_state(ent.entity_id)
                state = full["state"]
                f = light_fields(full)
            except requests.RequestException:
                state = "unknown"
                f = {"brightness_pct": -1, "r": -1, "g": -1, "b": -1,
                     "temp_k": -1, "min_k": -1, "max_k": -1}
            out.append(
                f"ENTITY|{ent.entity_id}|{ent.friendly_name}|{state}"
                f"|{f['brightness_pct']}|{f['r']}|{f['g']}|{f['b']}"
                f"|{f['temp_k']}|{f['min_k']}|{f['max_k']}"
            )
        out.append("END")
        return out

    def _get(self, entity_id: str) -> list[str]:
        if entity_id not in self.entities:
            return [f"ERR|unknown entity {entity_id}"]
        state = self.ha.get_state(entity_id)
        return [f"STATE|{entity_id}|{state}"]

    def _service(self, cmd: str, entity_id: str) -> list[str]:
        if entity_id not in self.entities:
            return [f"ERR|unknown entity {entity_id}"]
        domain = domain_of(entity_id)
        service = {"ON": "turn_on", "OFF": "turn_off", "TOGGLE": "toggle"}[cmd]
        self.ha.call_service(domain, service, entity_id)
        return ["OK"]

    def _setrgb(self, arg: str) -> list[str]:
        parts = arg.split()
        if len(parts) != 4:
            return ["ERR|usage: SETRGB <entity_id> <r> <g> <b>"]
        entity_id, r_str, g_str, b_str = parts
        if entity_id not in self.entities:
            return [f"ERR|unknown entity {entity_id}"]
        if domain_of(entity_id) != "light":
            return [f"ERR|{entity_id} is not a light"]
        try:
            rgb = [max(0, min(255, int(v))) for v in (r_str, g_str, b_str)]
        except ValueError:
            return [f"ERR|bad RGB {r_str} {g_str} {b_str}"]
        self.ha.call_service("light", "turn_on", entity_id, extra={"rgb_color": rgb})
        return ["OK"]

    def _settemp(self, arg: str) -> list[str]:
        parts = arg.split(None, 1)
        if len(parts) != 2:
            return ["ERR|usage: SETTEMP <entity_id> <kelvin>"]
        entity_id, k_str = parts
        if entity_id not in self.entities:
            return [f"ERR|unknown entity {entity_id}"]
        if domain_of(entity_id) != "light":
            return [f"ERR|{entity_id} is not a light"]
        try:
            kelvin = int(k_str)
        except ValueError:
            return [f"ERR|bad kelvin {k_str}"]
        self.ha.call_service(
            "light", "turn_on", entity_id, extra={"color_temp_kelvin": kelvin}
        )
        return ["OK"]

    def _setbright(self, arg: str) -> list[str]:
        parts = arg.split(None, 1)
        if len(parts) != 2:
            return ["ERR|usage: SETBRIGHT <entity_id> <pct>"]
        entity_id, pct_str = parts
        if entity_id not in self.entities:
            return [f"ERR|unknown entity {entity_id}"]
        if domain_of(entity_id) != "light":
            return [f"ERR|{entity_id} is not a light"]
        try:
            pct = max(0, min(100, int(pct_str)))
        except ValueError:
            return [f"ERR|bad brightness {pct_str}"]
        self.ha.call_service(
            "light", "turn_on", entity_id, extra={"brightness_pct": pct}
        )
        return ["OK"]


def load_config(path: str):
    cp = configparser.ConfigParser()
    if not cp.read(path):
        raise SystemExit(f"config file not found: {path}")

    ha = HomeAssistantClient(
        base_url=cp.get("home_assistant", "base_url"),
        token=cp.get("home_assistant", "token"),
    )
    entities = {
        entity_id: Entity(entity_id, name)
        for entity_id, name in cp.items("entities")
    }
    return cp, ha, entities


# --- TCP transport -----------------------------------------------------

class TCPHandler(socketserver.StreamRequestHandler):
    def handle(self):
        peer = self.client_address
        LOG.info("TCP connect from %s", peer)
        cmd_handler: CommandHandler = self.server.cmd_handler  # type: ignore[attr-defined]
        try:
            while True:
                raw = self.rfile.readline(MAX_LINE + 2)
                if not raw:
                    break
                line = raw.decode("ascii", errors="replace")
                LOG.debug("TCP %s > %r", peer, line.strip())
                for reply in cmd_handler.handle(line):
                    self.wfile.write((reply + LINE_END).encode("ascii", errors="replace"))
        except (ConnectionResetError, BrokenPipeError):
            pass
        LOG.info("TCP disconnect %s", peer)


class TCPServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def run_tcp(host: str, port: int, cmd_handler: CommandHandler):
    server = TCPServer((host, port), TCPHandler)
    server.cmd_handler = cmd_handler  # type: ignore[attr-defined]
    LOG.info("TCP listening on %s:%d", host, port)
    server.serve_forever()


# --- Serial transport ----------------------------------------------------

def run_serial(port: str, baud: int, cmd_handler: CommandHandler):
    if serial is None:
        raise SystemExit("pyserial not installed; pip install -r requirements.txt")
    LOG.info("Serial listening on %s @ %d baud", port, baud)
    with serial.Serial(port, baudrate=baud, timeout=1) as ser:
        buf = b""
        while True:
            chunk = ser.read(64)
            if chunk:
                buf += chunk
                while b"\n" in buf:
                    raw, buf = buf.split(b"\n", 1)
                    line = raw.decode("ascii", errors="replace")
                    LOG.debug("SER > %r", line.strip())
                    try:
                        for reply in cmd_handler.handle(line):
                            ser.write((reply + LINE_END).encode("ascii", errors="replace"))
                    except Exception:
                        LOG.exception("Error handling serial line %r", line.strip())
            if len(buf) > MAX_LINE * 2:
                buf = b""  # runaway line, drop it


def main():
    logging.basicConfig(
        level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s"
    )
    config_path = sys.argv[1] if len(sys.argv) > 1 else "config.ini"
    cp, ha, entities = load_config(config_path)

    if not ha.ping():
        LOG.warning("Could not reach Home Assistant at startup — check base_url/token")

    cmd_handler = CommandHandler(ha, entities)
    threads: list[threading.Thread] = []

    if cp.getboolean("tcp", "enabled", fallback=False):
        t = threading.Thread(
            target=run_tcp,
            args=(cp.get("tcp", "host"), cp.getint("tcp", "port"), cmd_handler),
            daemon=True,
        )
        t.start()
        threads.append(t)

    if cp.getboolean("serial", "enabled", fallback=False):
        t = threading.Thread(
            target=run_serial,
            args=(cp.get("serial", "port"), cp.getint("serial", "baud"), cmd_handler),
            daemon=True,
        )
        t.start()
        threads.append(t)

    if not threads:
        raise SystemExit("Neither [tcp] nor [serial] is enabled in config.ini")

    for t in threads:
        t.join()


if __name__ == "__main__":
    main()
