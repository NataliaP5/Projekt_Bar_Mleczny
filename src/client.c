#include "common.h"
#include "ipc.h"

int main(int argc, char **argv) {
    int id = 0;
    if (argc >= 2) id = parse_int(argv[1], 0, 1000000, "client_id");

    IPC ipc;
    if (!ipc_open(&ipc, "ipc.key")) {
        log_line("client", "Client %d cannot open IPC", id);
        return 1;
    }

    log_line("client", "Client %d started (pid=%d) and exits (stub).", id, (int)getpid());
    ipc_close(&ipc);
    return 0;
}
