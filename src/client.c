#include "common.h"
#include "ipc.h"
#include <signal.h>

static volatile sig_atomic_t g_fire = 0;
static void on_term(int sig){ (void)sig; g_fire = 1; }

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

    int group = (id % 3) + 1;

    int table = -1;
    while (!g_fire) {
        table = pick_table_and_reserve(&ipc, group, NULL);
        if (table != -1) break;
        sleep_ms(200);
    }

    if (g_fire) {
        log_line("client", "Client %d got SIGTERM before seating, exiting", id);
        ipc_close(&ipc);
        return 0;
    }

    log_line("client", "Client %d group=%d reserved table=%d", id, group, table);

    activate_seating(&ipc, group, table);
    sleep_ms(300 + (id % 5) * 100);
    finish_eating_and_leave(&ipc, group, table);

    log_line("client", "Client %d left table=%d", id, table);

    ipc_close(&ipc);
    return 0;
}
