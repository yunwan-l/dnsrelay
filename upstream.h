/*
 * upstream.h -- 上游DNS服务器管理
 *
 * 支持配置多个上游DNS。当前上游超时时切换到下一个上游；
 * 成功收到响应后恢复主上游，便于现场演示多上游容错。
 */
#ifndef UPSTREAM_H
#define UPSTREAM_H

#include "dns.h"

#define UPSTREAM_MAX    8           /* 最多配置的上游DNS数量 */
#define UPSTREAM_TIMEOUT 3          /* 预留字段，当前超时由TIMEOUT_SEC统一控制 */

/*
 * 初始化上游列表。default_ip非空时作为主上游。
 */
void upstream_init(const char *default_ip);

/*
 * 添加一个上游DNS服务器，参数为点分十进制IPv4字符串。
 * 成功返回下标，失败返回-1。
 */
int upstream_add(const char *ip_str);

/*
 * 获取当前活动上游地址，供sendto使用。
 */
struct sockaddr_in *upstream_current(void);

/*
 * 切换到下一个上游，通常在当前上游超时后调用。
 */
void upstream_failover(void);

/*
 * 当前上游成功返回响应后调用。
 * 如果之前发生过切换，则恢复到主上游。
 */
void upstream_success(void);

/*
 * 返回当前活动上游下标。
 */
int upstream_index(void);

/*
 * 返回已配置的上游数量。
 */
int upstream_count(void);

/*
 * 当前上游如果属于RFC保留的TEST-NET文档地址，则作为测试黑洞处理。
 * 这样可稳定演示超时和多上游切换，避免被校园网/运营商透明DNS影响。
 */
int upstream_current_is_test_blackhole(void);

/*
 * 打印上游列表，主要用于调试和统计输出。
 */
void upstream_print(void);

#endif /* UPSTREAM_H */
