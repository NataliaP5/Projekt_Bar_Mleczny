#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#define DIE_PERROR(msg) do { perror(msg); exit(EXIT_FAILURE); } while(0)

void ensure_logs_dir(void);
void log_line(const char *role, const char *fmt, ...);
uint64_t now_ms(void);
void sleep_ms(int ms);
int parse_int(const char *s, int minv, int maxv, const char *name);
