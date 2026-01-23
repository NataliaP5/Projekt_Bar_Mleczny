#include "common.h"
#include <sys/stat.h>

static int g_fd = -1;

void ensure_logs_dir(void) {
    struct stat st;
    if (stat("logs", &st) == -1) {
        if (mkdir("logs", 0700) == -1) DIE_PERROR("mkdir logs");
    }
}

long long now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) DIE_PERROR("clock_gettime");
    return (long long)ts.tv_sec * 1000LL + (long long)ts.tv_nsec / 1000000LL;
}

void sleep_ms(int ms) {
    if (ms <= 0) return;
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

void log_line(const char *role, const char *fmt, ...) {
    ensure_logs_dir();

    if (g_fd == -1) {
        char path[256];
        snprintf(path, sizeof(path), "logs/%s.log", role ? role : "app");
        g_fd = open(path, O_CREAT | O_WRONLY | O_APPEND, 0600);
        if (g_fd == -1) DIE_PERROR("open log file");
    }

    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    char line[1400];
    int n = snprintf(line, sizeof(line), "[%llu] %s\n",
                     (unsigned long long)now_ms(), msg);
    if (n > 0) {
        if (write(g_fd, line, (size_t)n) == -1) {
            perror("write log");
        }
    }
}

int parse_int(const char *s, int minv, int maxv, const char *name) {
    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (errno || end == s || *end) {
        fprintf(stderr, "Invalid %s: %s\n", name, s);
        exit(1);
    }
    if (v < minv || v > maxv) {
        fprintf(stderr, "%s out of range [%d..%d]\n", name, minv, maxv);
        exit(1);
    }
    return (int)v;
}
