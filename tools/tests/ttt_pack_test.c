#include "../../src/tekken3_ttt_pack.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void put(unsigned char *p,unsigned v) {
    for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));
}
int main(int argc,char **argv) {
    /* Optional imported move-ID base; Jun's is 8192. */
    assert(argc==2 || argc==3);
    unsigned base=argc==3?(unsigned)strtoul(argv[2],NULL,0):8192;
    FILE *f=fopen(argv[1],"rb");assert(f);
    fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);assert(size>32);
    unsigned char *p=malloc(size),*copy=malloc(size);assert(p && copy);
    assert(fread(p,1,size,f)==(size_t)size);fclose(f);
    assert(ttt_pack_validate(p,size,base));
    for(unsigned i=0;i<32;i++)assert(!ttt_pack_validate(p,i,base));
    assert(!ttt_pack_validate(p,size-1,base));
    unsigned r=ttt_pack_word(p+28),clip=ttt_pack_word(p+r),cp=ttt_pack_word(p+r+12);
    unsigned mutations[][2]={{4,99},{8,0},{12,1025},{16,1024},{20,0xffffffff},
        {24,0},{28,0},{36,413},{40,0},{44,0xffffffff},{r,0},{r+12,0xffffffff},
        {r+16,base-1},{r+40,18},{r+40,0x12000000},{4,2},
        {clip,0x4a554e00},{clip+4,49},{cp+4,0x7fff0000}};
    for(unsigned i=0;i<sizeof mutations/sizeof mutations[0];i++) {
        memcpy(copy,p,size);put(copy+mutations[i][0],mutations[i][1]);
        assert(!ttt_pack_validate(copy,size,base));
    }
    free(p);free(copy);puts("TTT1 pack: valid export accepted; truncation and malformed references rejected");
    return 0;
}
