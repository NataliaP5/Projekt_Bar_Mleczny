#include "common.h"
#include "ipc.h"
#include <sys/wait.h>
#include <signal.h>

static volatile sig_atomic_t g_usr1 = 0;
static volatile sig_atomic_t g_usr2 = 0;
static volatile sig_atomic_t g_term = 0;

static void on_usr1(int sig){ (void)sig; g_usr1 = 1; }
static void on_usr2(int sig){ (void)sig; g_usr2 = 1; }
static void on_term(int sig){ (void)sig; g_term = 1; }

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

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s X1 X2 X3 X4 [CLIENTS]\n", argv[0]);
        return 1;
    }
    int X1 = parse_int(argv[1], 0, 100, "X1");
    int X2 = parse_int(argv[2], 0, 100, "X2");
    int X3 = parse_int(argv[3], 0, 100, "X3");
    int X4 = parse_int(argv[4], 0, 100, "X4");
    int clients = (argc >= 6) ? parse_int(argv[5], 0, 600, "CLIENTS") : 120;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_usr1; sigaction(SIGUSR1, &sa, NULL);
    sa.sa_handler = on_usr2; sigaction(SIGUSR2, &sa, NULL);
    sa.sa_handler = on_term; sigaction(SIGTERM, &sa, NULL);

    IPC ipc;
    if (!ipc_create(&ipc, "ipc.key")) {
        if (!ipc_open(&ipc, "ipc.key")) {
            fprintf(stderr, "Cannot create/open IPC\n");
            return 1;
        }
    }

    ipc_init_tables_for_manager(&ipc, X1, X2, X3, X4);

    log_line("manager", "Manager started pid=%d. Spawning worker+cashier.", (int)getpid());
    pid_t worker = spawn("./bin/worker", NULL);
    pid_t cashier = spawn("./bin/cashier", NULL);
    log_line("manager", "Spawned worker pid=%d, cashier pid=%d", (int)worker, (int)cashier);

    pid_t client_pids[600];
    int spawned = 0;

    for (int i = 1; i <= clients && spawned < 600; i++) {
        if (g_term) break;

        char idbuf[32];
        snprintf(idbuf, sizeof(idbuf), "%d", i);
        pid_t cpid = spawn("./bin/client", idbuf);
        client_pids[spawned++] = cpid;

        log_line("manager", "Spawned client %d pid=%d", i, (int)cpid);
        sleep_ms(60);
    }

    int finished = 0;
    while (finished < spawned) {

        if (g_usr1) {
            g_usr1 = 0;
            int added = add_more_x3_tables_once(&ipc);
            log_line("manager", "SIGUSR1: added %d new 3-seat tables (once)", added);
        }

        if (g_usr2) {
            g_usr2 = 0;

            Msg m;
            memset(&m, 0, sizeof(m));
            m.mtype = MTYPE_WORKER;
            m.kind = MSG_RESERVE_REQ;
            m.pid = getpid();
            m.value = 5;

            if (msgsnd(ipc.msg_id, &m, msgsz(), 0) == -1) {
                perror("manager msgsnd RESERVE_REQ");
            } else {
                Msg rep;
                ssize_t r = msgrcv(ipc.msg_id, &rep, msgsz(), (long)getpid(), 0);
                if (r == -1) perror("manager msgrcv RESERVE_REPLY");
                else log_line("manager", "SIGUSR2: reserved seats=%d", rep.value);
            }
        }

        if (g_term) {
            log_line("manager", "FIRE (SIGTERM) received -> evacuating clients NOW.");
            sem_lock(ipc.sem_id);
            ipc.st->fire_alarm = 1;
            ipc.st->closing = 1;
            sem_unlock(ipc.sem_id);

            for (int i = 0; i < spawned; i++) {
                kill(client_pids[i], SIGTERM);
            }
            break;
        }

        pid_t w = waitpid(-1, NULL, WNOHANG);
        if (w > 0) finished++;
        else sleep_ms(50);
    }

    if (g_term) {
        sleep_ms(200);
        for (int i = 0; i < spawned; i++) {
            kill(client_pids[i], SIGTERM);
        }
        for (int i = finished; i < spawned; i++) {
            wait(NULL);
        }
    } else {
        for (; finished < spawned; finished++) wait(NULL);
    }

    log_line("manager", "Stopping worker/cashier (SIGTERM).");
    kill(worker, SIGTERM);
    kill(cashier, SIGTERM);
    waitpid(worker, NULL, 0);
    waitpid(cashier, NULL, 0);

    log_line("manager", "Cleaning IPC and exiting.");
    ipc_close(&ipc);
    ipc_destroy(&ipc);
    return 0;
}
