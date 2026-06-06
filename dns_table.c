/*
 * dns_table.c -- 域名-IP对照表实现
 */
#include "dns_table.h"

DomainEntry domain_table[MAX_TABLE];
int domain_count = 0;

static int parse_ipv4(const char *text, uint32_t *out_ip)
{
    int a, b, c, d;
    char extra;

    if (sscanf(text, "%d.%d.%d.%d%c", &a, &b, &c, &d, &extra) != 4)
        return 0;

    if (a < 0 || a > 255 || b < 0 || b > 255 ||
        c < 0 || c > 255 || d < 0 || d > 255)
        return 0;

    if (out_ip) {
        *out_ip = htonl(((uint32_t)a << 24) |
                        ((uint32_t)b << 16) |
                        ((uint32_t)c << 8) |
                        (uint32_t)d);
    }
    return 1;
}

int domain_table_load(const char *filename)
{
    FILE *fp;
    char line[512];
    char first[DOMAIN_MAX];
    char second[DOMAIN_MAX];
    char domain[DOMAIN_MAX];
    char ip_str[IP_STR_MAX];
    int line_no = 0;

    fp = fopen(filename, "r");
    if (!fp) {
        printf("[ERROR] Cannot open domain table file: %s\n", filename);
        return -1;
    }

    domain_count = 0;

    while (fgets(line, sizeof(line), fp) && domain_count < MAX_TABLE) {
        char *p;
        uint32_t parsed_ip;

        line_no++;

        p = line + strlen(line);
        while (p > line && (p[-1] == '\n' || p[-1] == '\r'))
            *--p = '\0';

        p = line;
        if ((unsigned char)p[0] == 0xEF &&
            (unsigned char)p[1] == 0xBB &&
            (unsigned char)p[2] == 0xBF) {
            p += 3;
        }
        while (*p == ' ' || *p == '\t')
            p++;

        if (*p == '\0' || *p == '#')
            continue;

        if (sscanf(p, "%255s %255s", first, second) != 2) {
            printf("[WARN] %s line %d: malformed entry, skipped\n",
                   filename, line_no);
            continue;
        }

        /*
         * 老师参考表使用“IP 域名”格式；这里同时兼容“域名 IP”，
         * 便于测试旧表文件和格式兼容功能。
         */
        if (parse_ipv4(first, &parsed_ip)) {
            strncpy(ip_str, first, IP_STR_MAX - 1);
            ip_str[IP_STR_MAX - 1] = '\0';
            strncpy(domain, second, DOMAIN_MAX - 1);
            domain[DOMAIN_MAX - 1] = '\0';
        } else if (parse_ipv4(second, &parsed_ip)) {
            strncpy(domain, first, DOMAIN_MAX - 1);
            domain[DOMAIN_MAX - 1] = '\0';
            strncpy(ip_str, second, IP_STR_MAX - 1);
            ip_str[IP_STR_MAX - 1] = '\0';
        } else {
            printf("[WARN] %s line %d: no valid IPv4 address, skipped\n",
                   filename, line_no);
            continue;
        }

        strncpy(domain_table[domain_count].domain, domain, DOMAIN_MAX - 1);
        domain_table[domain_count].domain[DOMAIN_MAX - 1] = '\0';

        strncpy(domain_table[domain_count].ip_str, ip_str, IP_STR_MAX - 1);
        domain_table[domain_count].ip_str[IP_STR_MAX - 1] = '\0';

        domain_table[domain_count].is_blocked =
            (strcmp(ip_str, "0.0.0.0") == 0) ? 1 : 0;
        domain_table[domain_count].ip = parsed_ip;
        ip_str_to_bytes(ip_str, domain_table[domain_count].addr);

        DEBUG2("Load[%d]: %s -> %s (%s)",
               domain_count + 1,
               domain,
               ip_str,
               domain_table[domain_count].is_blocked ? "BLOCK" : "OK");

        domain_count++;
    }

    fclose(fp);
    printf("Loaded %d domain records from %s\n", domain_count, filename);
    return domain_count;
}

int domain_table_search(const char *domain, uint32_t *out_ip)
{
    int i;
    for (i = 0; i < domain_count; i++) {
        if (strcasecmp(domain, domain_table[i].domain) == 0) {
            *out_ip = domain_table[i].ip;
            return domain_table[i].is_blocked ? 1 : 0;
        }
    }
    return -1;
}

void domain_table_print(void)
{
    int i;
    printf("\nDomain table (%d records):\n", domain_count);
    printf("  %-40s %-16s %s\n", "Domain", "IP", "Status");
    printf("  %s\n",
           "---------------------------------------- "
           "----------------- ------");
    for (i = 0; i < domain_count; i++) {
        printf("  %-40s %-16s %s\n",
               domain_table[i].domain,
               domain_table[i].ip_str,
               domain_table[i].is_blocked ? "[BLOCKED]" : "[OK]");
    }
    printf("\n");
}

void domain_table_free(void)
{
    /* 使用静态数组保存表项，不需要释放堆内存。 */
}
