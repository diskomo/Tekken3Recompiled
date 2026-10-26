/* Optional HD selector portraits, matched by content. A 63x252 8bpp upload
 * whose pixels hash to a pack entry claims that VRAM rect for as long as the
 * pixels stay the same. Disk records contain no guest payload. */
#ifndef GPU_HD_PORTRAITS_H
#define GPU_HD_PORTRAITS_H
#include "gpu_hd_texture_match.h"
#define HD_PORTRAIT_KIND 12
#define HD_PORTRAIT_MAX 64
#define HD_PORTRAIT_SLOTS 8
#define HD_PORTRAIT_W 63 /* words: 126 8bpp texels */
#define HD_PORTRAIT_H 252
typedef struct HdPortrait {
    uint64_t hash;
    float u0, v0, u1, v1; /* cell in the normalized atlas */
} HdPortrait;
typedef struct HdPortraits {
    HdPortrait entries[HD_PORTRAIT_MAX];
    uint32_t count, next;
    HdTextureTile slots[HD_PORTRAIT_SLOTS];
    uint64_t generation;
} HdPortraits;
static void hd_portraits_clear(HdPortraits *p) {
    memset(p, 0, sizeof *p); p->generation = 1;
}
static int hd_portraits_load(HdPortraits *p, const char *path) {
    char magic[8]; uint32_t count = 0; int ok = 0;
    FILE *f = fopen(path, "rb");
    hd_portraits_clear(p);
    if (!f) return 0;
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "HDPORT01", 8) ||
        fread(&count, 4, 1, f) != 1 || !count || count > HD_PORTRAIT_MAX) goto done;
    for (uint32_t i = 0; i < count; ++i) {
        HdPortrait *e = &p->entries[i];
        if (fread(&e->hash, 8, 1, f) != 1 || fread(&e->u0, 4, 4, f) != 4) goto done;
        if (!isfinite(e->u0) || !isfinite(e->v0) || !isfinite(e->u1) || !isfinite(e->v1) ||
            e->u0 < 0 || e->v0 < 0 || e->u1 > 1 || e->v1 > 1 || e->u0 >= e->u1 || e->v0 >= e->v1)
            goto done;
    }
    if (fgetc(f) != EOF) goto done;
    p->count = count; ok = 1;
done:
    fclose(f);
    if (!ok) hd_portraits_clear(p);
    return ok;
}
/* After the transfer has landed in VRAM: claim the rect if it is a portrait. */
static void hd_portraits_upload(HdPortraits *p, const uint16_t *vram, int x, int y, int w, int h) {
    if (!p->count || !vram || w != HD_PORTRAIT_W || h != HD_PORTRAIT_H ||
        x < 0 || y < 0 || x + w > 1024 || y + h > 512) return;
    uint64_t hash = hd_hash_rect(vram, (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h);
    const HdPortrait *e = NULL;
    for (uint32_t i = 0; i < p->count && !e; ++i)
        if (p->entries[i].hash == hash) e = &p->entries[i];
    if (!e) return;
    HdTextureTile *t = NULL;
    for (int i = 0; i < HD_PORTRAIT_SLOTS && !t; ++i)
        if (p->slots[i].w && p->slots[i].x == (uint32_t)x && p->slots[i].y == (uint32_t)y) t = &p->slots[i];
    if (!t) t = &p->slots[p->next++ % HD_PORTRAIT_SLOTS];
    memset(t, 0, sizeof *t);
    t->x = (uint32_t)x; t->y = (uint32_t)y; t->w = HD_PORTRAIT_W; t->h = HD_PORTRAIT_H;
    t->pixels_hash = hash; t->kind = HD_PORTRAIT_KIND; t->depth = 1;
    /* 8bpp texel u in [2x, 2x+126) and v in [y, y+252) span the cell. */
    float su = (e->u1 - e->u0) / (HD_PORTRAIT_W * 2), sv = (e->v1 - e->v0) / HD_PORTRAIT_H;
    t->map[0] = su; t->map[2] = e->u0 - su * (float)(x * 2);
    t->map[4] = sv; t->map[5] = e->v0 - sv * (float)y;
    t->checked_generation = p->generation; t->matches = 1;
}
static const HdTextureTile *hd_portraits_find(HdPortraits *p, const uint16_t *vram,
        int tx, int ty, int depth, const int *us, const int *vs) {
    if (!p->count || !vram || depth != 1) return NULL;
    for (int i = 0; i < HD_PORTRAIT_SLOTS; ++i) {
        HdTextureTile *t = &p->slots[i]; int inside = 1;
        if (!t->w) continue;
        for (int k = 0; k < 3; ++k) {
            int x = tx * 2 + us[k], y = ty + vs[k];
            if (x < (int)t->x * 2 || x > (int)(t->x + t->w) * 2 || y < (int)t->y || y > (int)(t->y + t->h))
                inside = 0;
        }
        if (!inside) continue;
        if (t->checked_generation != p->generation) {
            t->matches = hd_hash_rect(vram, t->x, t->y, t->w, t->h) == t->pixels_hash;
            t->checked_generation = p->generation;
        }
        if (t->matches) return t;
    }
    return NULL;
}
#endif
