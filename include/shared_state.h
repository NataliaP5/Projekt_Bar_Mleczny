#pragma once
#include <sys/types.h>

#define MAX_TABLES 20000

typedef struct {
    int capacity;
    int reserved_fixed;
    int group_size_allowed;
    int occupied_seats;
    int pending_seats;
} Table;

typedef struct {
    int x1, x2, x3, x4;
    int x3_base;
    int tables_count;
    Table tables[MAX_TABLES];

    int cashier_queue_len; // ile procesow czeka na obsluzenie przez kasjera
    int fire_alarm;
    int closing;
    int x3_boost_used;
    int dishes_returned_total;
    long long revenue_total;
    int reserve_remaining;
} SharedState;
