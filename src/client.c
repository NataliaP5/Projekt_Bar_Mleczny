#include "common.h"
#include "ipc.h"
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#define WAIT_SEAT_TIMEOUT_MS 8000

static volatile sig_atomic_t g_stop = 0;
static void on_term(int sig) { (void)sig; g_stop = 1; }

static int msgsz(void) { return (int)(sizeof(Msg) - sizeof(long)); }

static int is_fire_now(IPC *ipc) {
    int fire = 0;
    sem_lock(ipc->sem_id);
    fire = ipc->st->fire_alarm;
    sem_unlock(ipc->sem_id);
    return fire;
}

static void notify_manager(IPC *ipc, int kind, pid_t me, int group, int table, int value) {
    Msg m;
    memset(&m, 0, sizeof(m));
    m.mtype = MTYPE_MANAGER;
    m.kind = kind;
    m.pid = me;
    m.group_size = group;
    m.table_index = table;
    m.value = value;

    for (;;) {
        if (msgsnd(ipc->msg_id, &m, msgsz(), 0) == 0) return;

        if (errno == EINTR) {
            if (is_fire_now(ipc) || g_stop) return;
            continue;
        }

        perror("client msgsnd MANAGER_NOTIFY");
        return;
    }
}


static int wait_reply(IPC *ipc, long mytype, int expected_kind, Msg *out) {
    while (!g_stop) {
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

static int send_pay_request(IPC *ipc, pid_t me, int group, int table, int amount) {
    Msg m;
    memset(&m, 0, sizeof(m));
    m.mtype = MTYPE_CASHIER;
    m.kind = MSG_PAY_REQ;
    m.pid = me;
    m.group_size = group;
    m.table_index = table;
    m.value = amount;

    if (msgsnd(ipc->msg_id, &m, msgsz(), 0) == -1) {
        perror("client msgsnd PAY_REQ");
        return 0;
    }

    Msg rep;
    if (!wait_reply(ipc, (long)me, MSG_PAY_REPLY, &rep)) return 0;
    return rep.value != 0;
}

static int send_serve_request(IPC *ipc, pid_t me, int group, int table) {
    Msg m;
    memset(&m, 0, sizeof(m));
    m.mtype = MTYPE_WORKER;
    m.kind = MSG_SERVE_REQ;
    m.pid = me;
    m.group_size = group;
    m.table_index = table;

    if (msgsnd(ipc->msg_id, &m, msgsz(), 0) == -1) {
        perror("client msgsnd SERVE_REQ");
        return 0;
    }

    Msg rep;
    if (!wait_reply(ipc, (long)me, MSG_SERVE_REPLY, &rep)) return 0;
    return rep.value != 0;
}

static void eat_interruptible(int total_ms) {
    const int step = 20;
    int left = total_ms;
    while (left > 0 && !g_stop) {
        int s = (left > step) ? step : left;
        sleep_ms(s);
        left -= s;
    }
}

static void send_dish_return_fancy(IPC *ipc, pid_t me, int id, int group, int table, int fire) {
    if (fire) {
        log_line("client", "Client %d FIRE: left dishes on table=%d group=%d", id, table, group);
        return;
    }

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

        if (msgsnd(ipc->msg_id, &d, msgsz(), IPC_NOWAIT) == -1) {
            if (errno != EAGAIN) perror("client msgsnd DISH_RETURN (collective)");
        }
    } else {
        for (int j = 0; j < group; j++) {
            Msg d;
            memset(&d, 0, sizeof(d));
            d.mtype = MTYPE_WORKER;
            d.kind = MSG_DISH_RETURN_REQ;
            d.pid = me;
            d.group_size = group;
            d.table_index = table;
            d.value = 1;

            if (msgsnd(ipc->msg_id, &d, msgsz(), IPC_NOWAIT) == -1) {
                if (errno != EAGAIN) perror("client msgsnd DISH_RETURN (single)");
                break;
            }
        }
    }

    if (g_stop) log_line("client", "Client %d force-closed -> returned dishes and left", id);
    else        log_line("client", "Client %d finished -> returned dishes and left", id);
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
        log_line("client", "Client %d cannot open IPC", id);
        return 1;
    }

    int group = (id % 3) + 1;
    int table = -1;
    int seated = 0;
    int pending = 0;

    unsigned h = (unsigned)(now_ms() ^ (unsigned)getpid() ^ (unsigned)(id * 2654435761u));
    int no_order = (h % 100) < 5;
    if (no_order) {
        log_line("client", "Client %d: no order (leaves immediately)", id);
        ipc_close(&ipc);
        return 0;
    }

    long long seat_deadline = now_ms() + WAIT_SEAT_TIMEOUT_MS;
    while (!g_stop) {
        int picked = pick_table_and_reserve(&ipc, group, &table);
        if (picked >= 0) { pending = 1; break; }

        long long now = now_ms();
        if (now >= seat_deadline) break;

        int remaining = (int)(seat_deadline - now);
        int wr = sem_timedwait_event_ms(ipc.sem_id, SEM_TABLE_EVENT, remaining);
        if (wr < 0) {
            continue;
        }
    }

    if (!pending || g_stop) {
        int fire = is_fire_now(&ipc);
        if (fire) {
            log_line("client", "Client %d evacuated before reserving seat", id);
        } else if (g_stop) {
            log_line("client", "Client %d stopped before reserving seat", id);
        } else {
            log_line("client", "Client %d left (no seat after %dms)", id, WAIT_SEAT_TIMEOUT_MS);
        }
        ipc_close(&ipc);
        return 0;
    }

    log_line("client", "Client %d group=%d reserved table=%d (pending)", id, group, table);
    notify_manager(&ipc, MSG_CLIENT_PENDING, me, group, table, 0);

    if (!send_pay_request(&ipc, me, group, table, 10 * group) || g_stop) {
        int fire = is_fire_now(&ipc);
        if (fire) log_line("client", "Client %d PAY interrupted by FIRE -> cancel pending", id);
        else      log_line("client", "Client %d PAY interrupted/failed -> cancel pending", id);

        cancel_reservation(&ipc, group, table);
        notify_manager(&ipc, MSG_CLIENT_LEFT, me, group, table, 0);
        ipc_close(&ipc);
        return 0;
    }

    if (!send_serve_request(&ipc, me, group, table) || g_stop) {
        int fire = is_fire_now(&ipc);
        if (fire) log_line("client", "Client %d SERVE interrupted by FIRE -> cancel pending", id);
        else      log_line("client", "Client %d SERVE interrupted/failed -> cancel pending", id);

        cancel_reservation(&ipc, group, table);
        notify_manager(&ipc, MSG_CLIENT_LEFT, me, group, table, 0);
        ipc_close(&ipc);
        return 0;
    }

    activate_seating(&ipc, group, table);
    seated = 1;
    pending = 0;
    notify_manager(&ipc, MSG_CLIENT_SEATED, me, group, table, 1);
    log_line("client", "Client %d seated table=%d group=%d", id, table, group);

    int eat_ms = EAT_BASE_MS + (id % EAT_VARIANTS) * EAT_STEP_MS;
    eat_interruptible(eat_ms);

    int fire = is_fire_now(&ipc);

    if (seated) {
        send_dish_return_fancy(&ipc, me, id, group, table, fire);
        finish_eating_and_leave(&ipc, group, table);
        notify_manager(&ipc, MSG_CLIENT_LEFT, me, group, table, 1);
        log_line("client", "Client %d left table=%d", id, table);
    } else if (pending) {
        cancel_reservation(&ipc, group, table);
        notify_manager(&ipc, MSG_CLIENT_LEFT, me, group, table, 0);
        log_line("client", "Client %d canceled pending table=%d", id, table);
    }

    ipc_close(&ipc);
    return 0;
}
