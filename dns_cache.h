/*
 * dns_cache.h -- DNS响应缓存
 *
 * 缓存完整的上游DNS响应报文，并按TTL自动过期。
 */
#ifndef DNS_CACHE_H
#define DNS_CACHE_H

#include "dns.h"

#define DNS_CACHE_MAX 512
#define DNS_CACHE_KEY_MAX (DOMAIN_MAX + 32)

void dns_cache_init(void);

/*
 * 按调用者构造的key保存DNS响应，例如：
 * "www.example.com|1|1" 表示 域名|QTYPE|QCLASS。
 */
void dns_cache_put(const char *cache_key,
                   const unsigned char *response, int response_len,
                   uint32_t ttl);

/*
 * 按key查找缓存响应，命中时复制完整DNS报文到out_buf。
 */
int dns_cache_get(const char *cache_key,
                  unsigned char *out_buf, int *out_len);

void dns_cache_cleanup(void);
int dns_cache_count(void);
void dns_cache_free(void);

#endif /* DNS_CACHE_H */
