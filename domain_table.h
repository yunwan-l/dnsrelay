#ifndef DOMAIN_TABLE_H
#define DOMAIN_TABLE_H

#include "dnsrelay.h"

typedef struct {
    char domain[DOMAIN_MAX];
    unsigned int ip;
    int is_blocked;
} DomainEntry;

typedef struct {
    DomainEntry entries[MAX_TABLE];
    int count;
} DomainTable;

typedef enum {
    DOMAIN_LOOKUP_MISS = -1,
    DOMAIN_LOOKUP_HIT = 0,
    DOMAIN_LOOKUP_BLOCKED = 1
} DomainLookupResult;

void domain_table_init(DomainTable *table);
int load_domain_table(DomainTable *table, const char *filename);
DomainLookupResult find_domain(const DomainTable *table, const char *domain,
                               unsigned int *out_ip);
void print_domain_table(const DomainTable *table);

#endif
