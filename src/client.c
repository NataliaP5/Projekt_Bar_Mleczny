#include "common.h"
#include "ipc.h"
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>

//#define WAIT_SEAT_TIMEOUT_MS 8000

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

// licznik kolejki do kasjera w shm
static void cashier_queue_inc(IPC *ipc) {
    sem_lock(ipc->sem_id);
    ipc->st->cashier_queue_len++;
    sem_unlock(ipc->sem_id);
}

static void cashier_queue_dec_safe(IPC *ipc) {
    sem_lock(ipc->sem_id);
    if (ipc->st->cashier_queue_len > 0) ipc->st->cashier_queue_len--;
    sem_unlock(ipc->sem_id);
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
            if (is_fire_now(ipc)) return;
            continue;
        }

        perror("client msgsnd MANAGER_NOTIFY");
        return;
    }
}

// wait_reply na dowolnej kolejce (reply queue)
static int wait_reply_on_queue(IPC *ipc, int msg_id, long mytype, int expected_kind, Msg *out) {
    for (;;) {
        Msg rep;
        ssize_t r = msgrcv(msg_id, &rep, msgsz(), mytype, 0);
        if (r == -1) {
            if (errno == EINTR) {
                if (is_fire_now(ipc)) return 0;
                continue;
            }
            perror("client msgrcv");
            return 0;
        }
        if (rep.kind == expected_kind) {
            if (out) *out = rep;
            return 1;
        }
    }
}

static int send_pay_request(IPC *ipc, pid_t me, int group, int table, int amount, int dish_id) {
    Msg m;
    memset(&m, 0, sizeof(m));
    m.mtype = MTYPE_CASHIER;
    m.kind = MSG_PAY_REQ;
    m.pid = me;
    m.group_size = group;
    m.table_index = table;
    m.value = amount;
    m.dish_id = dish_id;

    cashier_queue_inc(ipc);

    for (;;) {
        if (msgsnd(ipc->msg_pay_req_id, &m, msgsz(), 0) == 0) break;

        if (errno == EINTR) {
            if (is_fire_now(ipc)) {
                cashier_queue_dec_safe(ipc);
                return 0;
            }
            continue;
        }

        cashier_queue_dec_safe(ipc);
        perror("client msgsnd PAY_REQ");
        return 0;
    }

    Msg rep;
    if (!wait_reply_on_queue(ipc, ipc->msg_pay_rep_id, (long)me, MSG_PAY_REPLY, &rep)) {
        cashier_queue_dec_safe(ipc);
        return 0;
    }
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

    for (;;) {
        if (msgsnd(ipc->msg_work_req_id, &m, msgsz(), 0) == 0) break;

        if (errno == EINTR) {
            if (is_fire_now(ipc)) return 0;
            continue;
        }

        perror("client msgsnd SERVE_REQ");
        return 0;
    }

    Msg rep;
    if (!wait_reply_on_queue(ipc, ipc->msg_work_rep_id, (long)me, MSG_SERVE_REPLY, &rep)) return 0;
    return rep.value != 0;
}

typedef struct {
    const char *name;
    int price;
} MenuItem;

static const MenuItem menu[] = {
    {"Pierogi ruskie", 18},
    {"Nalesniki z serem", 12},
    {"Kotlet schabowy", 22},
    {"Rosol", 10},
    {"Pomidorowka", 14}
};

static int pick_menu_id(int client_id) {
    unsigned int seed =
        (unsigned int)time(NULL) ^
        (unsigned int)getpid() ^
        (unsigned int)(client_id * 1103515245u);

    int n = (int)(sizeof(menu) / sizeof(menu[0]));
    return (int)(rand_r(&seed) % (unsigned)n);
}

typedef struct {
    int group_size;
    int finished;
    pthread_mutex_t m;
    pthread_cond_t  cv;
} EatSync;

typedef struct {
    EatSync *sync;
    int person_idx;
    int client_id;
} EatArg;

static void* eater_thread(void *vp) {
    EatArg *a = (EatArg*)vp;
    EatSync *s = a->sync;

    unsigned int seed =
        (unsigned int)time(NULL) ^
        (unsigned int)(uintptr_t)pthread_self() ^
        (unsigned int)getpid() ^
        (unsigned int)(a->person_idx * 2654435761u) ^
        (unsigned int)(a->client_id * 1103515245u);

    int eat_sec = 1;

    log_line("client", "Client %d | person %d: eating %d sec",
             a->client_id, a->person_idx, eat_sec);

    for (int i = 0; i < eat_sec; i++) {
        if (g_stop) break;
        usleep(10);
    }

    pthread_mutex_lock(&s->m);
    s->finished++;
    if (s->finished >= s->group_size) {
        pthread_cond_broadcast(&s->cv);
    }
    pthread_mutex_unlock(&s->m);

    return NULL;
}

static void eat_group_with_threads(int group_size, int client_id) {
    if (group_size <= 0) return;
    if (group_size > 4) group_size = 4;

    EatSync s;
    s.group_size = group_size;
    s.finished = 0;
    pthread_mutex_init(&s.m, NULL);
    pthread_cond_init(&s.cv, NULL);

    pthread_t th[4];
    EatArg args[4];
    int created[4] = {0,0,0,0};

    log_line("client", "Client %d: spawning %d eater threads", client_id, group_size);

    for (int i = 0; i < group_size; i++) {
        args[i].sync = &s;
        args[i].person_idx = i + 1;
        args[i].client_id = client_id;

        int rc = pthread_create(&th[i], NULL, eater_thread, &args[i]);
        if (rc != 0) {
            errno = rc;
            perror("pthread_create");
            pthread_mutex_lock(&s.m);
            s.finished++;
            pthread_cond_broadcast(&s.cv);
            pthread_mutex_unlock(&s.m);
            created[i] = 0;
        } else {
            created[i] = 1;
        }
    }

    pthread_mutex_lock(&s.m);
    while (s.finished < s.group_size && !g_stop) {
        pthread_cond_wait(&s.cv, &s.m);
    }
    pthread_mutex_unlock(&s.m);

    for (int i = 0; i < group_size; i++) {
        if (created[i]) {
            int rc = pthread_join(th[i], NULL);
            if (rc != 0) {
                errno = rc;
                perror("pthread_join");
            }
        }
    }

    log_line("client", "Client %d: all eater threads finished", client_id);

    pthread_cond_destroy(&s.cv);
    pthread_mutex_destroy(&s.m);
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

        if (msgsnd(ipc->msg_work_req_id, &d, msgsz(), IPC_NOWAIT) == -1) {
            if (errno != EAGAIN && errno != EINTR) perror("client msgsnd DISH_RETURN (collective)");
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

            if (msgsnd(ipc->msg_work_req_id, &d, msgsz(), IPC_NOWAIT) == -1) {
                if (errno != EAGAIN && errno != EINTR) perror("client msgsnd DISH_RETURN (single)");
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

    // Klient ignoruje SIGINT, zamyka go manager przez SIGTERM
    struct sigaction si;
    memset(&si, 0, sizeof(si));
    sigemptyset(&si.sa_mask);
    si.sa_flags = 0;
    si.sa_handler = SIG_IGN;
    if (sigaction(SIGINT, &si, NULL) == -1) DIE_PERROR("sigaction SIGINT");

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

    /*unsigned h = (unsigned)(now_ms() ^ (unsigned)getpid() ^ (unsigned)(id * 2654435761u));
    int no_order = (h % 100) < 5;
    if (no_order) {
        log_line("client", "Client %d: no order (leaves immediately)", id);
        ipc_close(&ipc);
        return 0;
    }*/

    int dish_id = pick_menu_id(id);
    MenuItem item = menu[dish_id];
    int amount = item.price * group;
   // long long seat_deadline = now_ms() + WAIT_SEAT_TIMEOUT_MS;

    while (!g_stop) {
        int picked = pick_table_and_reserve(&ipc, group, &table);
        if (picked >= 0) { pending = 1; break; }
       /* long long now = now_ms();
        if (now >= seat_deadline) break;

        int remaining = (int)(seat_deadline - now);
        int wr = sem_timedwait_event_ms(ipc.sem_id, SEM_TABLE_EVENT, remaining);
        if (wr < 0) {
            continue;
        }*/
        sem_timedwait_event_ms(ipc.sem_id, SEM_TABLE_EVENT, 1000);

        if (is_fire_now(&ipc)) break;
    }

    if (!pending) {
        int fire = is_fire_now(&ipc);
        if (fire) {
            log_line("client", "Client %d evacuated before reserving seat", id);
        } else if (g_stop) {
            log_line("client", "Client %d stopped before reserving seat", id);
        }/* else {
            log_line("client", "Client %d left (no seat after %dms)", id, WAIT_SEAT_TIMEOUT_MS);
        }*/
        ipc_close(&ipc);
        return 0;
    }

    log_line("client", "Client %d group=%d reserved table=%d (pending)", id, group, table);
    log_line("client", "Client %d: selected '%s' price=%d per person (group=%d total=%d)",
             id, item.name, item.price, group, amount);
    notify_manager(&ipc, MSG_CLIENT_PENDING, me, group, table, 0);

    if (!send_pay_request(&ipc, me, group, table, amount, dish_id)) {
        int fire = is_fire_now(&ipc);
        if (fire) log_line("client", "Client %d PAY interrupted by FIRE -> cancel pending", id);
        else      log_line("client", "Client %d PAY interrupted/failed -> cancel pending", id);

        cancel_reservation(&ipc, group, table);
        notify_manager(&ipc, MSG_CLIENT_LEFT, me, group, table, 0);
        ipc_close(&ipc);
        return 0;
    }

    log_line("client", "Client %d: PAID for '%s' total=%d", id, item.name, amount);

    if (!send_serve_request(&ipc, me, group, table)) {
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

    eat_group_with_threads(group, id);

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
