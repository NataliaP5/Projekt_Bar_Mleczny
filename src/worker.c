#include "common.h"
#include "ipc.h"
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;
static void on_term(int sig){ (void)sig; g_stop = 1; }

static volatile sig_atomic_t g_sigusr1 = 0;
static volatile sig_atomic_t g_sigusr2 = 0;
static void on_usr1(int sig){ (void)sig; g_sigusr1 = 1; }
static void on_usr2(int sig){ (void)sig; g_sigusr2 = 1; }

static int msgsz(void){ return (int)(sizeof(Msg) - sizeof(long)); }

static int do_reserve_attempt(IPC *ipc) {
    sem_lock(ipc->sem_id);
    int rem = ipc->st->reserve_remaining;
    sem_unlock(ipc->sem_id);

    if (rem <= 0) return 0;

    int got = reserve_seats_fixed(ipc, rem);

    sem_lock(ipc->sem_id);
    if (got > ipc->st->reserve_remaining) got = ipc->st->reserve_remaining;
    ipc->st->reserve_remaining -= got;
    int left = ipc->st->reserve_remaining;
    sem_unlock(ipc->sem_id);

    log_line("worker", "RESERVE progress: requested_rem=%d got=%d left=%d", rem, got, left);
    return got;
}

int main(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sa.sa_handler = on_term;
    if (sigaction(SIGTERM, &sa, NULL) == -1) DIE_PERROR("sigaction SIGTERM");

    struct sigaction si;
    memset(&si, 0, sizeof(si));
    sigemptyset(&si.sa_mask);
    si.sa_flags = 0;
    si.sa_handler = SIG_IGN;
    if (sigaction(SIGINT, &si, NULL) == -1) DIE_PERROR("sigaction SIGINT");

    sa.sa_handler = on_usr1;
    if (sigaction(SIGUSR1, &sa, NULL) == -1) DIE_PERROR("sigaction SIGUSR1");
    sa.sa_handler = on_usr2;
    if (sigaction(SIGUSR2, &sa, NULL) == -1) DIE_PERROR("sigaction SIGUSR2");

    IPC ipc;
    if (!ipc_open(&ipc, "ipc.key")) {
        log_line("worker", "Cannot open IPC (ipc.key missing?)");
        return 1;
    }

    log_line("worker", "Worker started pid=%d", (int)getpid());

    while (!g_stop) {
        if (g_sigusr1) {
            g_sigusr1 = 0;
            int added = add_more_x3_tables_once(&ipc);
            if (added > 0) log_line("worker", "SIGUSR1: added %d new 3-seat tables (once)", added);
            else           log_line("worker", "SIGUSR1: ignored (boost already used)");
        }

        if (g_sigusr2) {
            g_sigusr2 = 0;
            Msg ask;
            memset(&ask, 0, sizeof(ask));
            ask.mtype = MTYPE_MANAGER;
            ask.kind  = MSG_RESERVE_ASK;
            ask.pid   = getpid();
            if (msgsnd(ipc.msg_id, &ask, msgsz(), IPC_NOWAIT) == -1) {
                if (errno != EAGAIN && errno != EINTR) perror("worker msgsnd RESERVE_ASK");
            } else {
                log_line("worker", "SIGUSR2: sent RESERVE_ASK to manager");
            }
        }

        Msg req;
        ssize_t r = msgrcv(ipc.msg_work_req_id, &req, msgsz(), MTYPE_WORKER, 0);
        if (r == -1) {
            if (errno == EINTR) {
                if (g_stop) break;
                continue;
            }
            perror("worker msgrcv");
            break;
        }

        switch (req.kind) {
            case MSG_SERVE_REQ: {
                int ok = 1;

                sem_lock(ipc.sem_id);
                if (req.table_index < 0 || req.table_index >= ipc.st->tables_count) {
                    ok = 0;
                } else {
                    Table *t = &ipc.st->tables[req.table_index];

                    if (req.group_size < 1 || req.group_size > 4) ok = 0;

                    else if (t->capacity < req.group_size) ok = 0;

                    if (ipc.st->fire_alarm || ipc.st->closing) ok = 0;
                }
                sem_unlock(ipc.sem_id);

                log_line("worker", "SERVE_REQ from pid=%d group=%d table=%d -> %s",
                         (int)req.pid, req.group_size, req.table_index, ok ? "OK" : "FAIL");

                Msg rep;
                memset(&rep, 0, sizeof(rep));
                rep.mtype = (long)req.pid;
                rep.kind = MSG_SERVE_REPLY;
                rep.pid = req.pid;
                rep.group_size = req.group_size;
                rep.table_index = req.table_index;
                rep.value = ok;

                for (;;) {
                    if (msgsnd(ipc.msg_work_rep_id, &rep, msgsz(), 0) == 0) break;
                    if (errno == EINTR) {
                        if (g_stop) break;
                        continue;
                    }
                    perror("worker msgsnd SERVE_REPLY");
                    g_stop = 1;
                    break;
                }
                break;
            }

            case MSG_RESERVE_REQ: {
                int got = do_reserve_attempt(&ipc);

                if (req.pid > 0) {
                    Msg rep;
                    memset(&rep, 0, sizeof(rep));
                    rep.mtype = (long)req.pid;
                    rep.kind = MSG_RESERVE_REPLY;
                    rep.pid = req.pid;
                    rep.value = got;

                    for (;;) {
                        if (msgsnd(ipc.msg_work_rep_id, &rep, msgsz(), 0) == 0) break;
                        if (errno == EINTR) {
                            if (g_stop) break;
                            continue;
                        }
                        perror("worker msgsnd RESERVE_REPLY");
                        g_stop = 1;
                        break;
                    }
                }
                break;
            }

            case MSG_RESERVE_TICK: {
                (void)do_reserve_attempt(&ipc);
                break;
            }

            case MSG_DISH_RETURN_REQ: {
                int dishes = req.value;
                if (dishes < 0) dishes = 0;

                sem_lock(ipc.sem_id);
                ipc.st->dishes_returned_total += dishes;
                int total = ipc.st->dishes_returned_total;
                sem_unlock(ipc.sem_id);

                log_line("worker", "DISH_RETURN from pid=%d table=%d dishes=%d (total=%d)",
                         (int)req.pid, req.table_index, dishes, total);
                break;
            }

            default:
                log_line("worker", "Ignoring msg kind=%d", req.kind);
                break;
        }
    }

    log_line("worker", "Worker stopping.");
    ipc_close(&ipc);
    return 0;
}
