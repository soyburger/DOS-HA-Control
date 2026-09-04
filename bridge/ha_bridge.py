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
        resp = self.session.get(
            f"{self.base_url}/api/states/{entity_id}", timeout=self.timeout
        )
        resp.raise_for_status()
        return resp.json()["state"]

    def call_service(self, domain: str, service: str, entity_id: str) -> None:
        resp = self.session.post(
            f"{self.base_url}/api/services/{domain}/{service}",
            json={"entity_id": entity_id},
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
            return [f"ERR|unknown command {cmd}"]
        except requests.HTTPError as exc:
            return [f"ERR|HA HTTP {exc.response.status_code}"]
        except requests.RequestException as exc:
            return [f"ERR|HA unreachable: {exc.__class__.__name__}"]

    def _list(self) -> list[str]:
        out = []
        for ent in self.entities.values():
            try:
                state = self.ha.get_state(ent.entity_id)
            except requests.RequestException:
                state = "unknown"
            out.append(f"ENTITY|{ent.entity_id}|{ent.friendly_name}|{state}")
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
                    self.wfile.write((reply + LINE_END).encode("ascii"))
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
                    for reply in cmd_handler.handle(line):
                        ser.write((reply + LINE_END).encode("ascii"))
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
