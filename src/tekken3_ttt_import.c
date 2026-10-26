/* Local, opt-in TTT1 fighter imports. Assets are loaded from explicit private
 * directories and never embedded in or distributed with the executable. */
#include "mod_plugins.h"
#include "psx_runtime.h"
#include "gpu_render.h"
#include "gpu.h"
#include "psx_sha256.h"
#include "psx_sdl.h"
#include "tekken3_jun_assets.h"
#include "tekken3_ttt_fighter.h"
#include "tekken3_outfits.h"
#include "tekken3_roster.h"
#include "tekken3_guest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned char *model, *textures;
    uint32_t *relocations, guest;
} TttCostume;
typedef struct {
    TttCostume costumes[3];
    uint32_t guest;            /* nonzero once every outfit is installed */
    int attempted, wanted;
} FighterAssets;
static FighterAssets assets[TTT_FIGHTER_COUNT];
static uint32_t control;
static uint32_t arena[2];
static uint32_t original_header[2][384], original_base[2];
/* The fighter whose rows are installed in each player's header. */
static const TttFighter *installed_fighter[2];
static int initializing;
static int face_variant[2]={-1,-1};
static unsigned player_outfit(const TttFighter *f,unsigned player) {
    unsigned outfit=psx_mod_read_half(T3_ACTOR(player)+T3_ACTOR_COSTUME)&3;
    return outfit<f->outfits?outfit:0;
}
static const TttFighter *installed_outfit(uint32_t base,int *outfit) {
    uint32_t first=psx_mod_read_word(base+24);
    for(unsigned f=0;f<TTT_FIGHTER_COUNT;f++)
        for(unsigned i=0;i<tekken3_ttt_fighters[f].outfits;i++) {
            const TttCostume *c=&assets[f].costumes[i];
            if(c->guest && first==psx_mod_read_word(c->guest+24)) {
                *outfit=(int)i;return &tekken3_ttt_fighters[f];
            }
        }
    *outfit=-1;return NULL;
}
static const char *asset_root(const TttFighter *f) {
    const char *override=getenv(f->env),*dir=f->dir;
    if(override && *override)return override;
    static char paths[TTT_FIGHTER_COUNT][4096];
    char *path=paths[tekken3_ttt_index(f)];
    if(!path[0]) {
        const char *base=SDL_GetBasePath();
        if(!base)return NULL;
        int n=snprintf(path,4096,"%smods/%s",base,dir);
#if !defined(PSX_SDL3)
        SDL_free((void*)base);
#endif
        if(n<0 || n>=4096){path[0]=0;return NULL;}
    }
    return path;
}
const char *tekken3_ttt_asset_root(const TttFighter *f) {return asset_root(f);}
const char *tekken3_jun_asset_root(void) {return tekken3_ttt_asset_root(&tekken3_ttt_fighters[TTT_JUN]);}
void tekken3_ttt_want(unsigned f) {if(f<TTT_FIGHTER_COUNT)assets[f].wanted=1;}
int tekken3_ttt_wanted(unsigned f) {return f<TTT_FIGHTER_COUNT && assets[f].wanted;}
extern void tekken3_ttt_combat_tick(void);
extern void tekken3_jun_selector_tick(void);
extern void tekken3_ttt_voices_tick(void);
extern int tekken3_jun_roster_enabled(void);
const TttFighter *tekken3_ttt_by_roster(unsigned id) {
    for(unsigned f=0;f<TTT_FIGHTER_COUNT;f++)
        if(assets[f].guest && tekken3_ttt_fighters[f].roster_id==id)return &tekken3_ttt_fighters[f];
    return NULL;
}
const TttFighter *tekken3_ttt_player_fighter(unsigned player) {
    if(player>1)return NULL;
    const TttFighter *f=tekken3_ttt_by_roster(psx_mod_read_half(T3_ACTOR(player)+T3_ACTOR_CHARACTER));
    if(f && (tekken3_ttt_index(f)!=TTT_JUN || tekken3_jun_roster_enabled()))return f;
    /* Without the shared roster, Jun is an alternative to P1's Jin. */
    if(!tekken3_jun_roster_enabled() && assets[TTT_JUN].guest && player==0 &&
       psx_mod_read_byte(T3_P1_CHARACTER)==9)return &tekken3_ttt_fighters[TTT_JUN];
    return NULL;
}
void tekken3_ttt_select(int enabled) {
    if(!control)return;
    if(psx_mod_read_byte(control)==(enabled?1:0) && psx_mod_read_byte(control+12)==(enabled?2:0))return;
    psx_mod_write_byte(control,enabled?1:0);
    psx_mod_write_byte(control+12,enabled?2:0);
    if(!enabled)tekken3_ttt_combat_tick();
    psx_mod_log("TTT1 import: %s\n",enabled?"active":"inactive");
}

void tekken3_ttt_before_init(uint32_t model) {
    initializing++;
    int outfit;
    for(unsigned p=0;p<2;p++)if (model==original_base[p] && control && installed_outfit(model,&outfit)) {
        for(unsigned i=0;i<384;i++) psx_mod_write_word(model+i*4,original_header[p][i]);
        psx_mod_log("TTT1 import: restored stock header before actor initialization\n");
    }
}
void tekken3_ttt_after_init(void) { if(initializing>0) initializing--; }

static uint32_t word(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static int verified_asset(const void *data, size_t size, const char *expected) {
    unsigned char hash[32]; char actual[65];
    psx_sha256_compute((const uint8_t*)data,size,hash);
    for(unsigned i=0;i<32;i++) snprintf(actual+i*2,3,"%02x",hash[i]);
    return strcmp(actual,expected)==0;
}
static int read_asset(const char *root, const char *name, void *data, size_t size) {
    char path[4096];
    if (snprintf(path,sizeof(path),"%s/%s",root,name)>=(int)sizeof(path)) return 0;
    FILE *f=fopen(path,"rb");
    if (!f) return 0;
    int ok=fread(data,1,size,f)==size && fgetc(f)==EOF;
    fclose(f); return ok;
}
static void load_assets(unsigned index) {
    const TttFighter *f=&tekken3_ttt_fighters[index];
    FighterAssets *fa=&assets[index];
    const char *root=tekken3_ttt_asset_root(f);
    if (!root || !*root) return;
    for(unsigned c=0;c<f->outfits;c++) {
        TttCostume *a=&fa->costumes[c];const TttCostumeSpec *s=&f->costumes[c];
        char name[64];
        a->model=malloc(s->model_size);a->textures=malloc(s->texture_size);
        a->relocations=malloc(s->relocation_count*4);
        if(!a->model || !a->textures || !a->relocations)goto invalid;
        snprintf(name,sizeof name,"%s-TTT1-arcade-P%u.3dm",f->name,c+1);
        if(!read_asset(root,name,a->model,s->model_size) || !verified_asset(a->model,s->model_size,s->model_sha))goto invalid;
        snprintf(name,sizeof name,"%s-TTT1-arcade-P%u.tim",f->name,c+1);
        if(!read_asset(root,name,a->textures,s->texture_size) || !verified_asset(a->textures,s->texture_size,s->texture_sha))goto invalid;
        snprintf(name,sizeof name,"%s-TTT1-arcade-P%u.relocs",f->name,c+1);
        if(!read_asset(root,name,a->relocations,s->relocation_count*4) || !verified_asset(a->relocations,s->relocation_count*4,s->relocation_sha))goto invalid;
        if(word(a->model)!=30 || word(a->model+8)!=0x4b4d4433 || word(a->model+16)!=0x6a0)goto invalid;
        for(unsigned i=0;i<s->relocation_count;i++) {
            uint32_t r=a->relocations[i];
            if((r&3) || r>s->model_size-4 || !word(a->model+r) || word(a->model+r)>=s->model_size)goto invalid;
        }
    }
    if(!control)control=psx_mod_alloc_guest_memory(32,16);
    if(!arena[0])arena[0]=psx_mod_alloc_gpu_dma_memory(262144,16);
    if(!arena[1])arena[1]=psx_mod_alloc_gpu_dma_memory(262144,16);
    if (!control || !arena[0] || !arena[1])goto invalid;
    for(unsigned c=0;c<f->outfits;c++) {
        TttCostume *a=&fa->costumes[c];const TttCostumeSpec *s=&f->costumes[c];
        a->guest=psx_mod_alloc_guest_memory(s->model_size,16);
        if(!a->guest)goto invalid;
        for(unsigned i=0;i<s->model_size;i+=4)psx_mod_write_word(a->guest+i,word(a->model+i));
        for(unsigned i=0;i<s->relocation_count;i++) {
            uint32_t r=a->relocations[i];psx_mod_write_word(a->guest+r,word(a->model+r)+a->guest);
        }
        psx_mod_log("%s outfit %u: model=%08X bytes=%u\n",f->name,c+1,a->guest,s->model_size);
    }
    fa->guest=fa->costumes[0].guest;
    tekken3_outfits_set_character_available(f->roster_id,1);
    psx_mod_log("%s import: control=%08X model=%08X\n",f->name,control,fa->guest);
    return;
invalid:
    for(unsigned c=0;c<f->outfits;c++) {
        free(fa->costumes[c].model);free(fa->costumes[c].textures);free(fa->costumes[c].relocations);
        memset(&fa->costumes[c],0,sizeof fa->costumes[c]);
    }
    fa->guest=0;
    psx_mod_log("%s import: all %u verified arcade outfits are required; run the local importer\n",
            f->name,f->outfits);
}
int tekken3_ttt_uses_arcade_renderer(uint32_t address) {
    if (!control || psx_mod_read_byte(control)!=1) return 0;
    /* address is a part's mesh bundle; +16 holds the arcade mesh pointer.
     * (The word at +0 belongs to the preceding header row.) */
    uint32_t p=psx_mod_read_word(address+16);
    for(unsigned f=0;f<TTT_FIGHTER_COUNT;f++)
        for(unsigned c=0;c<tekken3_ttt_fighters[f].outfits;c++)
            if(assets[f].costumes[c].guest && p>=assets[f].costumes[c].guest &&
               p<assets[f].costumes[c].guest+tekken3_ttt_fighters[f].costumes[c].model_size)return 1;
    return 0;
}
unsigned tekken3_ttt_player_motion_mode(unsigned player) {
    if(player>1 || !control || !original_base[player] || psx_mod_read_byte(control)!=1 || initializing)return 0;
    const TttFighter *f=tekken3_ttt_player_fighter(player);
    int outfit;
    return f && psx_mod_read_word(0x8009bd28u+player*4)==original_base[player] &&
        installed_outfit(original_base[player],&outfit)==f && installed_fighter[player]==f &&
        outfit==(int)player_outfit(f,player)
        ? psx_mod_read_byte(control+12) : 0;
}
unsigned tekken3_ttt_motion_mode(void) {return tekken3_ttt_player_motion_mode(0);}
static uint16_t half(const unsigned char *p) { return p[0] | (uint16_t)p[1]<<8; }
static void upload_textures(const TttFighter *f,unsigned player) {
    unsigned outfit=player_outfit(f,player);
    const unsigned char *textures=assets[tekken3_ttt_index(f)].costumes[outfit].textures;
    unsigned texture_size=f->costumes[outfit].texture_size;
    face_variant[player]=-1;
    for (unsigned o=0;o+8<texture_size;) {
        if (word(textures+o)!=16) return;
        unsigned flags=word(textures+o+4);o+=8;
        if (flags&8) {
            unsigned n=word(textures+o);
            gr_vram_transfer_in(half(textures+o+4),504+player*4+half(textures+o+6),
                half(textures+o+8),half(textures+o+10),(const uint16_t*)(textures+o+12));
            o+=n;
        }
        unsigned n=word(textures+o);
        unsigned x=half(textures+o+4), y=half(textures+o+6), w=half(textures+o+8), h=half(textures+o+10);
        /* Asset hashes and the offline UV audit bind this table; a block
         * without a tile is one the planner found no polygon or face uses. */
        const TttTextureTile *tile=tekken3_ttt_block_tile(f,outfit,flags&3,x,y,w,h);
        if(!tile){o+=n;continue;}
        const uint16_t *pixels=(const uint16_t*)(textures+o+12);
        if(tile->w==w && tile->h==h)
            gr_vram_transfer_in(384+tile->dx,player*256+tile->dy,w,h,pixels);
        else for(unsigned row=0;row<tile->h;row++)
            gr_vram_transfer_in(384+tile->dx,player*256+tile->dy+row,tile->w,1,
                pixels+(tile->y-y+row)*w+tile->x-x);
        o+=n;
    }
}
void tekken3_ttt_face(unsigned player,unsigned variant) {
    const TttFighter *f=tekken3_ttt_player_fighter(player);
    if(!f || installed_fighter[player]!=f || face_variant[player]==(int)variant)return;
    unsigned outfit=player_outfit(f,player);
    const TttFace *face=&f->faces[outfit];
    if(face->x==TTT_NO_FACE || variant>face->sources)return;
    const unsigned char *textures=assets[tekken3_ttt_index(f)].costumes[outfit].textures;
    unsigned texture_size=f->costumes[outfit].texture_size;
    /* Arcade face table 801945D8[model]: variant 0 restores the destination
     * tile, which the arcade backed up at (192,64); others copy a source. */
    unsigned sx=variant?face->source[variant-1][0]:face->x,sy=variant?face->source[variant-1][1]:face->y;
    unsigned fw=face->w?face->w:16,fh=face->h?face->h:64;
    for(unsigned o=0;o+8<texture_size;) {
        if(word(textures+o)!=16)return;
        unsigned flags=word(textures+o+4);o+=8;
        if(flags&8)o+=word(textures+o);
        unsigned n=word(textures+o),x=half(textures+o+4),y=half(textures+o+6);
        if(x==sx && y==sy && half(textures+o+8)==fw && half(textures+o+10)==fh) {
            const TttTextureTile *tile=tekken3_ttt_texture_tile(f,outfit,1,face->x,face->y);
            if(!tile)return;
            gr_vram_transfer_in(384+tile->dx,player*256+tile->dy,fw,fh,(const uint16_t*)(textures+o+12));
            face_variant[player]=(int)variant;return;
        }
        o+=n;
    }
}
static void ensure_textures(const TttFighter *f,unsigned player) {
    /* Native round/replay loads and PS1 save states can restore the loader's
     * Jin textures while retaining our model/packet pointers. */
    unsigned outfit=player_outfit(f,player);
    const unsigned char *textures=assets[tekken3_ttt_index(f)].costumes[outfit].textures;
    unsigned texture_size=f->costumes[outfit].texture_size;
    const TttFace *face=&f->faces[outfit];
    /* Host/CPU uploads update this shadow immediately. Do not call the GL
     * readback facade every VBlank: it downloads the entire framebuffer. */
    const uint16_t *vram=gpu_get_vram();
    if(!vram)return;
    /* Check every immutable tile, not just the first face. A late loading
     * thumbnail can overwrite the secondary page after model installation.
     * Read the CPU shadow only; this must never force a GPU readback. */
    for(unsigned o=0;o+8<texture_size;) {
        if(word(textures+o)!=16)return;
        unsigned flags=word(textures+o+4);o+=8;
        if(flags&8)o+=word(textures+o);
        unsigned n=word(textures+o),x=half(textures+o+4),y=half(textures+o+6);
        unsigned w=half(textures+o+8),h=half(textures+o+10);
        const TttTextureTile *tile=tekken3_ttt_block_tile(f,outfit,flags&3,x,y,w,h);
        if(!tile){o+=n;continue;}
        if(!(x==face->x && y==face->y))for(unsigned row=0;row<tile->h;row++) {
            const uint16_t *live=vram+(player*256+tile->dy+row)*1024+384+tile->dx;
            if(memcmp(live,textures+o+12+((tile->y-y+row)*w+tile->x-x)*2,tile->w*2)) {
                int expression=face_variant[player];
                upload_textures(f,player);
                if(expression>=0)tekken3_ttt_face(player,(unsigned)expression);
                return;
            }
        }
        o+=n;
    }
}
/* Build PS1 GPU packets from the donor's material/UV stream. The arcade
 * renderer supplies positions and lighting into these packet templates. */
static uint32_t make_packets(const TttFighter *f,uint32_t bundle,uint32_t out,unsigned player) {
    uint32_t m=psx_mod_read_word(bundle+16);
    uint32_t uv=m+psx_mod_read_word(m);
    uint32_t src=uv+psx_mod_read_half(uv);
    static const unsigned size[4]={32,40,40,52};
    static const unsigned opcode[4]={0x24,0x2c,0x34,0x3c};
    static const unsigned voff[4][4]={{12,20,28,0},{12,20,28,36},{12,24,36,0},{12,24,36,48}};
    for(unsigned kind=0;kind<4;kind++) {
        unsigned count=psx_mod_read_byte(src++),nv=(kind&1)?4:3;
        for(unsigned i=0;i<count;i++) {
            unsigned material=psx_mod_read_byte(src++);
            uint32_t mat=psx_mod_read_word(m+4+material*4);
            for(unsigned j=0;j<size[kind];j+=4) psx_mod_write_word(out+j,0);
            psx_mod_write_word(out+4,(opcode[kind]<<24)|0x808080);
            if(kind>=2) {
                psx_mod_write_word(out+16,0x808080);psx_mod_write_word(out+28,0x808080);
                if(nv==4) psx_mod_write_word(out+40,0x808080);
            }
            const TttTextureTile *tile=NULL;
            for(unsigned j=0;j<nv;j++) {
                unsigned idx=psx_mod_read_byte(src++);
                uint32_t v=psx_mod_read_half(uv+2+idx*2);
                unsigned page=mat>>16, mode=(page>>7)&3, ppw=4>>mode;
                unsigned x=(page&15)*64+(v&255)/ppw;
                unsigned y=(v>>8)+((page&16)?256:0);
                /* Each verified polygon lies within one TIM tile. Move its
                 * UVs with that tile, preserving texel sub-word coordinates.
                 * Never occupy the shared effects page or command/font data. */
                if(j==0)tile=tekken3_ttt_texture_tile(f,player_outfit(f,player),mode,x,y);
                if(!tile)return out;
                v=((tile->dx%64+x-tile->x)*ppw+(v&255)%ppw)|
                    ((tile->dy+y-tile->y)<<8);
                if(j==0) v|=((mat&0xffff)+0x7e00+player*256)<<16;
                if(j==1) v|=((page&~31u)| (6+tile->dx/64) | (player*16))<<16;
                psx_mod_write_word(out+voff[kind][j],v);
            }
            out+=size[kind];
        }
    }
    return out;
}
static int32_t mul12(int32_t a,int32_t b) { return (a*b)>>12; }
static void accessory_rotation(uint32_t destination,uint32_t row) {
    int32_t s[3],c[3];
    for(unsigned i=0;i<3;i++) {
        unsigned angle=psx_mod_read_word(row+28+i*4);
        uint32_t table=T3_SINE_TABLE+((angle>>3)&0x1ffe);
        s[i]=(int16_t)psx_mod_read_half(table);
        c[i]=(int16_t)psx_mod_read_half(table+0x800);
    }
    int32_t a=mul12(c[0],c[2]),b=mul12(c[0],s[2]);
    int32_t d=mul12(s[0],c[2]),e=mul12(s[0],s[2]);
    int32_t matrix[9]={mul12(c[1],c[2]),mul12(d,s[1])-b,e+mul12(a,s[1]),
        mul12(c[1],s[2]),a+mul12(e,s[1]),mul12(b,s[1])-d,
        -s[1],mul12(s[0],c[1]),mul12(c[0],c[1])};
    for(unsigned i=0;i<9;i++) psx_mod_write_half(destination+i*2,(uint16_t)matrix[i]);
}
/* A row hung exactly where the head is (same offset and parent, no rest
 * rotation) is a further head mesh, such as Kunimitsu's arcade row 23: its
 * accessory joint takes the head joint's animated rotation each frame. */
static int head_mesh(uint32_t base,unsigned row) {
    unsigned head=psx_mod_read_byte(0x8001a05c+17);
    for(unsigned i=0;i<4;i++)
        if(psx_mod_read_word(base+36+row*56+i*4)!=psx_mod_read_word(base+36+head*56+i*4))return 0;
    for(unsigned i=0;i<3;i++)if(psx_mod_read_word(base+52+row*56+i*4))return 0;
    return 1;
}
static void accessory_pose(uint32_t actor,unsigned bone,uint32_t base,unsigned row) {
    if(head_mesh(base,row)) {
        uint32_t head=actor+0xf74+psx_mod_read_byte(0x8001a074+17)*32;
        for(unsigned i=0;i<9;i++)psx_mod_write_half(actor+0xf74+bone*32+i*2,psx_mod_read_half(head+i*2));
    } else accessory_rotation(actor+0xf74+bone*32,base+24+row*56);
}
unsigned tekken3_ttt_part_limit(uint32_t actor) {
    for(unsigned p=0;p<2;p++)
        if(actor==T3_ACTOR(p) && installed_fighter[p] && tekken3_ttt_player_motion_mode(p))
            return installed_fighter[p]->parts;
    return 22;
}
extern void __real_func_80037CBC(CPUState *cpu);
void __wrap_func_80037CBC(CPUState *cpu) {
    /* The native accessory solver (parts 18..21) initializes its angles from
     * Jin's empty accessory slots and overwrites our matrices on the next
     * frame. Arcade accessories have their own rest rotations. Keep them
     * rigid until the arcade secondary-motion solver is ported. */
    if(cpu->pc==0 || cpu->pc==0x80037cbc) {
        uint32_t actor=cpu->gpr[4];unsigned bone=cpu->gpr[5];
        for(unsigned p=0;p<2;p++) {
            const TttFighter *f=installed_fighter[p];
            if(actor==T3_ACTOR(p) && f && bone>=f->accessory_first &&
               bone<=f->accessory_last && tekken3_ttt_player_motion_mode(p)) {
                unsigned row=psx_mod_read_byte(0x8001a05c+bone);
                accessory_pose(actor,bone,original_base[p],row);
                cpu->pc=cpu->gpr[31];return;
            }
        }
    }
    __real_func_80037CBC(cpu);
}
static void rebuild_packets(const TttFighter *f,uint32_t base,unsigned player) {
    const uint32_t actor=T3_ACTOR(player);
    uint32_t next=arena[player];
    for(unsigned i=f->accessory_first;i<=f->accessory_last;i++) {
        unsigned row=psx_mod_read_byte(0x8001a05c+i),bone=psx_mod_read_byte(0x8001a074+i);
        uint32_t part=actor+0x4f0+i*40;
        if(!psx_mod_read_word(base+24+row*56)) {
            psx_mod_write_word(part,0);continue;
        }
        unsigned parent=psx_mod_read_word(base+24+row*56+24);
        /* Arcade parents 18+ name accessory parts by arcade row (row = part
         * + 6), as in Kunimitsu's hair chain. A head mesh in part 18 moves
         * those rows along, so find the part that now holds the row. */
        if(parent>=18 && parent<24) {
            unsigned found=24;
            for(unsigned j=f->accessory_first;j<=f->accessory_last;j++)
                if(f->rows[psx_mod_read_byte(0x8001a05c+j)]==parent+6)found=j;
            parent=found;
        }
        if(parent>=24) {psx_mod_write_word(part,0);continue;}
        psx_mod_write_word(part,2);
        psx_mod_write_word(part+4,actor+0x8f4+bone*68);
        psx_mod_write_word(part+8,base+16+row*56);
        psx_mod_write_word(part+12,0);
        psx_mod_write_word(actor+0x8f4+bone*68+64,psx_mod_read_word(actor+0x4f4+parent*40));
        accessory_pose(actor,bone,base,row);
    }
    /* Two-mesh parts (8001A044) draw the following row when the fighter has
     * it; the native loader only knew Jin's rows. Arcade meshes copy shared
     * vertices from the mesh drawn just before them, so none may be skipped. */
    for(unsigned i=1;i<22;i++)if(psx_mod_read_byte(0x8001a044+i)==1 && psx_mod_read_word(actor+0x4f0+i*40)) {
        unsigned row=psx_mod_read_byte(0x8001a05c+i)+1;
        psx_mod_write_word(actor+0x4f0+i*40+12,psx_mod_read_word(base+16+row*56)?base+16+row*56:0);
    }
    for(unsigned i=1;i<24;i++) {
        uint32_t part=actor+0x4f0+i*40;
        if(!psx_mod_read_word(part)) continue;
        for(unsigned mesh=0;mesh<2;mesh++) {
            uint32_t bundle=psx_mod_read_word(part+8+mesh*4);
            if(!bundle) continue;
            for(unsigned frame=0;frame<2;frame++) {
                psx_mod_write_word(part+24+mesh*8+frame*4,next);
                next=make_packets(f,bundle,next,player);
            }
        }
        unsigned row=psx_mod_read_byte(0x8001a05c+i),bone=psx_mod_read_byte(0x8001a074+i);
        uint32_t pos=actor+0xf88+bone*32;
        psx_mod_write_word(pos,psx_mod_read_word(base+24+row*56+12));
        psx_mod_write_word(pos+4,psx_mod_read_word(base+24+row*56+16));
        psx_mod_write_word(pos+8,psx_mod_read_word(base+24+row*56+20));
    }
    psx_mod_log("%s import: P%u prepared %u packet bytes\n",f->name,player+1,next-arena[player]);
    upload_textures(f,player);
}
static void player_tick(unsigned player) {
    const TttFighter *f=tekken3_ttt_player_fighter(player);
    if (!f) return;
    /* Early fight substates still use fighter VRAM as a loading scratchpad.
     * Wait for the native actor/stage initialization before installing donor
     * packets and textures, otherwise a later MoveImage copies them on screen. */
    if(psx_mod_read_word(T3_SCREEN)!=T3_SCREEN_FIGHT || psx_mod_read_half(T3_SCREEN_PHASE)<6)return;
    uint32_t base=psx_mod_read_word(0x8009bd28u+player*4);
    if (base<0x80100000 || base>0x801f8000 ||
        psx_mod_read_word(base)!=27 || psx_mod_read_word(base+8)!=0x4b4d4433) return;
    FighterAssets *fa=&assets[tekken3_ttt_index(f)];
    unsigned outfit=player_outfit(f,player);
    uint32_t selected=fa->costumes[outfit].guest;
    int installed;
    const TttFighter *owner=installed_outfit(base,&installed);
    if (owner==f && installed==(int)outfit) {
        /* Mirror matches may share a model header, but GPU packets and VRAM
         * pages belong to the individual actor. */
        if(original_base[player]!=base) {
            original_base[player]=base;
            memcpy(original_header[player],original_header[1-player],sizeof original_header[player]);
        }
        installed_fighter[player]=f;
        uint32_t packet=psx_mod_read_word(T3_ACTOR(player)+0x4f0+40+24);
        if(packet && (packet<arena[player] || packet>=arena[player]+262144))rebuild_packets(f,base,player);
        ensure_textures(f,player);
        return;
    }
    if(owner && original_base[player]==base) {
        if(installed_fighter[1-player]==owner && original_base[1-player]==base) {
            /* Two different imports cannot share one donor header. */
            static int reported;
            if(!reported){reported=1;psx_mod_log("TTT1 import: P%u shares its donor header with another import\n",player+1);}
            return;
        }
        for(unsigned i=0;i<384;i++)psx_mod_write_word(base+i*4,original_header[player][i]);
    }
    /* Exact Jin punch-costume envelope. No other fighter or costume qualifies. */
    if (psx_mod_read_word(base+24)!=base+0x620 ||
        psx_mod_read_word(base+80)!=base+0x98c ||
        psx_mod_read_word(base+24+19*56)!=base+0x5714) return;
    if (psx_mod_read_byte(control)==2) {
        /* Relocation-only control: same live Jin asset, different storage. */
        uint32_t guest=fa->guest;
        for (unsigned i=0;i<26364;i+=4) {
            uint32_t v=psx_mod_read_word(base+i);
            if (v>=base && v<base+26364) v=v-base+guest;
            psx_mod_write_word(guest+i,v);
        }
        for (unsigned i=0;i<1536;i+=4)
            psx_mod_write_word(base+i,psx_mod_read_word(guest+i));
        psx_mod_write_word(control+4,base);
        psx_mod_write_word(control+8,psx_mod_read_word(control+8)+1);
        psx_mod_log("TTT1 import: relocation-only Jin control at %08X\n",base);
        return;
    }
    /* Fill the PS1 table's 27 rows (end marker kept) from the arcade's 30.
     * Both tables' 56-byte rows start at +16 with the vertex-list pointer
     * the renderer reads first (the code above addresses rows from +24).
     * Active-node parent indices are unchanged. */
    original_base[player]=base;
    installed_fighter[player]=f;
    for(unsigned i=0;i<384;i++) original_header[player][i]=psx_mod_read_word(base+i*4);
    for (unsigned i=0;i<27;i++)
        for (unsigned j=0;j<56;j+=4)
            psx_mod_write_word(base+16+i*56+j,psx_mod_read_word(selected+16+f->rows[i]*56+j));
    rebuild_packets(f,base,player);
    psx_mod_write_word(control+4,base);
    psx_mod_write_word(control+8,psx_mod_read_word(control+8)+1);
    psx_mod_log("%s import: P%u Jin model header replaced at %08X\n",f->name,player+1,base);
    psx_mod_log("%s outfit: P%u selected arcade P%u\n",f->name,player+1,outfit+1);
}
void tekken3_ttt_tick(void) {
    extern uint64_t s_frame_count;
    static uint64_t last_frame=~0ull;
    if (!psx_mod_game_started() || s_frame_count==last_frame) return;
    last_frame=s_frame_count;
    int any=0;
    for(unsigned f=0;f<TTT_FIGHTER_COUNT;f++) {
        if(assets[f].wanted && !assets[f].attempted) { assets[f].attempted=1; load_assets(f); }
        if(assets[f].guest)any=1;
    }
    if(assets[TTT_JUN].guest)tekken3_jun_selector_tick();
    for(unsigned f=TTT_JUN+1;f<TTT_FIGHTER_COUNT;f++)
        if(assets[f].guest)tekken3_ttt_roster_tick(f);
    /* Roster fights enable the import when either player picked one. */
    if(any && tekken3_roster_live() && psx_mod_read_word(T3_SCREEN)==T3_SCREEN_FIGHT) {
        int selected=0;
        for(unsigned player=0;player<2;player++)if(tekken3_ttt_player_fighter(player))selected=1;
        tekken3_ttt_select(selected);
    }
    if(any)tekken3_ttt_voices_tick();
    if(!any || initializing || !psx_mod_read_byte(control)) return;
    for(unsigned player=0;player<2;player++)player_tick(player);
    tekken3_ttt_combat_tick();
}
/* Each fighter's package enables its own tekken3.<dir>-probe plugin. */
static void probe_tick(void *fighter) {
    tekken3_ttt_want((unsigned)(uintptr_t)fighter);
    tekken3_ttt_tick();
}
PSX_MOD_CONSTRUCTOR(register_ttt_probes) {
    static char ids[TTT_FIGHTER_COUNT][48];
    for(unsigned f=0;f<TTT_FIGHTER_COUNT;f++) {
        snprintf(ids[f],sizeof ids[f],"tekken3.%s-probe",tekken3_ttt_fighters[f].dir);
        (void)psx_mod_register_vblank_plugin_with(ids[f],probe_tick,(void *)(uintptr_t)f);
    }
}
