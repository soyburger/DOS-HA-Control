# DOS-HA-Control

A DOS Shell-style text UI for controlling Home Assistant lights/switches
from real DOS hardware (tested and working on a 286; targets DOS 3.3
through 7 generally) over a plain serial cable.

## Why a bridge

DOS 6.22 has no TLS stack, and Home Assistant's API is HTTPS + JSON. Rather
than trying to shoehorn crypto onto a 486, this project splits into two
pieces:

- **`dos-client/`** — `HACLIENT.EXE`, a small C program (OpenWatcom, 16-bit
  real mode) that draws a blue DOS-Shell-like list of your devices and
  speaks a tiny plain-text line protocol — no JSON, no TLS.
- **`bridge/`** — a Python service (`ha_bridge.py`) that listens for that
  plain-text protocol over a serial port, and translates it into real Home
  Assistant REST API calls (HTTPS + your long-lived token).

```
 486 / DOS 6.22                    any always-on machine          Home Assistant
+----------------+     RS232       +----------------+   HTTPS     +----------------+
|   HACLIENT.EXE | <-------------> |  ha_bridge.py  | <---------> |                |
+----------------+  plaintext, no  +----------------+  token auth +----------------+
                      JSON/TLS
```

(An earlier version also supported TCP over a 3Com EtherLink III NIC via
the mTCP stack. That path was dropped — not worth the added complexity
for this project — but the bridge can still optionally serve the same
protocol over plain TCP if you want it for something else; see
`bridge/config.example.ini`.)

The wire protocol between DOS and the bridge is documented in
[docs/PROTOCOL.md](docs/PROTOCOL.md).

## Getting started

1. **Bridge**: see [bridge/](bridge/) — copy `config.example.ini` to
   `config.ini`, fill in your HA URL/token and the entities you want
   exposed, `pip install -r requirements.txt`, run `python3 ha_bridge.py`.
   This part runs and has been tested against a mock HA server. For
   running it on a Raspberry Pi as the physical bridge to a real 486, see
   [docs/PI_SETUP.md](docs/PI_SETUP.md) (imaging, wiring the USB-serial
   adapter, and installing it as a systemd service via
   `bridge/deploy/install.sh`).
2. **DOS client**: see [dos-client/docs/BUILD.md](dos-client/docs/BUILD.md)
   for the OpenWatcom build. Compiles clean and ran under DOSBox with no
   CPU exceptions — see the status table below.

## Status

**Working end-to-end on real hardware**: a 286 running `HACLIENT.EXE`
over a genuine serial link controls real Home Assistant lights through
the Pi bridge. That's the whole point of this project, confirmed for real.

| Piece                         | Status                                    |
|--------------------------------|--------------------------------------------|
| Wire protocol                  | Working, verified end-to-end on real hardware |
| Bridge (`ha_bridge.py`)        | Working, verified end-to-end on real hardware |
| DOS TUI (`screen.c`)           | Working, verified on real hardware; writes text-mode video memory directly + BIOS INT 10h (OpenWatcom's `conio.h` lacks Borland's textcolor/gotoxy/clrscr) |
| Serial transport (`net_serial.c`) | Working, verified on real hardware; programs the 8250/16450/16550 UART registers directly (no BIOS `INT 14h` for send/receive) for reliability across BIOS vendors and DOS versions |
| DOS client wiring (`main.c`, `config.c`) | Working, verified on real hardware; includes an interactive Port Settings screen at launch (COM port / baud), no `HACONFIG.INI` editing required for normal use |

One specific 486 in testing turned out to have a dead serial port
(confirmed via direct hardware register reads and independent testing on
two completely different receiving machines) — not a code or protocol
problem, just that one machine's hardware. Everything downstream of "does
the port actually carry a signal" has been proven out on working hardware.

See [dos-client/docs/BUILD.md](dos-client/docs/BUILD.md) for the OpenWatcom
build process (via a colima/Docker amd64-emulation loop, since no native
macOS OpenWatcom build exists) and [docs/PI_SETUP.md](docs/PI_SETUP.md)
for setting up the Raspberry Pi bridge.
