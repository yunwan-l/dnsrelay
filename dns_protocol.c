#include "dns_protocol.h"

static unsigned short read_u16(const unsigned char *buffer)
{
    unsigned short value;

    memcpy(&value, buffer, sizeof(value));
    return ntohs(value);
}

static int parse_qname(const unsigned char *packet, int packet_len, int offset,
                       char *output, int output_size, int *next_offset)
{
    int cursor = offset;
    int output_pos = 0;
    int jumped = 0;
    int jump_count = 0;
    int consumed_end = -1;

    while (1) {
        unsigned char label_len;
        int i;

        if (cursor >= packet_len) {
            return -1;
        }

        label_len = packet[cursor];

        if ((label_len & 0xC0) == 0xC0) {
            int pointer;

            if (cursor + 1 >= packet_len) {
                return -1;
            }

            pointer = ((label_len & 0x3F) << 8) | packet[cursor + 1];
            if (!jumped) {
                consumed_end = cursor + 2;
                jumped = 1;
            }

            cursor = pointer;
            jump_count++;
            if (jump_count > 16) {
                return -1;
            }
            continue;
        }

        cursor++;
        if (label_len == 0) {
            if (!jumped) {
                consumed_end = cursor;
            }
            break;
        }

        if (cursor + label_len > packet_len) {
            return -1;
        }

        if (output_pos > 0) {
            if (output_pos >= output_size - 1) {
                return -1;
            }
            output[output_pos++] = '.';
        }

        for (i = 0; i < label_len; i++) {
            if (output_pos >= output_size - 1) {
                return -1;
            }
            output[output_pos++] = (char)packet[cursor + i];
        }

        cursor += label_len;
    }

    output[output_pos] = '\0';
    if (next_offset != NULL) {
        *next_offset = consumed_end;
    }
    return 0;
}

int dns_packet_is_query(const unsigned char *packet, int packet_len)
{
    const DNS_HEADER *header;

    if (packet_len < (int)sizeof(DNS_HEADER)) {
        return 0;
    }

    header = (const DNS_HEADER *)packet;
    return header->qr == 0;
}

int dns_packet_is_response(const unsigned char *packet, int packet_len)
{
    const DNS_HEADER *header;

    if (packet_len < (int)sizeof(DNS_HEADER)) {
        return 0;
    }

    header = (const DNS_HEADER *)packet;
    return header->qr == 1;
}

int parse_dns_question(const unsigned char *packet, int packet_len,
                       DnsQuestion *question)
{
    const DNS_HEADER *header;
    int question_offset;

    if (packet_len < (int)sizeof(DNS_HEADER) || question == NULL) {
        return -1;
    }

    header = (const DNS_HEADER *)packet;
    if (ntohs(header->qdcount) != 1) {
        return -1;
    }

    memset(question, 0, sizeof(*question));

    if (parse_qname(packet, packet_len, sizeof(DNS_HEADER),
                    question->qname, sizeof(question->qname),
                    &question_offset) != 0) {
        return -1;
    }

    if (question_offset + 4 > packet_len) {
        return -1;
    }

    question->qtype = read_u16(packet + question_offset);
    question->qclass = read_u16(packet + question_offset + 2);
    question->question_end = question_offset + 4;

    return 0;
}

int build_dns_response(const unsigned char *query, int query_len,
                       unsigned char *response, int response_size,
                       int rcode, unsigned int answer_ip,
                       unsigned int ttl)
{
    DnsQuestion question;
    DNS_HEADER *response_header;
    int offset;

    if (parse_dns_question(query, query_len, &question) != 0) {
        return -1;
    }

    if (question.question_end > query_len || question.question_end > response_size) {
        return -1;
    }

    memcpy(response, query, question.question_end);
    response_header = (DNS_HEADER *)response;
    response_header->qr = 1;
    response_header->aa = 1;
    response_header->ra = 1;
    response_header->tc = 0;
    response_header->rcode = (unsigned char)rcode;
    response_header->ancount = htons(0);
    response_header->nscount = htons(0);
    response_header->arcount = htons(0);

    offset = question.question_end;

    if (rcode == DNS_RCODE_NOERROR &&
        answer_ip != 0 &&
        question.qtype == DNS_TYPE_A &&
        question.qclass == DNS_CLASS_IN) {
        RR_FIXED *record;

        if (offset + 2 + (int)sizeof(RR_FIXED) + 4 > response_size) {
            return -1;
        }

        response[offset++] = 0xC0;
        response[offset++] = 0x0C;

        record = (RR_FIXED *)(response + offset);
        record->type = htons(DNS_TYPE_A);
        record->dclass = htons(DNS_CLASS_IN);
        record->ttl = htonl(ttl);
        record->rdlength = htons(4);
        offset += sizeof(RR_FIXED);

        memcpy(response + offset, &answer_ip, 4);
        offset += 4;
        response_header->ancount = htons(1);
    }

    return offset;
}
