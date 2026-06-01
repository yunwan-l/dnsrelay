#include "relay_engine.h"

static int same_endpoint(const struct sockaddr_in *left,
                         const struct sockaddr_in *right)
{
    return left->sin_family == right->sin_family &&
           left->sin_addr.s_addr == right->sin_addr.s_addr &&
           left->sin_port == right->sin_port;
}

static void format_now(char *buffer, size_t buffer_size)
{
    time_t now = time(NULL);
    struct tm *local_tm = localtime(&now);

    if (local_tm == NULL) {
        strncpy(buffer, "00:00:00", buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        return;
    }

    strftime(buffer, buffer_size, "%H:%M:%S", local_tm);
}

static RelayMapping *find_mapping_by_upstream_id(RelayContext *context,
                                                 unsigned short upstream_id)
{
    int i;

    for (i = 0; i < MAX_PENDING_REQUESTS; i++) {
        if (context->mappings[i].in_use &&
            context->mappings[i].upstream_id == upstream_id) {
            return &context->mappings[i];
        }
    }

    return NULL;
}

static unsigned short allocate_upstream_id(RelayContext *context)
{
    unsigned int attempts;

    for (attempts = 0; attempts < 0xFFFFu; attempts++) {
        unsigned short candidate = context->next_upstream_id++;

        if (context->next_upstream_id == 0) {
            context->next_upstream_id = 1;
        }

        if (find_mapping_by_upstream_id(context, candidate) == NULL) {
            return candidate;
        }
    }

    return 0;
}

static RelayMapping *create_mapping(RelayContext *context,
                                    unsigned short client_id,
                                    const struct sockaddr_in *client_addr,
                                    const unsigned char *query, int query_len)
{
    int i;
    unsigned short upstream_id = allocate_upstream_id(context);

    if (upstream_id == 0) {
        return NULL;
    }

    for (i = 0; i < MAX_PENDING_REQUESTS; i++) {
        RelayMapping *mapping = &context->mappings[i];

        if (mapping->in_use) {
            continue;
        }

        memset(mapping, 0, sizeof(*mapping));
        mapping->upstream_id = upstream_id;
        mapping->client_id = client_id;
        mapping->client_addr = *client_addr;
        mapping->in_use = 1;
        mapping->timestamp = time(NULL);
        mapping->original_query_len = query_len;
        memcpy(mapping->original_query, query, query_len);

        context->active_count++;
        return mapping;
    }

    return NULL;
}

static void release_mapping(RelayContext *context, RelayMapping *mapping)
{
    if (mapping != NULL && mapping->in_use) {
        mapping->in_use = 0;
        context->active_count--;
    }
}

static int send_response_to_client(SOCKET server_socket,
                                   const struct sockaddr_in *client_addr,
                                   const unsigned char *response,
                                   int response_len)
{
    int sent_len = sendto(server_socket, (const char *)response, response_len, 0,
                          (const struct sockaddr *)client_addr,
                          sizeof(*client_addr));

    if (sent_len == SOCKET_ERROR) {
        DEBUG1("向客户端发送响应失败, error=%d", LAST_SOCKET_ERROR());
        return -1;
    }

    return 0;
}

static void send_servfail_for_mapping(SOCKET server_socket,
                                      const RelayMapping *mapping)
{
    unsigned char response[BUFFER_SIZE];
    int response_len = build_dns_response(mapping->original_query,
                                          mapping->original_query_len,
                                          response, sizeof(response),
                                          DNS_RCODE_SERVFAIL, 0, 0);

    if (response_len < 0) {
        DEBUG1("构造 SERVFAIL 响应失败，无法通知客户端超时。");
        return;
    }

    send_response_to_client(server_socket, &mapping->client_addr,
                            response, response_len);
}

static void log_query_summary(RelayContext *context,
                              unsigned short client_id,
                              const DnsQuestion *question,
                              const struct sockaddr_in *client_addr)
{
    char time_text[16];

    context->sequence_no++;
    format_now(time_text, sizeof(time_text));

    if (g_debug_level >= 1) {
        printf("[%s] Seq=%lu ID=%u QTYPE=%u FROM=%s:%u NAME=%s\n",
               time_text,
               context->sequence_no,
               client_id,
               question->qtype,
               inet_ntoa(client_addr->sin_addr),
               ntohs(client_addr->sin_port),
               question->qname);
    }
}

void relay_context_init(RelayContext *context,
                        const struct sockaddr_in *upstream_addr)
{
    memset(context, 0, sizeof(*context));
    context->upstream_addr = *upstream_addr;
    context->next_upstream_id = 1;
}

void relay_cleanup_timeouts(RelayContext *context, SOCKET server_socket)
{
    int i;
    time_t now = time(NULL);

    for (i = 0; i < MAX_PENDING_REQUESTS; i++) {
        RelayMapping *mapping = &context->mappings[i];

        if (!mapping->in_use) {
            continue;
        }

        if ((now - mapping->timestamp) > TIMEOUT_SEC) {
            DEBUG1("上游DNS超时: upstream_id=%u, client_id=%u",
                   mapping->upstream_id, mapping->client_id);
            send_servfail_for_mapping(server_socket, mapping);
            release_mapping(context, mapping);
        }
    }
}

int relay_is_upstream_response(const RelayContext *context,
                               const struct sockaddr_in *source_addr,
                               const unsigned char *packet, int packet_len)
{
    return same_endpoint(&context->upstream_addr, source_addr) &&
           dns_packet_is_response(packet, packet_len);
}

int relay_process_client_packet(RelayContext *context, SOCKET server_socket,
                                const DomainTable *domain_table,
                                const unsigned char *packet, int packet_len,
                                const struct sockaddr_in *client_addr)
{
    DNS_HEADER *request_header;
    DnsQuestion question;
    unsigned int answer_ip = 0;
    DomainLookupResult lookup_result;
    unsigned char response[BUFFER_SIZE];
    int response_len;

    if (packet_len < (int)sizeof(DNS_HEADER) || packet_len > BUFFER_SIZE) {
        return -1;
    }

    if (parse_dns_question(packet, packet_len, &question) != 0) {
        DEBUG1("收到格式错误或不支持的DNS查询，已忽略。");
        return -1;
    }

    request_header = (DNS_HEADER *)packet;
    log_query_summary(context, ntohs(request_header->id),
                      &question, client_addr);

    lookup_result = find_domain(domain_table, question.qname, &answer_ip);

    if (lookup_result == DOMAIN_LOOKUP_BLOCKED) {
        DEBUG1("本地拦截: %s -> NXDOMAIN", question.qname);
        response_len = build_dns_response(packet, packet_len, response,
                                          sizeof(response),
                                          DNS_RCODE_NXDOMAIN, 0, 0);
        if (response_len >= 0) {
            return send_response_to_client(server_socket, client_addr,
                                           response, response_len);
        }
        return -1;
    }

    if (lookup_result == DOMAIN_LOOKUP_HIT) {
        if (question.qtype == DNS_TYPE_A && question.qclass == DNS_CLASS_IN) {
            struct in_addr addr;

            addr.s_addr = answer_ip;
            DEBUG1("本地命中: %s -> %s", question.qname, inet_ntoa(addr));
            response_len = build_dns_response(packet, packet_len, response,
                                              sizeof(response),
                                              DNS_RCODE_NOERROR, answer_ip, 300);
        } else {
            DEBUG1("本地命中但类型非A/IN: %s, 返回空回答", question.qname);
            response_len = build_dns_response(packet, packet_len, response,
                                              sizeof(response),
                                              DNS_RCODE_NOERROR, 0, 0);
        }

        if (response_len >= 0) {
            return send_response_to_client(server_socket, client_addr,
                                           response, response_len);
        }
        return -1;
    }

    {
        RelayMapping *mapping;
        unsigned char relay_packet[BUFFER_SIZE];
        int sent_len;

        DEBUG1("中继查询: %s -> 外部DNS", question.qname);

        mapping = create_mapping(context, ntohs(request_header->id),
                                 client_addr, packet, packet_len);
        if (mapping == NULL) {
            DEBUG1("TID映射表已满，返回 SERVFAIL。");
            response_len = build_dns_response(packet, packet_len, response,
                                              sizeof(response),
                                              DNS_RCODE_SERVFAIL, 0, 0);
            if (response_len >= 0) {
                return send_response_to_client(server_socket, client_addr,
                                               response, response_len);
            }
            return -1;
        }

        memcpy(relay_packet, packet, packet_len);
        ((DNS_HEADER *)relay_packet)->id = htons(mapping->upstream_id);

        sent_len = sendto(server_socket, (const char *)relay_packet, packet_len, 0,
                          (const struct sockaddr *)&context->upstream_addr,
                          sizeof(context->upstream_addr));
        if (sent_len == SOCKET_ERROR) {
            DEBUG1("向上游DNS发送失败, error=%d", LAST_SOCKET_ERROR());
            release_mapping(context, mapping);

            response_len = build_dns_response(packet, packet_len, response,
                                              sizeof(response),
                                              DNS_RCODE_SERVFAIL, 0, 0);
            if (response_len >= 0) {
                return send_response_to_client(server_socket, client_addr,
                                               response, response_len);
            }
            return -1;
        }
    }

    return 0;
}

int relay_process_upstream_packet(RelayContext *context, SOCKET server_socket,
                                  const unsigned char *packet, int packet_len)
{
    const DNS_HEADER *upstream_header;
    RelayMapping *mapping;
    unsigned char response[BUFFER_SIZE];

    if (packet_len < (int)sizeof(DNS_HEADER) || packet_len > BUFFER_SIZE) {
        return -1;
    }

    upstream_header = (const DNS_HEADER *)packet;
    mapping = find_mapping_by_upstream_id(context, ntohs(upstream_header->id));
    if (mapping == NULL) {
        DEBUG1("收到迟到或无法匹配的上游响应，已丢弃。");
        return -1;
    }

    memcpy(response, packet, packet_len);
    ((DNS_HEADER *)response)->id = htons(mapping->client_id);

    if (send_response_to_client(server_socket, &mapping->client_addr,
                                response, packet_len) == 0) {
        DEBUG2("中继返回成功: upstream_id=%u -> client_id=%u",
               mapping->upstream_id, mapping->client_id);
    }

    release_mapping(context, mapping);
    return 0;
}
