# OpenWatcom makefile for HACLIENT.EXE (16-bit real-mode DOS).
#
# Usage (from an OpenWatcom "DOS 16-bit" build environment / owsetenv):
#   wmake                    -> full build, requires mTCP SDK (see below)
#   wmake SERIALONLY=1       -> serial-only build, no mTCP SDK needed
#
# Full build requires the mTCP SDK's INC and LIB directories. Point these
# at wherever you unpacked it, e.g.:
#   set MTCPINC=C:\MTCP\INC
#   set MTCPLIB=C:\MTCP\LIB
#
# See docs/BUILD.md for the full toolchain setup (OpenWatcom install,
# environment variables, DOSBox test loop).

CC     = wcc
CXX    = wpp
LINK   = wlink

# -bt=dos targets plain real-mode DOS. -ms = small memory model (fine for
# this program: one code segment, one data segment, well under 64K each).
# -eoo forces classic OMF object output -- some host builds of wcc/wpp
# (e.g. the Linux cross-host binaries) default to ELF .o objects otherwise,
# which wlink (a DOS/OMF linker) can't use.
CFLAGS  = -bt=dos -ms -0 -w4 -zq -eoo
CXXFLAGS = $(CFLAGS)

!ifdef SERIALONLY
OBJS = main.obj config.obj protocol.obj screen.obj net_serial.obj net_tcp_stub.obj
!else
CFLAGS   += -I"$(MTCPINC)"
CXXFLAGS += -I"$(MTCPINC)"
OBJS = main.obj config.obj protocol.obj screen.obj net_serial.obj net_tcp.obj
!endif

TARGET = HACLIENT.EXE

all: $(TARGET) .SYMBOLIC

$(TARGET): $(OBJS)
!ifdef SERIALONLY
	$(LINK) system dos name $(TARGET) file { $(OBJS) }
!else
	$(LINK) system dos name $(TARGET) file { $(OBJS) } library "$(MTCPLIB)\mtcp.lib"
!endif

.c.obj:
	$(CC) $(CFLAGS) $^& -fo=$^&.obj

.cpp.obj:
	$(CXX) $(CXXFLAGS) $^& -fo=$^&.obj

clean: .SYMBOLIC
	del *.obj
	del $(TARGET)
