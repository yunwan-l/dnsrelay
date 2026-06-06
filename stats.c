/*
 * stats.c -- 运行统计实现
 */
#include "stats.h"

Stats g_stats;

void stats_init(void)
{
    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.start_time = time(NULL);
}

void stats_print(void)
{
    time_t now       = time(NULL);
    double uptime    = difftime(now, g_stats.start_time);
    int    hours     = (int)(uptime / 3600);
    int    minutes   = (int)((uptime - hours * 3600) / 60);
    int    seconds   = (int)(uptime - hours * 3600 - minutes * 60);

    printf("\n");
    printf("========================================\n");
    printf("  DNS Relay Statistics\n");
    printf("========================================\n");
    printf("  Uptime        : %02d:%02d:%02d\n", hours, minutes, seconds);
    printf("----------------------------------------\n");
    printf("  Total queries : %-10llu\n", (unsigned long long)g_stats.total_queries);
    printf("  Local hits    : %-10llu\n", (unsigned long long)g_stats.local_hits);
    printf("  Cache hits    : %-10llu\n", (unsigned long long)g_stats.cache_hits);
    printf("  Blocked       : %-10llu\n", (unsigned long long)g_stats.blocked);
    printf("  Relayed       : %-10llu\n", (unsigned long long)g_stats.relayed);
    printf("  Relay OK      : %-10llu\n", (unsigned long long)g_stats.relay_responses);
    printf("  Relay timeouts: %-10llu\n", (unsigned long long)g_stats.relay_timeouts);
    printf("  Errors        : %-10llu\n", (unsigned long long)g_stats.errors);
    printf("========================================\n\n");
}
