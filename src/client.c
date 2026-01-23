#include "common.h"
#include "ipc.h"
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static volatile sig_atomic_t g_stop = 0;

static void on_term(int sig) {
    (void)sig;
    g_stop = 1;
}

static int msgsz(void){ return (int)(sizeof(Msg) - sizeof(long)); }

static void notify_manager(IPC *ipc, int kind, pid_t me, int group, int table, int value) {
    Msg m;
    memset(&m, 0, sizeof(m));
    m.mtype = MTYPE_MANAGER;
    m.kind = kind;
    m.pid = me;
    m.group_size = group;
    m.table_index = table;
    m.value = value;

    if (msgsnd(ipc->msg_id, &m, msgsz(), IPC_NOWAIT) == -1) {
        if (errno != EAGAIN) perror("client msgsnd MANAGER_NOTIFY");
    }
}

static int send_pay_request(IPC *ipc, pid_t me, int amount) {
    Msg m;
    memset(&m, 0, sizeof(m));
    m.mtype = MTYPE_CASHIER;
    m.kind  = MSG_PAY_REQ;
    m.pid   = me;
    m.value = amount;

    if (msgsnd(ipc->msg_id, &m, msgsz(), 0) == -1) {
        perror("client msgsnd PAY_REQ");
        return -1;
    }

    Msg rep;
    ssize_t r;
    for (;;) {
        r = msgrcv(ipc->msg_id, &rep, msgsz(), (long)me, 0);
        if (r == -1 && errno == EINTR) {
            if (g_stop) return -1;
            continue;
        }
        break;
    }
    if (r == -1) {
        perror("client msgrcv PAY_REPLY");
        return -1;
    }
    return rep.value;
}

static int send_serve_request(IPC *ipc, pid_t me, int group_size, int table_index) {
    Msg m;
    memset(&m, 0, sizeof(m));
    m.mtype = MTYPE_WORKER;
    m.kind  = MSG_SERVE_REQ;
    m.pid   = me;
    m.group_size  = group_size;
    m.table_index = table_index;

    if (msgsnd(ipc->msg_id, &m, msgsz(), 0) == -1) {
        perror("client msgsnd SERVE_REQ");
        return -1;
    }

    Msg rep;
    ssize_t r;
    for (;;) {
        r = msgrcv(ipc->msg_id, &rep, msgsz(), (long)me, 0);
        if (r == -1 && errno == EINTR) {
            if (g_stop) return -1;
            continue;
        }
        break;
    }
    if (r == -1) {
        perror("client msgrcv SERVE_REPLY");
        return -1;
    }
    return rep.value;
}

static void send_dish_return(IPC *ipc, pid_t me, int table_index, int dishes) {
    Msg m;
    memset(&m, 0, sizeof(m));
    m.mtype = MTYPE_WORKER;
    m.kind  = MSG_DISH_RETURN_REQ;
    m.pid   = me;
    m.table_index = table_index;
    m.value = dishes;

    if (msgsnd(ipc->msg_id, &m, msgsz(), IPC_NOWAIT) == -1) {
        if (errno != EAGAIN) perror("client msgsnd DISH_RETURN");
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s CLIENT_ID\n", argv[0]);
        return 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = on_term;
    if (sigaction(SIGTERM, &sa, NULL) == -1) DIE_PERROR("sigaction SIGTERM");
    if (sigaction(SIGINT,  &sa, NULL) == -1) DIE_PERROR("sigaction SIGINT");

    int id = parse_int(argv[1], 1, 10000000, "CLIENT_ID");
    pid_t me = getpid();

    IPC ipc;
    if (!ipc_open(&ipc, "ipc.key")) {
        fprintf(stderr, "client: cannot open IPC\n");
        return 1;
    }

    int group = (id % 3) + 1;

    if ((id % 20) == 0) {
        log_line("client", "id=%d pid=%d group=%d leaves_without_order", id, (int)me, group);
        ipc_close(&ipc);
        return 0;
    }

    int table = -1;
    int picked = pick_table_and_reserve(&ipc, group, &table);
    if (picked < 0) {
        log_line("client", "id=%d pid=%d group=%d no_table_available", id, (int)me, group);
        ipc_close(&ipc);
        return 0;
    }

    log_line("client", "id=%d pid=%d group=%d reserved_pending table=%d", id, (int)me, group, table);
    notify_manager(&ipc, MSG_CLIENT_PENDING, me, group, table, 0);

    if (g_stop) {
        cancel_reservation(&ipc, group, table);
        notify_manager(&ipc, MSG_CLIENT_LEFT, me, group, table, 0);
        ipc_close(&ipc);
        return 0;
    }

    int pay_ok = send_pay_request(&ipc, me, 10 * group);
    if (pay_ok <= 0 || g_stop) {
        cancel_reservation(&ipc, group, table);
        notify_manager(&ipc, MSG_CLIENT_LEFT, me, group, table, 0);
        log_line("client", "id=%d pid=%d pay_failed -> cancel", id, (int)me);
        ipc_close(&ipc);
        return 0;
    }

    int serve_ok = send_serve_request(&ipc, me, group, table);
    if (serve_ok <= 0 || g_stop) {
        cancel_reservation(&ipc, group, table);
        notify_manager(&ipc, MSG_CLIENT_LEFT, me, group, table, 0);
        log_line("client", "id=%d pid=%d serve_failed -> cancel", id, (int)me);
        ipc_close(&ipc);
        return 0;
    }

    activate_seating(&ipc, group, table);
    notify_manager(&ipc, MSG_CLIENT_SEATED, me, group, table, 1);
    log_line("client", "id=%d pid=%d seated table=%d group=%d", id, (int)me, table, group);

    int eat_ms = EAT_BASE_MS + (id % EAT_VARIANTS) * EAT_STEP_MS;
    sleep_ms(eat_ms);

    if (g_stop) {
        send_dish_return(&ipc, me, table, group);
        finish_eating_and_leave(&ipc, group, table);
        notify_manager(&ipc, MSG_CLIENT_LEFT, me, group, table, 1);
        ipc_close(&ipc);
        return 0;
    }

    send_dish_return(&ipc, me, table, group);
    finish_eating_and_leave(&ipc, group, table);
    notify_manager(&ipc, MSG_CLIENT_LEFT, me, group, table, 1);
    log_line("client", "id=%d pid=%d left table=%d group=%d", id, (int)me, table, group);

    ipc_close(&ipc);
    return 0;
}
