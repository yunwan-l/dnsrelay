/*
 * dns_packet.c -- DNS报文解析与构造实现
 */
#include "dns_packet.h"

void dns_parse_qname(const unsigned char *buf, int buf_len, int offset,
                     char *out_name, int max_len)
{
    int pos = offset;
    int out_pos = 0;
    int jump_count = 0;

    while (1) {
        if (pos >= buf_len) {
            out_name[out_pos] = '\0';
            return;
        }

        int len = buf[pos];

        /* QNAME压缩指针：最高两位为11 */
        if ((len & 0xC0) == 0xC0) {
            if (pos + 1 >= buf_len)
                break;
            pos = ((len & 0x3F) << 8) | buf[pos + 1];
            jump_count++;
            if (jump_count > 10) /* 防止异常报文导致循环跳转 */
                break;
            continue;
        }

        /* QNAME结束标记 */
        if (len == 0)
            break;

        /* 普通label，转换成点分域名字符串 */
        pos++;
        if (out_pos > 0 && out_pos < max_len - 1)
            out_name[out_pos++] = '.';

        while (len > 0 && pos < buf_len && out_pos < max_len - 1) {
            out_name[out_pos++] = buf[pos++];
            len--;
        }
    }

    out_name[out_pos] = '\0';
}

int dns_parse_question(const unsigned char *query, int query_len,
                       uint16_t *qtype, uint16_t *qclass)
{
    if (query_len < (int)sizeof(DNS_HEADER) + 5)
        return 0;

    /* 跳过QNAME，定位到QTYPE/QCLASS */
    int offset = sizeof(DNS_HEADER);
    while (offset < query_len && query[offset] != 0) {
        if ((query[offset] & 0xC0) == 0xC0) {
            offset += 2;
            goto found;
        }
        offset += 1 + query[offset];
    }
    if (offset >= query_len)
        return 0;
    offset += 1; /* QNAME末尾0字节 */

found:
    if (offset + 4 > query_len)
        return 0;

    /* 读取QTYPE和QCLASS，二者均为网络字节序的两字节字段 */
    *qtype  = (query[offset] << 8) | query[offset + 1];
    *qclass = (query[offset + 2] << 8) | query[offset + 3];
    return 1;
}

int dns_build_response(const unsigned char *query, int query_len,
                       unsigned char *response, int rcode,
                       uint32_t answer_ip)
{
    DNS_HEADER *resp_hdr = (DNS_HEADER *)response;
    int offset;

    if (query_len < (int)sizeof(DNS_HEADER) + 5)
        return 0;

    /* 复制请求头，并改成响应头 */
    memcpy(response, query, sizeof(DNS_HEADER));
    resp_hdr->qr      = 1;
    resp_hdr->aa      = (rcode == RCODE_OK) ? 1 : 0;
    resp_hdr->rd      = 1;
    resp_hdr->ra      = 1;
    resp_hdr->ancount = 0;
    resp_hdr->nscount = 0;
    resp_hdr->arcount = 0;
    resp_hdr->rcode   = (unsigned char)rcode;

    /* 复制Question区，保持客户端原始查询内容 */
    {
        int tmp = sizeof(DNS_HEADER);
        while (tmp < query_len && query[tmp] != 0) {
            if ((query[tmp] & 0xC0) == 0xC0) {
                tmp += 2;
                break;
            }
            tmp += 1 + query[tmp];
        }
        tmp += 1;
        int qname_len = tmp - sizeof(DNS_HEADER);

        if (sizeof(DNS_HEADER) + qname_len + 4 > (size_t)query_len)
            return 0;

        memcpy(response + sizeof(DNS_HEADER),
               query + sizeof(DNS_HEADER),
               qname_len + 4);

        offset = sizeof(DNS_HEADER) + qname_len + 4;
    }

    /* 普通本地命中时添加一条A记录到Answer区 */
    if ((rcode == RCODE_OK || rcode == RCODE_NXDOMAIN) && answer_ip != 0) {
        RR_FIXED *rr;

        response[offset++] = 0xC0;
        response[offset++] = 0x0C;

        rr = (RR_FIXED *)(response + offset);
        rr->type     = htons(DNS_TYPE_A);
        rr->dclass   = htons(DNS_CLASS_IN);
        rr->ttl      = htonl(300);
        rr->rdlength = htons(4);
        offset += sizeof(RR_FIXED);

        *(uint32_t *)(response + offset) = answer_ip;
        offset += 4;

        resp_hdr->ancount = htons(1);
    }

    return offset;
}

int dns_build_cname_response(const unsigned char *query, int query_len,
                             unsigned char *response,
                             const char *cname)
{
    DNS_HEADER *resp_hdr = (DNS_HEADER *)response;
    int offset;

    if (query_len < (int)sizeof(DNS_HEADER) + 5)
        return 0;

    memcpy(response, query, sizeof(DNS_HEADER));
    resp_hdr->qr      = 1;
    resp_hdr->aa      = 1;
    resp_hdr->rd      = 1;
    resp_hdr->ra      = 1;
    resp_hdr->ancount = htons(1);
    resp_hdr->nscount = 0;
    resp_hdr->arcount = 0;
    resp_hdr->rcode   = 0;

    /* 复制Question区 */
    {
        int tmp = sizeof(DNS_HEADER);
        while (tmp < query_len && query[tmp] != 0) {
            if ((query[tmp] & 0xC0) == 0xC0) {
                tmp += 2;
                break;
            }
            tmp += 1 + query[tmp];
        }
        tmp += 1;
        int qname_len = tmp - sizeof(DNS_HEADER);

        memcpy(response + sizeof(DNS_HEADER),
               query + sizeof(DNS_HEADER),
               qname_len + 4);
        offset = sizeof(DNS_HEADER) + qname_len + 4;
    }

    /* Answer区：CNAME记录 */
    response[offset++] = 0xC0;
    response[offset++] = 0x0C;

    /* TYPE = CNAME */
    response[offset++] = 0;
    response[offset++] = DNS_TYPE_CNAME;
    /* CLASS = IN */
    response[offset++] = 0;
    response[offset++] = DNS_CLASS_IN;
    /* TTL = 300秒 */
    uint32_t ttl = htonl(300);
    memcpy(response + offset, &ttl, 4);
    offset += 4;
    /* 先占位RDLENGTH，写完RDATA后回填 */
    int rdlength_pos = offset;
    offset += 2;

    /* RDATA使用DNS label格式保存规范域名 */
    int cname_pos = offset;
    {
        const char *p = cname;
        while (*p) {
            const char *dot = strchr(p, '.');
            int label_len = dot ? (int)(dot - p) : (int)strlen(p);
            if (label_len > 63) break;
            response[offset++] = (unsigned char)label_len;
            memcpy(response + offset, p, label_len);
            offset += label_len;
            p = dot ? dot + 1 : p + label_len;
        }
        response[offset++] = 0;
    }

    /* 回填RDLENGTH */
    uint16_t rdlength = htons((uint16_t)(offset - cname_pos));
    memcpy(response + rdlength_pos, &rdlength, 2);

    return offset;
}

int dns_build_servfail(const unsigned char *query, int query_len,
                       unsigned char *response)
{
    return dns_build_response(query, query_len, response, RCODE_SERVFAIL, 0);
}

uint32_t dns_extract_ttl(const unsigned char *packet, int len)
{
    if (len < (int)sizeof(DNS_HEADER) + 5)
        return 60;

    DNS_HEADER *hdr = (DNS_HEADER *)packet;
    if (ntohs(hdr->ancount) == 0)
        return 60;

    int offset = sizeof(DNS_HEADER);
    while (offset < len && packet[offset] != 0) {
        if ((packet[offset] & 0xC0) == 0xC0) {
            offset += 2;
            goto question_done;
        }
        offset += 1 + packet[offset];
    }
    offset += 1;

question_done:
    offset += 4; /* QTYPE + QCLASS */

    if (offset >= len)
        return 60;

    /* 跳过第一条Answer的NAME字段 */
    if ((packet[offset] & 0xC0) == 0xC0) {
        offset += 2;
    } else {
        while (offset < len && packet[offset] != 0)
            offset += 1 + packet[offset];
        offset += 1;
    }

    if (offset + 8 > len)
        return 60;

    RR_FIXED *rr = (RR_FIXED *)(packet + offset);
    return ntohl(rr->ttl);
}
