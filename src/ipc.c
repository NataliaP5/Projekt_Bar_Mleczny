#include "ipc.h"
#include "common.h"
#include <string.h>

union semun { int val; struct semid_ds *buf; unsigned short *array; };

static key_t make_key(const char *keyfile, int proj) {
    int fd = open(keyfile, O_CREAT | O_RDWR, 0600);
    if (fd == -1) DIE_PERROR("open keyfile");
    close(fd);

    key_t k = ftok(keyfile, proj);
    if (k == -1) DIE_PERROR("ftok");
    return k;
}

bool ipc_create(IPC *ipc, const char *keyfile) {
    memset(ipc, 0, sizeof(*ipc));

    key_t k_shm = make_key(keyfile, 0x43);
    key_t k_sem = make_key(keyfile, 0x44);
    key_t k_msg = make_key(keyfile, 0x45);

    ipc->shm_id = shmget(k_shm, sizeof(SharedState), IPC_CREAT | IPC_EXCL | IPC_PERMS);
    if (ipc->shm_id == -1) return false;

    ipc->sem_id = semget(k_sem, 1, IPC_CREAT | IPC_EXCL | IPC_PERMS);
    if (ipc->sem_id == -1) return false;

    ipc->msg_id = msgget(k_msg, IPC_CREAT | IPC_EXCL | IPC_PERMS);
    if (ipc->msg_id == -1) return false;

    ipc->st = (SharedState*)shmat(ipc->shm_id, NULL, 0);
    if (ipc->st == (void*)-1) DIE_PERROR("shmat");

    union semun su;
    su.val = 1;
    if (semctl(ipc->sem_id, SEM_MUTEX, SETVAL, su) == -1) DIE_PERROR("semctl SETVAL");

    memset(ipc->st, 0, sizeof(*ipc->st));
    return true;
}

bool ipc_open(IPC *ipc, const char *keyfile) {
    memset(ipc, 0, sizeof(*ipc));

    key_t k_shm = make_key(keyfile, 0x43);
    key_t k_sem = make_key(keyfile, 0x44);
    key_t k_msg = make_key(keyfile, 0x45);

    ipc->shm_id = shmget(k_shm, sizeof(SharedState), IPC_PERMS);
    if (ipc->shm_id == -1) return false;

    ipc->sem_id = semget(k_sem, 1, IPC_PERMS);
    if (ipc->sem_id == -1) return false;

    ipc->msg_id = msgget(k_msg, IPC_PERMS);
    if (ipc->msg_id == -1) return false;

    ipc->st = (SharedState*)shmat(ipc->shm_id, NULL, 0);
    if (ipc->st == (void*)-1) DIE_PERROR("shmat");
    return true;
}

void ipc_close(IPC *ipc) {
    if (ipc->st && ipc->st != (void*)-1) {
        if (shmdt(ipc->st) == -1) perror("shmdt");
    }
    ipc->st = NULL;
}

void ipc_destroy(IPC *ipc) {
    if (ipc->msg_id > 0) msgctl(ipc->msg_id, IPC_RMID, NULL);
    if (ipc->shm_id > 0) shmctl(ipc->shm_id, IPC_RMID, NULL);
    if (ipc->sem_id > 0) semctl(ipc->sem_id, 0, IPC_RMID);
}

void sem_lock(int sem_id) {
    struct sembuf op = { .sem_num = SEM_MUTEX, .sem_op = -1, .sem_flg = 0 };
    if (semop(sem_id, &op, 1) == -1) DIE_PERROR("semop lock");
}

void sem_unlock(int sem_id) {
    struct sembuf op = { .sem_num = SEM_MUTEX, .sem_op = +1, .sem_flg = 0 };
    if (semop(sem_id, &op, 1) == -1) DIE_PERROR("semop unlock");
}

void ipc_init_tables_for_manager(IPC *ipc, int x1, int x2, int x3, int x4) {
    sem_lock(ipc->sem_id);

    memset(ipc->st, 0, sizeof(*ipc->st));
    ipc->st->x1 = x1;
    ipc->st->x2 = x2;
    ipc->st->x3 = x3;
    ipc->st->x3_base = x3;
    ipc->st->x4 = x4;

    int idx = 0;

    for (int i = 0; i < x1 && idx < MAX_TABLES; i++, idx++) {
        ipc->st->tables[idx].capacity = 1;
    }
    for (int i = 0; i < x2 && idx < MAX_TABLES; i++, idx++) {
        ipc->st->tables[idx].capacity = 2;
    }
    for (int i = 0; i < x3 && idx < MAX_TABLES; i++, idx++) {
        ipc->st->tables[idx].capacity = 3;
    }
    for (int i = 0; i < x4 && idx < MAX_TABLES; i++, idx++) {
        ipc->st->tables[idx].capacity = 4;
    }

    ipc->st->tables_count = idx;

    sem_unlock(ipc->sem_id);
}

static bool can_reserve(Table *t, int group_size) {
    if (t->capacity <= 0) return false;
    if (t->group_size_allowed != 0 && t->group_size_allowed != group_size) return false;

    int used = t->reserved_fixed + t->occupied_seats + t->pending_seats;
    int free_seats = t->capacity - used;
    return free_seats >= group_size;
}

int pick_table_and_reserve(IPC *ipc, int group_size, int *out_table) {
    if (group_size < 1 || group_size > 3) return -1;

    sem_lock(ipc->sem_id);

    if (ipc->st->closing || ipc->st->fire_alarm) {
        sem_unlock(ipc->sem_id);
        return -1;
    }

    int best = -1;
    int best_waste = 9999;

    for (int i = 0; i < ipc->st->tables_count; i++) {
        Table *t = &ipc->st->tables[i];
        if (!can_reserve(t, group_size)) continue;

        int used = t->reserved_fixed + t->occupied_seats + t->pending_seats;
        int free_seats = t->capacity - used;
        int waste = free_seats - group_size;

        if (waste < best_waste) {
            best_waste = waste;
            best = i;
            if (waste == 0) break;
        }
    }

    if (best != -1) {
        Table *t = &ipc->st->tables[best];
        t->pending_seats += group_size;
        if (t->group_size_allowed == 0) t->group_size_allowed = group_size;
        if (out_table) *out_table = best;
    }

    sem_unlock(ipc->sem_id);
    return best;
}

void activate_seating(IPC *ipc, int group_size, int table_index) {
    sem_lock(ipc->sem_id);
    if (table_index >= 0 && table_index < ipc->st->tables_count) {
        Table *t = &ipc->st->tables[table_index];
        if (t->pending_seats >= group_size) {
            t->pending_seats -= group_size;
            t->occupied_seats += group_size;
            if (t->group_size_allowed == 0) t->group_size_allowed = group_size;
        }
    }
    sem_unlock(ipc->sem_id);
}

void cancel_reservation(IPC *ipc, int group_size, int table_index) {
    sem_lock(ipc->sem_id);
    if (table_index >= 0 && table_index < ipc->st->tables_count) {
        Table *t = &ipc->st->tables[table_index];
        if (t->pending_seats >= group_size) t->pending_seats -= group_size;

        if (t->occupied_seats == 0 && t->pending_seats == 0) {
            t->group_size_allowed = 0;
        }
    }
    sem_unlock(ipc->sem_id);
}

void finish_eating_and_leave(IPC *ipc, int group_size, int table_index) {
    sem_lock(ipc->sem_id);
    if (table_index >= 0 && table_index < ipc->st->tables_count) {
        Table *t = &ipc->st->tables[table_index];
        if (t->occupied_seats >= group_size) t->occupied_seats -= group_size;

        if (t->occupied_seats == 0 && t->pending_seats == 0) {
            t->group_size_allowed = 0;
        }
    }
    sem_unlock(ipc->sem_id);
}

int add_more_x3_tables_once(IPC *ipc) {
    int added = 0;
    sem_lock(ipc->sem_id);

    if (!ipc->st->x3_boost_used) {
        int can_add = ipc->st->x3_base;
        while (can_add > 0 && ipc->st->tables_count < MAX_TABLES) {
            ipc->st->tables[ipc->st->tables_count].capacity = 3;
            ipc->st->tables_count++;
            ipc->st->x3++;
            added++;
            can_add--;
        }
        ipc->st->x3_boost_used = 1;
    }

    sem_unlock(ipc->sem_id);
    return added;
}

int reserve_seats_fixed(IPC *ipc, int seats) {
    if (seats <= 0) return 0;

    int reserved = 0;
    sem_lock(ipc->sem_id);

    for (int i = 0; i < ipc->st->tables_count && reserved < seats; i++) {
        Table *t = &ipc->st->tables[i];

        int used = t->reserved_fixed + t->occupied_seats + t->pending_seats;
        int free = t->capacity - used;

        while (free > 0 && reserved < seats) {
            t->reserved_fixed++;
            reserved++;
            free--;
        }
    }

    sem_unlock(ipc->sem_id);
    return reserved;
}
