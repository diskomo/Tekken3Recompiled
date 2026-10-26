#ifndef TEKKEN3_TTT_UI_H
#define TEKKEN3_TTT_UI_H
/* Menu art for an imported fighter in the shared extra roster: the cabinet
 * portrait, selector and loading thumbnails (8bpp, own CLUTs) and the name
 * strip. Thumbnails live in menu-only scratch of fighter page 0x86 (x 384..511,
 * blank on every selector and loading screen above row 224). Each fighter owns
 * two 16-halfword columns 58 rows tall, in the band at row 160 (four fighters)
 * or row 96 (four more), and three CLUT rows (x 0..255) from 503 downwards.
 * Native contents are restored when a menu closes. */
#include <stdint.h>
#include "tekken3_ttt_ui_palette.h"
#include "tekken3_ttt_ui_texture.h"
#include "tekken3_ttt_fighter.h"

struct CPUState;

typedef struct TttUi {
    const TttFighter *fighter;
    const char *pack, *name;          /* files in the fighter's asset root */
    const char *pack_sha, *name_sha;
    unsigned name_width;              /* pixels, a multiple of 4 */
    unsigned icon_x, loading_x;       /* thumbnail scratch columns */
    unsigned icon_y;                  /* thumbnail band; 0 = ICON_Y (160) */
    unsigned grey_row, selector_row, loading_row;
    uint32_t color;                   /* distinct portrait image-cache ID */
    int usage_owner;                  /* draws saved statistics row 21 */
    /* State. */
    unsigned char *ui;
    uint32_t ui_size, offsets[5], lengths[5], record;
    uint16_t name_pixels[24*16], loading_pixels[16*58], grey_colors[256];
    TttUiPalette selector_palette, loading_palette, grey_palette;
    TttUiTexture selector_texture, loading_texture;
    unsigned icon_screen, grey_screen;
    uint32_t stock_icon_uv, stock_icon_page;
} TttUi;

/* Validate and load the private pack; 1 when every hash matches. */
int tekken3_ttt_ui_load(TttUi *ui);
/* Roster hooks (see Tekken3RosterImport). */
void tekken3_ttt_ui_describe(TttUi *ui, uint32_t descriptor);
void tekken3_ttt_ui_tick(TttUi *ui);
int tekken3_ttt_ui_portrait(TttUi *ui, struct CPUState *cpu);
int tekken3_ttt_ui_loading(TttUi *ui, struct CPUState *cpu);
int tekken3_ttt_ui_icon(TttUi *ui, struct CPUState *cpu, unsigned id);
#endif
