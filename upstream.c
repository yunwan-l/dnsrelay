/*
 * upstream.c -- 上游DNS服务器切换实现
 */
#include "upstream.h"

static struct sockaddr_in upstream_list[UPSTREAM_MAX];
static int upstream_total = 0;
static int upstream_cur = 0;//当前正在使用的上游服务器索引
static int has_failed_over = 0;//是否已经切换过服务器

static int parse_ipv4_addr(const char *ip_str, struct in_addr *out_addr)//把点分十进制IPv4字符串转换为网络字节序整数
{
    unsigned char bytes[4];

    if (ip_str_to_bytes(ip_str, bytes) != 0)
        return 0;

    out_addr->s_addr = htonl(((uint32_t)bytes[0] << 24) |
                             ((uint32_t)bytes[1] << 16) |
                             ((uint32_t)bytes[2] << 8) |
                             (uint32_t)bytes[3]);
    return 1;
}

static int is_test_net_addr(struct in_addr addr)//检查IP地址是否在文档测试网范围内
{
    uint32_t ip = ntohl(addr.s_addr);//转换为主机字节序方便比较

    return (ip & 0xFFFFFF00u) == 0xC0000200u ||  /* 192.0.2.0/24，文档测试网 */
           (ip & 0xFFFFFF00u) == 0xC6336400u ||  /* 198.51.100.0/24，文档测试网 */
           (ip & 0xFFFFFF00u) == 0xCB007100u;    /* 203.0.113.0/24，文档测试网 */
}

void upstream_init(const char *default_ip)//上游服务器列表初始化，默认上游服务器IP地址可选
{
    upstream_total = 0;
    upstream_cur = 0;
    has_failed_over = 0;

    if (default_ip)//如果提供了默认上游服务器IP地址，尝试添加到列表
        upstream_add(default_ip);
}

int upstream_add(const char *ip_str)//添加一个上游服务器，返回新服务器的索引，失败返回-1
{
    if (upstream_total >= UPSTREAM_MAX)
        return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));//先清零结构体
    addr.sin_family = AF_INET;//地址族是IPv4
    addr.sin_port   = htons(53);//默认DNS端口是53

    if (!parse_ipv4_addr(ip_str, &addr.sin_addr)) {
        printf("[WARN] Invalid upstream IP: %s\n", ip_str);
        return -1;
    }//解析IP地址失败，可能格式不正确

    upstream_list[upstream_total++] = addr;//添加到列表末尾
    DEBUG2("Upstream[%d]: %s", upstream_total - 1, ip_str);
    return upstream_total - 1;//返回新服务器的索引
}

struct sockaddr_in *upstream_current(void)//返回当前正在使用的上游服务器地址，若列表为空返回NULL
{
    if (upstream_total == 0)
        return NULL;
    return &upstream_list[upstream_cur];
}

void upstream_failover(void)//切换到下一个上游服务器，轮询方式，如果只有一个服务器则不切换
{
    if (upstream_total <= 1)
        return;  /* 只有一个上游时无法切换 */

    int old = upstream_cur;
    upstream_cur = (upstream_cur + 1) % upstream_total;
    has_failed_over = 1;

    char old_ip[32], new_ip[32];
    strncpy(old_ip, inet_ntoa(upstream_list[old].sin_addr), sizeof(old_ip) - 1);//把旧服务器IP地址转换为字符串
    old_ip[sizeof(old_ip) - 1] = '\0';
    strncpy(new_ip, inet_ntoa(upstream_list[upstream_cur].sin_addr), sizeof(new_ip) - 1);//把新服务器IP地址转换为字符串
    new_ip[sizeof(new_ip) - 1] = '\0';

    printf("[UPSTREAM] Failover: %s -> %s\n", old_ip, new_ip);
}

void upstream_success(void)//如果之前发生过切换且有多个上游服务器，恢复到第一个上游服务器
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

int upstream_index(void)//返回当前上游服务器的索引
{
    return upstream_cur;
}

int upstream_count(void)//返回上游服务器的总数量
{
    return upstream_total;
}

int upstream_current_is_test_blackhole(void)//检查当前上游服务器是否在文档测试网范围内
{
    if (upstream_total == 0)
        return 0;
    return is_test_net_addr(upstream_list[upstream_cur].sin_addr);
}

void upstream_print(void)//打印当前上游服务器列表和状态
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
