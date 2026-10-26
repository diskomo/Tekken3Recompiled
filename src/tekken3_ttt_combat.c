/* Source-derived solo combat adapter for imported TTT1 fighters. See each
 * fighter's combat-report.json for coverage. Every fighter's imported move
 * IDs start at its own base, so two imports can share a match. */
#include "psx_runtime.h"
#include "mod_plugins.h"
#include "tekken3_ttt_pack.h"
#include "tekken3_ttt_fighter.h"
#include "tekken3_fight_camera.h"
#include "tekken3_guest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern unsigned tekken3_ttt_player_motion_mode(unsigned player);
extern int tekken3_jun_roster_enabled(void);
extern void tekken3_ttt_face(unsigned player,unsigned variant);
extern void __real_func_8002D178(CPUState *cpu);
extern void __real_func_80038B4C(CPUState *cpu);
extern void __real_func_800389C0(CPUState *cpu);
extern void __real_func_8002CC28(CPUState *cpu);
extern void __real_func_8002CD7C(CPUState *cpu);
extern void __real_func_80059890(CPUState *cpu);
extern void __real_func_8006A3BC(CPUState *cpu);
extern void __real_func_8002D264(CPUState *cpu);
extern void __real_func_8002E8D0(CPUState *cpu);
extern void __real_func_8002E0BC(CPUState *cpu);
extern void func_8002CA84(CPUState *cpu);
/* Native table sizes: reaction rows, pushback bytes, counter entries. */
enum { NATIVE_ROWS=633, NATIVE_PUSH=1420, NATIVE_COUNTERS=269 };
typedef struct {
    uint32_t native_base,native_common,alias_table,original_alias,header,records,source,hit_bones;
} PlayerCombat;
typedef struct {uint16_t source,native;unsigned count;uint32_t clips[128];} ContactGroup;
typedef struct {
    const TttFighter *f;
    uint32_t guest,size,count,records,neutral;
    unsigned char *data;
    int loaded;
    PlayerCombat players[2];
    uint32_t clip_addresses[1024],clip_offsets[1024];
    uint32_t patterns[1024];
    unsigned pattern_count;
    uint16_t native_aliases[4023];
    /* Imported recovery IDs -> the receiving fighter's native engine aliases.
     * Zero means no equivalent: unique paired victim clips must stay imported. */
    uint16_t recovery_aliases[1024];
    uint32_t source_aliases[5515];
    ContactGroup groups[64];
    unsigned group_count;
    /* Reaction rows; a nonzero high half also replaces the damage. */
    struct {unsigned record,mask,alternate,normal;} dynamic_hits[64];
    unsigned dynamic_count;
    /* Cancel rows valid only against one TTT1 opponent. */
    struct {unsigned record,row,source;} dynamic_cancels[64];
    unsigned dynamic_cancel_count;
    /* Parsed tables until the merged install. */
    unsigned char *tables;
    uint32_t nr,np,nc,event_offset,event_bytes;
} FighterCombat;
static FighterCombat combat[TTT_FIGHTER_COUNT];
static unsigned char native_reactions[4023];
static int attempted,loaded;
static uint32_t push_table=0x8001075c;
uint32_t tekken3_ttt_push_base(void) {return push_table;}
static uint32_t word(const unsigned char *p) {
    return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put16(unsigned char *p,unsigned v) {p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
static void put32(unsigned char *p,uint32_t v) {put16(p,v&65535);put16(p+2,v>>16);}
static int in_range(const FighterCombat *fc,unsigned id) {
    return fc->loaded && id>=fc->f->base_id && id<fc->f->base_id+fc->count;
}
static FighterCombat *combat_of(const TttFighter *f) {return f?&combat[tekken3_ttt_index(f)]:NULL;}
/* The imported fighter a player controls in combat mode, or NULL. */
static FighterCombat *player_fighter(unsigned player) {
    if(tekken3_ttt_player_motion_mode(player)!=2)return NULL;
    return combat_of(tekken3_ttt_player_fighter(player));
}
/* As above, once its combat pack is loaded. */
static FighterCombat *player_combat(unsigned player) {
    FighterCombat *fc=player_fighter(player);
    return fc && fc->loaded?fc:NULL;
}
static FighterCombat *record_owner(uint32_t record,unsigned *index) {
    for(unsigned f=0;f<TTT_FIGHTER_COUNT;f++)for(unsigned p=0;p<2;p++) {
        const FighterCombat *fc=&combat[f];uint32_t r=fc->players[p].records;
        if(fc->loaded && r && record>=r && record<r+fc->count*56 && (record-r)%56==0) {
            if(index)*index=(record-r)/56;
            return &combat[f];
        }
    }
    return NULL;
}
static int source_record(uint32_t record) {return record_owner(record,NULL)!=NULL;}
int tekken3_ttt_source_condition(uint32_t actor,unsigned kind) {
    /* Contact conditions 69..75 remain in the separate contact pass. */
    return kind>=76 && kind<=82 && source_record(psx_mod_read_word(actor+0x54));
}
int tekken3_ttt_source_transition(CPUState *cpu) {
    uint32_t actor=cpu->gpr[17];unsigned kind=cpu->gpr[3];
    if(kind<45 || kind>47 || !source_record(psx_mod_read_word(actor+0x1a4)))return 0;
    /* TTT 80100E24..80100EF4, called only after the native transition's
     * frame gate has committed. s2 retains the native root-refresh decision. */
    psx_mod_write_half(actor+0x58,1);psx_mod_write_half(actor+0x50,1);
    psx_mod_write_half(actor+0x74,0);
    psx_mod_write_word(actor+0x4c,psx_mod_read_word(actor+0x1a4));
    psx_mod_write_byte(actor+0xb9,kind==45?8:kind-34);
    if(kind==45)psx_mod_write_half(actor+0x2c,psx_mod_read_half(actor+0x2a));
    psx_mod_write_half(actor+0xe,psx_mod_read_half(actor+(kind==45?0x2a:0x34)));
    if(kind==45 || cpu->gpr[18]) {
        psx_mod_write_word(actor,psx_mod_read_word(actor+0xf68));
        psx_mod_write_word(actor+8,psx_mod_read_word(actor+0xf70));
    }
    return 1;
}
int tekken3_ttt_source_rotation(uint32_t actor) {
    unsigned mode=psx_mod_read_byte(actor+0xb9);
    if((mode!=12 && mode!=13) || !source_record(psx_mod_read_word(actor+0x54)))return 0;
    /* The PS1 camera uses 65536 angle units; TTT uses 4096. Match the
     * source camera-plane axis, closest half-turn, and eight-frame blend. */
    int yaw=tekken3_fight_camera_yaw()%65536;
    int axis=(0x4000-yaw)%65535;
    if(psx_mod_read_half(actor+0x12))axis+=0x8000;
    if(mode==13)axis=(int16_t)axis-0x4000;
    int facing=psx_mod_read_half(actor+0x2c);
    int delta=(int16_t)(facing-axis);
    unsigned distance=(uint16_t)(delta<0?65535-delta:delta);
    if(distance>=0x4000)axis+=0x8000;
    int frame=(int16_t)psx_mod_read_half(actor+0x5c);if(frame>8)frame=8;
    int step=(int16_t)(((int16_t)(axis-facing)*frame)/8);
    if(step>0x71c)step=0x71c;if(step< -0x71c)step= -0x71c;
    psx_mod_write_half(actor+0x2c,(uint16_t)(facing+step));
    psx_mod_write_half(actor+0xe,(uint16_t)(facing+step));
    return 1;
}
void __wrap_func_8002E0BC(CPUState *cpu) {
    if(cpu->pc==0 || cpu->pc==0x8002e0bc) {
        unsigned kind=psx_mod_read_byte(cpu->gpr[4]+3),actor=cpu->gpr[5],match=0;
        if(kind>=76 && kind<=82 && source_record(psx_mod_read_word(actor+0x54))) {
            int relative=(int16_t)psx_mod_read_half(actor+0x2e);
            if(kind==76)match=(int16_t)psx_mod_read_half(actor+0x30)<=0x4000;
            else if(kind==77)match=relative<0;
            else if(kind==78)match=relative>=0;
            else {
                int axis=0x4000-tekken3_fight_camera_yaw()%65536;
                unsigned quarter=(uint16_t)(psx_mod_read_half(actor+0x2c)-axis+0x2000)/0x4000;
                static const unsigned wanted[4]={1,2,0,3};
                match=quarter==wanted[kind-79];
            }
            cpu->gpr[2]=match;cpu->pc=cpu->gpr[31];return;
        }
    }
    __real_func_8002E0BC(cpu);
}
static void patch_base(uint32_t high_at,uint32_t low_at,unsigned reg,uint32_t target) {
    psx_mod_write_code_word(high_at,0x3c000000u|(reg<<16)|((target+0x8000)>>16));
    psx_mod_write_code_word(low_at,0x24000000u|(reg<<21)|(reg<<16)|(target&65535));
}
static int load_tables(FighterCombat *fc,const char *root) {
    char path[4096];snprintf(path,sizeof path,"%s/%s-TTT1-tables.jst",root,fc->f->name);
    FILE *f=fopen(path,"rb");if(!f)return 0;
    unsigned char h[32];if(fread(h,1,32,f)!=32){fclose(f);return 0;}
    uint32_t bytes=word(h+8),nr=word(h+12),np=word(h+16),nc=word(h+20);
    uint32_t fixed=nr*42+np+nc*4;
    if(word(h)!=0x3154534a || word(h+4)!=2 || nr<NATIVE_ROWS || nr>4096 ||
       np<NATIVE_PUSH || np>65534 || (np&1) || nc<NATIVE_COUNTERS || nc>4096 ||
       bytes<32+fixed || bytes>1024*1024 || word(h+24) || word(h+28)){fclose(f);return 0;}
    unsigned char *p=malloc(bytes-32);if(!p){fclose(f);return 0;}
    int ok=fread(p,1,bytes-32,f)==bytes-32 && fgetc(f)==EOF;fclose(f);
    if(!ok){free(p);return 0;}
    unsigned chunks=0;
    for(unsigned at=fixed;ok && at<bytes-32;) {
        if(bytes-32-at<8){ok=0;break;}
        unsigned type=word(p+at),n=word(p+at+4);at+=8;
        if(n>bytes-32-at){ok=0;break;}
        if(type==0x41494c41 && !(chunks&1) && n%4==0) {
            chunks|=1;
            for(unsigned i=0;i<n;i+=4) {
                unsigned a=ttt_pack_half(p+at+i),b=ttt_pack_half(p+at+i+2);
                if(a>=4023 || fc->native_aliases[a] || !ttt_pack_target(b,fc->f->base_id,fc->count)){ok=0;break;}
                fc->native_aliases[a]=b;
            }
        } else if(type==0x41435253 && !(chunks&2) && n==sizeof fc->source_aliases) {
            chunks|=2;for(unsigned i=0;i<5515;i++)fc->source_aliases[i]=word(p+at+i*4);
        } else if(type==0x50555247 && !(chunks&4)) {
            chunks|=4;
            for(unsigned i=0;i<n;) {
                if(n-i<8 || fc->group_count>=64){ok=0;break;}
                ContactGroup *g=&fc->groups[fc->group_count++];
                g->source=ttt_pack_half(p+at+i);g->native=ttt_pack_half(p+at+i+2);
                g->count=word(p+at+i+4);i+=8;
                if(g->count>128 || g->count*4>n-i || g->native>=52){ok=0;break;}
                for(unsigned k=0;k<g->count;k++)g->clips[k]=word(p+at+i+k*4);
                i+=g->count*4;
            }
        } else if(type==0x544e5645 && !(chunks&8)) {
            chunks|=8;fc->event_offset=at;fc->event_bytes=n;
            for(unsigned i=0;i<n;) {
                if(n-i<8){ok=0;break;}
                unsigned id=word(p+at+i),ne=word(p+at+i+4);i+=8;
                if(id<1024 || id>=4096 || !ne || ne>256 || ne*4>n-i || word(p+at+i+(ne-1)*4)){ok=0;break;}
                unsigned prev=0;
                for(unsigned k=0;k+1<ne;k++) {
                    unsigned v=word(p+at+i+k*4),frame=v&4095;
                    if(!frame || frame<prev || ((v>>12)&15)>3)ok=0;prev=frame;
                }
                i+=ne*4;
            }
        } else if(type==0x484e5944 && !(chunks&16) && n%16==0 && n/16<=64) {
            chunks|=16;fc->dynamic_count=n/16;
            for(unsigned i=0;i<fc->dynamic_count;i++) {
                fc->dynamic_hits[i].record=word(p+at+i*16);
                fc->dynamic_hits[i].mask=word(p+at+i*16+4);
                fc->dynamic_hits[i].alternate=word(p+at+i*16+8);
                fc->dynamic_hits[i].normal=word(p+at+i*16+12);
                if(fc->dynamic_hits[i].record>=fc->count || (fc->dynamic_hits[i].alternate&65535)>=nr ||
                   (fc->dynamic_hits[i].normal&65535)>=nr ||
                   !(fc->dynamic_hits[i].alternate>>16)!=!(fc->dynamic_hits[i].normal>>16))ok=0;
            }
        } else if(type==0x434e5944 && !(chunks&32) && n%8==0 && n/8<=64) {
            chunks|=32;fc->dynamic_cancel_count=n/8;
            for(unsigned i=0;i<fc->dynamic_cancel_count;i++) {
                unsigned record=ttt_pack_half(p+at+i*8),row=ttt_pack_half(p+at+i*8+2),source=word(p+at+i*8+4);
                fc->dynamic_cancels[i].record=record;fc->dynamic_cancels[i].row=row;
                fc->dynamic_cancels[i].source=source;
                if(record>=fc->count || row>=word(fc->data+32+record*16+12) || source>=33)ok=0;
            }
        } else ok=0;
        at+=n;
    }
    if((chunks&31)!=31)ok=0;
    for(unsigned i=NATIVE_ROWS;ok && i<nr;i++) {
        for(unsigned k=0;k<14;k++)if(!ttt_pack_target(ttt_pack_half(p+i*42+k*2),fc->f->base_id,fc->count))ok=0;
        for(unsigned k=15;k<21;k+=2)if(ttt_pack_half(p+i*42+k*2)*2+20>np)ok=0;
        if(ttt_pack_half(p+i*42+40)*2+20>np)ok=0;
    }
    if(!ok){free(p);return 0;}
    fc->tables=p;fc->nr=nr;fc->np=np;fc->nc=nc;
    return 1;
}
/* Rebase a fighter's appended reaction rows, pushback curves and counter
 * entries to follow those of the fighters loaded before it. */
static void rebase(FighterCombat *fc,unsigned rows,unsigned push,unsigned counters) {
    unsigned char *p=fc->tables;
    for(unsigned i=NATIVE_ROWS;i<fc->nr;i++) {
        static const unsigned fields[4]={15,17,19,20};
        for(unsigned k=0;k<4;k++) {
            unsigned v=ttt_pack_half(p+i*42+fields[k]*2);
            if(v)put16(p+i*42+fields[k]*2,v+push/2);
        }
    }
    unsigned char *counter=p+fc->nr*42+fc->np;
    for(unsigned i=NATIVE_COUNTERS;i<fc->nc;i++)
        put16(counter+i*4,ttt_pack_half(counter+i*4)+rows);
    for(unsigned i=0;i<fc->count;i++) {
        unsigned char *r=fc->data+fc->records+i*56;
        put32(r+48,(word(r+48)&65535)|((word(r+48)>>16)+rows)<<16);
        put32(r+52,word(r+52)+counters);
    }
    for(unsigned i=0;i<fc->dynamic_count;i++) {
        fc->dynamic_hits[i].alternate+=rows;fc->dynamic_hits[i].normal+=rows;
    }
}
static int install_tables(void) {
    FighterCombat *first=NULL;
    uint32_t nr=NATIVE_ROWS,np=NATIVE_PUSH,nc=NATIVE_COUNTERS,event_bytes=0;
    for(unsigned f=0;f<TTT_FIGHTER_COUNT;f++) {
        FighterCombat *fc=&combat[f];if(!fc->loaded)continue;
        if(!first)first=fc;
        else if(memcmp(fc->tables,first->tables,NATIVE_ROWS*42) ||
                memcmp(fc->tables+fc->nr*42,first->tables+first->nr*42,NATIVE_PUSH) ||
                memcmp(fc->tables+fc->nr*42+fc->np,first->tables+first->nr*42+first->np,NATIVE_COUNTERS*4)) {
            psx_mod_log("%s combat: native tables differ from %s's\n",fc->f->name,first->f->name);
            fc->loaded=0;continue;
        }
        rebase(fc,nr-NATIVE_ROWS,np-NATIVE_PUSH,nc-NATIVE_COUNTERS);
        nr+=fc->nr-NATIVE_ROWS;np+=fc->np-NATIVE_PUSH;nc+=fc->nc-NATIVE_COUNTERS;
        event_bytes+=fc->event_bytes;
    }
    if(!first)return 0;
    uint32_t fixed=nr*42+np+nc*4;
    unsigned char *merged=malloc(fixed);
    uint32_t base=merged?psx_mod_alloc_guest_memory(fixed,16):0;
    if(!base){free(merged);return 0;}
    unsigned char *rows=merged,*push=merged+nr*42,*counter=push+np;
    memcpy(rows,first->tables,NATIVE_ROWS*42);rows+=NATIVE_ROWS*42;
    memcpy(push,first->tables+first->nr*42,NATIVE_PUSH);push+=NATIVE_PUSH;
    memcpy(counter,first->tables+first->nr*42+first->np,NATIVE_COUNTERS*4);counter+=NATIVE_COUNTERS*4;
    for(unsigned f=0;f<TTT_FIGHTER_COUNT;f++) {
        FighterCombat *fc=&combat[f];if(!fc->loaded)continue;
        unsigned char *t=fc->tables;
        memcpy(rows,t+NATIVE_ROWS*42,(fc->nr-NATIVE_ROWS)*42);rows+=(fc->nr-NATIVE_ROWS)*42;
        memcpy(push,t+fc->nr*42+NATIVE_PUSH,fc->np-NATIVE_PUSH);push+=fc->np-NATIVE_PUSH;
        memcpy(counter,t+fc->nr*42+fc->np+NATIVE_COUNTERS*4,(fc->nc-NATIVE_COUNTERS)*4);
        counter+=(fc->nc-NATIVE_COUNTERS)*4;
    }
    /* Reaction destinations are semantic engine entries, even when their
     * records are stored in a character's bank. Removing all Jin-bank
     * records also removed these hit states and silently substituted idle.
     * Preserve the original 633 rows and the separate paired-reaction list. */
    for(unsigned i=0;i<NATIVE_ROWS;i++)for(unsigned k=0;k<14;k++) {
        unsigned alias=ttt_pack_half(merged+i*42+k*2);
        if(alias<4023)native_reactions[alias]=1;
    }
    for(uint32_t at=0x800174c4;at<0x800177dc;at+=2) {
        unsigned alias=psx_mod_read_half(at);
        if(alias<4023)native_reactions[alias]=1;
    }
    for(uint32_t i=0;i<fixed;i++)psx_mod_write_byte(base+i,merged[i]);
    free(merged);
    uint32_t events=psx_mod_alloc_guest_memory(4096*4+4+event_bytes,16);
    if(!events)return 0;
    uint32_t cursor=events+4096*4;
    psx_mod_write_word(cursor,0);
    for(unsigned i=0;i<4096;i++)psx_mod_write_word(events+i*4,
        i<470?psx_mod_read_word(0x800971f8+i*4):cursor);
    cursor+=4;
    /* Event IDs are TTT1's own script numbers, so fighters share them. */
    static unsigned char defined[4096];
    for(unsigned f=0;f<TTT_FIGHTER_COUNT;f++) {
        FighterCombat *fc=&combat[f];if(!fc->loaded)continue;
        const unsigned char *p=fc->tables+fc->event_offset;
        for(unsigned i=0;i<fc->event_bytes;) {
            unsigned id=word(p+i),ne=word(p+i+4);i+=8;
            if(!defined[id]) {
                defined[id]=1;psx_mod_write_word(events+id*4,cursor);
                for(unsigned k=0;k<ne;k++)psx_mod_write_word(cursor+k*4,word(p+i+k*4));
                cursor+=ne*4;
            }
            i+=ne*4;
        }
    }
    for(unsigned f=0;f<TTT_FIGHTER_COUNT;f++){free(combat[f].tables);combat[f].tables=NULL;}
    patch_base(0x80041400,0x80041404,2,events);
    uint32_t pushed=base+nr*42,counters=pushed+np;
    patch_base(0x8002d7ac,0x8002d7b4,2,base);
    patch_base(0x80044590,0x80044598,2,base);
    patch_base(0x80044608,0x80044610,3,base);
    patch_base(0x80044664,0x8004466c,2,base);
    patch_base(0x800445c4,0x800445cc,2,counters);
    patch_base(0x80044ac0,0x80044ac4,3,counters);
    push_table=pushed;
    psx_mod_log("TTT1 combat: installed %u source reaction rows and original pushback data\n",nr-NATIVE_ROWS);
    return 1;
}
static int load_combat(FighterCombat *fc) {
    const char *root=tekken3_ttt_asset_root(fc->f);if(!root)return 0;
    char path[4096];snprintf(path,sizeof path,"%s/%s-TTT1-combat.jmv",root,fc->f->name);
    FILE *f=fopen(path,"rb");if(!f)return 0;
    unsigned char header[32];
    if(fread(header,1,32,f)!=32 || word(header)!=0x314d554a || word(header+4)!=4){fclose(f);return 0;}
    uint32_t size=word(header+8),count=word(header+12),neutral=word(header+16),records=word(header+28);
    uint32_t ro=word(header+20),rn=word(header+24);
    if(size>16*1024*1024 || count>1024 || !count || neutral>=count || records!=32+count*16 || records+count*56>size || ro>size || rn>(size-ro)/4){fclose(f);return 0;}
    unsigned char *data=(unsigned char*)malloc(size);if(!data){fclose(f);return 0;}
    memcpy(data,header,32);
    int ok=fread(data+32,1,size-32,f)==size-32 && fgetc(f)==EOF;fclose(f);
    for(unsigned i=0;ok && i<rn;i++) {
        uint32_t r=word(data+ro+i*4);
        if(r>size-4 || (r&3) || word(data+r)>=size)ok=0;
    }
    if(!ok || !ttt_pack_validate(data,size,fc->f->base_id)){
        psx_mod_log("%s combat: rejected invalid combat pack\n",fc->f->name);
        free(data);return 0;
    }
    fc->data=data;fc->size=size;fc->count=count;fc->neutral=neutral;fc->records=records;
    if(!load_tables(fc,root)){free(data);fc->data=NULL;return 0;}
    return 1;
}
/* Guest copies are made after table rebasing has adjusted the records. */
static int install_combat(FighterCombat *fc) {
    const unsigned char *data=fc->data;
    uint32_t size=fc->size,count=fc->count,records=fc->records;
    uint32_t ro=word(data+20),rn=word(data+24);
    /* The guest arena is 1 MiB. Keep decoded motion in host memory; guest
     * records point at compact clip headers consumed by the decoder bridge. */
    uint32_t compact=records+count*56;
    for(unsigned i=0;i<count;i++) {
        uint32_t n=word(data+32+i*16+12);
        if(n>4096)return 0;
        uint32_t rp=records+i*56;
        compact+=8+n*12+8;
        unsigned props=word(data+rp+32),j=0;
        do {compact+=4;} while(ttt_pack_half(data+props+j++*4));
    }
    uint32_t guest=psx_mod_alloc_guest_memory(compact,16);
    if(!guest){psx_mod_log("%s combat: cannot allocate %u guest bytes\n",fc->f->name,compact);return 0;}
    uint32_t cursor=records+count*56;
    for(unsigned i=0;i<cursor;i++)psx_mod_write_byte(guest+i,data[i]);
    for(unsigned i=0;i<count;i++) {
        uint32_t rp=records+i*56,cp=word(data+rp+12),n=word(data+32+i*16+12)*12;
        uint32_t clip=word(data+rp);
        if(cp>size || n>size-cp || clip>size-8)return 0;
        fc->clip_addresses[i]=guest+cursor;fc->clip_offsets[i]=clip;
        for(unsigned j=0;j<8;j++)psx_mod_write_byte(guest+cursor+j,data[clip+j]);
        psx_mod_write_word(guest+rp,guest+cursor);cursor+=8;
        psx_mod_write_word(guest+rp+12,guest+cursor);
        for(unsigned j=0;j<n;j++)psx_mod_write_byte(guest+cursor+j,data[cp+j]);
        cursor+=n;
        uint32_t sounds=word(data+rp+28),props=word(data+rp+32);
        psx_mod_write_word(guest+rp+28,guest+cursor);
        for(unsigned j=0;j<8;j++)psx_mod_write_byte(guest+cursor+j,j<6?data[sounds+j]:255);
        cursor+=8;
        psx_mod_write_word(guest+rp+32,guest+cursor);
        unsigned j=0;
        do {for(unsigned k=0;k<4;k++)psx_mod_write_byte(guest+cursor+k,data[props+j*4+k]);cursor+=4;}
        while(ttt_pack_half(data+props+j++*4));
    }
    uint32_t po=ro+rn*4;
    if(po>size-4)return 0;
    fc->pattern_count=word(data+po);po+=4;if(fc->pattern_count>1024)return 0;
    for(unsigned i=0;i<fc->pattern_count;i++) {
        if(po>size-4)return 0;
        unsigned command=data[po]|(unsigned)data[po+1]<<8,n=data[po+2]|(unsigned)data[po+3]<<8;po+=4;
        if(command!=0xe000+i || n<3 || n>65 || n*2>size-po)return 0;
        fc->patterns[i]=psx_mod_alloc_guest_memory(n*2,4);if(!fc->patterns[i])return 0;
        for(unsigned j=0;j<n*2;j++)psx_mod_write_byte(fc->patterns[i]+j,data[po+j]);po+=n*2;
    }
    for(unsigned i=0;i<4023;i++)if(fc->native_aliases[i]>=fc->f->base_id) {
        unsigned index=fc->native_aliases[i]-fc->f->base_id;
        /* Store alias + 1 so native alias zero is not the missing marker.
         * ALIA is ordered; retain the first equivalent engine entry. */
        if(!fc->recovery_aliases[index])fc->recovery_aliases[index]=(uint16_t)(i+1);
    }
    fc->guest=guest;
    psx_mod_log("%s combat: loaded %u source records and %u input sequences at %08X\n",
            fc->f->name,count,fc->pattern_count,guest);
    return 1;
}
/* Load every wanted fighter once, so the merged tables cover them all. */
static int load_all(void) {
    if(attempted)return loaded;attempted=1;
    for(unsigned f=0;f<TTT_FIGHTER_COUNT;f++) {
        FighterCombat *fc=&combat[f];fc->f=&tekken3_ttt_fighters[f];
        if(tekken3_ttt_wanted(f) && tekken3_ttt_by_roster(fc->f->roster_id) && load_combat(fc))fc->loaded=1;
    }
    if(!install_tables())return 0;
    for(unsigned f=0;f<TTT_FIGHTER_COUNT;f++)
        if(combat[f].loaded && !install_combat(&combat[f]))combat[f].loaded=0;
    loaded=1;
    return 1;
}
static void keep_native_reactions(const FighterCombat *fc,uint32_t aliases,uint32_t missing,unsigned char keep[4023]) {
    uint16_t pending[4023];unsigned head=0,tail=0;
    memcpy(keep,native_reactions,4023);
    for(unsigned i=0;i<4023;i++)if(keep[i])pending[tail++]=(uint16_t)i;
    while(head<tail) {
        unsigned alias=pending[head++];
        /* The fighter's translated common locomotion already handles this
         * entry. Do not follow the donor's neutral/crouch graph into Jin attacks. */
        if(fc->native_aliases[alias])continue;
        uint32_t record=psx_mod_read_word(aliases+alias*4);
        if(record==missing || record<0x80010000 || record>0x801fffc8)continue;
        unsigned next=psx_mod_read_half(record+16);
        if(next<4023 && !keep[next]){keep[next]=1;pending[tail++]=(uint16_t)next;}
        /* Terminal cancel destinations can differ from record + 16. Keep
         * their native recovery chains too (e.g. A46 -> A47), but never
         * promote ordinary input-command links to donor attacks. */
        uint32_t cancel=psx_mod_read_word(record+12);
        for(unsigned i=0;i<1024 && cancel>=0x80010000 && cancel<=0x801ffff4;i++,cancel+=12) {
            unsigned command=psx_mod_read_half(cancel);
            if(command==0xc00d)break; /* End of a shared cancel subroutine. */
            if(command!=0xc000)continue;
            next=psx_mod_read_half(cancel+6);
            if(next<4023 && !keep[next]){keep[next]=1;pending[tail++]=(uint16_t)next;}
            break;
        }
    }
}
static void restore_legacy_jump_velocity(const FighterCombat *fc,PlayerCombat *p,uint32_t source) {
    /* Older v4 exports wrote only damage into word +14, discarding its
     * signed movement halfword at +16. Native 8003F3E8 reads that velocity
     * during the jump's +19..+1A frame window. Direction/animation alone do
     * not move the fighter. Repair only semantic jump entries in old packs;
     * corrected exports retain their original arcade velocities unchanged.
     * Use the loaded native jump profile for compatibility, not an invented
     * speed or a replacement animation. Native alias 82 may be absent, so
     * its forward variant falls back to 81 (backward variants to 83). */
    for(unsigned alias=0x7f;alias<=0x88;alias++) {
        unsigned imported=fc->native_aliases[alias];
        if(!in_range(fc,imported))continue;
        uint32_t record=p->records+(imported-fc->f->base_id)*56;
        unsigned direction=(psx_mod_read_word(record+4)>>16)&3;
        if((direction!=1 && direction!=2) || psx_mod_read_half(record+22) ||
           !psx_mod_read_byte(record+25))continue;
        uint32_t native=psx_mod_read_word(p->original_alias+alias*4);
        int valid=native>=0x80010000 && native<=0x801fffc8 && native!=source-56;
        if(!valid || !psx_mod_read_half(native+22))
            native=psx_mod_read_word(p->original_alias+(direction==1?0x81:0x83)*4);
        if(native>=0x80010000 && native<=0x801fffc8 && native!=source-56)
            psx_mod_write_half(record+22,psx_mod_read_half(native+22));
    }
}
/* SLUS-00402 move key -> TTT1 select ID, for opponent-specific rules. Stock
 * keys 15 (Mokujin) and 17 have no arcade counterpart; 18 is Anna. Console-
 * only fighters have none. Panda/Tiger use their shared stock keys, just as
 * the native hit resolver does. */
static unsigned source_key(unsigned key) {
    const TttFighter *f=tekken3_ttt_by_roster(key);
    if(f)return f->source_key;
    return key<=14 || key==16?key:key==18?17:32;
}
static int ready(unsigned player) {
    FighterCombat *fc=player_fighter(player);
    if(!fc || !load_all() || !fc->loaded)return 0;
    PlayerCombat *p=&fc->players[player];
    const unsigned base_id=fc->f->base_id,count=fc->count,neutral=fc->neutral;
    uint32_t slot=0x800adc20+player*4,base=psx_mod_read_word(slot);
    if(base==p->header)base=p->native_base;
    if(base<0x80010000 || base>0x801f0000 || (psx_mod_read_word(base)&0xffff)!=0x0901)return 0;
    uint32_t common=psx_mod_read_word(0x800adc28);
    if(base!=p->native_base || psx_mod_read_word(slot)!=p->header || common!=p->native_common ||
       psx_mod_read_word(base+8)!=p->source || psx_mod_read_word(base+12)!=p->original_alias) {
        uint32_t source=psx_mod_read_word(base+8);
        p->original_alias=psx_mod_read_word(base+12);
        if(!p->alias_table)p->alias_table=psx_mod_alloc_guest_memory((base_id+count)*4,16);
        if(!p->header)p->header=psx_mod_alloc_guest_memory(64,16);
        if(!p->records)p->records=psx_mod_alloc_guest_memory(count*56,16);
        if(!p->hit_bones)p->hit_bones=psx_mod_alloc_guest_memory(count*8,16);
        if(!p->alias_table || !p->header || !p->records || !p->hit_bones)return 0;
        for(unsigned i=0;i<64;i+=4)psx_mod_write_word(p->header+i,psx_mod_read_word(base+i));
        uint32_t idle=p->records+neutral*56;
        unsigned native_count=psx_mod_read_word(base)>>16;
        unsigned char keep[4023];
        keep_native_reactions(fc,p->original_alias,source-56,keep);
        for(unsigned i=0;i<base_id+count;i++)psx_mod_write_word(p->alias_table+i*4,idle);
        for(unsigned i=0;i<4023;i++) {
            uint32_t original=psx_mod_read_word(p->original_alias+i*4);
            /* Keep actual hit/victim states, including character-owned
             * records and terminal continuations. Only donor attacks are
             * excluded; shared engine records remain available as before. */
            if(original>=source && original<source+native_count*56 && !keep[i])continue;
            if(original==source-56)continue; /* native missing-alias record */
            psx_mod_write_word(p->alias_table+i*4,original);
        }
        for(unsigned i=0;i<count;i++) {
            uint32_t dst=p->records+i*56;
            for(unsigned j=0;j<56;j+=4)psx_mod_write_word(dst+j,psx_mod_read_word(fc->guest+fc->records+i*56+j));
            psx_mod_write_word(p->hit_bones+i*8,word(fc->data+fc->records+i*56+40));
            psx_mod_write_word(p->hit_bones+i*8+4,0);
            psx_mod_write_word(dst+40,p->hit_bones+i*8);
            psx_mod_write_word(p->alias_table+(base_id+i)*4,dst);
        }
        restore_legacy_jump_velocity(fc,p,source);
        psx_mod_write_word(p->alias_table+3*4,p->records+neutral*56);
        for(unsigned i=0;i<4023;i++)if(fc->native_aliases[i]) {
            unsigned index=fc->native_aliases[i]==3?neutral:fc->native_aliases[i]-base_id;
            psx_mod_write_word(p->alias_table+i*4,p->records+index*56);
        }
        psx_mod_write_word(p->header+12,p->alias_table);
        if(tekken3_ttt_index(fc->f)!=TTT_JUN || tekken3_jun_roster_enabled())
            psx_mod_write_byte(p->header+1,fc->f->roster_id);
        /* Native cache headers may be shared by two actors. Keep them intact. */
        psx_mod_write_word(slot,p->header);
        p->native_base=base;p->native_common=common;p->source=source;
        psx_mod_log("%s combat: installed P%u alias table, original=%08X custom=%08X records=%08X\n",
                fc->f->name,player+1,p->original_alias,p->alias_table,p->records);
    }
    unsigned opponent=source_key(psx_mod_read_half(T3_ACTOR(1-player)+0x16));
    for(unsigned i=0;i<fc->dynamic_count;i++) {
        uint32_t address=p->records+fc->dynamic_hits[i].record*56+48;
        unsigned rx=opponent<32 && (fc->dynamic_hits[i].mask&(1u<<opponent))?
                    fc->dynamic_hits[i].alternate:fc->dynamic_hits[i].normal;
        psx_mod_write_word(address,(psx_mod_read_word(address)&65535)|((rx&65535)<<16));
        if(rx>>16)psx_mod_write_half(address-28,(uint16_t)(rx>>16));
    }
    /* Native character byte: 0 always holds; 0x5D+ID holds unless the actor
     * is that roster ID, so it never holds for this fighter. */
    for(unsigned i=0;i<fc->dynamic_cancel_count;i++) {
        uint32_t list=psx_mod_read_word(p->records+fc->dynamic_cancels[i].record*56+12);
        psx_mod_write_byte(list+fc->dynamic_cancels[i].row*12+2,
            opponent==fc->dynamic_cancels[i].source?0:0x5d+fc->f->roster_id);
    }
    return 1;
}
void __wrap_func_8002D264(CPUState *cpu) {
    unsigned param=(uint16_t)cpu->gpr[5];
    if((cpu->pc==0 || cpu->pc==0x8002d264) && loaded && (param&0x4000)) {
        /* Group lists are TTT1's own, identical in every fighter's tables. */
        const ContactGroup *g=NULL;
        for(unsigned f=0;f<TTT_FIGHTER_COUNT && !g;f++)if(combat[f].loaded)
            for(unsigned i=0;i<combat[f].group_count;i++)
                if(combat[f].groups[i].source==(param&0x3fff))g=&combat[f].groups[i];
        if(g) {
            for(unsigned f=0;f<TTT_FIGHTER_COUNT;f++)if(combat[f].loaded)
                for(unsigned i=0;i<combat[f].count;i++)if(combat[f].clip_addresses[i]==cpu->gpr[4]) {
                    unsigned clip=word(combat[f].data+32+i*16),match=0;
                    for(unsigned k=0;k<g->count;k++)if(g->clips[k]==clip)match=1;
                    cpu->gpr[2]=match;cpu->pc=cpu->gpr[31];return;
                }
            cpu->gpr[5]=g->native;
        } else {cpu->gpr[2]=0;cpu->pc=cpu->gpr[31];return;}
    }
    __real_func_8002D264(cpu);
}
void __wrap_func_8002E8D0(CPUState *cpu) {
    if((cpu->pc==0 || cpu->pc==0x8002e8d0) && loaded &&
       psx_mod_read_byte(cpu->gpr[4]+3)==74 && (psx_mod_read_half(cpu->gpr[4]+4)&0x8000)) {
        unsigned alias=psx_mod_read_half(cpu->gpr[4]+4)&0x7fff,match=0,i;
        uint32_t defender=cpu->gpr[5],attacker=cpu->gpr[6],record=psx_mod_read_word(attacker+0x54);
        const FighterCombat *fc=record_owner(record,&i);
        if(fc)match=alias<5515 && fc->source_aliases[alias]==word(fc->data+36+i*16);
        unsigned native=alias==0x125?0xf7:alias==0x45a?0x351:alias==0x47f?0x36f:alias==0xb56?0x85d:0xffff;
        if(native!=0xffff && psx_mod_read_half(attacker+0xa0)==native)match=1;
        match=match && (int16_t)psx_mod_read_half(defender+0x30)<0x2aaa &&
                       (int16_t)psx_mod_read_half(attacker+0x30)<0x2aaa;
        cpu->gpr[2]=match;cpu->pc=cpu->gpr[31];return;
    }
    __real_func_8002E8D0(cpu);
}
void __wrap_func_8006A3BC(CPUState *cpu) {
    if(cpu->pc==0 || cpu->pc==0x8006a3bc) {
        uint32_t actor=cpu->gpr[4];
        for(unsigned p=0;p<2;p++) if(actor==T3_ACTOR(p)) {
            const FighterCombat *fc=player_combat(p);
            if(!fc || !fc->players[p].records)break;
            uint32_t record=psx_mod_read_word(actor+0x54),base=fc->players[p].records;
            if(record<base || record>=base+fc->count*56 || (record-base)%56)break;
            uint32_t bones=psx_mod_read_word(record+40);
            /* Arcade 8010AEFC..8010B178 uses two pairs of limb IDs. A
             * zero second ID sweeps the first limb from its previous
             * position. Otherwise the capsule joins two current joints.
             * Core limb numbering 0..17 is unchanged in the PS1 skeleton. */
            for(unsigned k=0;k<2;k++) {
                unsigned end=psx_mod_read_byte(bones+k*2),start=psx_mod_read_byte(bones+k*2+1);
                if(!end)continue;
                uint32_t capsule=actor+0x1ac+k*24;
                uint32_t end_pos=actor+0x908+end*68;
                uint32_t start_pos=start?actor+0x908+start*68:capsule+12;
                for(unsigned axis=0;axis<3;axis++) {
                    psx_mod_write_word(capsule+axis*4,psx_mod_read_word(start_pos+axis*4));
                    psx_mod_write_word(capsule+12+axis*4,psx_mod_read_word(end_pos+axis*4));
                }
            }
            cpu->pc=cpu->gpr[31];return;
        }
    }
    __real_func_8006A3BC(cpu);
}
void __wrap_func_8002CC28(CPUState *cpu) {
    unsigned player=cpu->gpr[4]==T3_ACTOR_P2?1:0;
    const FighterCombat *fc=player_combat(player);
    if((cpu->pc==0 || cpu->pc==0x8002cc28) && cpu->gpr[4]==T3_ACTOR(player) &&
       fc && fc->players[player].native_base) {
        unsigned command=psx_mod_read_half(cpu->gpr[5]);
        if(command>=0xe000 && command<0xe000+fc->pattern_count) {
            cpu->gpr[5]=fc->patterns[command-0xe000];cpu->pc=0;
            func_8002CA84(cpu);return;
        }
    }
    __real_func_8002CC28(cpu);
}
void __wrap_func_80059890(CPUState *cpu) {
    /* CPU initialization caches the alias table separately from the actor's
     * move header. If it ran before the import was installed, it still holds
     * Jin's short donor table: imported move IDs then read outside it.
     * Refresh both native caches immediately before CPU decision-making,
     * including after a round/team transition. Do not reset AI difficulty. */
    if(cpu->pc==0 || cpu->pc==0x80059890) {
        uint32_t actor=cpu->gpr[4];
        for(unsigned p=0;p<2;p++)if(actor==T3_ACTOR(p) &&
                tekken3_ttt_by_roster(psx_mod_read_half(actor+0x18)) && ready(p)) {
            unsigned index=psx_mod_read_byte(actor+0x1886);
            const FighterCombat *fc=player_combat(p);
            if(index<2 && fc) {
                psx_mod_write_word(0x8009f2e8+index*4,fc->players[p].alias_table);
                psx_mod_write_word(0x8009f318+index*0x330+12,fc->players[p].alias_table);
            }
        }
    }
    __real_func_80059890(cpu);
}
void __wrap_func_8002CD7C(CPUState *cpu) {
    /* Native CPU command synthesis (800618C0) asks this lookup for a
     * sequence. The human matcher above already understands E000+ IDs,
     * but the original lookup returns NULL for E000+ imported commands.
     * The current pack uses native-format inputs; support the converter's
     * extended format too without changing stock command handling.
     * Resolve only an installed imported actor; other fighters keep their
     * own commands, including when fighting against one. */
    unsigned command=cpu->gpr[4]&65535;
    if((cpu->pc==0 || cpu->pc==0x8002cd7c) && loaded && command>=0xe000) {
        uint32_t actor=psx_mod_read_word(0x800afa5c);
        for(unsigned p=0;p<2;p++)if(actor==T3_ACTOR(p) &&
                tekken3_ttt_by_roster(psx_mod_read_half(actor+0x18))) {
            const FighterCombat *fc=player_combat(p);
            if(fc && fc->players[p].native_base && command<0xe000+fc->pattern_count) {
                cpu->gpr[2]=fc->patterns[command-0xe000];cpu->pc=cpu->gpr[31];return;
            }
        }
    }
    __real_func_8002CD7C(cpu);
}
void tekken3_ttt_combat_tick(void) {
    for(unsigned player=0;player<2;player++) {
        FighterCombat *active=player_fighter(player);
        for(unsigned f=0;f<TTT_FIGHTER_COUNT;f++) {
            FighterCombat *fc=&combat[f];
            if(fc==active)continue;
            PlayerCombat *p=&fc->players[player];
            if(p->native_base && psx_mod_read_word(0x800adc20+player*4)==p->header)
                psx_mod_write_word(0x800adc20+player*4,p->native_base);
            p->native_base=0;
        }
        if(active && ready(player)) {
            uint32_t actor=T3_ACTOR(player),record=psx_mod_read_word(actor+0x54);
            unsigned expression=0,frame=psx_mod_read_half(actor+0x58);
            if(source_record(record)) {
                /* Face variants 0x3F.. from arcade kinds 0x25.. (0x40: Jun's). */
                uint32_t prop=psx_mod_read_word(record+32);
                for(unsigned i=0;i<256;i++,prop+=4) {
                    unsigned at=psx_mod_read_half(prop),op=psx_mod_read_half(prop+2);
                    if(!at)break;
                    if(op>>8>=0x3f && op>>8<=0x44 && frame>=at && frame<at+(op&255))expression=(op>>8)-0x3f;
                }
            }
            tekken3_ttt_face(player,expression);
        }
    }
}
void __wrap_func_8002D178(CPUState *cpu) {
    unsigned player=cpu->gpr[4]==T3_ACTOR_P2?1:0;
    unsigned id=(uint16_t)cpu->gpr[5];
    int entry=(cpu->pc==0 || cpu->pc==0x8002d178) && cpu->gpr[4]==T3_ACTOR(player);
    FighterCombat *owner=NULL;
    for(unsigned f=0;f<TTT_FIGHTER_COUNT;f++)if(in_range(&combat[f],id))owner=&combat[f];
    if(entry && owner) {
        int own=player_combat(player)==owner;
        unsigned native=owner->recovery_aliases[id-owner->f->base_id];
        if(!own && native) {
            /* A victim may borrow an import's unique hit/throw clip, not its
             * get-up, crouch, or movement command graph. Translating only
             * neutral (ID 3) lets early cancels/get-up attacks leak the
             * entire moveset to the opponent. Resolve recovery against the
             * receiving fighter's own table, in either player slot. */
            cpu->gpr[5]=native-1;
            if(psx_mod_read_half(cpu->gpr[4]+0x1a0)==id)
                psx_mod_write_half(cpu->gpr[4]+0x1a0,native-1);
            __real_func_8002D178(cpu);return;
        }
        /* Unmapped IDs are the original unique victim/paired sequences.
         * Their eventual recovery is intercepted above. Mirrors keep their
         * separate per-player source records and full move graphs. */
        unsigned holder=own?player:1-player;
        if(player_combat(holder)==owner && ready(holder)) {
            cpu->gpr[2]=owner->players[holder].records+(id-owner->f->base_id)*56;
            cpu->pc=cpu->gpr[31];return;
        }
    }
    FighterCombat *fc=player_combat(player);
    if(entry && fc && ready(player)) {
        static unsigned char reported[4023];
        if(id<4023 && id!=3 && !fc->native_aliases[id] && !reported[id] &&
            psx_mod_read_word(fc->players[player].alias_table+id*4)==fc->players[player].records+fc->neutral*56) {
            reported[id]=1;psx_mod_log("%s combat: unmapped native entry %04X requested by P%u\n",fc->f->name,id,player+1);
        }
        if(id==3 || in_range(fc,id)) {
            unsigned index=id==3?fc->neutral:id-fc->f->base_id;
            cpu->gpr[2]=fc->players[player].records+index*56;cpu->pc=cpu->gpr[31];return;
        }
    }
    __real_func_8002D178(cpu);
}
static const uint16_t *pose(uint32_t clip,int frame) {
    if(!loaded)return NULL;
    for(unsigned f=0;f<TTT_FIGHTER_COUNT;f++) {
        const FighterCombat *fc=&combat[f];
        if(!fc->loaded || clip<fc->guest)continue;
        unsigned index=0;
        while(index<fc->count && fc->clip_addresses[index]!=clip)index++;
        if(index==fc->count)continue;
        unsigned offset=fc->clip_offsets[index],n=word(fc->data+offset)&255;
        if((word(fc->data+offset)&0xffffff00)!=0x4a554e00 || word(fc->data+offset+4)!=57 || !n ||
           offset+8+n*114>fc->size)return NULL;
        if(frame<0)frame=0;if((unsigned)frame>=n)frame=n-1;
        return (const uint16_t*)(fc->data+offset+8+frame*114);
    }
    return NULL;
}
const uint16_t *tekken3_ttt_decoded_pose(uint32_t input) {
    if(psx_mod_read_word(input+8)!=0x504e554a)return NULL;
    uint32_t clip=psx_mod_read_word(input+12);
    return pose(clip,psx_mod_read_half(input+16));
}
static int decode_pose(CPUState *cpu,int full) {
    const uint16_t *p=pose(cpu->gpr[4],(int)cpu->gpr[6]);if(!p)return 0;
    uint32_t out=cpu->gpr[5],angle=p[0],table=T3_SINE_TABLE+((angle>>3)&0x1ffe);
    int32_t s=(int16_t)psx_mod_read_half(table),c=(int16_t)psx_mod_read_half(table+0x800),distance=(int16_t)p[2];
    if(full)for(unsigned i=0;i<49;i++)psx_mod_write_half(out+i*2,0);
    psx_mod_write_half(out,(uint16_t)((s*distance)>>12));
    psx_mod_write_half(out+2,(uint16_t)-(int16_t)p[1]);
    psx_mod_write_half(out+4,(uint16_t)((c*distance)>>12));
    if(full) {
        psx_mod_write_word(out+8,0x504e554a);
        psx_mod_write_word(out+12,cpu->gpr[4]);
        psx_mod_write_half(out+16,(uint16_t)((int)cpu->gpr[6]<0?0:cpu->gpr[6]));
    }
    cpu->pc=cpu->gpr[31];return 1;
}
void __wrap_func_80038B4C(CPUState *cpu) {
    if((cpu->pc==0 || cpu->pc==0x80038b4c) && decode_pose(cpu,1))return;
    __real_func_80038B4C(cpu);
}
void __wrap_func_800389C0(CPUState *cpu) {
    if((cpu->pc==0 || cpu->pc==0x800389c0) && decode_pose(cpu,0))return;
    __real_func_800389C0(cpu);
}
