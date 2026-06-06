/*
 * tid_map.h -- DNS事务ID映射表
 *
 * 中继转发时为每个上游查询分配新的DNS ID。本模块保存：
 * 新ID、客户端原始ID、客户端地址、原始查询报文和重试次数。
 * 收到上游响应后可恢复原始ID；超时时可重发或返回SERVFAIL。
 */
#ifndef TID_MAP_H
#define TID_MAP_H

#include "dns.h"

typedef struct {
    uint16_t new_id;
    uint16_t orig_id;
    struct sockaddr_in client;
    unsigned char query[BUFFER_SIZE];
    int query_len;
    int attempts;
} TidTimeout;

void tid_map_init(void);

/*
 * 分配一个未占用的新事务ID，并保存原始查询上下文。
 * 映射表满时返回0。
 */
uint16_t tid_map_alloc(uint16_t orig_id, struct sockaddr_in *client,
                       const unsigned char *query, int query_len);

/*
 * 根据上游响应的新ID找回客户端原始ID和地址，并删除该映射。
 */
int tid_map_restore(uint16_t new_id, uint16_t *out_orig_id,
                    struct sockaddr_in *out_client);

/*
 * 找出一条超时映射并复制出来，但暂不删除。
 * 调用者负责决定重试或最终删除。
 */
int tid_map_get_timeout(TidTimeout *out_timeout);

/*
 * 标记某条超时请求已经切换上游重试，并刷新时间戳。
 */
void tid_map_mark_retry(uint16_t new_id);

/*
 * 删除一条映射，通常用于最终超时或清理过期响应。
 */
void tid_map_remove(uint16_t new_id);

int tid_map_count(void);

#endif /* TID_MAP_H */
