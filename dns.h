/*
 * dns.h -- DNS中继服务器公共定义
 *
 * 本文件集中保存跨平台socket兼容定义、DNS报文结构、常量、
 * 调试宏和少量工具函数，避免各模块重复声明。
 */
#ifndef DNS_H
#define DNS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #if defined(_MSC_VER)
        #pragma comment(lib, "ws2_32.lib")
        #define strcasecmp(s1, s2) _stricmp(s1, s2)
    #endif
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <sys/time.h>
    #include <errno.h>
    #include <fcntl.h>
    typedef int SOCKET;
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR   (-1)
    #define closesocket(x) close(x)
#endif

/* 基本运行参数 */
#define DNS_PORT        53
#define BUFFER_SIZE     1024
#define MAX_TABLE       2048
#define DOMAIN_MAX      256
#define IP_STR_MAX      16
#define MAX_CLIENTS     256
#define TIMEOUT_SEC     5

/* DNS报文固定头部和资源记录固定字段，格式参考 RFC 1035 4.1节 */
#pragma pack(push, 1)
typedef struct {
    uint16_t id;
    uint8_t  rd     : 1;
    uint8_t  tc     : 1;
    uint8_t  aa     : 1;
    uint8_t  opcode : 4;
    uint8_t  qr     : 1;
    uint8_t  rcode  : 4;
    uint8_t  cd     : 1;
    uint8_t  ad     : 1;
    uint8_t  z      : 1;
    uint8_t  ra     : 1;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} DNS_HEADER;

typedef struct {
    uint16_t type;
    uint16_t dclass;
    uint32_t ttl;
    uint16_t rdlength;
} RR_FIXED;
#pragma pack(pop)

/* 常见DNS资源记录类型 */
#define DNS_TYPE_A      1
#define DNS_TYPE_NS     2
#define DNS_TYPE_CNAME  5
#define DNS_TYPE_PTR    12
#define DNS_TYPE_MX     15
#define DNS_TYPE_AAAA   28

/* DNS查询类别：IN表示Internet */
#define DNS_CLASS_IN    1

/* DNS响应码 */
#define RCODE_OK        0
#define RCODE_FORMERR   1
#define RCODE_SERVFAIL  2
#define RCODE_NXDOMAIN  3

/* 调试输出宏：-d启用DEBUG1，-dd启用DEBUG1和DEBUG2 */
extern int debug_level;

#define DEBUG1(fmt, ...) \
    do { if (debug_level >= 1) { printf("[DEBUG1] " fmt "\n", ##__VA_ARGS__); } } while (0)

#define DEBUG2(fmt, ...) \
    do { if (debug_level >= 2) { printf("[DEBUG2] " fmt "\n", ##__VA_ARGS__); } } while (0)

/* 将点分十进制IPv4字符串转换为网络字节序整数 */
static inline uint32_t ip_str_to_net(const char *ip_str)
{
    int a, b, c, d;
    if (sscanf(ip_str, "%d.%d.%d.%d", &a, &b, &c, &d) != 4)
        return 0;
    return htonl((a << 24) | (b << 16) | (c << 8) | d);
}

/* 将点分十进制IPv4字符串拆成4个字节 */
static inline int ip_str_to_bytes(const char *ip_str, unsigned char out[4])
{
    int a, b, c, d;
    if (sscanf(ip_str, "%d.%d.%d.%d", &a, &b, &c, &d) != 4)
        return -1;
    out[0] = (unsigned char)a;
    out[1] = (unsigned char)b;
    out[2] = (unsigned char)c;
    out[3] = (unsigned char)d;
    return 0;
}

#endif /* DNS_H */
