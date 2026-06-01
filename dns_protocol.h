#ifndef DNS_PROTOCOL_H
#define DNS_PROTOCOL_H

#include "dnsrelay.h"

enum {
    DNS_TYPE_A = 1,
    DNS_CLASS_IN = 1,
    DNS_RCODE_NOERROR = 0,
    DNS_RCODE_SERVFAIL = 2,
    DNS_RCODE_NXDOMAIN = 3
};

#pragma pack(push, 1)
typedef struct {
    unsigned short id;

    unsigned char rd : 1;
    unsigned char tc : 1;
    unsigned char aa : 1;
    unsigned char opcode : 4;
    unsigned char qr : 1;

    unsigned char rcode : 4;
    unsigned char cd : 1;
    unsigned char ad : 1;
    unsigned char z : 1;
    unsigned char ra : 1;

    unsigned short qdcount;
    unsigned short ancount;
    unsigned short nscount;
    unsigned short arcount;
} DNS_HEADER;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    unsigned short type;
    unsigned short dclass;
    unsigned int ttl;
    unsigned short rdlength;
} RR_FIXED;
#pragma pack(pop)

typedef struct {
    char qname[DOMAIN_MAX];
    unsigned short qtype;
    unsigned short qclass;
    int question_end;
} DnsQuestion;

int dns_packet_is_query(const unsigned char *packet, int packet_len);
int dns_packet_is_response(const unsigned char *packet, int packet_len);
int parse_dns_question(const unsigned char *packet, int packet_len,
                       DnsQuestion *question);
int build_dns_response(const unsigned char *query, int query_len,
                       unsigned char *response, int response_size,
                       int rcode, unsigned int answer_ip,
                       unsigned int ttl);

#endif
