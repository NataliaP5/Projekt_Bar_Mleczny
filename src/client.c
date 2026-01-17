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

    int table = -1;
    while (!g_fire) {
        table = pick_table_and_reserve(&ipc, group, NULL);
        if (table != -1) break;
        sleep_ms(150);
    }

    if (g_fire) {
        log_line("client", "Client %d got SIGTERM before reserving, exiting", id);
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
        log_line("client", "Client %d PAY failed or interrupted -> cancel", id);
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
        log_line("client", "Client %d SERVE failed or interrupted -> cancel", id);
        cancel_reservation(&ipc, group, table);
        ipc_close(&ipc);
        return 0;
    }

    log_line("client", "Client %d SERVE ok -> seating", id);

    activate_seating(&ipc, group, table);
    sleep_ms(400 + (id % 5) * 120);

    finish_eating_and_leave(&ipc, group, table);
    log_line("client", "Client %d left table=%d", id, table);

    ipc_close(&ipc);
    return 0;
}
