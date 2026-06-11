/*
 * dns_table.h -- 域名-IP对照表
 */
#ifndef DNS_TABLE_H
#define DNS_TABLE_H

#include "dns.h"

/* 对照表中的一条记录 */
typedef struct {
    char domain[DOMAIN_MAX];           /* 域名，例如 "www.baidu.com" */
    char ip_str[IP_STR_MAX];           /* 点分十进制IP，例如 "14.215.177.38" */
    unsigned char addr[4];             /* 拆分后的IPv4字节 */
    uint32_t ip;                       /* 网络字节序IPv4地址 */
    int is_blocked;                    /* 1表示拦截并返回NXDOMAIN，0表示普通记录 */
} DomainEntry;

/* 全局静态对照表 */
extern DomainEntry domain_table[MAX_TABLE];
extern int domain_count;

/*
 * 从文件加载域名表。
 * 每行一条记录，'#'开头表示注释。
 * 支持两种格式：
 *   IP地址 域名        课程参考实现格式，提交时优先使用
 *   域名 IP地址        兼容旧表文件
 * IP为0.0.0.0表示拦截该域名。
 * 返回成功加载的记录数，失败返回-1。
 */
int domain_table_load(const char *filename);

/*
 * 重新加载域名表。
 * 采用“先加载到临时表，成功后再替换全局表”的方式，
 * 这样即使配置文件写到一半，旧表也不会被破坏。
 * 返回成功加载的记录数，失败返回-1。
 */
int domain_table_reload(const char *filename);

/*
 * 在表中查找域名，大小写不敏感。
 * 返回值： 1 = 命中拦截记录
 *          0 = 命中普通IP，结果写入 *out_ip
 *         -1 = 未命中
 */
int domain_table_search(const char *domain, uint32_t *out_ip);

/* 打印完整域名表，主要用于-dd调试。 */
void domain_table_print(void);

/* 静态表没有堆内存需要释放，保留接口方便热重载流程统一调用。 */
void domain_table_free(void);

#endif /* DNS_TABLE_H */
