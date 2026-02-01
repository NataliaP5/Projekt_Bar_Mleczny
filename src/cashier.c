#include "common.h"
#include "ipc.h"
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;
static void on_term(int sig){ (void)sig; g_stop = 1; }

static int msgsz(void){ return (int)(sizeof(Msg) - sizeof(long)); }

static const char *menu_names[] = {
    "Pierogi ruskie",
    "Nalesniki z serem",
    "Kotlet schabowy",
    "Rosol",
    "Pomidorowka"
};

int main(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
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
            if (errno == EINTR) {
                if (g_stop) break;
                continue;
            }
            perror("cashier msgrcv");
            break;
        }

        if (req.kind != MSG_PAY_REQ) {
            log_line("cashier", "Ignoring msg kind=%d", req.kind);
            continue;
        }

        const char *dish = "UNKNOWN";
        int n = (int)(sizeof(menu_names) / sizeof(menu_names[0]));
        if (req.dish_id >= 0 && req.dish_id < n) {
            dish = menu_names[req.dish_id];
        }

        int ok = 1;
        long long new_total = -1;

        sem_lock(ipc.sem_id);
        if (ipc.st->fire_alarm) ok = 0;
        if (ipc.st->closing) ok = 0;

        if (ok) {
            ipc.st->revenue_total += (long long)req.value;
            new_total = ipc.st->revenue_total;
        }
        sem_unlock(ipc.sem_id);

        if (ok) {
            log_line("cashier", "PAY_REQ pid=%d group=%d table=%d dish=%s amount=%d -> OK (revenue=%lld)",
                     (int)req.pid, req.group_size, req.table_index, dish, req.value, new_total);
        } else {
            log_line("cashier", "PAY_REQ pid=%d group=%d table=%d dish=%s amount=%d -> FAIL",
                     (int)req.pid, req.group_size, req.table_index, dish, req.value);
        }

        Msg rep;
        memset(&rep, 0, sizeof(rep));
        rep.mtype = (long)req.pid;
        rep.kind = MSG_PAY_REPLY;
        rep.pid = req.pid;
        rep.group_size = req.group_size;
        rep.table_index = req.table_index;
        rep.value = ok;

        if (msgsnd(ipc.msg_id, &rep, msgsz(), 0) == -1) {
            perror("cashier msgsnd PAY_REPLY");
            g_stop = 1;
        }
    }

    log_line("cashier", "Cashier stopping.");
    ipc_close(&ipc);
    return 0;
}
