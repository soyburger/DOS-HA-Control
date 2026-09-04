# DOS-HA-Control

A DOS Shell-style text UI for controlling Home Assistant lights/switches
from a real (or emulated) 486 running MS-DOS 6.22, over either a 3Com
EtherLink III NIC or a plain serial cable.

## Why a bridge

DOS 6.22 has no TLS stack, and Home Assistant's API is HTTPS + JSON. Rather
than trying to shoehorn crypto onto a 486, this project splits into two
pieces:

- **`dos-client/`** — `HACLIENT.EXE`, a small C program (OpenWatcom, 16-bit
  real mode) that draws a blue DOS-Shell-like list of your devices and
  speaks a tiny plain-text line protocol — no JSON, no TLS.
- **`bridge/`** — a Python service (`ha_bridge.py`) that listens for that
  plain-text protocol over TCP and/or a serial port, and translates it into
  real Home Assistant REST API calls (HTTPS + your long-lived token).

```
 486 / DOS 6.22                    any always-on machine          Home Assistant
+----------------+   TCP or RS232  +----------------+   HTTPS     +----------------+
|   HACLIENT.EXE | <-------------> |  ha_bridge.py  | <---------> |                |
+----------------+  plaintext, no  +----------------+  token auth +----------------+
                      JSON/TLS
```

The wire protocol between DOS and the bridge is documented in
[docs/PROTOCOL.md](docs/PROTOCOL.md).

## Getting started

1. **Bridge**: see [bridge/](bridge/) — copy `config.example.ini` to
   `config.ini`, fill in your HA URL/token and the entities you want
   exposed, `pip install -r requirements.txt`, run `python3 ha_bridge.py`.
   This part runs and has been tested against a mock HA server.
2. **DOS client**: see [dos-client/docs/BUILD.md](dos-client/docs/BUILD.md)
   for the OpenWatcom + (optionally) mTCP SDK build. This part has **not**
   been compiled or run yet — no DOSBox/OpenWatcom toolchain was available
   while writing it; budget time for a first-build pass, especially around
   `net_tcp.cpp` (see the warning comment at its top).

## Status

| Piece                         | Status                                    |
|--------------------------------|--------------------------------------------|
| Wire protocol                  | Designed, documented                        |
| Bridge (`ha_bridge.py`)        | Working, tested against a mock HA server    |
| DOS TUI (`screen.c`)           | Compiles clean; rewritten to use direct video-memory writes + BIOS INT 10h after discovering OpenWatcom's `conio.h` lacks Borland's textcolor/gotoxy/clrscr |
| Serial transport (`net_serial.c`) | Compiles clean; ran under DOSBox with no CPU exceptions, not yet tested against real hardware |
| mTCP transport (`net_tcp.cpp`) | Written from memory of mTCP samples — not yet build-tested, needs the real mTCP SDK headers |
| DOS client wiring (`main.c`, `config.c`) | Compiles clean, linked into a working `HACLIENT.EXE`, ran under DOSBox |

The `SERIALONLY=1` build was compiled end-to-end with OpenWatcom (via a
colima/Docker amd64-emulation loop, since no native macOS OpenWatcom
build exists) and smoke-tested under DOSBox — see
[dos-client/docs/BUILD.md](dos-client/docs/BUILD.md) for the exact steps
and the three real bugs that loop caught. The TUI layout itself hasn't
been eyeballed on screen yet (no screenshot access to the native DOSBox
window in that environment).
