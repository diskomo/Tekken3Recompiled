/*
 * mod_host_hooks.c — host hooks for trusted game plugins. See mod_plugins.h.
 */
#include "mod_host_hooks.h"
#include "mod_plugins.h"

#include <stddef.h>

enum { MAX_HOOKS = 8 };

typedef struct { uint32_t address; PSXModCodeEntryCallback callback; } CodeEntry;

uint32_t g_mod_code_entry_count;
static CodeEntry s_code_entries[MAX_HOOKS];
static PSXModFrameCallback s_frames[MAX_HOOKS];
static unsigned s_frame_count;
static PSXModGameStartCallback s_starts[MAX_HOOKS];
static unsigned s_start_count;
static int s_quit;

int psx_mod_register_code_entry(uint32_t address, PSXModCodeEntryCallback callback) {
    if (!callback || g_mod_code_entry_count >= MAX_HOOKS) return 0;
    s_code_entries[g_mod_code_entry_count++] = (CodeEntry){address, callback};
    return 1;
}

int psx_mod_register_frame_hook(PSXModFrameCallback callback) {
    if (!callback || s_frame_count >= MAX_HOOKS) return 0;
    s_frames[s_frame_count++] = callback;
    return 1;
}

int psx_mod_register_game_start_hook(PSXModGameStartCallback callback) {
    if (!callback || s_start_count >= MAX_HOOKS) return 0;
    s_starts[s_start_count++] = callback;
    return 1;
}

void psx_mod_request_quit(void) { s_quit = 1; }

int mod_code_entry(uint32_t address) {
    for (uint32_t i = 0; i < g_mod_code_entry_count; ++i)
        if (s_code_entries[i].address == address && s_code_entries[i].callback())
            return 1;
    return 0;
}

void mod_run_frame_hooks(void) {
    for (unsigned i = 0; i < s_frame_count; ++i) s_frames[i]();
}

void mod_run_game_start_hooks(const char *game_id, const char *save_dir) {
    for (unsigned i = 0; i < s_start_count; ++i) s_starts[i](game_id, save_dir);
}

int mod_quit_requested(void) { return s_quit; }
