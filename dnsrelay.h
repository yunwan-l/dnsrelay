#ifndef DNSRELAY_H
#define DNSRELAY_H

#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#if defined(_MSC_VER)
#pragma comment(lib, "ws2_32.lib")
#define strcasecmp _stricmp
#endif
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket close
#endif

#ifdef _WIN32
typedef int socket_length_t;
#else
typedef socklen_t socket_length_t;
#endif

#ifndef DNS_PORT
#define DNS_PORT 53
#endif

#define BUFFER_SIZE 4096
#define MAX_TABLE 2048
#define DOMAIN_MAX 256
#define IP_STR_MAX 16
#define MAX_PENDING_REQUESTS 256
#define TIMEOUT_SEC 5

#define DEFAULT_UPSTREAM_DNS "202.106.0.20"
#define DEFAULT_TABLE_FILE "dnsrelay.txt"

extern int g_debug_level;

#define DEBUG1(fmt, ...)                                                        \
    do {                                                                        \
        if (g_debug_level >= 1) {                                               \
            printf("[DEBUG1] " fmt "\n", ##__VA_ARGS__);                       \
        }                                                                       \
    } while (0)

#define DEBUG2(fmt, ...)                                                        \
    do {                                                                        \
        if (g_debug_level >= 2) {                                               \
            printf("[DEBUG2] " fmt "\n", ##__VA_ARGS__);                       \
        }                                                                       \
    } while (0)

#ifdef _WIN32
#define LAST_SOCKET_ERROR() WSAGetLastError()
#else
#define LAST_SOCKET_ERROR() errno
#endif

#endif
