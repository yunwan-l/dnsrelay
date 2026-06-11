@echo off
setlocal

if not defined CC set "CC=gcc"
if not defined CFLAGS set "CFLAGS=-Wall -Wextra -O2 -std=c99 -Wno-stringop-truncation"
if not defined LDFLAGS set "LDFLAGS="

"%CC%" %CFLAGS% ^
    -o dnsrelay.exe ^
    dns_relay.c config_reload.c dns_table.c tid_map.c dns_packet.c dns_cache.c stats.c upstream.c ^
    %LDFLAGS% -lws2_32

if errorlevel 1 (
    echo Build failed.
    echo If you see "Permission denied", stop the running dnsrelay.exe with Ctrl+C and build again.
    exit /b 1
)

echo Build succeeded: dnsrelay.exe
