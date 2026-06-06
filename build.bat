@echo off
setlocal

gcc -Wall -Wextra -O2 -std=c99 -Wno-stringop-truncation ^
    -o dnsrelay.exe ^
    dns_relay.c dns_table.c tid_map.c dns_packet.c dns_cache.c stats.c upstream.c ^
    -lws2_32

if errorlevel 1 (
    echo Build failed.
    echo If you see "Permission denied", stop the running dnsrelay.exe with Ctrl+C and build again.
    exit /b 1
)

echo Build succeeded: dnsrelay.exe
