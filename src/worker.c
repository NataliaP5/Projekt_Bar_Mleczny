#include "common.h"
#include "ipc.h"
#include <signal.h>

static volatile sig_atomic_t g_stop = 0;
static void on_term(int sig){ (void)sig; g_stop = 1; }

static int msgsz(void){ return (int)(sizeof(Msg) - sizeof(long)); }

int main(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_term;
    sigaction(SIGTERM, &sa, NULL);

    IPC ipc;
    if (!ipc_open(&ipc, "ipc.key")) {
        log_line("worker", "Cannot open IPC (ipc.key missing?)");
        return 1;
    }

    log_line("worker", "Worker started pid=%d", (int)getpid());

    while (!g_stop) {
        Msg req;
        ssize_t r = msgrcv(ipc.msg_id, &req, msgsz(), MTYPE_WORKER, 0);
        if (r == -1) {
            if (errno == EINTR) continue;
            perror("worker msgrcv");
            break;
        }

        switch (req.kind) {
            case MSG_SERVE_REQ: {
                int ok = 1;
                sem_lock(ipc.sem_id);
                if (req.table_index < 0 || req.table_index >= ipc.st->tables_count) ok = 0;
                else {
                    Table *t = &ipc.st->tables[req.table_index];
                    if (req.group_size < 1 || req.group_size > 3) ok = 0;
                    else if (t->pending_seats < req.group_size) ok = 0;
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

                if (msgsnd(ipc.msg_id, &rep, msgsz(), 0) == -1) {
                    perror("worker msgsnd SERVE_REPLY");
                    g_stop = 1;
                }
                break;
            }

            case MSG_RESERVE_REQ: {
                int want = req.value;
                int got = reserve_seats_fixed(&ipc, want);
                log_line("worker", "RESERVE_REQ want=%d -> reserved=%d", want, got);

                Msg rep;
                memset(&rep, 0, sizeof(rep));
                rep.mtype = (long)req.pid;
                rep.kind = MSG_RESERVE_REPLY;
                rep.pid = req.pid;
                rep.value = got;

                if (msgsnd(ipc.msg_id, &rep, msgsz(), 0) == -1) {
                    perror("worker msgsnd RESERVE_REPLY");
                    g_stop = 1;
                }
                break;
            }

            case MSG_DISH_RETURN_REQ: {
                int dishes = req.value;
                if (dishes < 0) dishes = 0;

                sem_lock(ipc.sem_id);
                ipc.st->dishes_returned_total += dishes;
                sem_unlock(ipc.sem_id);

                log_line("worker", "DISH_RETURN from pid=%d table=%d dishes=%d (total=%d)",
                         (int)req.pid, req.table_index, dishes, ipc.st->dishes_returned_total);
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
