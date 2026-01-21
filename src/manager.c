#include "common.h"
#include "ipc.h"
#include <sys/wait.h>
#include <signal.h>
#include <stdlib.h>
#include <stdarg.h>

#define C_RESET "\033[0m"
#define C_RED   "\033[31m"
#define C_GRN   "\033[32m"
#define C_YEL   "\033[33m"
#define C_BLU   "\033[34m"
#define C_MAG   "\033[35m"
#define C_CYN   "\033[36m"

static volatile sig_atomic_t g_usr1 = 0;
static volatile sig_atomic_t g_usr2 = 0;
static volatile sig_atomic_t g_fire = 0;
static volatile sig_atomic_t g_close = 0;

static void on_usr1(int sig){ (void)sig; g_usr1 = 1; }
static void on_usr2(int sig){ (void)sig; g_usr2 = 1; }
static void on_fire(int sig){ (void)sig; g_fire = 1; }
static void on_close(int sig){ (void)sig; g_close = 1; }

static int msgsz(void){ return (int)(sizeof(Msg) - sizeof(long)); }

static pid_t spawn(const char *path, const char *arg1) {
    pid_t pid = fork();
    if (pid == -1) DIE_PERROR("fork");
    if (pid == 0) {
        if (arg1) execl(path, path, arg1, (char*)NULL);
        else      execl(path, path, (char*)NULL);
        DIE_PERROR("exec");
    }
    return pid;
}

static int next_arrival_ms(int min_ms, int max_ms, int seq) {
    if (min_ms < 0) min_ms = 0;
    if (max_ms < min_ms) max_ms = min_ms;
    unsigned h = (unsigned)(now_ms() ^ (unsigned)getpid() ^ (unsigned)(seq * 2654435761u));
    int span = (max_ms - min_ms) + 1;
    if (span <= 0) return min_ms;
    return min_ms + (int)(h % (unsigned)span);
}

static void send_reserve_tick(IPC *ipc) {
    Msg t;
    memset(&t, 0, sizeof(t));
    t.mtype = MTYPE_WORKER;
    t.kind  = MSG_RESERVE_TICK;
    t.pid   = getpid();
    t.value = 0;
    if (msgsnd(ipc->msg_id, &t, msgsz(), IPC_NOWAIT) == -1) {
        if (errno != EAGAIN) perror("manager msgsnd RESERVE_TICK");
    }
}

static void term_printf(const char *color, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stdout, "%s", color);
    vfprintf(stdout, fmt, ap);
    fprintf(stdout, "%s", C_RESET);
    fflush(stdout);
    va_end(ap);
}


static void snapshot_status(IPC *ipc,
                            int *out_tables,
                            int *out_total_seats,
                            int *out_occ,
                            int *out_pend,
                            int *out_res,
                            int *out_res_rem,
                            int *out_dishes,
                            int *out_closing,
                            int *out_fire)
{
    int tables = 0, total = 0, occ = 0, pend = 0, res = 0;

    sem_lock(ipc->sem_id);

    tables = ipc->st->tables_count;
    for (int i = 0; i < ipc->st->tables_count; i++) {
        Table *t = &ipc->st->tables[i];
        total += t->capacity;
        occ   += t->occupied_seats;
        pend  += t->pending_seats;
        res   += t->reserved_fixed;
    }

    int res_rem = ipc->st->reserve_remaining;
    int dishes  = ipc->st->dishes_returned_total;
    int closing = ipc->st->closing;
    int fire    = ipc->st->fire_alarm;

    sem_unlock(ipc->sem_id);

    *out_tables = tables;
    *out_total_seats = total;
    *out_occ = occ;
    *out_pend = pend;
    *out_res = res;
    *out_res_rem = res_rem;
    *out_dishes = dishes;
    *out_closing = closing;
    *out_fire = fire;
}

static void print_final_status(IPC *ipc, const char *tag) {
    int tables, total, occ, pend, res, res_rem, dishes, closing, fire;
    snapshot_status(ipc, &tables, &total, &occ, &pend, &res, &res_rem, &dishes, &closing, &fire);

    log_line("manager",
        "%s tables=%d seats=%d occ=%d pend=%d res=%d reserve_remaining=%d dishes=%d closing=%d fire=%d",
        tag, tables, total, occ, pend, res, res_rem, dishes, closing, fire);

    term_printf(C_YEL,
        "%s tables=%d seats=%d occ=%d pend=%d res=%d reserve_remaining=%d dishes=%d closing=%d fire=%d\n",
        tag, tables, total, occ, pend, res, res_rem, dishes, closing, fire);
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s X1 X2 X3 X4 [CLIENTS] [RESERVESEATS] [ARR_MIN_MS] [ARR_MAX_MS]\n", argv[0]);
        fprintf(stderr, "  CLIENTS=0 -> tryb ciagly (do zamkniecia)\n");
        fprintf(stderr, "  Ctrl+C (SIGINT) -> normalne zamkniecie (bez ewakuacji)\n");
        fprintf(stderr, "  SIGTERM -> POZAR (ewakuacja natychmiast)\n");
        return 1;
    }

    int X1 = parse_int(argv[1], 0, 100, "X1");
    int X2 = parse_int(argv[2], 0, 100, "X2");
    int X3 = parse_int(argv[3], 0, 100, "X3");
    int X4 = parse_int(argv[4], 0, 100, "X4");

    int clients = (argc >= 6) ? parse_int(argv[5], 0, 1000000, "CLIENTS") : 120;
    int reserve_target = (argc >= 7) ? parse_int(argv[6], 0, 2000, "RESERVESEATS") : -1;
    int arr_min = (argc >= 8) ? parse_int(argv[7], 0, 60000, "ARR_MIN_MS") : 60;
    int arr_max = (argc >= 9) ? parse_int(argv[8], 0, 60000, "ARR_MAX_MS") : 60;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = on_usr1;
    if (sigaction(SIGUSR1, &sa, NULL) == -1) DIE_PERROR("sigaction SIGUSR1");
    sa.sa_handler = on_usr2;
    if (sigaction(SIGUSR2, &sa, NULL) == -1) DIE_PERROR("sigaction SIGUSR2");
    sa.sa_handler = on_fire;
    if (sigaction(SIGTERM, &sa, NULL) == -1) DIE_PERROR("sigaction SIGTERM");
    sa.sa_handler = on_close;
    if (sigaction(SIGINT,  &sa, NULL) == -1) DIE_PERROR("sigaction SIGINT");

    IPC ipc;
    if (!ipc_create(&ipc, "ipc.key")) {
        if (!ipc_open(&ipc, "ipc.key")) {
            fprintf(stderr, "Cannot create/open IPC\n");
            return 1;
        }
    }

    ipc_init_tables_for_manager(&ipc, X1, X2, X3, X4);

    sem_lock(ipc.sem_id);
    ipc.st->reserve_remaining = 0;
    ipc.st->fire_alarm = 0;
    ipc.st->closing = 0;
    sem_unlock(ipc.sem_id);

    log_line("manager", "Manager started pid=%d. Spawning worker+cashier.", (int)getpid());
    pid_t worker = spawn("./bin/worker", NULL);
    pid_t cashier = spawn("./bin/cashier", NULL);
    log_line("manager", "Spawned worker pid=%d, cashier pid=%d", (int)worker, (int)cashier);

    pid_t client_pids[6000];
    int spawned = 0;
    int finished = 0;

    int i = 1;
    long long next_spawn_at = now_ms() + next_arrival_ms(arr_min, arr_max, i);
    long long last_tick = now_ms();
    long long last_status = now_ms();

    while (!g_fire) {
        if (g_usr1) {
            g_usr1 = 0;
            int added = add_more_x3_tables_once(&ipc);
            if (added > 0)
                log_line("manager", "SIGUSR1: added %d new 3-seat tables (once)", added);
            else
                log_line("manager", "SIGUSR1: ignored (boost already used)");
            term_printf(C_MAG, "[SIGUSR1] boost X3 tables\n");
        }

        if (g_usr2) {
            g_usr2 = 0;

            int want = reserve_target;
            if (want < 0) {
                char buf[64];
                fprintf(stderr, "\n[SIGUSR2] Podaj liczbe miejsc do rezerwacji: ");
                fflush(stderr);
                if (fgets(buf, sizeof(buf), stdin)) want = (int)strtol(buf, NULL, 10);
                else want = 0;
                if (want < 0) want = 0;
            }

            sem_lock(ipc.sem_id);
            ipc.st->reserve_remaining += want;
            int rem = ipc.st->reserve_remaining;
            sem_unlock(ipc.sem_id);

            log_line("manager", "SIGUSR2: requested %d seats; reserve_remaining now=%d", want, rem);
            term_printf(C_BLU, "[SIGUSR2] reservation requested\n");

            Msg m;
            memset(&m, 0, sizeof(m));
            m.mtype = MTYPE_WORKER;
            m.kind  = MSG_RESERVE_REQ;
            m.pid   = getpid();
            m.value = want;

            if (msgsnd(ipc.msg_id, &m, msgsz(), 0) == -1) {
                perror("manager msgsnd RESERVE_REQ");
            } else {
                Msg rep;
                ssize_t r = msgrcv(ipc.msg_id, &rep, msgsz(), (long)getpid(), 0);
                if (r == -1) perror("manager msgrcv RESERVE_REPLY");
                else {
                    sem_lock(ipc.sem_id);
                    int left = ipc.st->reserve_remaining;
                    sem_unlock(ipc.sem_id);
                    log_line("manager", "SIGUSR2: reserved immediately=%d, left_to_reserve=%d", rep.value, left);
                }
            }
        }

        while (1) {
            pid_t w = waitpid(-1, NULL, WNOHANG);
            if (w > 0) finished++;
            else break;
        }

        if (g_close) {
            sem_lock(ipc.sem_id);
            ipc.st->closing = 1;
            sem_unlock(ipc.sem_id);
            print_final_status(&ipc, "FINAL_STATUS (NORMAL_CLOSE)");
            log_line("manager", "NORMAL CLOSE (Ctrl+C): stop spawning new clients, wait for remaining to finish.");
            term_printf(C_YEL, "[CLOSE] normal close: stop spawning new clients\n");
            break;
        }

        if (clients > 0 && i > clients) {
            sem_lock(ipc.sem_id);
            ipc.st->closing = 1;
            sem_unlock(ipc.sem_id);
            log_line("manager", "NORMAL CLOSE: client limit reached, waiting for remaining to finish.");
            break;
        }

        long long now = now_ms();

        if (now - last_status >= 1000) {
            int tables, total, occ, pend, res, res_rem, dishes, closing, fire;
            snapshot_status(&ipc, &tables, &total, &occ, &pend, &res, &res_rem, &dishes, &closing, &fire);

            log_line("manager",
                     "STATUS tables=%d seats=%d occ=%d pend=%d res=%d reserve_remaining=%d dishes=%d closing=%d fire=%d",
                     tables, total, occ, pend, res, res_rem, dishes, closing, fire);

            term_printf(C_CYN,
                "[STATUS] tables=%d seats=%d occ=%d pend=%d res=%d reserve_remaining=%d dishes=%d closing=%d fire=%d\n",
                tables, total, occ, pend, res, res_rem, dishes, closing, fire);

            last_status = now;
        }

        if (now >= next_spawn_at) {
            if (spawned < (int)(sizeof(client_pids)/sizeof(client_pids[0]))) {
                char idbuf[32];
                snprintf(idbuf, sizeof(idbuf), "%d", i);
                pid_t cpid = spawn("./bin/client", idbuf);
                client_pids[spawned++] = cpid;
                log_line("manager", "Spawned client %d pid=%d", i, (int)cpid);
            }
            i++;
            next_spawn_at = now_ms() + next_arrival_ms(arr_min, arr_max, i);
        }

        if (now - last_tick >= 200) {
            sem_lock(ipc.sem_id);
            int rem = ipc.st->reserve_remaining;
            sem_unlock(ipc.sem_id);
            if (rem > 0) send_reserve_tick(&ipc);
            last_tick = now;
        }

        sleep_ms(20);
    }

    if (g_fire) {
        log_line("manager", "FIRE (SIGTERM) received -> evacuating clients NOW.");
        term_printf(C_RED, "[FIRE] evacuating clients NOW!\n");
        sem_lock(ipc.sem_id);
        ipc.st->fire_alarm = 1;
        ipc.st->closing = 1;
        sem_unlock(ipc.sem_id);
        print_final_status(&ipc, "FINAL_STATUS (FIRE)");

        for (int k = 0; k < spawned; k++) {
             if (kill(client_pids[k], SIGTERM) == -1) {
                 if (errno != ESRCH) perror("kill client");
             }
        }
    }

    while (finished < spawned) {
        pid_t w = waitpid(-1, NULL, 0);
        if (w > 0) finished++;
    }

    log_line("manager", "Stopping worker/cashier (shutdown).");
    if (kill(worker, SIGTERM) == -1) perror("kill worker");
    if (kill(cashier, SIGTERM) == -1) perror("kill cashier");
    waitpid(worker, NULL, 0);
    waitpid(cashier, NULL, 0);

    log_line("manager", "Cleaning IPC and exiting.");
    ipc_close(&ipc);
    ipc_destroy(&ipc);
    return 0;
}
