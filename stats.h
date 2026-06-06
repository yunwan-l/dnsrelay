/*
 * stats.h -- 运行统计
 */
#ifndef STATS_H
#define STATS_H

#include "dns.h"

/* 运行过程中累计的关键计数。 */
typedef struct {
    uint64_t total_queries;       /* 收到的客户端查询总数 */
    uint64_t local_hits;          /* 本地表普通IP命中次数 */
    uint64_t blocked;             /* 拦截并返回NXDOMAIN次数 */
    uint64_t cache_hits;          /* DNS缓存命中次数 */
    uint64_t relayed;             /* 转发到上游DNS次数 */
    uint64_t relay_responses;     /* 收到上游响应次数 */
    uint64_t relay_timeouts;      /* 中继超时次数 */
    uint64_t errors;              /* 错误路径计数 */
    time_t   start_time;          /* 程序启动时间 */
} Stats;

extern Stats g_stats;

/* 初始化统计信息。 */
void stats_init(void);

/* 打印统计汇总。 */
void stats_print(void);

#endif /* STATS_H */
