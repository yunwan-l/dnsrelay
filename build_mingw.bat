@echo off
setlocal

gcc -Wall -Wextra -O2 dnsrelay.c dns_protocol.c domain_table.c relay_engine.c -lws2_32 -o dnsrelay.exe

if errorlevel 1 (
    echo.
    echo Build failed.
    exit /b 1
)

echo.
echo Build succeeded: dnsrelay.exe
