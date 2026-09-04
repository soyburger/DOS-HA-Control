# Building HACLIENT.EXE

## What you need

1. **OpenWatcom** (v2 fork, actively maintained: https://github.com/open-watcom/open-watcom-v2).
   Install it and run its environment setup script (`owsetenv.sh` /
   `owvars.bat`) so `wcc`, `wpp`, `wlink`, `wmake` are on your PATH.
2. **mTCP SDK** (only for the TCP/EtherLink III build) — http://www.brutman.com/mTCP/
   Download the source+samples archive. You'll need its `INC` headers
   (`tcp.h`, `tcpsockm.h`, `arp.h`, `packet.h`, `utils.h`, ...) and a built
   `mtcp.lib` to link against.
3. Something to test on: either a real 486 with DOS 6.22, or **DOSBox** /
   **DOSBox-X** for a quick local loop (DOSBox-X has better NE2000/packet
   driver emulation if you want to test the TCP path without real hardware).

## Build

Serial-only build (no mTCP SDK required — good first smoke test):

```
wmake SERIALONLY=1
```

Full build with networking:

```
set MTCPINC=C:\MTCP\INC
set MTCPLIB=C:\MTCP\LIB
wmake
```

Both produce `HACLIENT.EXE`. Copy it and `src/HACONFIG.INI` to the same
directory on the DOS machine (edit `HACONFIG.INI` first — see comments
inside it).

## About net_tcp.cpp

`net_tcp.cpp` was written from memory of mTCP's sample programs, without
the actual SDK headers on hand to check against — see the big warning
comment at the top of that file. Before your first full build, open one of
mTCP's own samples (e.g. `APPS/HTGET/HTGET.CPP` in the SDK) side by side
and confirm the init sequence (`Utils::parseEnv`, `Packet_init`,
`Arp::init`, `Tcp::init`) and the `TcpSocket` methods used
(`connect`, `send`, `recv`, `isConnected`, `close`) match your SDK version's
signatures. Everything else in this codebase (protocol.c, screen.c,
net_serial.c, config.c, main.c) doesn't touch mTCP internals and shouldn't
need changes.

## Network setup on the DOS machine (TCP path)

1. Load the packet driver for the 3Com EtherLink III, e.g.:
   ```
   3C5X9PD 0x60
   ```
   (interrupt vector `0x60` is the mTCP convention; adjust if it collides
   with something else on your machine).
2. Create `MTCPCFG.CFG` per mTCP's own docs (static IP is simplest on a
   home network — set `IPADDR`, `GATEWAY`, `NETMASK`, `PACKETINT 0x60`).
3. `SET MTCPCFG=C:\MTCP\MTCPCFG.CFG` in `AUTOEXEC.BAT` or before running
   `HACLIENT.EXE`.
4. Run the bridge (`bridge/ha_bridge.py`) on a machine on the same subnet,
   with `[tcp] enabled = true` in its config, and put that machine's IP in
   `HACONFIG.INI`'s `HOST=`.

## Serial path

Null-modem cable from the DOS machine's COM port to the bridge machine's
serial port (or USB-serial adapter on the bridge side). Set `TRANSPORT=SERIAL`,
`COM=`, and `BAUD=` in `HACONFIG.INI`, and `[serial] enabled = true` with
the matching `port=`/`baud=` in the bridge's `config.ini`. Baud must match
on both ends — 9600 is the safe default (BIOS INT 14h, which `net_serial.c`
uses, tops out there on most 486-era BIOSes).

## Known gaps / not yet done

- `net_tcp.cpp` needs the verification pass described above before it will
  compile against a real mTCP SDK.
- No DOSBox/OpenWatcom toolchain was available in the environment this was
  written in, so nothing on the DOS side has been compiled or run yet —
  only the transport-independent logic (`protocol.c`, `config.c`) was
  syntax-checked with a desktop C compiler. Budget time for a first-build
  debugging pass.
- The entity list currently doesn't scroll past what fits on screen
  (`LIST_BOTTOM - LIST_TOP` rows, ~18). Fine for a handful of lights/switches;
  extend `scr_draw_list` with paging if you configure many more.
