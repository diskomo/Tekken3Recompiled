/*
 * mod_log.h — always-on ring of messages from trusted mod plugins.
 *
 * Plugins report through psx_mod_log() (mod_plugins.h). Each line is kept
 * here for the debug server's mod_log command and echoed to stdout, so mods
 * never write their own log files or stderr.
 */
#ifndef PSXRECOMP_MOD_LOG_H
#define PSXRECOMP_MOD_LOG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOD_LOG_CAP 256u   /* lines retained; a power of two */
#define MOD_LOG_LINE 240u  /* bytes per line, including the terminator */

/* Total lines ever logged. */
uint64_t mod_log_seq(void);
/* Line `seq`, or NULL once it has left the ring. */
const char *mod_log_line(uint64_t seq);

#ifdef __cplusplus
}
#endif
#endif
