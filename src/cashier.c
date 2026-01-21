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
    if (sigaction(SIGTERM, &sa, NULL) == -1) DIE_PERROR("sigaction SIGTERM");

    IPC ipc;
    if (!ipc_open(&ipc, "ipc.key")) {
        log_line("cashier", "Cannot open IPC (ipc.key missing?)");
        return 1;
    }

    log_line("cashier", "Cashier started pid=%d", (int)getpid());

    while (!g_stop) {
        Msg req;
        ssize_t r = msgrcv(ipc.msg_id, &req, msgsz(), MTYPE_CASHIER, 0);
        if (r == -1) {
            if (errno == EINTR) continue;
            perror("cashier msgrcv");
            break;
        }

        if (req.kind != MSG_PAY_REQ) {
            log_line("cashier", "Ignoring msg kind=%d", req.kind);
            continue;
        }

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

        log_line("cashier", "PAY_REQ from pid=%d group=%d table=%d -> %s",
                 (int)req.pid, req.group_size, req.table_index, ok ? "OK" : "FAIL");

        Msg rep;
        memset(&rep, 0, sizeof(rep));
        rep.mtype = (long)req.pid;
        rep.kind = MSG_PAY_REPLY;
        rep.pid = req.pid;
        rep.group_size = req.group_size;
        rep.table_index = req.table_index;
        rep.value = ok;

        if (msgsnd(ipc.msg_id, &rep, msgsz(), 0) == -1) {
            perror("cashier msgsnd");
            break;
        }
    }

    log_line("cashier", "Cashier stopping.");
    ipc_close(&ipc);
    return 0;
}
