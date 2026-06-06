/*
 * dns_packet.h -- DNS报文解析与构造
 */
#ifndef DNS_PACKET_H
#define DNS_PACKET_H

#include "dns.h"

/*
 * 从原始DNS报文中解析QNAME。
 * offset通常为sizeof(DNS_HEADER)，函数支持0xC0压缩指针，
 * 并设置跳转次数上限，避免异常报文造成死循环。
 */
void dns_parse_qname(const unsigned char *buf, int buf_len, int offset,
                     char *out_name, int max_len);

/*
 * 从DNS查询报文的问题区提取QTYPE和QCLASS。
 * 成功返回1，报文不完整或格式异常返回0。
 */
int dns_parse_question(const unsigned char *query, int query_len,
                       uint16_t *qtype, uint16_t *qclass);

/*
 * 基于收到的查询构造DNS响应。
 * answer_ip为网络字节序；普通A记录命中时写入Answer区。
 * NXDOMAIN或SERVFAIL等失败响应只返回问题区和响应码。
 * 返回响应报文长度，失败返回0。
 */
int dns_build_response(const unsigned char *query, int query_len,
                       unsigned char *response, int rcode,
                       uint32_t answer_ip);

/*
 * 构造CNAME响应的预留接口。
 * 当前主流程对非A查询优先转发上游，此函数保留用于后续扩展。
 */
int dns_build_cname_response(const unsigned char *query, int query_len,
                             unsigned char *response,
                             const char *cname);

/*
 * 为中继超时构造SERVFAIL响应，RCODE=2。
 */
int dns_build_servfail(const unsigned char *query, int query_len,
                       unsigned char *response);

/*
 * 从上游DNS响应的第一条Answer RR提取TTL。
 * 无答案或格式异常时返回默认值60秒。
 */
uint32_t dns_extract_ttl(const unsigned char *packet, int len);

#endif /* DNS_PACKET_H */
