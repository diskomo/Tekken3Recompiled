/* Game-owned, independently selectable character texture packs. Each package
 * enables one plugin; the renderer applies a pack only to the player slots
 * whose outfit selected it (tekken3_outfits). Guest assets, gameplay and saves
 * stay intact. */
#include "mod_plugins.h"
#include "gpu_gl_renderer.h"
#include "tekken3_anna_skin_match.h"
#include "tekken3_outfits.h"
#include "psx_sdl.h"
#include <stdio.h>

typedef struct {
    const char *plugin, *folder, *image, *label, *env;
    int skin;
    GlHdFindTile find;
} CharacterPack;

static const CharacterPack packs[] = {
    {"tekken3.nina-white-satin", "nina-white-satin", "nina", "Nina White Satin", "PSX_NINA_SKIN", T3_SKIN_NINA},
    {"tekken3.xiaoyu-cherry-blossom", "xiaoyu-cherry-blossom", "xiaoyu", "Xiaoyu Cherry Blossom Ribbon",
     "PSX_XIAOYU_SKIN", T3_SKIN_XIAOYU},
    {"tekken3.anna-jessica-rabbit", "anna-jessica-rabbit", "anna", "Anna Jessica Rabbit", "PSX_ANNA_SKIN",
     T3_SKIN_ANNA, tekken3_anna_skin_find},
    {"tekken3.kuma-polar-bear", "kuma-polar-bear", "kuma", "Kuma Polar Bear", "PSX_KUMA_SKIN", T3_SKIN_KUMA},
    {"tekken3.eddy-monochrome", "eddy-monochrome", "eddy", "Eddy Monochrome", "PSX_EDDY_SKIN", T3_SKIN_EDDY},
    {"tekken3.julia-blue", "julia-blue", "julia", "Julia Blue", "PSX_JULIA_SKIN", T3_SKIN_JULIA},
    {"tekken3.heihachi-tiger-coat", "heihachi-tiger-coat", "heihachi", "Heihachi Tiger Coat",
     "PSX_HEIHACHI_SKIN", T3_SKIN_HEIHACHI},
};

static void activate(void *context) {
    const CharacterPack *p = context;
    const char *base = SDL_GetBasePath();
    if (!base) return;
    char path[1024];
    int n = snprintf(path, sizeof path, "%smods/%s", base, p->folder);
#if !defined(PSX_SDL3)
    SDL_free((void *)base);
#endif
    if (n <= 0 || n >= (int)sizeof path) return;
    GlHdCharacterPack pack = {
        .label = p->label, .directory = path, .image = p->image, .disable_env = p->env,
        .id = p->skin, .applies = tekken3_outfits_skin_enabled,
        .loaded = tekken3_outfits_set_available, .find = p->find};
    if (!gl_renderer_add_character_pack(&pack))
        psx_mod_log("%s skin: no free character pack slot\n", p->label);
}

/* The outfit menu's catalog lists every pack's art, enabled or not. */
static void load_catalog(const char *game_id, const char *save_dir) {
    (void)game_id; (void)save_dir;
    const char *base = SDL_GetBasePath();
    char path[1200];
    snprintf(path, sizeof path, "%smods/outfit-menu/catalog.txt", base ? base : "");
#if !defined(PSX_SDL3)
    SDL_free((void *)base);
#endif
    (void)tekken3_outfits_load_catalog(path);
}

PSX_MOD_CONSTRUCTOR(tekken3_register_character_packs) {
    for (unsigned i = 0; i < sizeof packs / sizeof packs[0]; ++i)
        (void)psx_mod_register_activation_plugin_with(packs[i].plugin, activate, (void *)&packs[i]);
    (void)psx_mod_register_game_start_hook(load_catalog);
}
