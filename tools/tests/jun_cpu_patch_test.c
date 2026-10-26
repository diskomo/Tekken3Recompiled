#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char ram[8*1024*1024];
static unsigned writes;
static unsigned index_of(uint32_t a) {
    a&=0x1fffffff;
    if(a>=0x1f000000 && a<0x1f400000)a=0x400000+a-0x1f000000;
    assert(a<sizeof ram);return a;
}
static uint32_t psx_mod_read_word(uint32_t a) {uint32_t v;memcpy(&v,ram+index_of(a),4);return v;}
static void psx_mod_write_code_word(uint32_t a,uint32_t v) {memcpy(ram+index_of(a),&v,4);writes++;}
static void patch(uint32_t a,uint32_t old,uint32_t v) {if(psx_mod_read_word(a)==old)psx_mod_write_code_word(a,v);}
#include "../../src/tekken3_roster_cpu.h"
int main(int argc,char **argv) {
    assert(argc==3);FILE *f=fopen(argv[1],"rb");assert(f);
    assert(fread(ram,1,sizeof ram,f)==sizeof ram);fclose(f);
    /* The shared roster owns the CPU tuning table; these are the pools. */
    /* Jun (23), as the roster pools her. */
    roster_cpu_patch(1u<<23);
    unsigned before=writes;roster_cpu_patch(1u<<23);assert(writes==before);
    f=fopen(argv[2],"wb");assert(f);assert(fwrite(ram,1,sizeof ram,f)==sizeof ram);fclose(f);
    printf("CPU patches: %u writes; repeat application is idempotent\n",writes);
}
