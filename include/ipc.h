#pragma once
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <stdbool.h>
#include "shared_state.h"

#define IPC_PERMS 0600
#define SEM_MUTEX 0

#define MTYPE_CASHIER 1
#define MTYPE_WORKER  2
#define MTYPE_MANAGER 3

#define MSG_PAY_REQ 1
#define MSG_PAY_REPLY 2
#define MSG_SERVE_REQ 3
#define MSG_SERVE_REPLY 4

#define MSG_RESERVE_REQ 5
#define MSG_RESERVE_REPLY 6
#define MSG_DISH_RETURN_REQ 7
#define MSG_RESERVE_TICK 8

#define MSG_CLIENT_PENDING 9
#define MSG_CLIENT_SEATED 10
#define MSG_CLIENT_LEFT 11
#define MSG_DISH_RETURN_REPLY 12
#define MSG_RESERVE_ASK 13

typedef struct {
    long mtype;
    int kind;
    pid_t pid;
    int value;
    int group_size;
    int table_index;
} Msg;

typedef struct {
    int shm_id;
    int sem_id;
    int msg_id;
    SharedState *st;
} IPC;

bool ipc_create(IPC *ipc, const char *keyfile);
bool ipc_open(IPC *ipc, const char *keyfile);
void ipc_close(IPC *ipc);
void ipc_destroy(IPC *ipc);

void sem_lock(int sem_id);
void sem_unlock(int sem_id);

void ipc_init_tables_for_manager(IPC *ipc, int x1, int x2, int x3, int x4);

int pick_table_and_reserve(IPC *ipc, int group_size, int *out_table);
void activate_seating(IPC *ipc, int group_size, int table_index);
void cancel_reservation(IPC *ipc, int group_size, int table_index);
void finish_eating_and_leave(IPC *ipc, int group_size, int table_index);

int add_more_x3_tables_once(IPC *ipc);
int reserve_seats_fixed(IPC *ipc, int seats);
