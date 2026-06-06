/*
 * tid_map.c -- DNS事务ID映射表实现
 */
#include "tid_map.h"

typedef struct {
    uint16_t new_id;
    uint16_t orig_id;
    struct sockaddr_in client;
    unsigned char query[BUFFER_SIZE];
    int query_len;
    int attempts;
    int in_use;
    time_t timestamp;
} TidEntry;

static TidEntry tid_table[MAX_CLIENTS];
static int tid_count = 0;
static uint16_t next_id = 1;

static int find_entry(uint16_t new_id)
{
    int i;
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (tid_table[i].in_use && tid_table[i].new_id == new_id)
            return i;
    }
    return -1;
}

static int id_in_use(uint16_t new_id)
{
    return find_entry(new_id) >= 0;
}

void tid_map_init(void)
{
    int i;
    for (i = 0; i < MAX_CLIENTS; i++)
        tid_table[i].in_use = 0;
    tid_count = 0;
    next_id = 1;
}

uint16_t tid_map_alloc(uint16_t orig_id, struct sockaddr_in *client,
                       const unsigned char *query, int query_len)
{
    int slot;
    int safety;
    uint16_t assigned_id;

    if (query_len <= 0 || query_len > BUFFER_SIZE)
        return 0;

    if (tid_count >= MAX_CLIENTS) {
        DEBUG1("Warning: TID table full, cannot relay orig_id=%u", orig_id);
        return 0;
    }

    for (slot = 0; slot < MAX_CLIENTS; slot++) {
        if (!tid_table[slot].in_use)
            break;
    }
    if (slot >= MAX_CLIENTS)
        return 0;

    safety = 0;
    while (next_id == 0 || id_in_use(next_id)) {
        next_id++;
        if (next_id == 0)
            next_id = 1;
        if (++safety > 65535)
            return 0;
    }

    assigned_id = next_id++;
    if (next_id == 0)
        next_id = 1;

    tid_table[slot].new_id = assigned_id;
    tid_table[slot].orig_id = orig_id;
    tid_table[slot].client = *client;
    memcpy(tid_table[slot].query, query, query_len);
    tid_table[slot].query_len = query_len;
    tid_table[slot].attempts = 1;
    tid_table[slot].timestamp = time(NULL);
    tid_table[slot].in_use = 1;
    tid_count++;

    if (debug_level >= 1) {
        printf("[%ld] NEW TID: orig=%u -> new=%u\n",
               (long)time(NULL), orig_id, assigned_id);
        DEBUG2("  Client: %s:%d",
               inet_ntoa(client->sin_addr),
               ntohs(client->sin_port));
    }

    return assigned_id;
}

int tid_map_restore(uint16_t new_id, uint16_t *out_orig_id,
                    struct sockaddr_in *out_client)
{
    int slot = find_entry(new_id);
    if (slot < 0)
        return 0;

    *out_orig_id = tid_table[slot].orig_id;
    *out_client = tid_table[slot].client;
    tid_table[slot].in_use = 0;
    tid_count--;

    if (debug_level >= 1) {
        printf("[%ld] RESTORE TID: new=%u -> orig=%u\n",
               (long)time(NULL), new_id, *out_orig_id);
    }

    return 1;
}

int tid_map_get_timeout(TidTimeout *out_timeout)
{
    int i;
    time_t now = time(NULL);

    for (i = 0; i < MAX_CLIENTS; i++) {
        if (!tid_table[i].in_use)
            continue;
        if ((now - tid_table[i].timestamp) <= TIMEOUT_SEC)
            continue;

        out_timeout->new_id = tid_table[i].new_id;
        out_timeout->orig_id = tid_table[i].orig_id;
        out_timeout->client = tid_table[i].client;
        memcpy(out_timeout->query, tid_table[i].query,
               tid_table[i].query_len);
        out_timeout->query_len = tid_table[i].query_len;
        out_timeout->attempts = tid_table[i].attempts;
        return 1;
    }

    return 0;
}

void tid_map_mark_retry(uint16_t new_id)
{
    int slot = find_entry(new_id);
    if (slot < 0)
        return;

    tid_table[slot].attempts++;
    tid_table[slot].timestamp = time(NULL);
}

void tid_map_remove(uint16_t new_id)
{
    int slot = find_entry(new_id);
    if (slot < 0)
        return;

    tid_table[slot].in_use = 0;
    tid_count--;
}

int tid_map_count(void)
{
    return tid_count;
}
