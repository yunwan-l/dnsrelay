      
/*
 * dnsrelay.c — DNS中继服务器
 * 计算机网络课程设计
 *
 * 功能:
 *   1) DNS服务器: 查询本地"域名-IP"对照表，命中则直接回复
 *   2) 网站拦截:  表中IP为0.0.0.0时，返回"域名不存在"(NXDOMAIN)
 *   3) DNS中继:  本地未命中，转发到上游DNS，结果返回客户端
 *
 * 编译 (Windows, Visual Studio / MinGW):
 *   cl dnsrelay.c wsock32.lib
 *   或: gcc dnsrelay.c -lws2_32 -o dnsrelay.exe
 *
 * 命令行:
 *   dnsrelay                    # 默认: 上游 202.106.0.20, 配置文件 dnsrelay.txt
 *   dnsrelay -d                # 调试级别1
 *   dnsrelay -dd               # 调试级别2 (冗长)
 *   dnsrelay -d 8.8.8.8       # 上游DNS设为 8.8.8.8
 *   dnsrelay -d 8.8.8.8 C:\table.txt
 *
 * 配置文件格式 (每行):
 *   www.baidu.com 14.215.177.38
 *   www.example.com 0.0.0.0    ← 0.0.0.0 表示拦截
 */

#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define strcasecmp(s1, s2) _stricmp(s1, s2)
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <sys/time.h>
    #include <errno.h>
    typedef int SOCKET;
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR   (-1)
    #define closesocket(x) close(x)
#endif

/* ============================================================
 * 常量定义
 * ============================================================ */
#define DNS_PORT        53          /* DNS 标准端口 */
#define BUFFER_SIZE     1024        /* 接收/发送缓冲区 */

#define MAX_TABLE       1024        /* 本地域名表最大条数 */
#define DOMAIN_MAX      256         /* 域名最大长度 */
#define IP_STR_MAX      16          /* IP 字符串最大长度 */

#define MAX_CLIENTS     128         /* 最大并发客户端数 */
#define TIMEOUT_SEC     5           /* 等待外部DNS响应的超时时间(秒) */

/* ============================================================
 * 调试级别定义 (全局变量)
 * ============================================================ */
int debug_level = 0;    /* 0=无调试, 1=基本信息, 2=冗长 */

#define DEBUG1(fmt, ...) \
    do { if (debug_level >= 1) { printf("[DEBUG1] " fmt "\n", ##__VA_ARGS__); } } while (0)

#define DEBUG2(fmt, ...) \
    do { if (debug_level >= 2) { printf("[DEBUG2] " fmt "\n", ##__VA_ARGS__); } } while (0)

/* ============================================================
 * DNS 报文结构 (RFC 1035)
 * ============================================================ */

/* DNS 报头 — 12 字节 (使用位域精确匹配) */
#pragma pack(push, 1)
typedef struct {
    /* 第1-2字节: ID */
    unsigned short id;

    /* 第3-4字节: 标志位 */
    unsigned char  rd     : 1;   /* 期望递归 */
    unsigned char  tc     : 1;   /* 报文截断 */
    unsigned char  aa     : 1;   /* 权威回答 */
    unsigned char  opcode : 4;   /* 操作码 */
    unsigned char  qr     : 1;   /* 0=查询, 1=响应 */

    unsigned char  rcode  : 4;   /* 返回码 */
    unsigned char  cd     : 1;   /* 禁用验证 */
    unsigned char  ad     : 1;   /* 认证数据 */
    unsigned char  z      : 1;   /* 保留 */
    unsigned char  ra     : 1;   /* 递归可用 */

    /* 第5-12字节: 计数字段 */
    unsigned short qdcount;      /* 问题数 */
    unsigned short ancount;      /* 回答数 */
    unsigned short nscount;      /* 权威数 */
    unsigned short arcount;      /* 附加数 */
} DNS_HEADER;
#pragma pack(pop)

/* DNS 问题部分 (QNAME 是可变长度, 需单独处理) */
/* QNAME 后面紧跟 QTYPE(2字节) + QCLASS(2字节) */

/* DNS 资源记录 (固定部分 + RDATA 可变) */
#pragma pack(push, 1)
typedef struct {
    unsigned short type;         /* RR类型: 1=A, 5=CNAME, 15=MX ... */
    unsigned short dclass;       /* 类别, 通常为 1 (IN) */
    unsigned int   ttl;          /* 生存时间(秒) */
    unsigned short rdlength;     /* RDATA 长度 */
    /* RDATA 紧随其后, 通过指针访问 */
} RR_FIXED;
#pragma pack(pop)

/* ============================================================
 * 本地域名-IP 对照表
 * ============================================================ */
typedef struct {
    char domain[DOMAIN_MAX];     /* 域名 */
    unsigned int ip;             /* IP地址 (网络字节序) */
    int is_blocked;              /* 1=拦截(0.0.0.0), 0=正常 */
} DomainEntry;

DomainEntry domain_table[MAX_TABLE];
int domain_count = 0;

/* ============================================================
 * ID 映射表 (用于多客户端并发)
 * ============================================================ */
typedef struct {
    unsigned short new_id;       /* 发给外部DNS时用的ID */
    unsigned short orig_id;      /* 客户端原始的ID */
    struct sockaddr_in client;   /* 原始客户端地址 */
    int            in_use;       /* 1=该条目正在使用 */
    time_t         timestamp;    /* 发送时间戳 (用于超时清理) */
} TidMapping;

TidMapping tid_table[MAX_CLIENTS];
int tid_count = 0;

/* ============================================================
 * 函数声明
 * ============================================================ */
int  init_winsock(void);
void cleanup_winsock(void);
int  load_domain_table(const char *filename);
int  search_domain(const char *domain, unsigned int *out_ip);
void parse_qname(const unsigned char *buf, int buf_len, int offset,
                 char *out_name, int max_len);
int  build_qname(const char *domain, unsigned char *out_buf);
int  build_dns_response(const unsigned char *query, int query_len,
                        unsigned char *response, int rcode,
                        unsigned int answer_ip);
int  relay_to_ns(SOCKET sock, struct sockaddr_in *ns_addr,
                 const unsigned char *query, int query_len);
int  wait_ns_response(SOCKET sock, unsigned char *buf, int buf_size,
                      struct sockaddr_in *ns_addr, int timeout_sec);
int  handle_client_request(SOCKET server_sock, SOCKET ns_sock,
                           const unsigned char *req, int req_len,
                           struct sockaddr_in *client_addr);
void print_domain_list(void);
void cleanup_stale_tids(void);
unsigned short get_new_tid(unsigned short orig_id,
                           struct sockaddr_in *client);
int  restore_original(unsigned short new_id,
                      unsigned short *out_orig_id,
                      struct sockaddr_in *out_client);

/* ============================================================
 * Socket 初始化
 * ============================================================ */
int init_winsock(void)
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup failed\n");
        return -1;
    }
#endif
    return 0;
}

void cleanup_winsock(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

/* ============================================================
 * 加载本地域名-IP对照表
 * ============================================================ */
int load_domain_table(const char *filename)
{
    FILE *fp;
    char line[512];
    char domain[DOMAIN_MAX];
    char ip_str[IP_STR_MAX];
    int  a, b, c, d;
    unsigned int ip;

    fp = fopen(filename, "r");
    if (fp == NULL) {
        printf("警告: 无法打开配置文件 '%s', 仅使用中继模式\n", filename);
        return 0;
    }

    domain_count = 0;
    while (fgets(line, sizeof(line), fp) && domain_count < MAX_TABLE) {
        /* 跳过空行和注释 */
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        if (sscanf(line, "%s %s", domain, ip_str) < 2)
            continue;

        if (sscanf(ip_str, "%d.%d.%d.%d", &a, &b, &c, &d) != 4)
            continue;

        /* 构造IP (网络字节序) */
        ip = htonl((a << 24) | (b << 16) | (c << 8) | d);

        strncpy(domain_table[domain_count].domain, domain,
                DOMAIN_MAX - 1);
        domain_table[domain_count].ip = ip;
        domain_table[domain_count].is_blocked = (ip == 0);
        domain_count++;

        DEBUG2("加载: %s -> %s (%s)", domain, ip_str,
               (ip == 0) ? "拦截" : "正常");
    }

    fclose(fp);
    printf("已加载 %d 条域名记录\n", domain_count);
    return domain_count;
}

/* ============================================================
 * 域名查询
 * ============================================================ */
int search_domain(const char *domain, unsigned int *out_ip)
{
    int i;
    for (i = 0; i < domain_count; i++) {
        if (strcasecmp(domain, domain_table[i].domain) == 0) {
            *out_ip = domain_table[i].ip;
            return domain_table[i].is_blocked ? 1 : 0;
            /* 返回 0=正常, 1=拦截 */
        }
    }
    return -1;  /* 未找到 */
}

/* ============================================================
 * 解析 DNS 报文中的域名 (QNAME)
 *
 * DNS 域名编码: 每段前1字节为长度, 以0长度结束
 * 支持指针压缩 (前2字节高2位为11时表示指针)
 * ============================================================ */
void parse_qname(const unsigned char *buf, int buf_len, int offset,
                 char *out_name, int max_len)
{
    int pos = offset;
    int out_pos = 0;
    int jumped = 0;
    int len;

    /* 处理指针时的最大跳转次数, 防止死循环 */
    int jump_count = 0;

    while (1) {
        if (pos >= buf_len) {
            out_name[out_pos] = '\0';
            return;
        }

        len = buf[pos];

        /* 指针压缩: 高2位为 11 */
        if ((len & 0xC0) == 0xC0) {
            if (pos + 1 >= buf_len) break;
            if (!jumped) {
                /* 第一次遇到指针, pos仍然指向原始offset之后的结尾 */
                jumped = 1;
            }
            pos = ((len & 0x3F) << 8) | buf[pos + 1];

            jump_count++;
            if (jump_count > 10) break;  /* 防死循环 */
            continue;
        }

        if (len == 0) break;  /* 域名结束 */

        pos++;
        if (out_pos > 0 && out_pos < max_len - 1)
            out_name[out_pos++] = '.';

        while (len > 0 && pos < buf_len && out_pos < max_len - 1) {
            out_name[out_pos++] = buf[pos++];
            len--;
        }
    }

    out_name[out_pos] = '\0';

    if (!jumped) {
        /* 没有跳转, 则pos在结束符之后 */
        /* 调用者可以用这个计算QNAME长度 */
    }
}

/* ============================================================
 * 将域名编码为DNS格式 (长度+标签+结束符)
 * ============================================================ */
int build_qname(const char *domain, unsigned char *out_buf)
{
    int pos = 0;
    int label_len = 0;
    int label_start = 0;
    int i;

    /* 先留一个长度字节的位置 */
    pos = 1;
    label_len = 0;

    for (i = 0; domain[i] != '\0'; i++) {
        if (domain[i] == '.') {
            out_buf[label_start] = (unsigned char)label_len;
            label_start = pos;
            label_len = 0;
            pos++;
        } else {
            out_buf[pos++] = (unsigned char)domain[i];
            label_len++;
        }
    }

    /* 最后一个标签 */
    out_buf[label_start] = (unsigned char)label_len;
    /* 结束符 */
    out_buf[pos++] = 0;

    return pos;
}

/* ============================================================
 * 构造 DNS 响应报文
 *
 * 参数:
 *   query      - 客户端发来的查询报文
 *   query_len  - 查询报文长度
 *   response   - 输出缓冲区
 *   rcode      - 返回码 (0=成功, 3=NXDOMAIN)
 *   answer_ip  - 回答的IP地址 (网络字节序), 仅在 rcode==0 时有效
 *
 * 返回: 响应报文长度
 * ============================================================ */
int build_dns_response(const unsigned char *query, int query_len,
                       unsigned char *response, int rcode,
                       unsigned int answer_ip)
{
    DNS_HEADER *resp_hdr = (DNS_HEADER *)response;
    int qname_len;
    int offset;

    /* 1) 复制并修改报头 */
    memcpy(response, query, sizeof(DNS_HEADER));
    resp_hdr->qr     = 1;             /* 响应 */
    resp_hdr->aa     = 1;             /* 权威回答 */
    resp_hdr->ancount = htons(0);     /* 先置0 */
    resp_hdr->nscount = htons(0);
    resp_hdr->arcount = htons(0);

    if (rcode == 3) {
        resp_hdr->rcode = 3;          /* NXDOMAIN */
    } else {
        resp_hdr->rcode = 0;
    }

    /* 2) 复制 Question 部分 */
    offset = sizeof(DNS_HEADER);

    /* 计算 QNAME 长度 */
    {
        int tmp = offset;
        while (tmp < query_len && query[tmp] != 0) {
            if ((query[tmp] & 0xC0) == 0xC0) {
                /* 指针跳转, 但QNAME一般不是指针 */
                tmp += 2;
                break;
            }
            tmp += 1 + query[tmp];
        }
        tmp += 1; /* 结尾的0 */
        qname_len = tmp - offset;
    }

    memcpy(response + offset, query + offset, qname_len + 4);
    offset += qname_len + 4;    /* QNAME + QTYPE(2) + QCLASS(2) */

    /* 3) 如果 rcode == 0, 添加 Answer 部分 (A记录) */
    if (rcode == 0 && answer_ip != 0) {
        RR_FIXED *rr;

        /* 在 Answer 中复用 QNAME (指针 0xC00C 指向报文头部的 QNAME 起始位置) */
        response[offset++] = 0xC0;
        response[offset++] = 0x0C;  /* 指针指向报文起始+12 (即QNAME) */

        rr = (RR_FIXED *)(response + offset);
        rr->type    = htons(1);      /* A 记录 */
        rr->dclass  = htons(1);      /* IN */
        rr->ttl     = htonl(300);    /* TTL = 300秒 */
        rr->rdlength = htons(4);     /* IPv4 = 4字节 */
        offset += sizeof(RR_FIXED);

        /* RDATA: IP地址 */
        *(unsigned int *)(response + offset) = answer_ip;
        offset += 4;

        resp_hdr->ancount = htons(1);  /* 1条回答 */
    }

    return offset;
}

/* ============================================================
 * ID 映射表操作
 * ============================================================ */

/* 清理超时的 ID 映射条目 */
void cleanup_stale_tids(void)
{
    int i;
    time_t now = time(NULL);

    for (i = 0; i < MAX_CLIENTS; i++) {
        if (tid_table[i].in_use &&
            (now - tid_table[i].timestamp) > TIMEOUT_SEC) {
            DEBUG2("清理超时的TID映射: new_id=%u, orig_id=%u",
                   tid_table[i].new_id, tid_table[i].orig_id);
            tid_table[i].in_use = 0;
            tid_count--;
        }
    }
}

/* 分配一个新的 ID, 记录映射关系, 返回新ID */
unsigned short get_new_tid(unsigned short orig_id,
                           struct sockaddr_in *client)
{
    int i;
    static unsigned short next_id = 1;

    cleanup_stale_tids();

    for (i = 0; i < MAX_CLIENTS; i++) {
        if (!tid_table[i].in_use) {
            /* 找一个未被使用的新ID */
            while (1) {
                int conflict = 0;
                int j;
                for (j = 0; j < MAX_CLIENTS; j++) {
                    if (tid_table[j].in_use &&
                        tid_table[j].new_id == next_id) {
                        conflict = 1;
                        break;
                    }
                }
                if (!conflict) break;
                next_id++;
            }

            tid_table[i].new_id    = next_id;
            tid_table[i].orig_id   = orig_id;
            tid_table[i].client    = *client;
            tid_table[i].in_use    = 1;
            tid_table[i].timestamp = time(NULL);
            tid_count++;

            DEBUG2("分配新TID: orig=%u -> new=%u, 客户端 %s:%d",
                   orig_id, next_id,
                   inet_ntoa(client->sin_addr),
                   ntohs(client->sin_port));

            return next_id++;
        }
    }

    DEBUG1("警告: TID表已满, 无法分配新ID");
    return orig_id;  /* 回退: 不转换 */
}

/* 根据新ID恢复原始ID和客户端地址 */
int restore_original(unsigned short new_id,
                     unsigned short *out_orig_id,
                     struct sockaddr_in *out_client)
{
    int i;
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (tid_table[i].in_use && tid_table[i].new_id == new_id) {
            *out_orig_id = tid_table[i].orig_id;
            *out_client  = tid_table[i].client;
            tid_table[i].in_use = 0;
            tid_count--;

            DEBUG2("恢复TID: new=%u -> orig=%u", new_id, *out_orig_id);
            return 1;  /* 找到 */
        }
    }
    return 0;  /* 未找到 (可能是超时或迟到的响应) */
}

/* ============================================================
 * 向外部DNS服务器转发查询
 * ============================================================ */
int relay_to_ns(SOCKET sock, struct sockaddr_in *ns_addr,
                const unsigned char *query, int query_len)
{
    int ret = sendto(sock, (const char *)query, query_len, 0,
                     (struct sockaddr *)ns_addr, sizeof(*ns_addr));
    if (ret == SOCKET_ERROR) {
        DEBUG1("向外部DNS发送失败, error=%d",
#ifdef _WIN32
               WSAGetLastError()
#else
               errno
#endif
              );
        return -1;
    }
    return ret;
}

/* ============================================================
 * 等待外部DNS的回复 (带超时)
 * ============================================================ */
int wait_ns_response(SOCKET sock, unsigned char *buf, int buf_size,
                     struct sockaddr_in *ns_addr, int timeout_sec)
{
    fd_set readfds;
    struct timeval tv;
    int ret;
    socklen_t addr_len = sizeof(*ns_addr);

    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);

    tv.tv_sec  = timeout_sec;
    tv.tv_usec = 0;

    ret = select((int)sock + 1, &readfds, NULL, NULL, &tv);
    if (ret == SOCKET_ERROR) {
        DEBUG1("select() 失败");
        return -1;
    }
    if (ret == 0) {
        /* 超时 */
        DEBUG2("等待外部DNS响应超时");
        return -2;
    }

    ret = recvfrom(sock, (char *)buf, buf_size, 0,
                   (struct sockaddr *)ns_addr, &addr_len);
    if (ret == SOCKET_ERROR) {
        DEBUG1("recvfrom 外部DNS失败");
        return -1;
    }

    return ret;
}

/* ============================================================
 * 显示本地域名表
 * ============================================================ */
void print_domain_list(void)
{
    int i;
    printf("\n本地域名表 (%d 条):\n", domain_count);
    printf("  %-30s %-16s %s\n", "域名", "IP地址", "状态");
    printf("  " "------------------------------ ---------------- --------\n");
    for (i = 0; i < domain_count; i++) {
        struct in_addr addr;
        addr.s_addr = domain_table[i].ip;
        printf("  %-30s %-16s %s\n",
               domain_table[i].domain,
               inet_ntoa(addr),
               domain_table[i].is_blocked ? "[拦截]" : "[正常]");
    }
    printf("\n");
}

/* ============================================================
 * 处理一个客户端的DNS请求
 * ============================================================ */
int handle_client_request(SOCKET server_sock, SOCKET ns_sock,
                          const unsigned char *req, int req_len,
                          struct sockaddr_in *client_addr)
{
    DNS_HEADER *hdr = (DNS_HEADER *)req;
    char qname[DOMAIN_MAX];
    unsigned int answer_ip;
    unsigned char response[BUFFER_SIZE];
    int resp_len;
    struct sockaddr_in reply_addr;
    int search_result;

    /* 检查最小长度 */
    if (req_len < (int)sizeof(DNS_HEADER) + 5) {
        DEBUG1("收到过短的报文, 忽略");
        return -1;
    }

    /* 解析查询域名 */
    parse_qname(req, req_len, sizeof(DNS_HEADER), qname, sizeof(qname));

    if (debug_level >= 1) {
        printf("[%ld] ID=%u, Q=%s\n",
               (long)time(NULL),
               ntohs(hdr->id), qname);
    }

    /* 查询本地表 */
    search_result = search_domain(qname, &answer_ip);

    if (search_result >= 0) {
        /* ===== 本地命中 ===== */
        if (search_result == 1) {
            /* 拦截 (0.0.0.0) — 返回 NXDOMAIN */
            DEBUG1("拦截: %s (0.0.0.0)", qname);
            resp_len = build_dns_response(req, req_len,
                                          response, 3, 0);
        } else {
            /* 正常命中 — 直接回复 */
            struct in_addr addr;
            addr.s_addr = answer_ip;
            DEBUG1("本地命中: %s -> %s", qname, inet_ntoa(addr));
            resp_len = build_dns_response(req, req_len,
                                          response, 0, answer_ip);
        }

        sendto(server_sock, (const char *)response, resp_len, 0,
               (struct sockaddr *)client_addr, sizeof(*client_addr));
        return 0;

    } else {
        /* ===== 本地未命中 → 中继模式 ===== */
        unsigned char relay_req[BUFFER_SIZE];
        int relay_len;
        unsigned short new_id;
        unsigned short orig_id;
        unsigned char ns_reply[BUFFER_SIZE];
        int ns_reply_len;
        DNS_HEADER *relay_hdr;

        DEBUG1("中继: %s -> 外部DNS", qname);

        /* 分配新TID */
        new_id = get_new_tid(ntohs(hdr->id), client_addr);

        /* 复制查询报文, 替换ID */
        memcpy(relay_req, req, req_len);
        relay_len = req_len;
        relay_hdr = (DNS_HEADER *)relay_req;
        relay_hdr->id = htons(new_id);

        /* 发送到外部DNS */
        relay_to_ns(ns_sock, &reply_addr, relay_req, relay_len);

        /* 等待回复 */
        ns_reply_len = wait_ns_response(ns_sock, ns_reply,
                                        sizeof(ns_reply),
                                        &reply_addr, TIMEOUT_SEC);

        if (ns_reply_len < 0) {
            if (ns_reply_len == -2) {
                DEBUG1("中继超时: %s", qname);
                /* 超时的条目已被 cleanup_stale_tids 清理 */
            }
            return -1;
        }

        /* 恢复原始ID和客户端地址 */
        if (ns_reply_len >= (int)sizeof(DNS_HEADER)) {
            DNS_HEADER *reply_hdr = (DNS_HEADER *)ns_reply;
            unsigned short reply_new_id = ntohs(reply_hdr->id);

            if (restore_original(reply_new_id, &orig_id, client_addr)) {
                reply_hdr->id = htons(orig_id);
            } else {
                /* 可能是迟到的响应, 丢弃 */
                DEBUG1("丢弃迟到的外部DNS响应 (ID=%u)", reply_new_id);
                return -1;
            }
        }

        /* 转发回客户端 */
        sendto(server_sock, (const char *)ns_reply, ns_reply_len, 0,
               (struct sockaddr *)client_addr, sizeof(*client_addr));

        DEBUG2("中继返回: %s (%d bytes)", qname, ns_reply_len);
        return 0;
    }
}

/* ============================================================
 * 主函数
 * ============================================================ */
int main(int argc, char *argv[])
{
    SOCKET server_sock = INVALID_SOCKET;
    SOCKET ns_sock     = INVALID_SOCKET;
    struct sockaddr_in server_addr;
    struct sockaddr_in ns_addr;
    char config_file[256] = "dnsrelay.txt";
    char ns_ip[32]        = "202.106.0.20";
    unsigned char recvbuf[BUFFER_SIZE];
    int recv_len;
    struct sockaddr_in client_addr;
    socklen_t client_len;
    int optval;
    int i;

    /* ---- 解析命令行参数 ---- */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            debug_level = 1;
        } else if (strcmp(argv[i], "-dd") == 0) {
            debug_level = 2;
        } else if (argv[i][0] != '-') {
            /* 第一个非选项参数是外部DNS IP */
            strncpy(ns_ip, argv[i], sizeof(ns_ip) - 1);
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                strncpy(config_file, argv[i + 1], sizeof(config_file) - 1);
            }
            break;
        }
    }

    printf("========================================\n");
    printf("  DNS中继服务器 v1.0\n");
    printf("  外部DNS: %s\n", ns_ip);
    printf("  配置文件: %s\n", config_file);
    printf("  调试级别: %d\n", debug_level);
    printf("========================================\n");

    /* ---- 初始化 Socket ---- */
    if (init_winsock() != 0) {
        printf("初始化Winsock失败\n");
        return 1;
    }

    /* ---- 创建 UDP Socket ---- */
    server_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_sock == INVALID_SOCKET) {
        printf("创建服务器Socket失败\n");
        cleanup_winsock();
        return 1;
    }

    ns_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (ns_sock == INVALID_SOCKET) {
        printf("创建外部DNS Socket失败\n");
        closesocket(server_sock);
        cleanup_winsock();
        return 1;
    }

    /* ---- 设置地址复用 ---- */
    optval = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&optval, sizeof(optval));

    /* ---- 绑定服务器 ---- */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port        = htons(DNS_PORT);

    if (bind(server_sock, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == SOCKET_ERROR) {
        printf("绑定端口 %d 失败 (可能已被占用)\n", DNS_PORT);
        printf("请以管理员身份运行, 或检查端口是否被占用\n");
        closesocket(server_sock);
        closesocket(ns_sock);
        cleanup_winsock();
        return 1;
    }

    /* ---- 设置外部DNS地址 ---- */
    memset(&ns_addr, 0, sizeof(ns_addr));
    ns_addr.sin_family      = AF_INET;
    ns_addr.sin_port        = htons(DNS_PORT);
    ns_addr.sin_addr.s_addr = inet_addr(ns_ip);

    if (ns_addr.sin_addr.s_addr == INADDR_NONE) {
        printf("无效的DNS服务器地址: %s\n", ns_ip);
        closesocket(server_sock);
        closesocket(ns_sock);
        cleanup_winsock();
        return 1;
    }

    /* ---- 加载本地域名表 ---- */
    load_domain_table(config_file);
    if (debug_level >= 1) {
        print_domain_list();
    }

    printf("\nDNS中继服务器已启动, 监听端口 %d ...\n\n", DNS_PORT);
    printf("提示: 将计算机的DNS设为 127.0.0.1 即可使用本服务\n\n");

    /* ---- 主循环: 接收并处理客户端请求 ---- */
    while (1) {
        fd_set readfds;
        struct timeval tv;
        int ret;

        FD_ZERO(&readfds);
        FD_SET(server_sock, &readfds);

        /* 使用 select 实现多路复用, 这也是非忙等待的关键 */
        tv.tv_sec  = 5;   /* 5秒超时, 用于定期清理TID表 */
        tv.tv_usec = 0;

        ret = select((int)server_sock + 1, &readfds, NULL, NULL, &tv);

        if (ret == SOCKET_ERROR) {
            DEBUG1("select() 错误");
            break;
        }

        if (ret == 0) {
            /* 超时: 定期清理过期的TID映射 */
            cleanup_stale_tids();
            continue;
        }

        /* 收到客户端请求 */
        client_len = sizeof(client_addr);
        recv_len = recvfrom(server_sock, (char *)recvbuf,
                            sizeof(recvbuf), 0,
                            (struct sockaddr *)&client_addr,
                            &client_len);

        if (recv_len == SOCKET_ERROR) {
            DEBUG1("recvfrom 错误");
            continue;
        }

        if (recv_len < (int)sizeof(DNS_HEADER)) {
            continue;  /* 报文太短 */
        }

        /* 处理请求 */
        handle_client_request(server_sock, ns_sock,
                              recvbuf, recv_len, &client_addr);
    }

    /* ---- 清理 ---- */
    closesocket(server_sock);
    closesocket(ns_sock);
    cleanup_winsock();

    return 0;
}

    
