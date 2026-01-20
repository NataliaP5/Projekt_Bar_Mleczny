#include "common.h"
#include "ipc.h"
#include <signal.h>

static volatile sig_atomic_t g_fire = 0;
static void on_term(int sig){ (void)sig; g_fire = 1; }

static int msgsz(void){ return (int)(sizeof(Msg) - sizeof(long)); }

static int wait_reply(IPC *ipc, long mytype, int expected_kind, Msg *out) {
    while (!g_fire) {
        Msg rep;
        ssize_t r = msgrcv(ipc->msg_id, &rep, msgsz(), mytype, 0);
        if (r == -1) {
            if (errno == EINTR) continue;
            perror("client msgrcv");
            return 0;
        }
        if (rep.kind == expected_kind) {
            if (out) *out = rep;
            return 1;
        }
    }
    return 0;
}

static void eat_interruptible(int total_ms) {
    const int step = 80;
    int left = total_ms;
    while (left > 0 && !g_fire) {
        int s = (left > step) ? step : left;
        sleep_ms(s);
        left -= s;
    }
}

int main(int argc, char **argv) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_term;
    sigaction(SIGTERM, &sa, NULL);

    int id = (argc >= 2) ? parse_int(argv[1], 0, 1000000, "client_id") : 0;

    IPC ipc;
    if (!ipc_open(&ipc, "ipc.key")) {
        log_line("client", "Client %d cannot open IPC", id);
        return 1;
    }

    pid_t me = getpid();
    int group = (id % 3) + 1;

    int evacuated = 0;
    int table = -1;
    int seated = 0;

    unsigned h = (unsigned)(now_ms() ^ (unsigned)getpid() ^ (unsigned)(id * 2654435761u));
    int no_order = (h % 100) < 5;
    if (no_order) {
        log_line("client", "Client %d: no order (leaves immediately)", id);
        ipc_close(&ipc);
        return 0;
    }

    while (!g_fire) {
        table = pick_table_and_reserve(&ipc, group, NULL);
        if (table != -1) break;
        sleep_ms(120);
    }

    if (g_fire) {
        evacuated = 1;
        log_line("client", "Client %d evacuated before reserving seat", id);
        ipc_close(&ipc);
        return 0;
    }

    log_line("client", "Client %d group=%d reserved table=%d (pending)", id, group, table);

    Msg pay;
    memset(&pay, 0, sizeof(pay));
    pay.mtype = MTYPE_CASHIER;
    pay.kind = MSG_PAY_REQ;
    pay.pid = me;
    pay.group_size = group;
    pay.table_index = table;

    if (msgsnd(ipc.msg_id, &pay, msgsz(), 0) == -1) {
        perror("client msgsnd PAY_REQ");
        cancel_reservation(&ipc, group, table);
        ipc_close(&ipc);
        return 1;
    }

    Msg payrep;
    if (!wait_reply(&ipc, (long)me, MSG_PAY_REPLY, &payrep) || !payrep.value) {
        if (g_fire) evacuated = 1;
        log_line("client", "Client %d PAY failed/evacuated -> cancel pending", id);
        cancel_reservation(&ipc, group, table);
        ipc_close(&ipc);
        return 0;
    }

    log_line("client", "Client %d PAY ok", id);

    Msg srv;
    memset(&srv, 0, sizeof(srv));
    srv.mtype = MTYPE_WORKER;
    srv.kind = MSG_SERVE_REQ;
    srv.pid = me;
    srv.group_size = group;
    srv.table_index = table;

    if (msgsnd(ipc.msg_id, &srv, msgsz(), 0) == -1) {
        perror("client msgsnd SERVE_REQ");
        cancel_reservation(&ipc, group, table);
        ipc_close(&ipc);
        return 1;
    }

    Msg srvrep;
    if (!wait_reply(&ipc, (long)me, MSG_SERVE_REPLY, &srvrep) || !srvrep.value) {
        if (g_fire) evacuated = 1;
        log_line("client", "Client %d SERVE failed/evacuated -> cancel pending", id);
        cancel_reservation(&ipc, group, table);
        ipc_close(&ipc);
        return 0;
    }

    log_line("client", "Client %d SERVE ok -> seating", id);

    activate_seating(&ipc, group, table);
    seated = 1;

    int eat_ms = 400 + (id % 5) * 120;
    eat_interruptible(eat_ms);

    if (g_fire) {
        evacuated = 1;
        log_line("client", "Client %d evacuated during meal", id);
    }

    if (seated) {
        finish_eating_and_leave(&ipc, group, table);
    } else if (table != -1) {
        cancel_reservation(&ipc, group, table);
    }

    if (!evacuated) {
        unsigned h2 = (unsigned)(now_ms() ^ (unsigned)getpid() ^ (unsigned)(id * 1103515245u));
        int collective = (h2 % 2);

        if (collective) {
            Msg d;
            memset(&d, 0, sizeof(d));
            d.mtype = MTYPE_WORKER;
            d.kind = MSG_DISH_RETURN_REQ;
            d.pid = me;
            d.group_size = group;
            d.table_index = table;
            d.value = group;

            if (msgsnd(ipc.msg_id, &d, msgsz(), 0) == -1) {
                perror("client msgsnd DISH_RETURN (collective)");
            } else {
                log_line("client", "Client %d returned dishes collectively=%d", id, group);
            }
        } else {
            for (int i = 0; i < group; i++) {
                Msg d;
                memset(&d, 0, sizeof(d));
                d.mtype = MTYPE_WORKER;
                d.kind = MSG_DISH_RETURN_REQ;
                d.pid = me;
                d.group_size = group;
                d.table_index = table;
                d.value = 1;

                if (msgsnd(ipc.msg_id, &d, msgsz(), 0) == -1) {
                    perror("client msgsnd DISH_RETURN (single)");
                    break;
                }
            }
            log_line("client", "Client %d returned dishes individually=%d", id, group);
        }
    } else {
        log_line("client", "Client %d left dishes on table (evacuation)", id);
    }

    log_line("client", "Client %d left table=%d", id, table);

    ipc_close(&ipc);
    return 0;
}
