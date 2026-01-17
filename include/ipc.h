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

typedef struct {
    int shm_id;
    int sem_id;
    int msg_id;
    SharedState *st;
} IPC;

typedef struct {
    long mtype;
    int kind;
    pid_t pid;
    int group_size;
    int table_index;
    int value;
} Msg;

bool ipc_create(IPC *ipc, const char *keyfile);
bool ipc_open(IPC *ipc, const char *keyfile);
void ipc_close(IPC *ipc);
void ipc_destroy(IPC *ipc);

void sem_lock(int sem_id);
void sem_unlock(int sem_id);

void ipc_init_tables_for_manager(IPC *ipc, int x1, int x2, int x3, int x4);
