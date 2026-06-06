/*
 * upstream.c -- 上游DNS服务器切换实现
 */
#include "upstream.h"

static struct sockaddr_in upstream_list[UPSTREAM_MAX];
static int upstream_total = 0;
static int upstream_cur = 0;
static int has_failed_over = 0;

static int parse_ipv4_addr(const char *ip_str, struct in_addr *out_addr)
{
    return inet_pton(AF_INET, ip_str, out_addr) == 1;
}

static int is_test_net_addr(struct in_addr addr)
{
    uint32_t ip = ntohl(addr.s_addr);

    return (ip & 0xFFFFFF00u) == 0xC0000200u ||  /* 192.0.2.0/24，文档测试网 */
           (ip & 0xFFFFFF00u) == 0xC6336400u ||  /* 198.51.100.0/24，文档测试网 */
           (ip & 0xFFFFFF00u) == 0xCB007100u;    /* 203.0.113.0/24，文档测试网 */
}

void upstream_init(const char *default_ip)
{
    upstream_total = 0;
    upstream_cur = 0;
    has_failed_over = 0;

    if (default_ip)
        upstream_add(default_ip);
}

int upstream_add(const char *ip_str)
{
    if (upstream_total >= UPSTREAM_MAX)
        return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(53);

    if (!parse_ipv4_addr(ip_str, &addr.sin_addr)) {
        printf("[WARN] Invalid upstream IP: %s\n", ip_str);
        return -1;
    }

    upstream_list[upstream_total++] = addr;
    DEBUG2("Upstream[%d]: %s", upstream_total - 1, ip_str);
    return upstream_total - 1;
}

struct sockaddr_in *upstream_current(void)
{
    if (upstream_total == 0)
        return NULL;
    return &upstream_list[upstream_cur];
}

void upstream_failover(void)
{
    if (upstream_total <= 1)
        return;  /* 只有一个上游时无法切换 */

    int old = upstream_cur;
    upstream_cur = (upstream_cur + 1) % upstream_total;
    has_failed_over = 1;

    char old_ip[32], new_ip[32];
    strncpy(old_ip, inet_ntoa(upstream_list[old].sin_addr), sizeof(old_ip) - 1);
    old_ip[sizeof(old_ip) - 1] = '\0';
    strncpy(new_ip, inet_ntoa(upstream_list[upstream_cur].sin_addr), sizeof(new_ip) - 1);
    new_ip[sizeof(new_ip) - 1] = '\0';

    printf("[UPSTREAM] Failover: %s -> %s\n", old_ip, new_ip);
}

void upstream_success(void)
{
    if (has_failed_over && upstream_total > 1) {
        char first_ip[32];
        strncpy(first_ip, inet_ntoa(upstream_list[0].sin_addr), sizeof(first_ip) - 1);
        first_ip[sizeof(first_ip) - 1] = '\0';

        if (upstream_cur != 0) {
            printf("[UPSTREAM] Restored to primary: %s\n", first_ip);
        }
        has_failed_over = 0;
        upstream_cur = 0;
    }
}

int upstream_index(void)
{
    return upstream_cur;
}

int upstream_count(void)
{
    return upstream_total;
}

int upstream_current_is_test_blackhole(void)
{
    if (upstream_total == 0)
        return 0;
    return is_test_net_addr(upstream_list[upstream_cur].sin_addr);
}

void upstream_print(void)
{
    int i;
    printf("\nUpstream DNS servers (%d configured):\n", upstream_total);
    for (i = 0; i < upstream_total; i++) {
        printf("  [%d] %s %s\n", i,
               inet_ntoa(upstream_list[i].sin_addr),
               (i == upstream_cur) ? "<<< ACTIVE" : "");
    }
    printf("\n");
}
