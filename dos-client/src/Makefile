# OpenWatcom makefile for HACLIENT.EXE (16-bit real-mode DOS).
#
# Usage (from an OpenWatcom "DOS 16-bit" build environment / owsetenv):
#   wmake
#
# See docs/BUILD.md for the full toolchain setup (OpenWatcom install,
# environment variables, DOSBox test loop). That doc also has a verified
# colima+Docker recipe for building on a Mac with no native OpenWatcom.

CC     = wcc
LINK   = wlink

# -bt=dos targets plain real-mode DOS. -ms = small memory model (fine for
# this program: one code segment, one data segment, well under 64K each).
# -eoo forces classic OMF object output -- some host builds of wcc
# (e.g. the Linux cross-host binaries) default to ELF .o objects otherwise,
# which wlink (a DOS/OMF linker) can't use.
CFLAGS = -bt=dos -ms -0 -w4 -zq -eoo

OBJS = main.obj config.obj protocol.obj screen.obj net_serial.obj

TARGET = HACLIENT.EXE

all: $(TARGET) .SYMBOLIC

$(TARGET): $(OBJS)
	$(LINK) system dos name $(TARGET) file { $(OBJS) }

.c.obj:
	$(CC) $(CFLAGS) $^& -fo=$^&.obj

clean: .SYMBOLIC
	del *.obj
	del $(TARGET)
