/*
 * mod_log.c — always-on ring of mod plugin messages. See mod_log.h.
 */
#include "mod_log.h"
#include "mod_plugins.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char     s_lines[MOD_LOG_CAP][MOD_LOG_LINE];
static uint64_t s_seq;

void psx_mod_log(const char *format, ...) {
    char *line = s_lines[s_seq & (MOD_LOG_CAP - 1u)];
    va_list args;
    va_start(args, format);
    vsnprintf(line, MOD_LOG_LINE, format, args);
    va_end(args);
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
    s_seq++;
    fprintf(stdout, "%s\n", line);
    fflush(stdout);
}

uint64_t mod_log_seq(void) { return s_seq; }

const char *mod_log_line(uint64_t seq) {
    if (seq >= s_seq || s_seq - seq > MOD_LOG_CAP) return NULL;
    return s_lines[seq & (MOD_LOG_CAP - 1u)];
}
