#include "common.h"
#include "ipc.h"

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s X1 X2 X3 X4\n", argv[0]);
        return 1;
    }
    int X1 = parse_int(argv[1], 0, 100, "X1");
    int X2 = parse_int(argv[2], 0, 100, "X2");
    int X3 = parse_int(argv[3], 0, 100, "X3");
    int X4 = parse_int(argv[4], 0, 100, "X4");

    IPC ipc;
    if (!ipc_create(&ipc, "ipc.key")) {
        if (!ipc_open(&ipc, "ipc.key")) {
            fprintf(stderr, "Cannot create/open IPC\n");
            return 1;
        }
    }

    ipc_init_tables_for_manager(&ipc, X1, X2, X3, X4);
    log_line("manager", "Manager stub started. X1=%d X2=%d X3=%d X4=%d", X1, X2, X3, X4);

    ipc_close(&ipc);
    ipc_destroy(&ipc);
    return 0;
}
