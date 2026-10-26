/* Original local TTT1 PCM for each imported fighter, routed by the game's
 * character-voice dispatcher.
 * Playback runs on the emulation thread, before the existing host audio queue,
 * fade, master volume and resampler. No sample ROM is embedded in the binary. */
#include "psx_runtime.h"
#include "mod_plugins.h"
#include "psx_sha256.h"
#include "spu.h"
#include "tekken3_ttt_fighter.h"
#include "tekken3_guest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern unsigned tekken3_ttt_player_motion_mode(unsigned player);
extern void __real_func_80040C90(CPUState *cpu);
extern void __real_spu_render(int16_t *out, int frames);
extern void __real_spu_set_host_mix_volume(int sfx, int music);
extern void __real_spu_init(void);
extern int __real_spu_snapshot_read(const uint8_t *p, uint32_t len);

enum { MAX_SAMPLES=32 };
typedef struct { const unsigned char *pcm; uint32_t frames; unsigned id, group, index; } TttSample;
typedef struct { const TttSample *sample; unsigned id, group; uint32_t pos, calls; } TttVoice;
typedef struct {
    TttSample samples[MAX_SAMPLES];
    unsigned count, group_count[6];
    unsigned char *bank;
    int attempted, loaded;
} VoiceBank;
static VoiceBank banks[TTT_FIGHTER_COUNT];
static TttVoice voices[2];
static int any_loaded, sfx_gain = 100;
static uint32_t telemetry;

static uint32_t rd32(const unsigned char *p) {
    return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static int16_t rd16(const unsigned char *p) {
    return (int16_t)(p[0] | (unsigned)p[1]<<8);
}
/* The voice bank of the imported fighter a player controls, or NULL. */
static VoiceBank *player_bank(unsigned p) {
    if(p>1 || tekken3_ttt_player_motion_mode(p)!=2)return NULL;
    const TttFighter *f=tekken3_ttt_player_fighter(p);
    return f?&banks[tekken3_ttt_index(f)]:NULL;
}
static void stop_voices(void) { memset(voices,0,sizeof voices); }

static int load_bank(VoiceBank *b) {
    if(b->attempted)return b->loaded;
    b->attempted=1;
    const TttFighter *fighter=&tekken3_ttt_fighters[b-banks];
    const char *root=tekken3_ttt_asset_root(fighter);
    if(!root)return 0;
    char path[4096];
    if(snprintf(path,sizeof path,"%s/%s-TTT1-voices.juv",root,fighter->name)>=(int)sizeof path)return 0;
    FILE *f=fopen(path,"rb");
    if(!f){psx_mod_log("%s voices: missing private voice bank\n",fighter->name);return 0;}
    const unsigned size=fighter->voice_size;
    unsigned char *data=(unsigned char*)malloc(size);
    if(!data){fclose(f);return 0;}
    int ok=fread(data,1,size,f)==size && fgetc(f)==EOF;
    fclose(f);
    unsigned char digest[32];char hash[65];
    if(ok) {
        psx_sha256_compute(data,size,digest);
        for(unsigned i=0;i<32;i++)snprintf(hash+i*2,3,"%02x",digest[i]);
        ok=!strcmp(hash,fighter->voice_sha);
    }
    unsigned count=ok?rd32(data+12):0;
    ok=ok && rd32(data)==0x3156554a && rd32(data+4)==1 && rd32(data+8)==size &&
        count && count<=MAX_SAMPLES && rd32(data+16)==44100;
    uint32_t next=32+count*16;
    unsigned previous=0;
    /* Entries are ordered by sample ID; each names its role and its index
     * within that role (TTT1 profile order). A sample serving two roles
     * (Armor King's 200) has an entry, with its own copy, for each. */
    for(unsigned i=0;ok && i<count;i++) {
        const unsigned char *entry=data+32+i*16;
        uint32_t id=rd32(entry),offset=rd32(entry+4),frames=rd32(entry+8),role=rd32(entry+12);
        unsigned group=role>>16,index=role&65535;
        if((i && id<previous) || !id || offset!=next || offset>size || !frames || frames>(size-offset)/2 ||
           group<2 || group>5 || index>=MAX_SAMPLES) {
            ok=0;break;
        }
        for(unsigned k=0;k<i;k++)if(b->samples[k].group==group && b->samples[k].index==index)ok=0;
        b->samples[i]=(TttSample){data+offset,frames,id,group,index};
        b->group_count[group]++;previous=id;
        next+=frames*2;
    }
    /* Every role needs samples 0..n-1. */
    for(unsigned i=0;ok && i<count;i++)if(b->samples[i].index>=b->group_count[b->samples[i].group])ok=0;
    if(!ok || next!=size || !b->group_count[2] || !b->group_count[3] || !b->group_count[4] || !b->group_count[5]) {
        psx_mod_log("%s voices: rejected corrupt or incompatible voice bank\n",fighter->name);
        memset(b->group_count,0,sizeof b->group_count);
        free(data);return 0;
    }
    b->bank=data;b->count=count;
    if(!telemetry) {
        telemetry=psx_mod_alloc_guest_memory(80,16);
        if(telemetry)for(unsigned i=0;i<80;i+=4)psx_mod_write_word(telemetry+i,0);
    }
    psx_mod_log("%s voices: loaded %u verified TTT1 samples; telemetry=%08X\n",fighter->name,count,telemetry);
    return any_loaded=b->loaded=1;
}
/* A role's sample: the PS1 command's index wraps within the role. */
static const TttSample *role_sample(const VoiceBank *b,unsigned group,unsigned index) {
    unsigned n=b->group_count[group];
    for(unsigned i=0;i<b->count;i++)
        if(b->samples[i].group==group && b->samples[i].index==index%n)return &b->samples[i];
    return NULL;
}

void tekken3_ttt_voices_tick(void) {
    for(unsigned p=0;p<2;p++) {
        VoiceBank *b=player_bank(p);
        if(!b || psx_mod_read_half(T3_SCREEN)!=T3_SCREEN_FIGHT)voices[p].group=0;
        else (void)load_bank(b);
        if(telemetry) {
            uint32_t dst=telemetry+p*32;
            TttVoice *v=&voices[p];
            psx_mod_write_word(dst,v->calls);
            psx_mod_write_word(dst+4,v->id);
            psx_mod_write_word(dst+8,v->group);
            psx_mod_write_word(dst+12,v->pos);
            psx_mod_write_word(dst+16,v->sample?v->sample->frames:0);
        }
    }
    if(telemetry)psx_mod_write_word(telemetry+64,(uint32_t)sfx_gain);
}

void __wrap_func_80040C90(CPUState *cpu) {
    unsigned p=cpu->gpr[4], command=cpu->gpr[6]&65535;
    VoiceBank *b=(cpu->pc==0 || cpu->pc==0x80040c90)?player_bank(p):NULL;
    if(b && load_bank(b)) {
        unsigned group=command>>12, index=command&4095;
        /* The native common/reaction records still use Jin's category sizes.
         * Translate within the matching category of the fighter's own bank;
         * never into another bank. Source commands are already in range and
         * keep their exact sample. */
        if(group<2 || group>5) {cpu->pc=cpu->gpr[31];return;}
        const TttSample *sample=role_sample(b,group,group>=4?0:index);
        TttVoice *v=&voices[p];
        v->sample=sample;v->id=sample->id;v->group=group;v->pos=0;v->calls++;
        psx_mod_log("TTT1 voice: P%u command=%04X sample=%u\n",p+1,command,v->id);
        /* Only this actor's character cry is replaced. Shared hit, movement,
         * announcer and opponent calls keep the native sound path. */
        cpu->pc=cpu->gpr[31];return;
    }
    __real_func_80040C90(cpu);
}

void __wrap_spu_render(int16_t *out,int frames) {
    __real_spu_render(out,frames);
    if(!out || frames<=0 || !any_loaded)return;
    SpuGlobalState g;spu_get_global_state(&g);
    int audible=(g.ctrl&0xc000)==0xc000;
    int16_t main_volume[2]={(int16_t)spu_read(0x1f801db8),(int16_t)spu_read(0x1f801dba)};
    for(unsigned p=0;p<2;p++) {
        TttVoice *v=&voices[p];
        if(!v->group)continue;
        if(!player_bank(p) || psx_mod_read_half(T3_SCREEN)!=T3_SCREEN_FIGHT){v->group=0;continue;}
        const TttSample *sample=v->sample;
        for(int f=0;f<frames && v->pos<sample->frames;f++,v->pos++) {
            int32_t value=rd16(sample->pcm+v->pos*2);
            /* Match the PS1 voice mix level, with a short attack ramp. PCM
             * stays uncompressed; SFX and host master controls both apply. */
            int ramp=v->pos<220?(int)v->pos:220;
            value=value*sfx_gain/100*ramp/220/4;
            if(!audible)value=0;
            for(unsigned ch=0;ch<2;ch++) {
                int32_t mixed=(int32_t)out[f*2+ch]+value*main_volume[ch]/32768;
                out[f*2+ch]=(int16_t)(mixed>32767?32767:mixed< -32768?-32768:mixed);
            }
        }
        if(v->pos==sample->frames)v->group=0;
    }
}
void __wrap_spu_set_host_mix_volume(int sfx,int music) {
    sfx_gain=sfx<0?0:sfx>100?100:sfx;
    __real_spu_set_host_mix_volume(sfx,music);
}
void __wrap_spu_init(void) { stop_voices();__real_spu_init(); }
int __wrap_spu_snapshot_read(const uint8_t *p,uint32_t len) {
    int result=__real_spu_snapshot_read(p,len);
    if(result)stop_voices();
    return result;
}
