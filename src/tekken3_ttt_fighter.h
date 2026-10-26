#ifndef TEKKEN3_TTT_FIGHTER_H
#define TEKKEN3_TTT_FIGHTER_H
/* Fighters imported from local TTT1 arcade data. Each keeps its own roster
 * ID, asset directory, outfits and imported move IDs; the arcade renderer,
 * skeleton bridge and combat adapter are shared. Jin is every fighter's
 * donor: his first outfit's loaded model header receives the arcade rows. */
#include <stdint.h>
#include "tekken3_jun_costumes.h"
#include "tekken3_jun_texture_layout.h"

enum { TTT_FIGHTER_COUNT=1, TTT_FACE_SOURCES=4, TTT_NO_FACE=0xffff };

/* Face expressions copy a 16x64 source tile over the destination tile;
 * variant 0 restores the destination's own tile. A masked fighter has none:
 * its x is TTT_NO_FACE. */
typedef struct {
    uint16_t x,y,sources,source[TTT_FACE_SOURCES][2];
    uint16_t w,h;  /* face tile size; 0 means 16x64 */
} TttFace;

typedef struct TttFighter {
    const char *name;          /* asset prefix, e.g. Jun-TTT1-combat.jmv */
    const char *dir;           /* mods/<dir> beside the executable */
    const char *env;           /* asset directory override */
    unsigned roster_id;        /* SLUS-00402 character ID */
    unsigned source_key;       /* TTT1 select ID, used by opponent rules */
    unsigned base_id;          /* first imported move ID */
    unsigned outfits;
    const TttCostumeSpec *costumes;
    const unsigned *texture_counts;
    const TttTextureTile (*texture_tiles)[32];
    const TttFace *faces;
    unsigned char rows[27];    /* installed header row -> arcade row */
    /* Parts whose rows are rigid rest-pose accessories. */
    unsigned char accessory_first,accessory_last;
    /* Parts drawn and solved: SLUS-00402's 22, or all 24 actor slots. */
    unsigned char parts;
    unsigned voice_size;
    const char *voice_sha;
} TttFighter;

extern const TttFighter tekken3_ttt_fighters[TTT_FIGHTER_COUNT];
enum { TTT_JUN=0 };

/* Explicit override, otherwise the private pack beside the executable. */
const char *tekken3_ttt_asset_root(const TttFighter *fighter);
/* A fighter's package plugin marks it wanted before its first VBlank. */
void tekken3_ttt_want(unsigned fighter);
int tekken3_ttt_wanted(unsigned fighter);
/* The fighter a player has selected, or NULL for stock characters. */
const TttFighter *tekken3_ttt_player_fighter(unsigned player);
const TttFighter *tekken3_ttt_by_roster(unsigned id);
static inline unsigned tekken3_ttt_index(const TttFighter *f) {
    return (unsigned)(f-tekken3_ttt_fighters);
}
static inline const TttTextureTile *tekken3_ttt_texture_tile(const TttFighter *f,unsigned outfit,
                                                             unsigned mode,unsigned x,unsigned y) {
    if(outfit>=f->outfits)return 0;
    for(unsigned i=0;i<f->texture_counts[outfit];i++) {
        const TttTextureTile *t=&f->texture_tiles[outfit][i];
        if(t->mode==mode && x>=t->x && x<t->x+t->w && y>=t->y && y<t->y+t->h)return t;
    }
    return 0;
}
/* The tile placing a TIM block's texels: the block itself, or the part of it
 * the polygons use when the planner trimmed a costume to fit. */
static inline const TttTextureTile *tekken3_ttt_block_tile(const TttFighter *f,unsigned outfit,unsigned mode,
                                                           unsigned x,unsigned y,unsigned w,unsigned h) {
    if(outfit>=f->outfits)return 0;
    for(unsigned i=0;i<f->texture_counts[outfit];i++) {
        const TttTextureTile *t=&f->texture_tiles[outfit][i];
        if(t->mode==mode && t->x>=x && t->x+t->w<=x+w && t->y>=y && t->y+t->h<=y+h)return t;
    }
    return 0;
}
/* Part loop bound for an actor (generated shards 06/07 call this). */
unsigned tekken3_ttt_part_limit(uint32_t actor);
/* Shared per-frame work: model installs, combat, voices. Each fighter's
 * VBlank plugin calls it; later calls in the same frame are ignored. */
void tekken3_ttt_tick(void);
#endif
