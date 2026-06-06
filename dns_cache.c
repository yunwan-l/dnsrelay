/*
 * dns_cache.c -- DNS响应缓存实现
 *
 * 使用FNV-1a哈希和线性探测保存缓存项；缓存满时淘汰最早插入的项。
 */
#include "dns_cache.h"

/* FNV-1a 32位哈希，用于把缓存key映射到起始槽位。 */
static uint32_t hash_str(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= (unsigned char)(*s);
        h *= 16777619u;
        s++;
    }
    return h;
}

/* 单个缓存项，保存完整DNS响应报文。 */
typedef struct {
    int     in_use;
    char    key[DNS_CACHE_KEY_MAX];
    uint8_t data[BUFFER_SIZE];
    int     data_len;
    time_t  expiry;       /* 绝对过期时间 */
    time_t  insert_time;  /* 插入时间，用于淘汰旧项 */
} CacheEntry;

static CacheEntry cache[DNS_CACHE_MAX];
static int        cache_count = 0;

/* 初始化缓存表。 */
void dns_cache_init(void)
{
    int i;
    for (i = 0; i < DNS_CACHE_MAX; i++)
        cache[i].in_use = 0;
    cache_count = 0;
}

/* 写入或覆盖缓存项。 */
void dns_cache_put(const char *cache_key,
                   const unsigned char *response, int response_len,
                   uint32_t ttl)
{
    int slot = -1;
    int first_empty = -1;
    int i;

    if (response_len <= 0 || response_len > BUFFER_SIZE)
        return;

    /* 先通过哈希找到线性探测的起始位置。 */
    uint32_t idx = hash_str(cache_key) % DNS_CACHE_MAX;

    /* 查找同key旧项，同时记住第一个空槽。 */
    for (i = 0; i < DNS_CACHE_MAX; i++) {
        slot = (int)((idx + i) % DNS_CACHE_MAX);
        if (!cache[slot].in_use) {
            if (first_empty < 0)
                first_empty = slot;
            continue;
        }
        if (strcasecmp(cache[slot].key, cache_key) == 0)
            goto store;
    }

    if (first_empty >= 0) {
        slot = first_empty;
        goto store;
    }

    /* 缓存满时淘汰最早插入的项。 */
    {
        int oldest = 0;
        time_t oldest_time = cache[0].insert_time;
        for (i = 1; i < DNS_CACHE_MAX; i++) {
            if (cache[i].insert_time < oldest_time) {
                oldest_time = cache[i].insert_time;
                oldest = i;
            }
        }
        DEBUG2("Cache evict: %s (full)", cache[oldest].key);
        cache[oldest].in_use = 0;
        cache_count--;
        slot = oldest;
        goto store;
    }

store:
    strncpy(cache[slot].key, cache_key, DNS_CACHE_KEY_MAX - 1);
    cache[slot].key[DNS_CACHE_KEY_MAX - 1] = '\0';
    memcpy(cache[slot].data, response, response_len);
    cache[slot].data_len = response_len;
    cache[slot].expiry    = time(NULL) + (time_t)ttl;
    cache[slot].insert_time = time(NULL);

    if (!cache[slot].in_use) {
        cache[slot].in_use = 1;
        cache_count++;
    }

    DEBUG2("CACHE PUT: %s (TTL=%u, expiry=%ld)", cache_key, ttl,
           (long)cache[slot].expiry);
}

/* 查找缓存项。 */
int dns_cache_get(const char *cache_key,
                  unsigned char *out_buf, int *out_len)
{
    uint32_t idx = hash_str(cache_key) % DNS_CACHE_MAX;
    int i;

    for (i = 0; i < DNS_CACHE_MAX; i++) {
        int slot = (int)((idx + i) % DNS_CACHE_MAX);

        if (!cache[slot].in_use)
            continue;

        if (strcasecmp(cache[slot].key, cache_key) != 0)
            continue;

        /* 找到同key项后先检查TTL是否过期。 */
        if (time(NULL) >= cache[slot].expiry) {
            cache[slot].in_use = 0;
            cache_count--;
            DEBUG2("CACHE EXPIRED: %s", cache_key);
            return 0;
        }

        /* 命中后复制完整响应报文。 */
        int copy_len = cache[slot].data_len;
        if (copy_len > *out_len)
            copy_len = *out_len;
        *out_len = copy_len;
        memcpy(out_buf, cache[slot].data, copy_len);
        DEBUG2("CACHE HIT: %s (len=%d)", cache_key, copy_len);
        return 1;
    }

    return 0;
}

/* 定期清理过期缓存项。 */
void dns_cache_cleanup(void)
{
    int i;
    time_t now = time(NULL);

    for (i = 0; i < DNS_CACHE_MAX; i++) {
        if (cache[i].in_use && now >= cache[i].expiry) {
            cache[i].in_use = 0;
            cache_count--;
            DEBUG2("CACHE CLEANUP: %s expired", cache[i].key);
        }
    }
}

/* 返回当前有效缓存项数量。 */
int dns_cache_count(void)
{
    return cache_count;
}

/* 清空缓存表。 */
void dns_cache_free(void)
{
    dns_cache_init();
}
