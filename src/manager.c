#include "common.h"
#include "ipc.h"
#include <sys/wait.h>
#include <signal.h>

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
    int clients = (argc >= 6) ? parse_int(argv[5], 0, 500, "CLIENTS") : 5;

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

    for (int i = 1; i <= clients; i++) {
        char idbuf[32];
        snprintf(idbuf, sizeof(idbuf), "%d", i);
        pid_t cpid = spawn("./bin/client", idbuf);
        log_line("manager", "Spawned client %d pid=%d", i, (int)cpid);
        sleep_ms(100);
    }

    for (int i = 0; i < clients; i++) {
        wait(NULL);
    }

    log_line("manager", "Stopping worker/cashier (SIGTERM).");
    kill(worker, SIGTERM);
    kill(cashier, SIGTERM);

    sleep_ms(300);
    waitpid(worker, NULL, 0);
    waitpid(cashier, NULL, 0);

    log_line("manager", "Cleaning IPC and exiting.");
    ipc_close(&ipc);
    ipc_destroy(&ipc);
    return 0;
}
