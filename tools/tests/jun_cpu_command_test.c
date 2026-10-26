#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef struct {uint32_t gpr[32],pc;} CPUState;
static int loaded=1,real_calls;
/* Jun (23) and Kazuya (24), each with its own patterns and per-player tables. */
typedef struct {uint32_t native_base,alias_table;} PlayerCombat;
typedef struct {PlayerCombat players[2];uint32_t patterns[3];unsigned pattern_count;} FighterCombat;
static FighterCombat fighters[2]={
    {{{1,0x80410000},{1,0x80420000}},{0x80400100,0x80400200,0x80400300},3},
    {{{1,0x80430000},{1,0x80440000}},{0x80500100,0x80500200},2}};
static uint32_t actor=0x800a9228;
static unsigned ids[2]={23,23},modes[2]={2,2};
static unsigned cpu_index,cache_writes;
static uint32_t cache_addresses[2],cache_values[2];
static FighterCombat *fighter_of(unsigned id) {return id==23 || id==24?&fighters[id-23]:NULL;}
static const FighterCombat *tekken3_ttt_by_roster(unsigned id) {return fighter_of(id);}
static unsigned psx_mod_read_byte(uint32_t a) {assert(a==actor+0x1886);return cpu_index;}
static void psx_mod_write_word(uint32_t a,uint32_t v) {assert(cache_writes<2);cache_addresses[cache_writes]=a;cache_values[cache_writes++]=v;}
static const FighterCombat *player_combat(unsigned p) {
    assert(p<2);return loaded && modes[p]==2?fighter_of(ids[p]):NULL;
}
static int ready(unsigned p) {const FighterCombat *fc=player_combat(p);return fc && fc->players[p].native_base;}
static void __real_func_80059890(CPUState *c) {real_calls++;c->pc=c->gpr[31];}
static uint32_t psx_mod_read_word(uint32_t a) {assert(a==0x800afa5c);return actor;}
static unsigned psx_mod_read_half(uint32_t a) {assert(a==actor+0x18);return ids[actor==0x800aaab4];}
static void __real_func_8002CD7C(CPUState *c) {real_calls++;c->gpr[2]=0xbadcafe;c->pc=c->gpr[31];}
/* Extracted verbatim from the production C file by the test runner. */
#include "jun_cpu_command_wrapper.inc"
static void check(unsigned command,unsigned pc,const FighterCombat *expected) {
    CPUState c={0};c.gpr[4]=command;c.gpr[31]=0x800618f0;c.pc=pc;
    int before=real_calls;__wrap_func_8002CD7C(&c);
    assert(c.pc==c.gpr[31]);assert(real_calls==before+!expected);
    assert(c.gpr[2]==(expected?expected->patterns[(command&65535)-0xe000]:0xbadcafe));
}
int main(void) {
    for(unsigned f=0;f<2;f++)for(unsigned p=0;p<2;p++) {
        FighterCombat *fc=&fighters[f];unsigned id=23+f,n=fc->pattern_count;
        actor=0x800a9228+p*0x188c;ids[p]=id;
        for(unsigned i=0;i<n;i++) {check(0xe000+i,0,fc);check(0xffffe000+i,0x8002cd7c,fc);}
        check(0xe000+n,0,NULL);  /* past this fighter's own patterns */
        for(unsigned other=0;other<23;other++) {ids[p]=other;check(0xe000,0,NULL);}ids[p]=id;
        modes[p]=0;check(0xe000,0,NULL);modes[p]=2;
        fc->players[p].native_base=0;check(0xe000,0,NULL);fc->players[p].native_base=1;
        loaded=0;check(0xe000,0,NULL);loaded=1;
        check(0xdfff,0,NULL);check(0xffff,0,NULL);check(0xc001,0,NULL);
        check(0xe000,0x8002cd80,NULL);
    }
    actor=0;check(0xe000,0,NULL);actor=0x800ac340;check(0xe000,0,NULL);
    for(unsigned p=0;p<2;p++)for(unsigned index=0;index<3;index++)for(unsigned id=9;id<=24;id+=id==9?14:1) {
        actor=0x800a9228+p*0x188c;ids[p]=id;cpu_index=index==2?255:index;
        CPUState c={0};c.gpr[4]=actor;c.gpr[31]=0x8002c4cc;cache_writes=0;
        int before=real_calls;__wrap_func_80059890(&c);assert(real_calls==before+1);
        const FighterCombat *fc=fighter_of(id);
        assert(cache_writes==(fc && index<2?2:0));
        if(cache_writes) {
            assert(cache_addresses[0]==0x8009f2e8+index*4);
            assert(cache_addresses[1]==0x8009f318+index*0x330+12);
            assert(cache_values[0]==fc->players[p].alias_table && cache_values[1]==cache_values[0]);
        }
    }
    puts("Production CPU command wrapper: Jun/Kazuya, P1/P2, bounds, disabled/uninstalled and opponent isolation passed");
    puts("Production CPU alias-cache refresh: both actor slots, CPU indices and stock/disabled-index isolation passed");
}
