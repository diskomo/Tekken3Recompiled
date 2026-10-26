/* Shared menu art for imported fighters (see tekken3_ttt_ui.h). Jun's
 * values reproduce her original roster UI exactly. */
#include "tekken3_ttt_ui.h"
#include "mod_plugins.h"
#include "psx_runtime.h"
#include "gpu_render.h"
#include "psx_sha256.h"
#include "tekken3_jun_ui_layout.h"
#include "tekken3_roster.h"
#include "tekken3_guest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t u32(const unsigned char *p) {return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint16_t u16(const unsigned char *p) {return p[0]|(uint16_t)p[1]<<8;}
static void copy_host(uint32_t dst,const unsigned char *src,unsigned n) {
    for(unsigned i=0;i<n;i++)psx_mod_write_byte(dst+i,src[i]);
}
static int hash_is(const void *data,size_t size,const char *expected) {
    unsigned char digest[32];char hex[65];
    psx_sha256_compute((const uint8_t*)data,size,digest);
    for(unsigned i=0;i<32;i++)snprintf(hex+i*2,3,"%02x",digest[i]);
    return !strcmp(hex,expected);
}
static unsigned name_words(const TttUi *ui) {return ui->name_width/4;}
static unsigned icon_y(const TttUi *ui) {return ui->icon_y?ui->icon_y:ICON_Y;}

int tekken3_ttt_ui_load(TttUi *ui) {
    const char *root=tekken3_ttt_asset_root(ui->fighter);char path[4096];
    ui->icon_screen=ui->grey_screen=~0u;
    if(!root || snprintf(path,sizeof path,"%s/%s",root,ui->pack)>=(int)sizeof path)return 0;
    FILE *f=fopen(path,"rb");if(!f)return 0;
    unsigned char h[56];int ok=fread(h,1,sizeof h,f)==sizeof h;
    if(!ok || u32(h)!=0x3149554a || u32(h+4)!=1 || u32(h+8)!=5 ||
       u32(h+12)!=40576){fclose(f);return 0;}
    ui->ui_size=u32(h+12);ui->ui=malloc(ui->ui_size);if(!ui->ui){fclose(f);return 0;}
    memcpy(ui->ui,h,sizeof h);ok=fread(ui->ui+56,1,ui->ui_size-56,f)==ui->ui_size-56 && fgetc(f)==EOF;fclose(f);
    for(unsigned i=0;ok && i<5;i++) {
        unsigned o=u32(h+16+i*8),n=u32(h+20+i*8);
        static const unsigned widths[5]={126,32,32,32,32},heights[5]={252,68,58,34,29};
        ok=o>=56 && o<=ui->ui_size && n<=ui->ui_size-o && n==544+widths[i]*heights[i] &&
           u32(ui->ui+o)==16 && u32(ui->ui+o+4)==9 && u32(ui->ui+o+8)==524 &&
           u16(ui->ui+o+540)==widths[i]/2 && u16(ui->ui+o+542)==heights[i];
        ui->offsets[i]=o;ui->lengths[i]=n;
    }
    if(!ok || !hash_is(ui->ui,ui->ui_size,ui->pack_sha)){free(ui->ui);ui->ui=NULL;return 0;}
    unsigned bytes=name_words(ui)*16*2;
    if(bytes>sizeof ui->name_pixels || snprintf(path,sizeof path,"%s/%s",root,ui->name)>=(int)sizeof path)return 0;
    f=fopen(path,"rb");if(!f)return 0;
    ok=fread(ui->name_pixels,1,bytes,f)==bytes && fgetc(f)==EOF;fclose(f);
    return ok && hash_is(ui->name_pixels,bytes,ui->name_sha);
}

void tekken3_ttt_ui_describe(TttUi *ui,uint32_t desc) {
    psx_mod_write_byte(desc+5,(uint8_t)ui->name_width);  /* Name strip width. */
    psx_mod_write_byte(desc+9,(uint8_t)ui->fighter->roster_id);
    /* A distinct image-cache ID avoids treating the fighter as the blank
     * portrait (21) when changing between character select and loading. */
    ui->record=psx_mod_alloc_guest_memory(16,16);
    if(ui->record)psx_mod_write_word(ui->record,ui->color);
    ui->stock_icon_uv=psx_mod_read_word(0x8002152c+21*8);
    ui->stock_icon_page=psx_mod_read_word(0x8002152c+21*8+4);
    const unsigned char *small=ui->ui+ui->offsets[4]+544;
    for(unsigned y=0;y<58;y++)memcpy(ui->loading_pixels+y*16,small+(y/2)*32,32);
    psx_mod_log("%s roster: registered character %u / model %u; UI loaded\n",ui->fighter->name,
            ui->fighter->roster_id,tekken3_roster_first_model(ui->fighter->roster_id));
}

static void icon_upload(TttUi *ui) {
    const unsigned char *p=ui->ui+ui->offsets[2];
    ttt_ui_palette_upload(&ui->selector_palette,ui->selector_row,(const uint16_t*)(p+20));
    ttt_ui_texture_upload(&ui->selector_texture,ui->icon_x,icon_y(ui),(const uint16_t*)(p+544));
}
static void loading_upload(TttUi *ui) {
    ttt_ui_palette_upload(&ui->loading_palette,ui->loading_row,(const uint16_t*)(ui->ui+ui->offsets[4]+20));
    ttt_ui_texture_upload(&ui->loading_texture,ui->loading_x,icon_y(ui),ui->loading_pixels);
}
static void grey_upload(TttUi *ui,unsigned screen) {
    const unsigned char *colors=ui->ui+ui->offsets[screen==11?4:2]+20;
    for(unsigned i=0;i<256;i++)ui->grey_colors[i]=jun_ui_grey(u16(colors+i*2));
    ttt_ui_palette_upload(&ui->grey_palette,ui->grey_row,ui->grey_colors);
    ui->grey_screen=screen;
}

void tekken3_ttt_ui_tick(TttUi *ui) {
    const unsigned id=ui->fighter->roster_id;
    int cabinet=psx_mod_read_word(T3_SCREEN)==T3_SCREEN_CABINET && tekken3_cabinet_selector_loaded();
    unsigned screen=psx_mod_read_word(T3_SCREEN);
    /* The last selector packets can still be visible while screen 11 is
     * preparing its first frame. Retain their CLUT until gameplay starts. */
    if(screen==8 || (!cabinet && screen!=10 && screen!=11 && screen!=ui->icon_screen)) {
        ttt_ui_palette_release(&ui->selector_palette,ui->selector_row);
        ttt_ui_texture_release(&ui->selector_texture,ui->icon_x,icon_y(ui));
    }
    if(screen!=11) {
        ttt_ui_palette_release(&ui->loading_palette,ui->loading_row);
        ttt_ui_texture_release(&ui->loading_texture,ui->loading_x,icon_y(ui));
    }
    if(screen==8 || screen!=ui->grey_screen)
        ttt_ui_palette_release(&ui->grey_palette,ui->grey_row);
    else if(ui->grey_palette.active)grey_upload(ui,screen);
    if(cabinet) {
        /* The roster points the portrait record at the blank one; the decode
         * hook supplies the TIM, and each panel showing the fighter gets it. */
        for(unsigned p=0;p<2;p++) {
            uint32_t panel=0x80118628+0x24+p*0x7c;
            if(psx_mod_read_word(panel+0x14)==id) {
                const unsigned char *portrait=ui->ui+ui->offsets[0];
                unsigned clut=psx_mod_read_half(panel+0x5c);
                gr_vram_transfer_in(psx_mod_read_half(panel+0x58),
                    psx_mod_read_half(panel+0x5a),63,252,(const uint16_t*)(portrait+544));
                gr_vram_transfer_in((clut&63)*16,clut>>6,256,1,(const uint16_t*)(portrait+20));
            }
        }
        /* Name strip row = character * 16 within its page. */
        gr_vram_transfer_in(736,id*16-256,name_words(ui),16,ui->name_pixels);
    }
    if(cabinet || screen==10 || (screen==11 && ui->selector_palette.active) ||
       (screen==ui->icon_screen && screen!=11 && screen!=8))
        icon_upload(ui);
    /* Arcade loading thumbnails have half-height pixels. The PS1's framed
     * team cards use 58 rows, so preserve their aspect ratio. Do not overwrite
     * the still-visible selector thumbnail; the two images use different texels. */
    if(screen==11 && ui->loading_palette.active)loading_upload(ui);
    /* Fight HUD name plates. */
    if(screen==8) {
        for(unsigned p=0;p<2;p++)if(psx_mod_read_half(T3_ACTOR(p)+T3_ACTOR_CHARACTER)==id)
            gr_vram_transfer_in(464,p*256,name_words(ui),16,ui->name_pixels);
    }
}

/* The selector and loading portraits replace the blank record's TIM. */
int tekken3_ttt_ui_portrait(TttUi *ui,CPUState *cpu) {
    copy_host(cpu->gpr[5],ui->ui+ui->offsets[0],ui->lengths[0]);
    cpu->gpr[2]=ui->lengths[0];cpu->pc=cpu->gpr[31];return 1;
}
int tekken3_ttt_ui_loading(TttUi *ui,CPUState *cpu) {
    if((cpu->pc==0 || cpu->pc==0x80052ad0) &&
       cpu->gpr[31]==0x80052770 && cpu->gpr[20]<2 &&
       psx_mod_read_half(T3_SIDE_CHARACTERS+cpu->gpr[20]*2)==ui->fighter->roster_id) {
        /* The loader expects compressed input. The portrait hook supplies
         * the validated TIM; never feed a raw TIM into the native LZ decoder. */
        psx_mod_write_byte(cpu->gpr[4],0);
        cpu->gpr[2]=0;cpu->pc=cpu->gpr[31];return 1;
    }
    return 0;
}
/* Roster icons borrow stock slot 21's UV entry, pointed at the fighter's
 * scratch texture for each draw. The usage-row owner restores it for
 * everyone else. */
int tekken3_ttt_ui_icon(TttUi *ui,CPUState *cpu,unsigned id) {
    uint32_t slot=cpu->gpr[29]+16;
    /* Usage rows use saved counter ID 21; all other Crow/empty callers
     * retain their native identity and art. */
    if(psx_mod_read_word(T3_SCREEN)!=T3_SCREEN_FIGHT && (id==ui->fighter->roster_id ||
       (ui->usage_owner && id==21 && cpu->gpr[31]==0x800c1538))) {
        ui->icon_screen=psx_mod_read_word(T3_SCREEN);
        if(ui->icon_screen!=11)icon_upload(ui);
        else loading_upload(ui);
        unsigned flags=psx_mod_read_word(slot+4),knocked_out=!!(flags&0x20);
        if(knocked_out) {
            /* Native KO replaces the CLUT with 0x7d50, whose indices belong
             * to stock portraits. Use the fighter's own grey CLUT. Leave all
             * geometry/other flags intact. */
            grey_upload(ui,ui->icon_screen);
            psx_mod_write_word(slot+4,flags&~0x20u);
        }
        unsigned loading=ui->icon_screen==11;
        unsigned row=knocked_out?ui->grey_row:loading?ui->loading_row:ui->selector_row;
        psx_mod_write_word(slot,21*4);
        psx_mod_write_word(0x8002152c+21*8,
            (row<<22)|(icon_y(ui)<<8)|(((loading?ui->loading_x:ui->icon_x)-ICON_X)*2));
        psx_mod_write_half(0x8002152c+21*8+4,ICON_PAGE);
        return 1;
    }
    if(ui->usage_owner && id==21) {
        psx_mod_write_word(0x8002152c+21*8,ui->stock_icon_uv);
        psx_mod_write_word(0x8002152c+21*8+4,ui->stock_icon_page);
    }
    return 0;
}
