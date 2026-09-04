# Building HACLIENT.EXE

Serial-only (RS-232) link to the bridge — no networking stack on the DOS
side at all.

## What you need

1. **OpenWatcom** (v2 fork, actively maintained: https://github.com/open-watcom/open-watcom-v2).
   Install it and run its environment setup script (`owsetenv.sh` /
   `owvars.bat`) so `wcc`, `wlink`, `wmake` are on your PATH.
2. Something to test on: either a real 486 with DOS 6.22, or **DOSBox** /
   **DOSBox-X** for a quick local loop.

## Build

```
cd dos-client/src
wmake
```

Produces `HACLIENT.EXE`. Copy it and `HACONFIG.INI` to the same directory
on the DOS machine (edit `HACONFIG.INI` first if COM port/baud need to
change — see comments inside it).

## Verified so far (macOS, arm64, via colima + Docker amd64 emulation)

No native macOS OpenWatcom binaries exist. Got a working build+run loop
anyway:

```
brew install dosbox-staging colima docker
colima start   # amd64 emulation ("linux/amd64") is on by default

# Grab OpenWatcom's Linux x64 binaries -- ow-snapshot.tar.xz is a ready
# extracted distribution (no installer to run), unlike the
# open-watcom-2_0-c-linux-x64 asset, which is an interactive installer
# that hangs with no TTY attached.
gh release download Current-build --repo open-watcom/open-watcom-v2 \
  --pattern 'ow-snapshot.tar.xz' --dir ~/ow_build --clobber
mkdir -p ~/ow_build/watcom && tar xJf ~/ow_build/ow-snapshot.tar.xz -C ~/ow_build/watcom

# colima only bind-mounts $HOME by default -- keep project copies under it.
cp -r dos-client ~/ow_build/proj/

docker run --rm --platform linux/amd64 \
  -v ~/ow_build/watcom:/watcom -v ~/ow_build/proj:/proj \
  -e WATCOM=/watcom -e PATH=/watcom/binl64:/usr/bin:/bin -e INCLUDE=/watcom/h \
  -w /proj/dos-client/src \
  debian:bookworm-slim /watcom/binl64/wmake -f Makefile
```

This produced a real `HACLIENT.EXE` and caught three real bugs, now fixed
in the source:

1. **wmake's implicit-rule macro is `$^&`, not make's `$<`/`$@`.** The
   Makefile's `.c.obj:` rule was a silent no-op with the GNU-style macros
   (no error, just "does not exist and cannot be made").
2. **The Linux-hosted `wcc` defaults to ELF `.o` output**, not DOS/OMF
   `.obj`, even with `-bt=dos`. Fixed by always passing `-eoo -fo=name.obj`
   explicitly rather than relying on default naming.
3. **OpenWatcom's `conio.h` does not have Borland's `textcolor()` /
   `textbackground()` / `gotoxy()` / `clrscr()` / color constants** — those
   are a Turbo C/Borland extension, not part of Watcom's C library.
   `screen.c` originally used them and wouldn't compile. Rewrote it to
   write directly to text-mode video memory at `B800:0000` plus BIOS
   INT 10h for the cursor, which is standard, compiler-independent DOS
   technique and doesn't depend on any vendor-specific console library.

The build now compiles and links cleanly to a 17KB `HACLIENT.EXE`, and it
ran under `dosbox-staging` without any CPU exception (no illegal-opcode/
divide traps in the DOSBox log) — it got as far as the "bridge did not
respond" alert box and sat blocked on `getch()` waiting for a keypress,
which is exactly the expected behavior with no real serial peer attached.
I could not visually confirm the TUI layout itself (no screenshot access
to the native DOSBox window from this environment) — that's the one thing
still worth a human eyeball check.

## Wiring it up

Null-modem cable from the DOS machine's COM port to the bridge machine's
serial port (or a USB-serial adapter on the bridge side). Set `COM=` and
`BAUD=` in `HACONFIG.INI` to match `[serial] port=`/`baud=` in the
bridge's `config.ini`. Baud must match on both ends — 9600 is the safe
default (BIOS INT 14h, which `net_serial.c` uses, tops out there on most
486-era BIOSes).

## Known gaps / not yet done

- The entity list currently doesn't scroll past what fits on screen
  (`LIST_BOTTOM - LIST_TOP` rows, ~18). Fine for a handful of lights/switches;
  extend `scr_draw_list` with paging if you configure many more.
- The TUI's actual on-screen appearance hasn't been eyeballed on a real
  display, only confirmed not to crash under DOSBox (see above).
