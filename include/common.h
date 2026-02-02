// Wspolne funkcje pomocnicze uzywane przez wszystkie procesy:
// - parsowanie argumentow, odmierzanie czasu, uspienie z obluga EINTR
// - logowanie do plikow
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

#define EAT_BASE_MS 400
#define EAT_STEP_MS 120
#define EAT_VARIANTS 5
#define EAT_MAX_MS (EAT_BASE_MS + (EAT_VARIANTS-1)*EAT_STEP_MS)
#define DRAIN_TIMEOUT_MS 20000
#define DRAIN_NO_PROGRESS_MS 3000

#define DIE_PERROR(msg) do { perror(msg); exit(EXIT_FAILURE); } while(0)

int parse_int(const char *s, int minv, int maxv, const char *name);
long long now_ms(void);
void sleep_ms(int ms);

void ensure_log_dir(void);
void log_line(const char *who, const char *fmt, ...);
void log_close(void);
