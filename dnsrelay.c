      
/*
 * dnsrelay.c -- DNS Relay Server
 * Computer Network Course Design
 *
 * Functions:
 *   1) DNS Server:     Lookup local domain-IP table, reply directly if found
 *   2) Website Block:  When IP is 0.0.0.0, return NXDOMAIN (domain not found)
 *   3) DNS Relay:      If not found locally, forward to upstream DNS and relay reply
 *
 * Compile (Windows, MinGW):
 *   gcc dnsrelay.c -lws2_32 -o dnsrelay.exe
 *
 * Compile (Visual Studio):
 *   cl dnsrelay.c wsock32.lib
 *
 * Usage:
 *   dnsrelay                    # Default: upstream 202.106.0.20, config dnsrelay.txt
 *   dnsrelay -d                # Debug level 1
 *   dnsrelay -dd               # Debug level 2 (verbose)
 *   dnsrelay -d 8.8.8.8       # Set upstream DNS
 *   dnsrelay -d 8.8.8.8 C:\table.txt
 *
 * Config file format (each line):
 *   www.baidu.com 14.215.177.38
 *   www.example.com 0.0.0.0    -- 0.0.0.0 means block this domain
 */

#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #if defined(_MSC_VER)  /* MSVC needs manual definition */
        #define strcasecmp(s1, s2) _stricmp(s1, s2)
    #endif                 /* MinGW already has strcasecmp */
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <sys/time.h>
    #include <errno.h>
    typedef int SOCKET;
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR   (-1)
    #define closesocket(x) close(x)
#endif

/* ============================================================
 * Constants
 * ============================================================ */
#define DNS_PORT        53
#define BUFFER_SIZE     1024
#define MAX_TABLE       1024
#define DOMAIN_MAX      256
#define IP_STR_MAX      16
#define MAX_CLIENTS     128
#define TIMEOUT_SEC     5

/* ============================================================
 * Debug level (global)
 * ============================================================ */
int debug_level = 0;    /* 0=none, 1=basic, 2=verbose */

#define DEBUG1(fmt, ...) \
    do { if (debug_level >= 1) { printf("[DEBUG1] " fmt "\n", ##__VA_ARGS__); } } while (0)

#define DEBUG2(fmt, ...) \
    do { if (debug_level >= 2) { printf("[DEBUG2] " fmt "\n", ##__VA_ARGS__); } } while (0)

/* ============================================================
 * DNS Packet Structures (RFC 1035)
 * ============================================================ */

/* DNS Header -- 12 bytes, using bit-fields */
#pragma pack(push, 1)
typedef struct {
    unsigned short id;             /* query identification number */

    unsigned char  rd     : 1;    /* recursion desired */
    unsigned char  tc     : 1;    /* truncated message */
    unsigned char  aa     : 1;    /* authoritative answer */
    unsigned char  opcode : 4;    /* purpose of message */
    unsigned char  qr     : 1;    /* 0=query, 1=response */

    unsigned char  rcode  : 4;    /* response code (0=OK, 3=NXDOMAIN) */
    unsigned char  cd     : 1;    /* checking disabled */
    unsigned char  ad     : 1;    /* authentic data */
    unsigned char  z      : 1;    /* reserved */
    unsigned char  ra     : 1;    /* recursion available */

    unsigned short qdcount;       /* number of questions */
    unsigned short ancount;       /* number of answers */
    unsigned short nscount;       /* number of authority RRs */
    unsigned short arcount;       /* number of additional RRs */
} DNS_HEADER;
#pragma pack(pop)

/* DNS Resource Record (fixed part + variable RDATA) */
#pragma pack(push, 1)
typedef struct {
    unsigned short type;          /* 1=A, 5=CNAME, 15=MX ... */
    unsigned short dclass;        /* usually 1 (IN) */
    unsigned int   ttl;           /* time to live (seconds) */
    unsigned short rdlength;      /* RDATA length */
} RR_FIXED;
#pragma pack(pop)

/* ============================================================
 * Local domain-IP table
 * ============================================================ */
typedef struct {
    char domain[DOMAIN_MAX];
    unsigned int ip;              /* network byte order */
    int is_blocked;               /* 1=blocked (0.0.0.0), 0=normal */
} DomainEntry;

DomainEntry domain_table[MAX_TABLE];
int domain_count = 0;

/* ============================================================
 * TID mapping table (for concurrent clients)
 * ============================================================ */
typedef struct {
    unsigned short new_id;        /* ID sent to external DNS */
    unsigned short orig_id;       /* original client ID */
    struct sockaddr_in client;    /* original client address */
    int            in_use;
    time_t         timestamp;     /* send timestamp (for timeout cleanup) */
} TidMapping;

TidMapping tid_table[MAX_CLIENTS];
int tid_count = 0;

/* ============================================================
 * Function declarations
 * ============================================================ */
int  init_winsock(void);
void cleanup_winsock(void);
int  load_domain_table(const char *filename);
int  search_domain(const char *domain, unsigned int *out_ip);
void parse_qname(const unsigned char *buf, int buf_len, int offset,
                 char *out_name, int max_len);
int  build_qname(const char *domain, unsigned char *out_buf);
int  build_dns_response(const unsigned char *query, int query_len,
                        unsigned char *response, int rcode,
                        unsigned int answer_ip);
int  relay_to_ns(SOCKET sock, struct sockaddr_in *ns_addr,
                 const unsigned char *query, int query_len);
int  wait_ns_response(SOCKET sock, unsigned char *buf, int buf_size,
                      struct sockaddr_in *ns_addr, int timeout_sec);
int  handle_client_request(SOCKET server_sock, SOCKET ns_sock,
                           const unsigned char *req, int req_len,
                           struct sockaddr_in *client_addr,
                           struct sockaddr_in *ns_addr);
void print_domain_list(void);
void cleanup_stale_tids(void);
unsigned short get_new_tid(unsigned short orig_id,
                           struct sockaddr_in *client);
int  restore_original(unsigned short new_id,
                      unsigned short *out_orig_id,
                      struct sockaddr_in *out_client);

/* ============================================================
 * Socket initialization
 * ============================================================ */
int init_winsock(void)
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup failed\n");
        return -1;
    }
#endif
    return 0;
}

void cleanup_winsock(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

/* ============================================================
 * Load local domain-IP table from config file
 * ============================================================ */
int load_domain_table(const char *filename)
{
    FILE *fp;
    char line[512];
    char domain[DOMAIN_MAX];
    char ip_str[IP_STR_MAX];
    int  a, b, c, d;
    unsigned int ip;

    fp = fopen(filename, "r");
    if (fp == NULL) {
        printf("Warning: cannot open config file '%s', relay-only mode\n", filename);
        return 0;
    }

    domain_count = 0;
    while (fgets(line, sizeof(line), fp) && domain_count < MAX_TABLE) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        if (sscanf(line, "%s %s", domain, ip_str) < 2)
            continue;

        if (sscanf(ip_str, "%d.%d.%d.%d", &a, &b, &c, &d) != 4)
            continue;

        ip = htonl((a << 24) | (b << 16) | (c << 8) | d);

        strncpy(domain_table[domain_count].domain, domain,
                DOMAIN_MAX - 1);
        domain_table[domain_count].ip = ip;
        domain_table[domain_count].is_blocked = (ip == 0);
        domain_count++;

        DEBUG2("Load: %s -> %s (%s)", domain, ip_str,
               (ip == 0) ? "BLOCK" : "OK");
    }

    fclose(fp);
    printf("Loaded %d domain records\n", domain_count);
    return domain_count;
}

/* ============================================================
 * Domain lookup
 * ============================================================ */
int search_domain(const char *domain, unsigned int *out_ip)
{
    int i;
    for (i = 0; i < domain_count; i++) {
        if (strcasecmp(domain, domain_table[i].domain) == 0) {
            *out_ip = domain_table[i].ip;
            return domain_table[i].is_blocked ? 1 : 0;
        }
    }
    return -1;  /* not found */
}

/* ============================================================
 * Parse DNS QNAME (domain name in DNS format)
 * Supports pointer compression (RFC 1035)
 * ============================================================ */
void parse_qname(const unsigned char *buf, int buf_len, int offset,
                 char *out_name, int max_len)
{
    int pos = offset;
    int out_pos = 0;
    int jumped = 0;
    int len;
    int jump_count = 0;

    while (1) {
        if (pos >= buf_len) {
            out_name[out_pos] = '\0';
            return;
        }

        len = buf[pos];

        if ((len & 0xC0) == 0xC0) {  /* pointer compression */
            if (pos + 1 >= buf_len) break;
            if (!jumped) {
                jumped = 1;
            }
            pos = ((len & 0x3F) << 8) | buf[pos + 1];

            jump_count++;
            if (jump_count > 10) break;  /* prevent infinite loop */
            continue;
        }

        if (len == 0) break;  /* end of domain */

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

/* ============================================================
 * Encode domain name to DNS format (length+label)
 * ============================================================ */
int build_qname(const char *domain, unsigned char *out_buf)
{
    int pos = 0;
    int label_len = 0;
    int label_start = 0;
    int i;

    pos = 1;
    label_len = 0;

    for (i = 0; domain[i] != '\0'; i++) {
        if (domain[i] == '.') {
            out_buf[label_start] = (unsigned char)label_len;
            label_start = pos;
            label_len = 0;
            pos++;
        } else {
            out_buf[pos++] = (unsigned char)domain[i];
            label_len++;
        }
    }

    out_buf[label_start] = (unsigned char)label_len;
    out_buf[pos++] = 0;

    return pos;
}

/* ============================================================
 * Build DNS response packet
 *
 * Parameters:
 *   query     - client query packet
 *   query_len - query length
 *   response  - output buffer
 *   rcode     - response code (0=OK, 3=NXDOMAIN)
 *   answer_ip - answer IP (network byte order), valid only when rcode==0
 *
 * Returns: response packet length
 * ============================================================ */
int build_dns_response(const unsigned char *query, int query_len,
                       unsigned char *response, int rcode,
                       unsigned int answer_ip)
{
    DNS_HEADER *resp_hdr = (DNS_HEADER *)response;
    int qname_len;
    int offset;

    /* copy and modify header */
    memcpy(response, query, sizeof(DNS_HEADER));
    resp_hdr->qr     = 1;
    resp_hdr->aa     = 1;
    resp_hdr->ancount = htons(0);
    resp_hdr->nscount = htons(0);
    resp_hdr->arcount = htons(0);

    if (rcode == 3) {
        resp_hdr->rcode = 3;       /* NXDOMAIN */
    } else {
        resp_hdr->rcode = 0;
    }

    /* copy Question section */
    offset = sizeof(DNS_HEADER);

    /* calculate QNAME length */
    {
        int tmp = offset;
        while (tmp < query_len && query[tmp] != 0) {
            if ((query[tmp] & 0xC0) == 0xC0) {
                tmp += 2;
                break;
            }
            tmp += 1 + query[tmp];
        }
        tmp += 1;
        qname_len = tmp - offset;
    }

    memcpy(response + offset, query + offset, qname_len + 4);
    offset += qname_len + 4;

    /* add Answer section (A record) if rcode == 0 */
    if (rcode == 0 && answer_ip != 0) {
        RR_FIXED *rr;

        response[offset++] = 0xC0;
        response[offset++] = 0x0C;  /* pointer to QNAME in header */

        rr = (RR_FIXED *)(response + offset);
        rr->type    = htons(1);     /* A record */
        rr->dclass  = htons(1);     /* IN */
        rr->ttl     = htonl(300);
        rr->rdlength = htons(4);
        offset += sizeof(RR_FIXED);

        *(unsigned int *)(response + offset) = answer_ip;
        offset += 4;

        resp_hdr->ancount = htons(1);
    }

    return offset;
}

/* ============================================================
 * TID mapping operations
 * ============================================================ */

void cleanup_stale_tids(void)
{
    int i;
    time_t now = time(NULL);

    for (i = 0; i < MAX_CLIENTS; i++) {
        if (tid_table[i].in_use &&
            (now - tid_table[i].timestamp) > TIMEOUT_SEC) {
            DEBUG2("Clean stale TID: new_id=%u, orig_id=%u",
                   tid_table[i].new_id, tid_table[i].orig_id);
            tid_table[i].in_use = 0;
            tid_count--;
        }
    }
}

unsigned short get_new_tid(unsigned short orig_id,
                           struct sockaddr_in *client)
{
    int i;
    static unsigned short next_id = 1;

    cleanup_stale_tids();

    for (i = 0; i < MAX_CLIENTS; i++) {
        if (!tid_table[i].in_use) {
            while (1) {
                int conflict = 0;
                int j;
                for (j = 0; j < MAX_CLIENTS; j++) {
                    if (tid_table[j].in_use &&
                        tid_table[j].new_id == next_id) {
                        conflict = 1;
                        break;
                    }
                }
                if (!conflict) break;
                next_id++;
            }

            tid_table[i].new_id    = next_id;
            tid_table[i].orig_id   = orig_id;
            tid_table[i].client    = *client;
            tid_table[i].in_use    = 1;
            tid_table[i].timestamp = time(NULL);
            tid_count++;

            DEBUG2("Alloc TID: orig=%u -> new=%u, client %s:%d",
                   orig_id, next_id,
                   inet_ntoa(client->sin_addr),
                   ntohs(client->sin_port));

            return next_id++;
        }
    }

    DEBUG1("Warning: TID table full, cannot allocate ID");
    return orig_id;
}

int restore_original(unsigned short new_id,
                     unsigned short *out_orig_id,
                     struct sockaddr_in *out_client)
{
    int i;
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (tid_table[i].in_use && tid_table[i].new_id == new_id) {
            *out_orig_id = tid_table[i].orig_id;
            *out_client  = tid_table[i].client;
            tid_table[i].in_use = 0;
            tid_count--;

            DEBUG2("Restore TID: new=%u -> orig=%u", new_id, *out_orig_id);
            return 1;
        }
    }
    return 0;
}

/* ============================================================
 * Forward query to external DNS
 * ============================================================ */
int relay_to_ns(SOCKET sock, struct sockaddr_in *ns_addr,
                const unsigned char *query, int query_len)
{
    int ret = sendto(sock, (const char *)query, query_len, 0,
                     (struct sockaddr *)ns_addr, sizeof(*ns_addr));
    if (ret == SOCKET_ERROR) {
        DEBUG1("Send to external DNS failed, error=%d",
#ifdef _WIN32
               WSAGetLastError()
#else
               errno
#endif
              );
        return -1;
    }
    return ret;
}

/* ============================================================
 * Wait for external DNS response (with timeout)
 * ============================================================ */
int wait_ns_response(SOCKET sock, unsigned char *buf, int buf_size,
                     struct sockaddr_in *ns_addr, int timeout_sec)
{
    fd_set readfds;
    struct timeval tv;
    int ret;
    socklen_t addr_len = sizeof(*ns_addr);

    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);

    tv.tv_sec  = timeout_sec;
    tv.tv_usec = 0;

    ret = select((int)sock + 1, &readfds, NULL, NULL, &tv);
    if (ret == SOCKET_ERROR) {
        DEBUG1("select() failed");
        return -1;
    }
    if (ret == 0) {
        DEBUG2("Wait for external DNS response timeout");
        return -2;
    }

    ret = recvfrom(sock, (char *)buf, buf_size, 0,
                   (struct sockaddr *)ns_addr, &addr_len);
    if (ret == SOCKET_ERROR) {
        DEBUG1("recvfrom external DNS failed");
        return -1;
    }

    return ret;
}

/* ============================================================
 * Display local domain table
 * ============================================================ */
void print_domain_list(void)
{
    int i;
    printf("\nDomain table (%d records):\n", domain_count);
    printf("  %-30s %-16s %s\n", "Domain", "IP Address", "Status");
    printf("  " "------------------------------ ---------------- --------\n");
    for (i = 0; i < domain_count; i++) {
        struct in_addr addr;
        addr.s_addr = domain_table[i].ip;
        printf("  %-30s %-16s %s\n",
               domain_table[i].domain,
               inet_ntoa(addr),
               domain_table[i].is_blocked ? "[BLOCKED]" : "[OK]");
    }
    printf("\n");
}

/* ============================================================
 * Handle one client DNS request
 * ============================================================ */
int handle_client_request(SOCKET server_sock, SOCKET ns_sock,
                          const unsigned char *req, int req_len,
                          struct sockaddr_in *client_addr,
                          struct sockaddr_in *ns_addr)
{
    DNS_HEADER *hdr = (DNS_HEADER *)req;
    char qname[DOMAIN_MAX];
    unsigned int answer_ip;
    unsigned char response[BUFFER_SIZE];
    int resp_len;
    int search_result;

    if (req_len < (int)sizeof(DNS_HEADER) + 5) {
        DEBUG1("Received short packet, ignored");
        return -1;
    }

    parse_qname(req, req_len, sizeof(DNS_HEADER), qname, sizeof(qname));

    if (debug_level >= 1) {
        printf("[%ld] ID=%u, Q=%s\n",
               (long)time(NULL),
               ntohs(hdr->id), qname);
    }

    search_result = search_domain(qname, &answer_ip);

    if (search_result >= 0) {
        /* local hit */
        if (search_result == 1) {
            DEBUG1("BLOCKED: %s (0.0.0.0)", qname);
            resp_len = build_dns_response(req, req_len,
                                          response, 3, 0);
        } else {
            struct in_addr addr;
            addr.s_addr = answer_ip;
            DEBUG1("LOCAL HIT: %s -> %s", qname, inet_ntoa(addr));
            resp_len = build_dns_response(req, req_len,
                                          response, 0, answer_ip);
        }

        sendto(server_sock, (const char *)response, resp_len, 0,
               (struct sockaddr *)client_addr, sizeof(*client_addr));
        return 0;

    } else {
        /* relay mode */
        unsigned char relay_req[BUFFER_SIZE];
        int relay_len;
        unsigned short new_id;
        unsigned short orig_id;
        unsigned char ns_reply[BUFFER_SIZE];
        int ns_reply_len;
        DNS_HEADER *relay_hdr;

        DEBUG1("RELAY: %s -> external DNS", qname);

        new_id = get_new_tid(ntohs(hdr->id), client_addr);

        memcpy(relay_req, req, req_len);
        relay_len = req_len;
        relay_hdr = (DNS_HEADER *)relay_req;
        relay_hdr->id = htons(new_id);

        relay_to_ns(ns_sock, ns_addr, relay_req, relay_len);

        ns_reply_len = wait_ns_response(ns_sock, ns_reply,
                                        sizeof(ns_reply),
                                        ns_addr, TIMEOUT_SEC);

        if (ns_reply_len < 0) {
            if (ns_reply_len == -2) {
                DEBUG1("RELAY TIMEOUT: %s", qname);
            }
            return -1;
        }

        if (ns_reply_len >= (int)sizeof(DNS_HEADER)) {
            DNS_HEADER *reply_hdr = (DNS_HEADER *)ns_reply;
            unsigned short reply_new_id = ntohs(reply_hdr->id);

            if (restore_original(reply_new_id, &orig_id, client_addr)) {
                reply_hdr->id = htons(orig_id);
            } else {
                DEBUG1("Discard late external DNS response (ID=%u)", reply_new_id);
                return -1;
            }
        }

        sendto(server_sock, (const char *)ns_reply, ns_reply_len, 0,
               (struct sockaddr *)client_addr, sizeof(*client_addr));

        DEBUG2("RELAY RETURNED: %s (%d bytes)", qname, ns_reply_len);
        return 0;
    }
}

/* ============================================================
 * Main function
 * ============================================================ */
int main(int argc, char *argv[])
{
    SOCKET server_sock = INVALID_SOCKET;
    SOCKET ns_sock     = INVALID_SOCKET;
    struct sockaddr_in server_addr;
    struct sockaddr_in ns_addr;
    char config_file[256] = "dnsrelay.txt";
    char ns_ip[32]        = "202.106.0.20";
    unsigned char recvbuf[BUFFER_SIZE];
    int recv_len;
    struct sockaddr_in client_addr;
    socklen_t client_len;
    int optval;
    int i;

    /* ---- parse command line arguments ---- */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            debug_level = 1;
        } else if (strcmp(argv[i], "-dd") == 0) {
            debug_level = 2;
        } else if (argv[i][0] != '-') {
            strncpy(ns_ip, argv[i], sizeof(ns_ip) - 1);
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                strncpy(config_file, argv[i + 1], sizeof(config_file) - 1);
            }
            break;
        }
    }

    printf("========================================\n");
    printf("  DNS Relay Server v1.0\n");
    printf("  External DNS: %s\n", ns_ip);
    printf("  Config file: %s\n", config_file);
    printf("  Debug level: %d\n", debug_level);
    printf("========================================\n");

    /* ---- init Winsock ---- */
    if (init_winsock() != 0) {
        printf("Winsock init failed\n");
        return 1;
    }

    /* ---- create UDP sockets ---- */
    server_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_sock == INVALID_SOCKET) {
        printf("Create server socket failed\n");
        cleanup_winsock();
        return 1;
    }

    ns_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (ns_sock == INVALID_SOCKET) {
        printf("Create NS socket failed\n");
        closesocket(server_sock);
        cleanup_winsock();
        return 1;
    }

    /* ---- set address reuse ---- */
    optval = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&optval, sizeof(optval));

    /* ---- bind server ---- */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port        = htons(DNS_PORT);

    if (bind(server_sock, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Bind port %d failed (maybe in use)\n", DNS_PORT);
        printf("Please run as admin, or check port usage\n");
        closesocket(server_sock);
        closesocket(ns_sock);
        cleanup_winsock();
        return 1;
    }

    /* ---- set external DNS address ---- */
    memset(&ns_addr, 0, sizeof(ns_addr));
    ns_addr.sin_family      = AF_INET;
    ns_addr.sin_port        = htons(DNS_PORT);
    ns_addr.sin_addr.s_addr = inet_addr(ns_ip);

    if (ns_addr.sin_addr.s_addr == INADDR_NONE) {
        printf("Invalid DNS server address: %s\n", ns_ip);
        closesocket(server_sock);
        closesocket(ns_sock);
        cleanup_winsock();
        return 1;
    }

    /* ---- load domain table ---- */
    load_domain_table(config_file);
    if (debug_level >= 1) {
        print_domain_list();
    }

    printf("\nDNS Relay Server started, listening on port %d ...\n\n", DNS_PORT);
    printf("Hint: Set your DNS to 127.0.0.1 to use this service\n\n");

    /* ---- main loop ---- */
    while (1) {
        fd_set readfds;
        struct timeval tv;
        int ret;

        FD_ZERO(&readfds);
        FD_SET(server_sock, &readfds);

        tv.tv_sec  = 5;
        tv.tv_usec = 0;

        ret = select((int)server_sock + 1, &readfds, NULL, NULL, &tv);

        if (ret == SOCKET_ERROR) {
            DEBUG1("select() error");
            break;
        }

        if (ret == 0) {
            cleanup_stale_tids();
            continue;
        }

        client_len = sizeof(client_addr);
        recv_len = recvfrom(server_sock, (char *)recvbuf,
                            sizeof(recvbuf), 0,
                            (struct sockaddr *)&client_addr,
                            &client_len);

        if (recv_len == SOCKET_ERROR) {
            DEBUG1("recvfrom error");
            continue;
        }

        if (recv_len < (int)sizeof(DNS_HEADER)) {
            continue;
        }

        handle_client_request(server_sock, ns_sock,
                              recvbuf, recv_len, &client_addr,
                              &ns_addr);
    }

    /* ---- cleanup ---- */
    closesocket(server_sock);
    closesocket(ns_sock);
    cleanup_winsock();

    return 0;
}

    
