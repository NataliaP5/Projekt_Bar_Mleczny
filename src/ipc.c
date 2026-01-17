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
    ipc->st->x4 = x4;

    sem_unlock(ipc->sem_id);
}
