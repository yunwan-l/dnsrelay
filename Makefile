#
# Makefile -- DNS Relay Server
#
# Targets:
#   make              Build for the current platform
#   make linux        Build for Linux
#   make win32        Cross-compile for Windows (i686-w64-mingw32)
#   make clean        Remove build artifacts
#

CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c99 -Wno-stringop-truncation
LDFLAGS =

SRCS    = dns_relay.c dns_table.c tid_map.c dns_packet.c dns_cache.c stats.c upstream.c
OBJS    = $(SRCS:.c=.o)
TARGET  = dnsrelay

# ============================================================
# Linux build
# ============================================================
linux: TARGET = dnsrelay
linux: CFLAGS += -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=199309L -D_GNU_SOURCE
linux: LDFLAGS =
linux: $(TARGET)

# ============================================================
# Windows cross-compile (MinGW)
# ============================================================
win32: CC = i686-w64-mingw32-gcc
win32: TARGET = dnsrelay.exe
win32: CFLAGS += -D_WIN32
win32: LDFLAGS = -lws2_32
win32: $(TARGET)

# ============================================================
# Native (auto-detect)
# ============================================================
ifeq ($(OS),Windows_NT)
TARGET  = dnsrelay.exe
LDFLAGS = -lws2_32
else
CFLAGS += -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=199309L -D_GNU_SOURCE
endif

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) $(TARGET).exe

.PHONY: all linux win32 clean
