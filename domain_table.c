#include "domain_table.h"

static int find_domain_index(const DomainTable *table, const char *domain)
{
    int i;

    for (i = 0; i < table->count; i++) {
        if (strcasecmp(table->entries[i].domain, domain) == 0) {
            return i;
        }
    }

    return -1;
}

void domain_table_init(DomainTable *table)
{
    memset(table, 0, sizeof(*table));
}

int load_domain_table(DomainTable *table, const char *filename)
{
    FILE *file;
    char line[512];

    file = fopen(filename, "r");
    if (file == NULL) {
        printf("警告: 无法打开配置文件 '%s'，程序将仅使用中继功能。\n", filename);
        return 0;
    }

    table->count = 0;

    while (fgets(line, sizeof(line), file) != NULL) {
        char domain[DOMAIN_MAX];
        char ip_text[IP_STR_MAX];
        struct in_addr addr;
        int entry_index;

        if (line[0] == '#' || line[0] == '\r' || line[0] == '\n') {
            continue;
        }

        if (sscanf(line, "%255s %15s", domain, ip_text) != 2) {
            continue;
        }

#ifdef _WIN32
        addr.s_addr = inet_addr(ip_text);
        if (addr.s_addr == INADDR_NONE && strcmp(ip_text, "255.255.255.255") != 0) {
            continue;
        }
#else
        if (inet_pton(AF_INET, ip_text, &addr) != 1) {
            continue;
        }
#endif

        entry_index = find_domain_index(table, domain);
        if (entry_index < 0) {
            if (table->count >= MAX_TABLE) {
                DEBUG1("本地域名表已满，后续条目被忽略。");
                break;
            }
            entry_index = table->count++;
        }

        snprintf(table->entries[entry_index].domain,
                 sizeof(table->entries[entry_index].domain),
                 "%s", domain);
        table->entries[entry_index].ip = addr.s_addr;
        table->entries[entry_index].is_blocked = (addr.s_addr == 0);

        DEBUG2("加载映射: %-32s -> %-15s %s",
               table->entries[entry_index].domain,
               ip_text,
               table->entries[entry_index].is_blocked ? "[拦截]" : "[正常]");
    }

    fclose(file);
    printf("已加载 %d 条本地域名记录。\n", table->count);
    return table->count;
}

DomainLookupResult find_domain(const DomainTable *table, const char *domain,
                               unsigned int *out_ip)
{
    int index = find_domain_index(table, domain);

    if (index < 0) {
        return DOMAIN_LOOKUP_MISS;
    }

    if (out_ip != NULL) {
        *out_ip = table->entries[index].ip;
    }

    return table->entries[index].is_blocked ?
           DOMAIN_LOOKUP_BLOCKED : DOMAIN_LOOKUP_HIT;
}

void print_domain_table(const DomainTable *table)
{
    int i;

    printf("本地域名表 (%d 条):\n", table->count);
    printf("  %-34s %-15s %s\n", "域名", "IP地址", "状态");
    printf("  ---------------------------------- --------------- --------\n");

    for (i = 0; i < table->count; i++) {
        struct in_addr addr;
        addr.s_addr = table->entries[i].ip;
        printf("  %-34s %-15s %s\n",
               table->entries[i].domain,
               inet_ntoa(addr),
               table->entries[i].is_blocked ? "[拦截]" : "[正常]");
    }

    printf("\n");
}
