#ifndef RELAY_ENGINE_H
#define RELAY_ENGINE_H

#include "dns_protocol.h"
#include "domain_table.h"

typedef struct {
    unsigned short upstream_id;
    unsigned short client_id;
    struct sockaddr_in client_addr;
    int in_use;
    time_t timestamp;
    int original_query_len;
    unsigned char original_query[BUFFER_SIZE];
} RelayMapping;

typedef struct {
    struct sockaddr_in upstream_addr;
    RelayMapping mappings[MAX_PENDING_REQUESTS];
    int active_count;
    unsigned short next_upstream_id;
    unsigned long sequence_no;
} RelayContext;

void relay_context_init(RelayContext *context,
                        const struct sockaddr_in *upstream_addr);
void relay_cleanup_timeouts(RelayContext *context, SOCKET server_socket);
int relay_is_upstream_response(const RelayContext *context,
                               const struct sockaddr_in *source_addr,
                               const unsigned char *packet, int packet_len);
int relay_process_client_packet(RelayContext *context, SOCKET server_socket,
                                const DomainTable *domain_table,
                                const unsigned char *packet, int packet_len,
                                const struct sockaddr_in *client_addr);
int relay_process_upstream_packet(RelayContext *context, SOCKET server_socket,
                                  const unsigned char *packet, int packet_len);

#endif
