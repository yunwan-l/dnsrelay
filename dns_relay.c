/*
 * dns_relay.c -- DNS中继服务器主程序
 *
 * 课程要求的基本功能：
 *   1) 本地解析：命中普通IP时直接构造A记录响应。
 *   2) 域名拦截：表中IP为0.0.0.0时返回NXDOMAIN。
 *   3) DNS中继：本地表未命中时转发外部DNS并返回结果。
 *   4) 并发处理：为上游查询分配新ID，响应回来后恢复客户端原始ID。
 *   5) 超时处理：UDP中继无响应时重试备用上游或返回SERVFAIL。
 *
 * 扩展功能：
 *   DNS缓存、多上游自动切换、日志文件、运行统计、Linux热重载和清缓存。
 *
 * 配置文件优先使用老师参考格式：
 *   IP地址 域名
 * 例如：
 *   14.215.177.38 www.baidu.com
 *   0.0.0.0 008.cn
 */

#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "dns.h"
#include "dns_table.h"
#include "dns_packet.h"
#include "tid_map.h"
#include "dns_cache.h"
#include "upstream.h"
#include "stats.h"
#include "config_reload.h"

#ifdef _WIN32
#include <conio.h>
#endif

/* 全局调试等级：0无调试，1基础日志，2详细日志。 */
int debug_level = 0;

/* 可选日志文件，-l <file> 启用。 */
static FILE *g_logfile = NULL;

/* 主要日志输出：同时写控制台和日志文件。 */
#define LOG(fmt, ...) \
    do { \
        printf(fmt, ##__VA_ARGS__); \
        if (g_logfile) { \
            fprintf(g_logfile, fmt, ##__VA_ARGS__); \
            fflush(g_logfile); \
        } \
    } while (0)

/* 跨平台网络初始化和非阻塞设置。 */
static int init_winsock(void)
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

static void cleanup_winsock(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

static int set_nonblocking(SOCKET s)
{
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode);
#else
    int flags = fcntl(s, F_GETFL, 0);
    return fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif
}

/* 读取文件修改时间，失败返回-1。 */
/* 打印命令行帮助。 */
static void print_usage(const char *prog)
{
    (void)prog;
    printf("Usage: dnsrelay [options] [upstream] [config-file]\n");
    printf("Options:\n");
    printf("  -d          Debug level 1 (basic info)\n");
    printf("  -dd         Debug level 2 (verbose)\n");
    printf("  -l <file>   Log output to file\n");
    printf("  -h, --help  Show this help\n");
    printf("\n");
    printf("Upstream DNS servers (comma-separated for failover):\n");
    printf("  dnsrelay -d 8.8.8.8,114.114.114.114\n");
    printf("  Default: 202.106.0.20\n");
    printf("\n");
    printf("Runtime:\n");
    printf("  Press 's'          -> Print statistics\n");
    printf("  Press 'c'          -> Clear DNS cache\n");
    printf("  Press 'r'          -> Reload domain table\n");
    printf("  Save config file   -> Auto reload when file changes\n");
    printf("  kill -HUP <pid>    -> Reload domain table (Linux)\n");
}

/* Linux下非阻塞读取标准输入，用于运行时统计和清缓存命令。 */
static int try_read_stdin(void)
{
#ifdef _WIN32
    int c;

    if (!_kbhit())
        return -1;

    c = _getch();
    if (c == 0 || c == 224) {
        if (_kbhit())
            (void)_getch();
        return -1;
    }
    return c;
#else
    fd_set rfds;
    struct timeval tv = {0, 0};

    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    if (select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) <= 0)
        return -1;

    char c;
    if (read(STDIN_FILENO, &c, 1) == 1)
        return (unsigned char)c;
    return -1;
#endif
}

/* 从DNS报文中安全提取查询域名，用于日志和超时处理。 */
static void extract_qname(const unsigned char *packet, int len,
                          char *out, int out_size)
{
    if (len < (int)sizeof(DNS_HEADER) + 1) {
        out[0] = '\0';
        return;
    }
    dns_parse_qname(packet, len, sizeof(DNS_HEADER), out, out_size);
}

/* 构造本地响应并发回客户端：普通命中返回A记录，拦截返回NXDOMAIN。 */
static void send_local_response(SOCKET server_sock,
                                const unsigned char *query, int query_len,
                                struct sockaddr_in *client,
                                uint32_t answer_ip, int is_blocked)
{
    unsigned char resp[BUFFER_SIZE];
    int resp_len = 0;

    if (is_blocked) {
        g_stats.blocked++;
        resp_len = dns_build_response(query, query_len, resp,
                                      RCODE_NXDOMAIN, 0);
    } else {
        g_stats.local_hits++;
        resp_len = dns_build_response(query, query_len, resp,
                                      RCODE_OK, answer_ip);
    }

    if (resp_len > 0) {
        sendto(server_sock, (const char *)resp, resp_len, 0,
               (struct sockaddr *)client, sizeof(*client));
    }
}

static void make_cache_key(char *out, int out_size, const char *qname,
                           uint16_t qtype, uint16_t qclass)
{
    /* 缓存必须区分QTYPE/QCLASS，避免A和AAAA等记录互相污染。 */
    snprintf(out, (size_t)out_size, "%s|%u|%u",
             qname, (unsigned)qtype, (unsigned)qclass);
}

/* 把客户端的原始查询报文，ID 替换成新分配的 ID，然后发送给当前上游 DNS 服务器 */
static int send_packet_to_upstream(SOCKET ns_sock,//上游socket
                                   const unsigned char *query,//原始查询报文
                                    int query_len,//报文长度
                                   uint16_t new_id)//分配的上游ID
{
    unsigned char relay_buf[BUFFER_SIZE];
    DNS_HEADER *relay_hdr;
    struct sockaddr_in *ns;

    if (query_len <= 0 || query_len > BUFFER_SIZE)
        return -1;

    ns = upstream_current();//获取当前上游服务器地址
    if (!ns)
        return -1;

    memcpy(relay_buf, query, query_len);//复制原始查询报文到中继缓冲区
    relay_hdr = (DNS_HEADER *)relay_buf;
    relay_hdr->id = htons(new_id);//替换事务ID为分配的上游ID
    relay_hdr->rd = 1;//设置递归查询标志，确保上游DNS返回最终结果


    if (upstream_current_is_test_blackhole()) {
        DEBUG1("Simulated timeout upstream: %s",
               inet_ntoa(ns->sin_addr));
        return query_len;
    }//如果当前上游服务器在测试网范围内，模拟超时行为，直接返回查询长度，避免发送实际报文

    return sendto(ns_sock, (const char *)relay_buf, query_len, 0,
                  (struct sockaddr *)ns, sizeof(*ns));//发送修改后的查询报文到当前上游DNS服务器
}

/* 只接受当前上游DNS返回的报文，丢弃迟到或来源异常的响应。 */
static int is_current_upstream_response(const struct sockaddr_in *resp_addr)
{
    struct sockaddr_in *ns = upstream_current();

    if (!ns)
        return 0;

    return resp_addr->sin_family == AF_INET &&
           resp_addr->sin_addr.s_addr == ns->sin_addr.s_addr &&
           resp_addr->sin_port == ns->sin_port;
}

/* 为客户端查询建立TID映射，并转发到上游DNS。 */
static int relay_client_query(SOCKET ns_sock,
                              const unsigned char *query, int query_len,
                              struct sockaddr_in *client,
                              const char *qname//解析出来的域名
                             )
{
    DNS_HEADER *hdr = (DNS_HEADER *)query;
    uint16_t orig_id = ntohs(hdr->id);//提取原始查询报文中的事务ID
    uint16_t new_id = tid_map_alloc(orig_id, client, query, query_len);//分配新ID
    struct sockaddr_in *ns = upstream_current();//获取当前上游服务器地址

    if (new_id == 0) {
        LOG("[%ld] ERROR  TID table full, cannot relay %s\n",
            (long)time(NULL), qname);
        g_stats.errors++;
        return -1;
    }//如果分配新ID失败，记录错误并返回

    if (!ns || send_packet_to_upstream(ns_sock, query, query_len, new_id) ==
        SOCKET_ERROR) {
        LOG("[%ld] ERROR  RELAY sendto failed for %s\n",
            (long)time(NULL), qname);
        tid_map_remove(new_id);
        g_stats.errors++;
        return -1;
    }//没有上游或者发送失败，记录错误，移除TID映射并返回

    g_stats.relayed++;//统计转发次数
    LOG("[%ld] RELAY  %s -> %s\n",
        (long)time(NULL), qname, inet_ntoa(ns->sin_addr));
    return 0;
}

/* 上游全部失败时，向客户端返回SERVFAIL，避免客户端一直等待。 */
static void send_servfail_response(SOCKET server_sock,
                                   const unsigned char *query, int query_len,
                                   struct sockaddr_in *client)
{
    unsigned char resp[BUFFER_SIZE];
    int resp_len = dns_build_servfail(query, query_len, resp);

    if (resp_len > 0) {
        sendto(server_sock, (const char *)resp, resp_len, 0,
               (struct sockaddr *)client, sizeof(*client));
    }
}

/* 检查超时的中继请求：有备用上游则重发，否则返回SERVFAIL。 */
static void handle_relay_timeouts(SOCKET server_sock,//本地socket
                                  SOCKET ns_sock//上游socket
                              )
{
    TidTimeout timeout;
    int guard = 0;

    while (guard++ < MAX_CLIENTS && tid_map_get_timeout(&timeout)) {
        char qname[DOMAIN_MAX];
        int total_upstreams = upstream_count();

        extract_qname(timeout.query, timeout.query_len,
                      qname, sizeof(qname));//从原始查询报文中提取查询域名，供日志使用
        if (qname[0] == '\0')//提取失败时使用默认值
            strncpy(qname, "(unknown)", sizeof(qname) - 1);
        qname[sizeof(qname) - 1] = '\0';

        g_stats.relay_timeouts++;

        if (total_upstreams > 1 && timeout.attempts < total_upstreams) {
            upstream_failover();//切换到下一个上游服务器
            if (send_packet_to_upstream(ns_sock, timeout.query,
                                        timeout.query_len,
                                        timeout.new_id) != SOCKET_ERROR) {
                tid_map_mark_retry(timeout.new_id);//标记重试次数增加，并更新时间戳，避免过快再次超时
                LOG("[%ld] TIMEOUT %s, retry upstream #%d\n",
                    (long)time(NULL), qname, upstream_index());
                continue;
            }

            g_stats.errors++;//如果重试发送失败，记录错误
            LOG("[%ld] ERROR  Retry sendto failed for %s\n",
                (long)time(NULL), qname);
        }

        send_servfail_response(server_sock, timeout.query,
                               timeout.query_len, &timeout.client);//所有上游都尝试过后返回SERVFAIL
        tid_map_remove(timeout.new_id);
        LOG("[%ld] SERVFAIL %s after %d attempt(s)\n",
            (long)time(NULL), qname, timeout.attempts);//所有上游都尝试过后返回SERVFAIL，并记录日志
    }
}//guard 保证最多循环 256 次，不会死循环。

/* 主程序入口。 */
int main(int argc, char *argv[])
{
    SOCKET server_sock, ns_sock;
    struct sockaddr_in server_addr, client_addr;
    unsigned char recv_buf[BUFFER_SIZE];
    unsigned char send_buf[BUFFER_SIZE];
    unsigned char ns_buf[BUFFER_SIZE];
    int recv_len;

    /* 默认参数，与老师参考实现保持一致。 */
    char *ns_ip_str = "202.106.0.20";
    char *config_file = "dnsrelay.txt";
    char *log_file = NULL;

    /* 解析命令行参数。 */
    {
        int i = 1;
        char *next_arg = NULL;

        while (i < argc) {
            if (strcmp(argv[i], "-d") == 0) {
                debug_level = 1;
            } else if (strcmp(argv[i], "-dd") == 0) {
                debug_level = 2;
            } else if (strcmp(argv[i], "-l") == 0) {
                if (i + 1 < argc)
                    log_file = argv[++i];
            } else if (strcmp(argv[i], "-h") == 0 ||
                       strcmp(argv[i], "--help") == 0) {
                print_usage(argv[0]);
                return 0;
            } else if (next_arg == NULL) {
                next_arg = argv[i];
            } else {
                config_file = argv[i];
            }
            i++;
        }

        if (next_arg) {
            ns_ip_str = next_arg;
        }
    }

    /* 打开日志文件。 */
    if (log_file) {
        g_logfile = fopen(log_file, "a");
        if (g_logfile) {
            time_t now = time(NULL);
            fprintf(g_logfile, "\n=== DNS Relay started at %s",
                    ctime(&now));
            fflush(g_logfile);
        } else {
            printf("[WARN] Cannot open log file: %s\n", log_file);
        }
    }

    /* 初始化网络库。 */
    if (init_winsock() != 0) {
        printf("Failed to initialize network\n");
        return 1;
    }

    /* 初始化域名表、TID映射、缓存和统计模块。 */
    if (domain_table_load(config_file) < 0) {
        printf("Failed to load domain table. Exiting.\n");
        cleanup_winsock();
        return 1;
    }
    runtime_reload_mark_loaded(config_file);

    tid_map_init();
    dns_cache_init();
    stats_init();

    /* 解析逗号分隔的上游DNS列表。 */
    {
        char buf[256];
        strncpy(buf, ns_ip_str, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        char *token = strtok(buf, ",");
        int first = 1;
        while (token) {
            while (*token == ' ') token++;
            char *end = token + strlen(token) - 1;
            while (end > token && *end == ' ') *end-- = '\0';

            int a, b, c, d;
            if (sscanf(token, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
                if (first) {
                    upstream_init(token);
                    first = 0;
                } else {
                    upstream_add(token);
                }
            }
            token = strtok(NULL, ",");
        }
        if (first) {
            /* 没有解析到任何合法上游DNS。 */
            printf("Invalid upstream DNS IP: %s\n", ns_ip_str);
            if (g_logfile) fclose(g_logfile);
            cleanup_winsock();
            return 1;
        }
    }

    if (debug_level >= 2)
        domain_table_print();

    /* 运行时重载钩子：Linux支持SIGHUP，Windows依靠文件时间戳轮询。 */
    runtime_reload_setup();

    /* 创建本地DNS服务socket，监听UDP 53端口。 */
    server_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_sock == INVALID_SOCKET) {
        printf("Failed to create server socket\n");
        if (g_logfile) fclose(g_logfile);
        cleanup_winsock();
        return 1;
    }

    {
        int optval = 1;
        setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR,
                   (const char *)&optval, sizeof(optval));
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(DNS_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_sock, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == SOCKET_ERROR) {
        LOG("[ERROR] Bind port %d failed! (Try running as root)\n", DNS_PORT);
        closesocket(server_sock);
        if (g_logfile) fclose(g_logfile);
        cleanup_winsock();
        return 1;
    }

    /* 创建连接上游DNS的UDP socket。 */
    ns_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (ns_sock == INVALID_SOCKET) {
        printf("Failed to create upstream socket\n");
        closesocket(server_sock);
        if (g_logfile) fclose(g_logfile);
        cleanup_winsock();
        return 1;
    }

    /* 打印启动信息。 */
    {
        struct sockaddr_in *ns = upstream_current();
        LOG("\n========================================\n");
        LOG("  DNS Relay Server v2.0\n");
        LOG("========================================\n");
        LOG("  Listening   : port %d (0.0.0.0)\n", DNS_PORT);
        if (ns)
            LOG("  Upstream    : %s:53\n", inet_ntoa(ns->sin_addr));
        LOG("  Config file : %s (%d records)\n", config_file, domain_count);
        LOG("  Max clients : %d\n", MAX_CLIENTS);
        LOG("  DNS cache   : %d entries\n", DNS_CACHE_MAX);
        LOG("  Debug level : %d\n", debug_level);
        if (log_file)
            LOG("  Log file    : %s\n", log_file);
#ifndef _WIN32
        LOG("  PID         : %d\n", getpid());
    LOG("  Hot reload  : kill -HUP %d\n", getpid());
    LOG("  Hot reload  : press 'r' to reload config now\n");
    LOG("  Stats       : press 's' to print statistics\n");
#endif
        LOG("========================================\n\n");
    }

    /* 设置非阻塞模式，配合select避免忙等待。 */
    set_nonblocking(server_sock);
    set_nonblocking(ns_sock);

    /* 主事件循环：select统一等待客户端、上游和运行时命令。 */
    while (1) {
        fd_set readfds;//文件描述符集合
        struct timeval tv;//select等待的超时时间
        int max_fd;
        int sel_ret;//select返回值

        FD_ZERO(&readfds);//清空文件描述符集合
        FD_SET(server_sock, &readfds);//监视本地DNS服务socket的可读事件，即客户端查询到达
        FD_SET(ns_sock, &readfds);//监视上游DNS socket的可读事件，即上游DNS响应到达
        max_fd = (server_sock > ns_sock) ? (int)server_sock : (int)ns_sock;//计算当前最大的文件描述符值，供select使用
        max_fd++;//select要求的参数是最大文件描述符值加1

        tv.tv_sec  = 1;//超时1s
        tv.tv_usec = 0;

        sel_ret = select(max_fd, &readfds, NULL, NULL, &tv);//等待任一事件发生或超时

        if (sel_ret == SOCKET_ERROR) {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err == WSAEINTR) continue;
            DEBUG1("select() error: %d", err);//Windows下select被信号中断时返回WSAEINTR，继续等待；其他错误则记录日志
#else
            if (errno == EINTR) {
                if (g_reload_requested) {
                    runtime_reload_force(config_file, g_logfile,
                        "\n--- Reloading domain table from SIGHUP ---");
                }
                continue;
            }
            DEBUG1("select() error: %d", errno);
#endif
            handle_relay_timeouts(server_sock, ns_sock);//在select错误时也检查中继超时，避免遗漏超时处理
            dns_cache_cleanup();//在select错误时也清理过期缓存，保持缓存健康
            continue;
        }

        /* 运行时命令：s打印统计，c清空缓存，r强制重载配置。 */
        {
            int c = try_read_stdin();

            if (c == 's' || c == 'S') {
                stats_print();//打印统计信息
                printf("  Active TIDs   : %d / %d\n",tid_map_count(), MAX_CLIENTS);//显示当前TID映射表中的活跃条目数量
                printf("  Domain table  : %d entries\n", domain_count);//显示当前域名表中的记录数量
                printf("  DNS cache     : %d / %d entries\n",dns_cache_count(), DNS_CACHE_MAX);//显示当前DNS缓存中的条目数量
                upstream_print();//显示当前上游DNS服务器列表和状态
                printf("========================================\n\n");
            } else if (c == 'c' || c == 'C') {
                dns_cache_free();
                printf("[CMD] DNS cache cleared.\n");
            } else if (c == 'r' || c == 'R') {
                runtime_reload_force(config_file, g_logfile,
                                     "\n--- Reloading domain table by command ---");
            }
        }

        /* select超时tick：检查中继超时并清理过期缓存。 */
        if (sel_ret == 0) {
            handle_relay_timeouts(server_sock, ns_sock);
            dns_cache_cleanup();
            goto handle_reload_check;//跳去热重载
        }

        /* 处理客户端DNS查询。 */
        if (FD_ISSET(server_sock, &readfds)) {
            socklen_t addr_len = sizeof(client_addr);
            recv_len = recvfrom(server_sock, (char *)recv_buf,
                                sizeof(recv_buf), 0,
                                (struct sockaddr *)&client_addr, &addr_len);//接收客户端查询报文，存储发送者地址信息
            if (recv_len == SOCKET_ERROR)
                continue;

            if (recv_len < (int)sizeof(DNS_HEADER) + 5)
                continue;

            DNS_HEADER *hdr = (DNS_HEADER *)recv_buf;//解析DNS报文头部，准备检查查询类型和域名

            if (hdr->qr != 0 || hdr->opcode != 0)//只处理标准查询报文
                continue;

            g_stats.total_queries++;//统计收到的查询总数

            /* 解析查询域名、类型和类别。 */
            char qname[DOMAIN_MAX];
            char cache_key[DNS_CACHE_KEY_MAX];
            uint16_t qtype = 0, qclass = 0;

            dns_parse_qname(recv_buf, recv_len, sizeof(DNS_HEADER),qname, sizeof(qname));//从查询报文中提取查询域名，存储在qname变量中
            if (!dns_parse_question(recv_buf, recv_len, &qtype, &qclass)) {
                g_stats.errors++;
                continue;
            }//如果解析失败，记录错误并跳过处理
            make_cache_key(cache_key, sizeof(cache_key),
                           qname, qtype, qclass);

            const char *type_str = "A";
            if (qtype == DNS_TYPE_AAAA)  type_str = "AAAA";
            else if (qtype == DNS_TYPE_CNAME) type_str = "CNAME";
            else if (qtype == DNS_TYPE_MX)    type_str = "MX";
            else if (qtype == DNS_TYPE_NS)    type_str = "NS";

            LOG("[%ld] QUERY  ID=%u, %s %s\n",
                (long)time(NULL), ntohs(hdr->id), type_str, qname);

            /* 第一步：查本地域名表。 */
            uint32_t answer_ip;//查询结果IP地址，0表示未命中或被拦截
            int search_result = domain_table_search(qname, &answer_ip);

            if (search_result >= 0) {
                if (search_result == 1) //拦截
                {
                    LOG("[%ld] BLOCK  %s\n", (long)time(NULL), qname);
                    send_local_response(server_sock, recv_buf, recv_len,
                                        &client_addr, 0, 1);//构造并发送NXDOMAIN响应，告知客户端该域名不存在
                } 
                //普通命中
                else {
                    if (qtype != DNS_TYPE_A || qclass != DNS_CLASS_IN) {
                        if (relay_client_query(ns_sock, recv_buf, recv_len,&client_addr, qname) < 0)//如果转发失败则返回SERVFAIL
                        {
                            send_servfail_response(server_sock, recv_buf,
                                                   recv_len, &client_addr);
                        }
                        continue;
                    }//如果查询类型不是A记录或查询类别不是IN类，转发到上游DNS服务器
                    struct in_addr addr;
                    addr.s_addr = answer_ip;
                    LOG("[%ld] HIT    %s -> %s\n",
                        (long)time(NULL), qname, inet_ntoa(addr));
                    send_local_response(server_sock, recv_buf, recv_len,
                                        &client_addr, answer_ip, 0);
                }//构造并发送本地普通响应，包含查询结果IP地址
                continue;
            }

            /* 第二步：查DNS缓存。 */
            {
                int cache_len = BUFFER_SIZE;
                if (dns_cache_get(cache_key, send_buf, &cache_len)) {
                    g_stats.cache_hits++;
                    LOG("[%ld] CACHE  %s (from cache, %d bytes)\n",
                        (long)time(NULL), cache_key, cache_len);
                    DNS_HEADER *cache_hdr = (DNS_HEADER *)send_buf;
                    cache_hdr->id = hdr->id;
                    sendto(server_sock, (const char *)send_buf, cache_len, 0,
                           (struct sockaddr *)&client_addr, sizeof(client_addr));
                    continue;
                }
            }//如果缓存命中，构造响应报文并发送给客户端，统计缓存命中次数，并跳过后续处理

            /* 第三步：本地和缓存均未命中，转发上游DNS。 */
            if (relay_client_query(ns_sock, recv_buf, recv_len,
                                   &client_addr, qname) < 0) {
                send_servfail_response(server_sock, recv_buf, recv_len,
                                       &client_addr);
            }
        }

        /* 处理上游DNS响应。 */
        if (FD_ISSET(ns_sock, &readfds)) {
            struct sockaddr_in ns_resp_addr;//接收上游DNS响应的地址
            socklen_t ns_addr_len = sizeof(ns_resp_addr);

            int ns_resp_len = recvfrom(ns_sock, (char *)ns_buf,
                                       sizeof(ns_buf), 0,
                                       (struct sockaddr *)&ns_resp_addr,
                                       &ns_addr_len);//接收上游DNS响应报文，存储地址信息
            if (ns_resp_len == SOCKET_ERROR)
                continue;

            if (ns_resp_len < (int)sizeof(DNS_HEADER))
                continue;

            if (!is_current_upstream_response(&ns_resp_addr)) {
                DEBUG1("Discarded response from unexpected upstream %s:%d",
                       inet_ntoa(ns_resp_addr.sin_addr),
                       ntohs(ns_resp_addr.sin_port));
                continue;
            }//验证响应来源是否为当前上游DNS服务器，丢弃来源异常的响应

            DNS_HEADER *ns_resp_hdr = (DNS_HEADER *)ns_buf;
            uint16_t ns_id = ntohs(ns_resp_hdr->id);

            uint16_t orig_id_back;
            struct sockaddr_in orig_client;

            if (tid_map_restore(ns_id, &orig_id_back, &orig_client)) {
                g_stats.relay_responses++;

                /* 上游成功响应后，如发生过切换则恢复主上游。 */
                upstream_success();

               
                ns_resp_hdr->id = htons(orig_id_back);//将上游响应报文中的事务ID替换回客户端原始ID

                /* 缓存成功的上游响应。 */
                {
                    char cache_qname[DOMAIN_MAX];
                    char response_cache_key[DNS_CACHE_KEY_MAX];
                    uint16_t cache_qtype = 0, cache_qclass = 0;
                    extract_qname(ns_buf, ns_resp_len, cache_qname,
                                  sizeof(cache_qname));
                    if (cache_qname[0] != '\0' &&ns_parse_question(ns_buf, ns_resp_len,&cache_qtype, &cache_qclass)
                     &&ns_resp_hdr->rcode == RCODE_OK &&ntohs(ns_resp_hdr->ancount) > 0)//只有当成功解析出查询域名、类型和类别，并且上游响应状态码为0（成功）且包含至少一个答案时，才考虑缓存该响应
                   {
                        uint32_t ttl = dns_extract_ttl(ns_buf, ns_resp_len);//从上游响应报文中提取TTL值，决定缓存有效期
                        if (ttl > 0) {
                            make_cache_key(response_cache_key,
                                           sizeof(response_cache_key),
                                           cache_qname,
                                           cache_qtype, cache_qclass);
                            dns_cache_put(response_cache_key, ns_buf,
                                          ns_resp_len, ttl);
                        }
                   }
                }

                sendto(server_sock, (const char *)ns_buf, ns_resp_len, 0,
                       (struct sockaddr *)&orig_client,
                       sizeof(orig_client));//将上游响应报文发送回原始客户端地址

                LOG("[%ld] REPLY  forwarded (cached)\n", (long)time(NULL));
            } else {
                /* 迟到响应或无映射响应，直接丢弃。 */
                DEBUG2("Discarded upstream response (stale TID=%u)", ns_id);
            }
        }

handle_reload_check:
    ;
    runtime_reload_poll(config_file, g_logfile);//在每次事件循环结束时检查配置文件是否有修改，如果有则自动重载域名表，并记录日志。
    }

    /* 正常情况下不会执行到这里。 */
    closesocket(server_sock);
    closesocket(ns_sock);
    if (g_logfile) fclose(g_logfile);
    cleanup_winsock();
    return 0;//程序结束，关闭socket和日志文件，清理网络库资源
}
