#include "common.h"
#include "ipc.h"
#include <signal.h>

static volatile sig_atomic_t g_stop = 0;

static void on_term(int sig) {
    (void)sig;
    g_stop = 1;
}

int main(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_term;
    sigaction(SIGTERM, &sa, NULL);

    IPC ipc;
    if (!ipc_open(&ipc, "ipc.key")) {
        log_line("cashier", "Cannot open IPC (ipc.key missing?)");
        return 1;
    }

    log_line("cashier", "Cashier started (pid=%d). Waiting...", (int)getpid());

    while (!g_stop) {
        sleep_ms(500);
    }

    log_line("cashier", "Cashier stopping.");
    ipc_close(&ipc);
    return 0;
}

