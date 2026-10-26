/*
 * mod_host_hooks.h — the runtime side of the host hooks a game registers
 * through mod_plugins.h. With nothing registered every call is a no-op.
 */
#ifndef PSXRECOMP_MOD_HOST_HOOKS_H
#define PSXRECOMP_MOD_HOST_HOOKS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Number of code-entry hooks; dispatch paths test it before calling below. */
extern uint32_t g_mod_code_entry_count;
/* 1 when a hook at `address` handled the call: return to $ra. */
int mod_code_entry(uint32_t address);
/* Every guest VBlank, before presenting the frame. */
void mod_run_frame_hooks(void);
/* Once the game ID and its save directory are final. */
void mod_run_game_start_hooks(const char *game_id, const char *save_dir);
/* A plugin asked the runtime to exit. */
int mod_quit_requested(void);

#ifdef __cplusplus
}
#endif
#endif
