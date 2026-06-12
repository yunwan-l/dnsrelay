/*
 * tid_map.c -- DNS事务ID映射表实现
 */
#include "tid_map.h"

typedef struct {
    uint16_t new_id;//上游新ID
    uint16_t orig_id;//客户端原始ID
    struct sockaddr_in client;//客户端地址
    unsigned char query[BUFFER_SIZE];//原始查询报文
    int query_len;//查询报文长度
    int attempts;//重试次数
    int in_use;//槽位是否被占用
    time_t timestamp;//记录分配时间，用于超时检查
} TidEntry;

static TidEntry tid_table[MAX_CLIENTS];//256个槽位的映射表
static int tid_count = 0;//当前正在使用的槽位数
static uint16_t next_id = 1;//下一个分配的事务ID

static int find_entry(uint16_t new_id)//根据上游ID查找映射表槽位索引
{
    int i;
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (tid_table[i].in_use && tid_table[i].new_id == new_id)
            return i;
    }
    return -1;
}

static int id_in_use(uint16_t new_id)//检查上游ID是否已经被分配
{
    return find_entry(new_id) >= 0;
}

void tid_map_init(void)//映射表初始化
{
    int i;
    for (i = 0; i < MAX_CLIENTS; i++)
        tid_table[i].in_use = 0;
    tid_count = 0;
    next_id = 1;
}

uint16_t tid_map_alloc(uint16_t orig_id, struct sockaddr_in *client,
                       const unsigned char *query, int query_len)//分配新的事务ID并记录映射关系
{
    int slot;//找到一个空闲槽位
    int safety;//防止死循环的安全计数器
    uint16_t assigned_id;//分配的上游ID

    if (query_len <= 0 || query_len > BUFFER_SIZE)//查询报文长度不合法
        return 0;

    if (tid_count >= MAX_CLIENTS) {
        DEBUG1("Warning: TID table full, cannot relay orig_id=%u", orig_id);
        return 0;
    }//槽位已满

    for (slot = 0; slot < MAX_CLIENTS; slot++) {
        if (!tid_table[slot].in_use)
            break;
    }//找到一个空闲槽位
    if (slot >= MAX_CLIENTS)
        return 0;

    safety = 0;
    while (next_id == 0 || id_in_use(next_id)) {
        next_id++;
        if (next_id == 0)
            next_id = 1;
        if (++safety > 65535)//所有ID（65536个）都被占用
            return 0;
    }

    assigned_id = next_id++;
    if (next_id == 0)//回绕到1，0保留为无效ID
        next_id = 1;

    tid_table[slot].new_id = assigned_id;
    tid_table[slot].orig_id = orig_id;
    tid_table[slot].client = *client;
    memcpy(tid_table[slot].query, query, query_len);//保存原始查询报文
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
    }//调试输出分配的事务ID和客户端信息

    return assigned_id;
}

int tid_map_restore(uint16_t new_id, uint16_t *out_orig_id,
                    struct sockaddr_in *out_client)//根据上游ID恢复原始ID和客户端信息，并释放槽位
{
    int slot = find_entry(new_id);
    if (slot < 0)
        return 0;

    //取出原始ID和客户端地址
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

int tid_map_get_timeout(TidTimeout *out_timeout)//检查映射表中是否有超时的事务，返回第一个超时事务的信息
{
    int i;
    time_t now = time(NULL);//当前时间

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

void tid_map_mark_retry(uint16_t new_id)//标记某个事务ID的重试次数增加，并更新时间戳
{
    int slot = find_entry(new_id);
    if (slot < 0)
        return;

    tid_table[slot].attempts++;
    tid_table[slot].timestamp = time(NULL);
}

void tid_map_remove(uint16_t new_id)//根据上游ID移除映射关系，释放槽位
{
    int slot = find_entry(new_id);
    if (slot < 0)
        return;

    tid_table[slot].in_use = 0;
    tid_count--;
}

int tid_map_count(void)//返回当前正在使用的槽位数  
{
    return tid_count;
}
