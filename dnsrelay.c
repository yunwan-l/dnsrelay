#include "dnsrelay.h"
#include "dns_protocol.h"
#include "domain_table.h"
#include "relay_engine.h"

int g_debug_level = 0;

typedef struct {
    char upstream_ip[IP_STR_MAX];
    char table_file[260];
} ProgramOptions;

static void print_usage(const char *program_name)
{
    printf("用法:\n");
    printf("  %s [ -d | -dd ] [ dns-server-ipaddr ] [ filename ]\n", program_name);
    printf("\n示例:\n");
    printf("  %s\n", program_name);
    printf("  %s -d\n", program_name);
    printf("  %s -dd 8.8.8.8\n", program_name);
    printf("  %s -d 8.8.8.8 C:\\\\dnsrelay.txt\n", program_name);
}

static int parse_args(int argc, char *argv[], ProgramOptions *options)
{
    int i;

    memset(options, 0, sizeof(*options));
    strncpy(options->upstream_ip, DEFAULT_UPSTREAM_DNS,
            sizeof(options->upstream_ip) - 1);
    strncpy(options->table_file, DEFAULT_TABLE_FILE,
            sizeof(options->table_file) - 1);

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            g_debug_level = 1;
        } else if (strcmp(argv[i], "-dd") == 0) {
            g_debug_level = 2;
        } else if (argv[i][0] == '-') {
            printf("未知参数: %s\n", argv[i]);
            print_usage(argv[0]);
            return -1;
        } else {
            strncpy(options->upstream_ip, argv[i],
                    sizeof(options->upstream_ip) - 1);

            if (i + 1 < argc && argv[i + 1][0] != '-') {
                strncpy(options->table_file, argv[i + 1],
                        sizeof(options->table_file) - 1);
            }
            break;
        }
    }

    return 0;
}

static int init_socket_env(void)
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

static void cleanup_socket_env(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

static int parse_ipv4_address(const char *ip_text, struct sockaddr_in *addr)
{
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(DNS_PORT);

#ifdef _WIN32
    addr->sin_addr.s_addr = inet_addr(ip_text);
    return (addr->sin_addr.s_addr == INADDR_NONE) ? -1 : 0;
#else
    return inet_pton(AF_INET, ip_text, &addr->sin_addr) == 1 ? 0 : -1;
#endif
}

static SOCKET create_server_socket(void)
{
    SOCKET server_socket;
    struct sockaddr_in listen_addr;
    int opt_value = 1;

    server_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_socket == INVALID_SOCKET) {
        printf("创建UDP Socket失败, error=%d\n", LAST_SOCKET_ERROR());
        return INVALID_SOCKET;
    }

    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&opt_value, sizeof(opt_value));

    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    listen_addr.sin_port = htons(DNS_PORT);

    if (bind(server_socket, (struct sockaddr *)&listen_addr,
             sizeof(listen_addr)) == SOCKET_ERROR) {
        printf("绑定端口 %d 失败 (可能被占用，或未以管理员身份运行)\n", DNS_PORT);
        closesocket(server_socket);
        return INVALID_SOCKET;
    }

    return server_socket;
}

static void print_banner(const ProgramOptions *options, const DomainTable *table)
{
    printf("========================================\n");
    printf("  DNS中继服务器\n");
    printf("  外部DNS: %s\n", options->upstream_ip);
    printf("  配置文件: %s\n", options->table_file);
    printf("  调试级别: %d\n", g_debug_level);
    printf("  本地域名数: %d\n", table->count);
    printf("  监听端口: %d\n", DNS_PORT);
    printf("========================================\n");
    printf("提示: 将系统DNS设置为 127.0.0.1 后即可测试。\n");
    printf("提示: 建议先执行 ipconfig /flushdns 清空本机DNS缓存。\n\n");
}

int main(int argc, char *argv[])
{
    ProgramOptions options;
    DomainTable domain_table;
    RelayContext relay_context;
    SOCKET server_socket = INVALID_SOCKET;
    struct sockaddr_in upstream_addr;
    unsigned char packet[BUFFER_SIZE];

    if (parse_args(argc, argv, &options) != 0) {
        return 1;
    }

    if (init_socket_env() != 0) {
        return 1;
    }

    if (parse_ipv4_address(options.upstream_ip, &upstream_addr) != 0) {
        printf("无效的外部DNS地址: %s\n", options.upstream_ip);
        cleanup_socket_env();
        return 1;
    }

    domain_table_init(&domain_table);
    load_domain_table(&domain_table, options.table_file);

    server_socket = create_server_socket();
    if (server_socket == INVALID_SOCKET) {
        cleanup_socket_env();
        return 1;
    }

    relay_context_init(&relay_context, &upstream_addr);
    print_banner(&options, &domain_table);

    if (g_debug_level >= 2) {
        print_domain_table(&domain_table);
    }

    while (1) {
        fd_set read_set;
        struct timeval timeout;
        int select_result;
        struct sockaddr_in source_addr;
        socket_length_t source_len = sizeof(source_addr);
        int packet_len;

        FD_ZERO(&read_set);
        FD_SET(server_socket, &read_set);

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        select_result = select((int)server_socket + 1, &read_set, NULL, NULL, &timeout);
        if (select_result == SOCKET_ERROR) {
            DEBUG1("select() 失败, error=%d", LAST_SOCKET_ERROR());
            break;
        }

        relay_cleanup_timeouts(&relay_context, server_socket);

        if (select_result == 0) {
            continue;
        }

        packet_len = recvfrom(server_socket, (char *)packet, sizeof(packet), 0,
                              (struct sockaddr *)&source_addr, &source_len);
        if (packet_len == SOCKET_ERROR) {
            DEBUG1("recvfrom() 失败, error=%d", LAST_SOCKET_ERROR());
            continue;
        }

        if (relay_is_upstream_response(&relay_context, &source_addr,
                                       packet, packet_len)) {
            relay_process_upstream_packet(&relay_context, server_socket,
                                          packet, packet_len);
        } else if (dns_packet_is_query(packet, packet_len)) {
            relay_process_client_packet(&relay_context, server_socket,
                                        &domain_table, packet, packet_len,
                                        &source_addr);
        } else {
            DEBUG2("忽略来源 %s:%u 的非查询报文",
                   inet_ntoa(source_addr.sin_addr),
                   ntohs(source_addr.sin_port));
        }
    }

    closesocket(server_socket);
    cleanup_socket_env();
    return 0;
}
