#include "common.h"
#include "ipc.h"
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>

#define C_RESET "\033[0m"
#define C_RED   "\033[31m"
#define C_YEL   "\033[33m"
#define C_BLU   "\033[34m"
#define C_MAG   "\033[35m"
#define C_CYN   "\033[36m"

static volatile sig_atomic_t g_usr1  = 0;
static volatile sig_atomic_t g_usr2  = 0;
static volatile sig_atomic_t g_fire  = 0;
static volatile sig_atomic_t g_close = 0;

static void on_usr1(int sig){ (void)sig; g_usr1 = 1; }
static void on_usr2(int sig){ (void)sig; g_usr2 = 1; }
static void on_fire(int sig){ (void)sig; g_fire = 1; }
static void on_close(int sig){ (void)sig; g_close = 1; }

static int msgsz(void){ return (int)(sizeof(Msg) - sizeof(long)); }

typedef struct {
    pid_t pid;
    int stage;
    int group_size;
    int table_index;
} ClientTrack;

static ClientTrack* find_track(ClientTrack *tracks, int n, pid_t pid) {
    for (int i = 0; i < n; i++) {
        if (tracks[i].pid == pid) return &tracks[i];
    }
    return NULL;
}

static void cleanup_if_needed(IPC *ipc, ClientTrack *tracks, int n, pid_t pid, const char *where) {
    ClientTrack *t = find_track(tracks, n, pid);
    if (!t) return;

    if (t->stage == 1) {
        cancel_reservation(ipc, t->group_size, t->table_index);
        log_line("manager", "CLEANUP(%s): pid=%d pending -> cancel_reservation(group=%d table=%d)",
                 where, (int)pid, t->group_size, t->table_index);
        t->stage = 0;
    } else if (t->stage == 2) {
        finish_eating_and_leave(ipc, t->group_size, t->table_index);
        log_line("manager", "CLEANUP(%s): pid=%d seated -> finish_eating_and_leave(group=%d table=%d)",
                 where, (int)pid, t->group_size, t->table_index);
        t->stage = 0;
    }
}

static int read_reserve_from_stdin(void) {
    for (int tries = 0; tries < 3; tries++) {
        char buf[64];
        fprintf(stderr, "\n[RESERVE] Podaj liczbe miejsc do rezerwacji (0..2000): ");
        fflush(stderr);
        if (!fgets(buf, sizeof(buf), stdin)) return 0;

        errno = 0;
        char *end = NULL;
        long v = strtol(buf, &end, 10);
        while (end && (*end == ' ' || *end == '\n' || *end == '\t')) end++;
        if (errno == 0 && end && (*end == '\0') && v >= 0 && v <= 2000) return (int)v;

        fprintf(stderr, "[RESERVE] Niepoprawna wartosc.\n");
    }
    return 0;
}

static void drain_manager_msgs(IPC *ipc, ClientTrack *tracks, int n, int reserve_target) {
    for (;;) {
        Msg m;
        ssize_t r = msgrcv(ipc->msg_id, &m, msgsz(), MTYPE_MANAGER, IPC_NOWAIT);
        if (r == -1) {
            if (errno == EINTR) continue;
            if (errno == ENOMSG) break;
            perror("manager msgrcv MTYPE_MANAGER");
            break;
        }

        if (m.kind == MSG_RESERVE_ASK) {
            int want = reserve_target;
            if (want <= 0) want = read_reserve_from_stdin();

            sem_lock(ipc->sem_id);
            ipc->st->reserve_remaining += want;
            int rem = ipc->st->reserve_remaining;
            sem_unlock(ipc->sem_id);

            log_line("manager", "RESERVE_ASK from worker pid=%d -> want=%d, reserve_remaining=%d",
                     (int)m.pid, want, rem);

            Msg req;
            memset(&req, 0, sizeof(req));
            req.mtype = MTYPE_WORKER;
            req.kind  = MSG_RESERVE_REQ;
            req.pid   = 0;
            req.value = want;
            if (msgsnd(ipc->msg_id, &req, msgsz(), 0) == -1) {
                perror("manager msgsnd RESERVE_REQ (from ASK)");
            }
            continue;
        }

        ClientTrack *t = find_track(tracks, n, m.pid);
        if (!t) continue;

        if (m.kind == MSG_CLIENT_PENDING) {
            t->stage = 1;
            t->group_size = m.group_size;
            t->table_index = m.table_index;
        } else if (m.kind == MSG_CLIENT_SEATED) {
            t->stage = 2;
            t->group_size = m.group_size;
            t->table_index = m.table_index;
        } else if (m.kind == MSG_CLIENT_LEFT) {
            t->stage = 0;
        }
    }
}

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

static int next_arrival_ms(int min_ms, int max_ms) {
    if (min_ms < 0) min_ms = 0;
    if (max_ms < min_ms) max_ms = min_ms;
    int span = (max_ms - min_ms) + 1;
    if (span <= 0) return min_ms;
    return min_ms + (rand() % span);
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

static void reap_nonblocking(IPC *ipc, ClientTrack *tracks, int n) {
    for (;;) {
        pid_t w = waitpid(-1, NULL, WNOHANG);
        if (w > 0) {
            cleanup_if_needed(ipc, tracks, n, w, "reap");
            continue;
        }
        if (w == 0) break;
        if (w == -1 && errno == EINTR) continue;
        if (w == -1 && errno == ECHILD) break;
        if (w == -1) { perror("waitpid WNOHANG"); break; }
    }
}

static void stop_pid(IPC *ipc, ClientTrack *tracks, int n,
                     pid_t pid, const char *name, int grace_ms) {
    if (pid <= 0) return;

    if (kill(pid, SIGTERM) == -1) {
        if (errno != ESRCH) {
            char buf[128];
            snprintf(buf, sizeof(buf), "kill SIGTERM (%s)", name);
            perror(buf);
        }
        return;
    }

    long long deadline = (long long)now_ms() + (long long)grace_ms;
    while ((long long)now_ms() < deadline) {
        pid_t w = waitpid(pid, NULL, WNOHANG);
        if (w == pid) {
            cleanup_if_needed(ipc, tracks, n, w, "stop_pid");
            return;
        }
        if (w == -1 && errno == ECHILD) return;
        if (w == -1 && errno == EINTR) continue;
        sleep_ms(50);
    }

    if (kill(pid, SIGKILL) == -1) {
        if (errno != ESRCH) {
            char buf[128];
            snprintf(buf, sizeof(buf), "kill SIGKILL (%s)", name);
            perror(buf);
        }
    }
    pid_t w = waitpid(pid, NULL, 0);
    if (w == pid) cleanup_if_needed(ipc, tracks, n, w, "stop_pid_kill");
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

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s X1 X2 X3 X4 [CLIENTS] [RESERVESEATS] [ARR_MIN_MS] [ARR_MAX_MS] [SEED]\n", argv[0]);
        fprintf(stderr, "  CLIENTS=0 -> tryb ciagly\n");
        fprintf(stderr, "  Ctrl+C (SIGINT) -> normal close (drain timeout)\n");
        fprintf(stderr, "  SIGTERM -> POZAR (fire_alarm=1, ewakuacja natychmiast)\n");
        return 1;
    }

    int X1 = parse_int(argv[1], 0, 100, "X1");
    int X2 = parse_int(argv[2], 0, 100, "X2");
    int X3 = parse_int(argv[3], 0, 100, "X3");
    int X4 = parse_int(argv[4], 0, 100, "X4");

    int clients = (argc >= 6) ? parse_int(argv[5], 0, 1000000, "CLIENTS") : 120;
    int reserve_target = (argc >= 7) ? parse_int(argv[6], -1, 2000, "RESERVESEATS") : -1;
    int arr_min = (argc >= 8) ? parse_int(argv[7], 0, 60000, "ARR_MIN_MS") : 60;
    int arr_max = (argc >= 9) ? parse_int(argv[8], 0, 60000, "ARR_MAX_MS") : 200;
    int seed_arg = (argc >= 10) ? parse_int(argv[9], 0, 2147483647, "SEED") : -1;

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

    unsigned int seed;
    if (seed_arg >= 0) seed = (unsigned int)seed_arg;
    else seed = (unsigned int)time(NULL) ^ (unsigned int)getpid();
    srand(seed);

    log_line("manager", "TIMES: eat=[%d,%d]ms drain=%dms no_progress=%dms arr=[%d,%d]ms seed=%u",
             EAT_BASE_MS, EAT_MAX_MS, DRAIN_TIMEOUT_MS, DRAIN_NO_PROGRESS_MS, arr_min, arr_max, seed);

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

    pid_t client_pids[6000];
    ClientTrack tracks[6000];
    int spawned = 0;

    int i = 1;
    int stop_spawning = 0;
    int close_started = 0;

    long long next_spawn_at = now_ms() + next_arrival_ms(arr_min, arr_max);
    long long last_tick = now_ms();
    long long last_status = now_ms();

    long long drain_deadline = -1;
    long long last_progress_at = -1;
    int last_occ = -1;
    int last_pend = -1;

    while (!g_fire) {
        if (g_usr1) {
            g_usr1 = 0;
            if (kill(worker, SIGUSR1) == -1) perror("manager kill(worker, SIGUSR1)");
            log_line("manager", "SIGUSR1: forwarded to worker pid=%d", (int)worker);
            term_printf(C_MAG, "[SIGUSR1] forwarded to worker\n");
        }

        if (g_usr2) {
            g_usr2 = 0;
            if (kill(worker, SIGUSR2) == -1) perror("manager kill(worker, SIGUSR2)");
            log_line("manager", "SIGUSR2: forwarded to worker pid=%d", (int)worker);
            term_printf(C_BLU, "[SIGUSR2] forwarded to worker (reservation handshake)\n");
        }

        drain_manager_msgs(&ipc, tracks, spawned, reserve_target);
        reap_nonblocking(&ipc, tracks, spawned);

        if (g_close && !close_started) {
            g_close = 0;
            close_started = 1;
            stop_spawning = 1;

            sem_lock(ipc.sem_id);
            ipc.st->closing = 1;
            sem_unlock(ipc.sem_id);

            drain_deadline = now_ms() + DRAIN_TIMEOUT_MS;
            last_progress_at = now_ms();
            last_occ = -1;
            last_pend = -1;

            print_final_status(&ipc, "CLOSE_STARTED_STATUS (NORMAL_CLOSE)");
            term_printf(C_YEL, "[CLOSE] normal close: stop spawning, draining (timeout=%dms)\n", DRAIN_TIMEOUT_MS);
        }

        if (!close_started && clients > 0 && i > clients) {
            close_started = 1;
            stop_spawning = 1;

            sem_lock(ipc.sem_id);
            ipc.st->closing = 1;
            sem_unlock(ipc.sem_id);

            drain_deadline = now_ms() + DRAIN_TIMEOUT_MS;
            last_progress_at = now_ms();
            last_occ = -1;
            last_pend = -1;

            print_final_status(&ipc, "CLOSE_STARTED_STATUS (LIMIT_REACHED)");
            term_printf(C_YEL, "[CLOSE] limit reached: stop spawning, draining (timeout=%dms)\n", DRAIN_TIMEOUT_MS);
        }

        long long now = now_ms();

        if (now - last_status >= 1000) {
            int tables, total, occ, pend, res, res_rem, dishes, closing, fire;
            snapshot_status(&ipc, &tables, &total, &occ, &pend, &res, &res_rem, &dishes, &closing, &fire);

            term_printf(C_CYN,
                "[STATUS] tables=%d seats=%d occ=%d pend=%d res=%d reserve_remaining=%d dishes=%d closing=%d fire=%d\n",
                tables, total, occ, pend, res, res_rem, dishes, closing, fire);

            last_status = now;

            if (close_started) {
                if (occ != last_occ || pend != last_pend) {
                    last_progress_at = now;
                    last_occ = occ;
                    last_pend = pend;
                }
            }

            if (close_started && occ == 0 && pend == 0) {
                term_printf(C_YEL, "[CLOSE] drain complete (bar empty)\n");
                print_final_status(&ipc, "FINAL_STATUS (END)");
                break;
            }

            if (close_started && (occ > 0 || pend > 0) &&
                last_progress_at > 0 && (now - last_progress_at) >= DRAIN_NO_PROGRESS_MS) {

                term_printf(C_YEL, "[CLOSE] no progress for %dms -> terminate clients (SIGTERM)\n",
                            DRAIN_NO_PROGRESS_MS);
                log_line("manager", "NO PROGRESS %dms: terminate clients (SIGTERM).", DRAIN_NO_PROGRESS_MS);

                for (int k = 0; k < spawned; k++) {
                    if (kill(client_pids[k], SIGTERM) == -1) {
                        if (errno != ESRCH) perror("kill client");
                    }
                }
                break;
            }
        }

        if (close_started && drain_deadline > 0 && now >= drain_deadline) {
            term_printf(C_YEL, "[CLOSE] drain timeout -> terminate clients (SIGTERM)\n");
            log_line("manager", "DRAIN TIMEOUT: terminate clients (SIGTERM).");

            for (int k = 0; k < spawned; k++) {
                if (kill(client_pids[k], SIGTERM) == -1) {
                    if (errno != ESRCH) perror("kill client");
                }
            }
            break;
        }

        if (!stop_spawning && now >= next_spawn_at) {
            if (spawned < (int)(sizeof(client_pids)/sizeof(client_pids[0]))) {
                char idbuf[32];
                snprintf(idbuf, sizeof(idbuf), "%d", i);
                pid_t cpid = spawn("./bin/client", idbuf);

                client_pids[spawned] = cpid;
                tracks[spawned].pid = cpid;
                tracks[spawned].stage = 0;
                tracks[spawned].group_size = 0;
                tracks[spawned].table_index = -1;
                spawned++;

                log_line("manager", "Spawned client %d pid=%d", i, (int)cpid);
            }
            i++;
            next_spawn_at = now_ms() + next_arrival_ms(arr_min, arr_max);
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
        term_printf(C_RED, "[FIRE] evacuating clients NOW!\n");
        log_line("manager", "FIRE received -> evacuating clients NOW.");

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

    for (int k = 0; k < spawned; k++) stop_pid(&ipc, tracks, spawned, client_pids[k], "client", 1500);
    stop_pid(&ipc, tracks, spawned, worker, "worker", 2000);
    stop_pid(&ipc, tracks, spawned, cashier, "cashier", 2000);

    while (1) {
        pid_t w = waitpid(-1, NULL, 0);
        if (w > 0) {
            cleanup_if_needed(&ipc, tracks, spawned, w, "final_wait");
            continue;
        }
        if (w == -1 && errno == EINTR) continue;
        if (w == -1 && errno == ECHILD) break;
        if (w == -1) { perror("waitpid"); break; }
    }

    log_line("manager", "Cleaning IPC and exiting.");
    ipc_close(&ipc);
    ipc_destroy(&ipc);
    if (unlink("ipc.key") == -1) {
        if (errno != ENOENT) perror("unlink ipc.key");
    }
    return 0;
}
