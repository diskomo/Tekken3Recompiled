/* SLUS-00402 extra roster: up to nine fighters with character IDs 23..31
 * beside the 21 stock fighters. 21 remains the Force enemies and 22 the
 * empty-selection sentinel. Private guest tables extend the stock tables
 * without moving their neighbours; original disc data is never changed.
 *
 * The cabinet selector keeps its two rows. At 4:3 it has eleven columns, so
 * one added fighter fits; with True Widescreen it grows to fourteen columns
 * and the renderer spreads them across the 16:9 canvas. The console selector
 * (VS, Team Battle) keeps its three rows and grows to eight to ten columns,
 * which fit every added fighter at either aspect. */
#include "mod_plugins.h"
#include "psx_runtime.h"
#include "tekken3_roster.h"
#include "tekken3_outfits.h"
#include "tekken3_selector_layout.h"
#include <stdio.h>
#include <string.h>

enum {
    FIRST_ID=TEKKEN3_ROSTER_FIRST_ID, MAX_EXTRA=TEKKEN3_ROSTER_MAX_EXTRA,
    STOCK_SELECTIONS=92, STOCK_NODES=22, NO_NODE=22, GON=17,
    NODE_SIZE=12, ROW_Y=62,
    CELL_SIZE=8, CELL_PITCH=36, CELL_ROW_Y=63, EMPTY_CELL=0xff
};
/* The single spare saved-statistics row: 21 belongs to the non-selectable
 * Force enemies. */
enum { SPARE_USAGE_ROW=21, FIRST_PRIVATE_MODEL=52 };
typedef struct {
    unsigned donor,first_model;
    const Tekken3RosterImport *import;  /* NULL for a stand-in */
} Slot;
static const Tekken3RosterImport *requests[MAX_EXTRA];
static unsigned request_count,proxy_donors[MAX_EXTRA],proxy_count;
static Slot slots[MAX_EXTRA];
static unsigned extra_count,private_models,usage_owner;
static uint32_t metadata,model_map,nodes,misc,model_sizes,face_profiles,usage_mask;
static uint32_t body_profiles,cpu_profiles,portrait_bank,force_bosses,descriptors;
static uint32_t console_cells,vs_table;
static uint32_t cpu_pool;  /* added fighters the CPU may pick as opponents */
static unsigned node_count=STOCK_NODES,visible_extras;
static unsigned console_columns,console_count,console_x0;
static int initialized,attempted;

int tekken3_roster_add_import(const Tekken3RosterImport *fighter) {
    if(attempted || !fighter || request_count>=MAX_EXTRA || fighter->donor>=21 ||
       fighter->id<FIRST_ID || fighter->id>=FIRST_ID+MAX_EXTRA)return 0;
    for(unsigned i=0;i<request_count;i++)if(requests[i]->id==fighter->id)return 0;
    requests[request_count++]=fighter;
    return 1;
}
int tekken3_roster_add_proxy(unsigned donor) {
    if(attempted || proxy_count>=MAX_EXTRA || donor>=21)return 0;
    proxy_donors[proxy_count++]=donor;
    return 1;
}
int tekken3_roster_live(void) {return initialized;}
unsigned tekken3_roster_extra_count(void) {return extra_count;}
static int extra(unsigned id) {return id>=FIRST_ID && id<FIRST_ID+extra_count;}
static const Tekken3RosterImport *imported(unsigned id) {
    return extra(id)?slots[id-FIRST_ID].import:NULL;
}
unsigned tekken3_roster_donor(unsigned id) {return extra(id)?slots[id-FIRST_ID].donor:id;}
unsigned tekken3_roster_first_model(unsigned id) {return extra(id)?slots[id-FIRST_ID].first_model:0;}
static unsigned usage_row(unsigned id) {
    return extra(id) && id==usage_owner?SPARE_USAGE_ROW:tekken3_roster_donor(id);
}
/* Imports keep their IDs; stand-ins fill the remaining IDs in order. IDs must
 * stay contiguous, so an import beyond the filled range is dropped. */
static void assign_slots(void) {
    unsigned next_proxy=0;
    for(extra_count=0;extra_count<MAX_EXTRA;extra_count++) {
        unsigned id=FIRST_ID+extra_count;
        const Tekken3RosterImport *fighter=NULL;
        for(unsigned i=0;i<request_count;i++)if(requests[i]->id==id)fighter=requests[i];
        Slot *slot=&slots[extra_count];
        if(fighter){slot->import=fighter;slot->donor=fighter->donor;}
        else if(next_proxy<proxy_count){slot->import=NULL;slot->donor=proxy_donors[next_proxy++];}
        else break;
    }
    for(unsigned i=0;i<request_count;i++)if(!extra(requests[i]->id))
        psx_mod_log("Roster: %s needs ID %u, past the contiguous roster; skipped\n",
                requests[i]->name?requests[i]->name:"import",requests[i]->id);
}
/* Exclusive upper bound for character IDs, and the matching selection bound
 * (character * 4 + costume). */
static unsigned id_limit(void) {return FIRST_ID+extra_count;}
static uint32_t extra_bits(void) {return ((1u<<extra_count)-1u)<<FIRST_ID;}

static void copy_guest(uint32_t dst,uint32_t src,unsigned n) {
    for(unsigned i=0;i<n;i++)psx_mod_write_byte(dst+i,psx_mod_read_byte(src+i));
}
static void patch(uint32_t address,uint32_t expected,uint32_t replacement) {
    if(psx_mod_read_word(address)==expected)psx_mod_write_code_word(address,replacement);
}
/* Replace "ADDIU rt, rs, lo(table)" with "LUI rt, hi(target)". A LUI does
 * not introduce a load-delay hazard, so each relocated table is 64 KiB aligned. */
static void indirect(uint32_t address,uint32_t expected,uint32_t target) {
    patch(address,expected,0x3c000000u|(expected&0x001f0000u)|(target>>16));
}
/* Redirect a LUI/ADDIU pair to a target with an arbitrary low half. */
static void lui_ori(uint32_t lui,uint32_t lui_word,uint32_t addiu,uint32_t addiu_word,uint32_t target) {
    patch(lui,lui_word,(lui_word&0xffff0000u)|(target>>16));
    patch(addiu,addiu_word,0x34000000u|(addiu_word&0x03ff0000u)|(target&65535));
}
#include "tekken3_roster_cpu.h"
#include "tekken3_guest.h"
static void immediate(uint32_t address,unsigned expected,unsigned replacement) {
    uint32_t op=psx_mod_read_word(address);
    if((op&65535)==expected)patch(address,op,(op&0xffff0000u)|replacement);
}

/* Console selector (VS, Practice, Team Battle). Stock 80022768 holds 21
 * cells of {x, y, selection byte} indexed (row * 7 + column) * 6, and its
 * search and draw loops walk the cells contiguously. The relocated table has
 * eight-byte cells in rows of 8-10, so every index becomes
 * (row * columns + column) << 3: short enough to rewrite in place. */
enum { R_V0=2,R_V1=3,R_A0=4,R_T0=8,R_T2=10 };
static uint32_t op_sll(unsigned rd,unsigned rt,unsigned sa) {return rt<<16|rd<<11|sa<<6;}
static uint32_t op_addu(unsigned rd,unsigned rs,unsigned rt) {return rs<<21|rt<<16|rd<<11|0x21;}
static uint32_t op_lui(unsigned rt,uint32_t address) {return 0x3c000000u|rt<<16|address>>16;}

static void layout_console(void) {
    console_count=21+extra_count;
    console_columns=(console_count+2)/3;
    console_x0=(368-(CELL_PITCH*(console_columns-1)+32))/2-2;
    for(unsigned i=0;i<console_columns*3;i++) {
        uint32_t cell=console_cells+i*CELL_SIZE;
        unsigned selection=i<21?psx_mod_read_byte(0x80022768+i*6+4):
            i<console_count?(FIRST_ID+i-21)*4:EMPTY_CELL;
        psx_mod_write_half(cell,(uint16_t)(console_x0+CELL_PITCH*(i%console_columns)));
        psx_mod_write_half(cell+2,(uint16_t)(67+CELL_ROW_Y*(i/console_columns)));
        psx_mod_write_word(cell+4,selection);
    }
    copy_guest(vs_table,0x80022804,22*2);
    for(unsigned k=0;k<extra_count;k++)
        copy_guest(vs_table+(FIRST_ID+k)*2,0x80022804+slots[k].donor*2,2);
}
static unsigned console_row_width(int row) {
    int left=(int)console_count-row*(int)console_columns;
    return left<(int)console_columns?(unsigned)left:console_columns;
}
/* Rewrite one index sequence once its original words still match: the kept
 * loads/stores, then dst = (y * columns + x) << 3 (x is consumed), then the
 * relocated table's LUI when the window loaded the base itself. */
static void rewrite_index(uint32_t address,const uint32_t *original,unsigned count,
                          const uint32_t *kept,unsigned kept_count,
                          unsigned y,unsigned x,unsigned temp,unsigned dst,int base) {
    for(unsigned i=0;i<count;i++)if(psx_mod_read_word(address+i*4)!=original[i])return;
    uint32_t code[12];unsigned n=0;
    for(unsigned i=0;i<kept_count;i++)code[n++]=kept[i];
    code[n++]=op_sll(temp,y,console_columns==10?2:3);
    if(console_columns>8)code[n++]=op_addu(temp,temp,y);
    if(console_columns==10)code[n++]=op_sll(temp,temp,1);
    code[n++]=op_addu(x,x,temp);
    code[n++]=op_sll(dst,x,3);
    while(n<count-(base>=0))code[n++]=0;
    if(base>=0)code[n++]=op_lui((unsigned)base,console_cells);
    for(unsigned i=0;i<count;i++)psx_mod_write_code_word(address+i*4,code[i]);
}
static void patch_console(void) {
    unsigned limit=id_limit();
    static const uint32_t a[]={0x000410c0,0x00441023,0x00621821,0x00031040,0x00431021,0x00021040,0x3c038002,0x24632768};
    rewrite_index(0x80052e08,a,8,NULL,0,R_A0,R_V1,R_V0,R_V0,R_V1);
    static const uint32_t b[]={0x3c088002,0x25082768,0x8e24ffe8,0x8e23ffe4,0x000410c0,0x00441023,0x00621821,0x00031040,0x00431021,0x00021040};
    rewrite_index(0x80054278,b,10,b+2,2,R_A0,R_V1,R_V0,R_V0,R_T0);
    static const uint32_t c[]={0x3c0a8002,0x254a2768,0x8e44ff90,0x8e43ff8c,0x000410c0,0x00441023,0x00621821,0x00031040,0x00431021,0x00021040};
    rewrite_index(0x80054410,c,10,c+2,2,R_A0,R_V1,R_V0,R_V0,R_T2);
    static const uint32_t d[]={0x3c0a8002,0x8e44ff90,0x254a2768,0xafa20028,0x8e43ff8c,0x000410c0,0x00441023,0x00621821,0x00031040,0x00431021,0x00021040};
    static const uint32_t d_kept[]={0x8e44ff90,0xafa20028,0x8e43ff8c};
    rewrite_index(0x80054494,d,11,d_kept,3,R_A0,R_V1,R_V0,R_V0,R_T2);
    static const uint32_t e[]={0x000310c0,0x00431023,0x00822021,0x00041840,0x00641821,0x00031840,0x3c028002,0x24422768};
    rewrite_index(0x8005483c,e,8,NULL,0,R_V1,R_A0,R_V0,R_V1,R_V0);
    static const uint32_t f[]={0x000410c0,0x00441023,0x00621821,0x00031040,0x00431021,0x00021040};
    rewrite_index(0x80054d74,f,6,NULL,0,R_A0,R_V1,R_V0,R_V0,-1);
    rewrite_index(0x80054da4,f,6,NULL,0,R_A0,R_V1,R_V0,R_V0,-1);
    static const uint32_t bases[]={0x800531b4,0x800532d0,0x800533a4,0x800534d0,0x800535d0,0x80053704,0x80054d10,0x80054e6c};
    for(unsigned i=0;i<sizeof bases/sizeof *bases;i++) {
        uint32_t w=psx_mod_read_word(bases[i]);
        if(w>>26==9 && (w&65535)==0x2768)indirect(bases[i],w,console_cells);
    }
    /* Six cursor-placement searches walk rows of 7 in 6-byte steps; the
     * portrait loop draws the first 21 cells. */
    static const uint32_t rows[]={0x800531ec,0x80053308,0x800533e8,0x8005350c,0x80053614,0x80053748};
    static const uint32_t steps[]={0x800531f4,0x80053310,0x800533f0,0x80053514,0x8005361c,0x80053750};
    for(unsigned i=0;i<6;i++){immediate(rows[i],7,console_columns);immediate(steps[i],6,CELL_SIZE);}
    immediate(0x80054ed0,6,CELL_SIZE);immediate(0x80054edc,6,CELL_SIZE);
    immediate(0x80054ed4,21,console_count);
    for(uint32_t at=0x80052de4;at<0x80054ee0;at+=4) {
        uint32_t w=psx_mod_read_word(at),op=w>>26;
        if((op==10 || op==11) && (w&65535)==0x58)patch(at,w,(w&0xffff0000u)|limit*4);
    }
    /* VS mode reads two bytes per fighter at 80022804. */
    lui_ori(0x80052ee4,0x3c028002,0x80052ee8,0x24472804,vs_table);
    /* Both navigation blocks call the native return stub at 80052938; its
     * wrapper moves the cursor over the uneven last row. */
    patch(0x8005387c,0x3c02800b,0x0c014a4e);patch(0x80053880,0x2442df00,0);
    patch(0x80053c08,0x3c02800b,0x0c014a4e);patch(0x80053c0c,0x2442df00,0);
    patch(0x80052de4,0x27bdffe0,0x08014a50);patch(0x80052de8,0xafb10014,0);
    tekken3_selector_declare_team_grid((int)console_columns,(int)console_x0,(int)console_count,
                                       console_cells,CELL_SIZE);
}

/* A private model borrows its donor's first model: its disc records, size
 * entry and properties. The importer installs its own mesh over that load. */
static unsigned donor_model(unsigned id) {return psx_mod_read_byte(0x800958c4+tekken3_roster_donor(id)*4);}
static const Slot *model_owner(unsigned model) {
    for(unsigned k=0;k<extra_count;k++) {
        const Slot *s=&slots[k];
        if(s->import && model>=s->first_model && model<s->first_model+s->import->models)return s;
    }
    return NULL;
}

static void initialize(void) {
    if(attempted)return;attempted=1;
    assign_slots();
    if(!extra_count)return;
    /* Imports join the CPU opponent pools in ID order while they fit;
     * stand-ins never do. */
    unsigned pooled=0;
    for(unsigned k=0;k<extra_count;k++) {
        if(!slots[k].import)continue;
        if(pooled<TEKKEN3_ROSTER_CPU_POOL_MAX){cpu_pool|=1u<<(FIRST_ID+k);pooled++;}
        else psx_mod_log("Roster: %s is not a CPU opponent; the Team Battle pool is full\n",
                     slots[k].import->name?slots[k].import->name:"import");
    }
    uint32_t allocation=psx_mod_alloc_guest_memory(0x60000+0x10000,16);
    if(!allocation){psx_mod_log("Roster: guest allocation failed\n");return;}
    uint32_t area=(allocation+65535u)&~65535u;
    metadata=area;model_map=area+0x10000;nodes=area+0x20000;misc=area+0x30000;
    body_profiles=misc;cpu_profiles=misc+0x100;portrait_bank=misc+0x300;
    force_bosses=misc+0x500;descriptors=misc+0x800;vs_table=misc+0xa00;
    usage_mask=misc+0x1000;  /* Its low half must stay below 0x8000 for an LW. */
    console_cells=area+0x40000;model_sizes=area+0x50000;face_profiles=model_sizes+0x1000;
    copy_guest(metadata,0x80097d40,STOCK_SELECTIONS*4);
    copy_guest(model_map,0x800958c4,STOCK_SELECTIONS);
    copy_guest(body_profiles,0x80096f60,22*4);
    copy_guest(cpu_profiles,0x80098260,22*12);
    /* The CPU table is also indexed by the opponent; give the sentinel a
     * valid row as the single added fighter did. */
    copy_guest(cpu_profiles+22*12,0x80098260+9*12,12);
    /* Native face animation indexes a four-byte table by model ID whose end
     * aliases model-size data; private models get disabled entries. */
    copy_guest(model_sizes,0x80095a9c,FIRST_PRIVATE_MODEL*12);
    copy_guest(face_profiles,0x800959cc,FIRST_PRIVATE_MODEL*4);
    unsigned next_model=FIRST_PRIVATE_MODEL;
    for(unsigned k=0;k<extra_count;k++) {
        Slot *slot=&slots[k];
        const Tekken3RosterImport *fighter=slot->import;
        unsigned id=FIRST_ID+k,donor=slot->donor;
        uint32_t desc=descriptors+k*32;
        copy_guest(desc,psx_mod_read_word(0x80097d40+donor*16),12);
        psx_mod_write_byte(desc+4,(uint8_t)id);
        slot->first_model=0;
        if(fighter && fighter->models) {
            slot->first_model=next_model;next_model+=fighter->models;
            for(unsigned m=0;m<fighter->models;m++) {
                copy_guest(model_sizes+(slot->first_model+m)*12,0x80095a9c+donor_model(id)*12,12);
                psx_mod_write_word(face_profiles+(slot->first_model+m)*4,0xffffffff);
            }
        }
        for(unsigned c=0;c<4;c++) {
            psx_mod_write_word(metadata+(id*4+c)*4,desc);
            unsigned model=slot->first_model?slot->first_model+(c<fighter->models?c:0):
                psx_mod_read_byte(0x800958c4+donor*4+c);
            psx_mod_write_byte(model_map+id*4+c,(uint8_t)model);
        }
        psx_mod_write_word(body_profiles+id*4,psx_mod_read_word(0x80096f60+donor*4));
        copy_guest(cpu_profiles+id*12,0x80098260+donor*12,12);
        if(fighter) {
            if(fighter->name) {
                char name[16]={0};strncpy(name,fighter->name,sizeof name-1);
                for(unsigned i=0;i<sizeof name;i++)psx_mod_write_byte(desc+12+i,(uint8_t)name[i]);
                psx_mod_write_word(desc,desc+12);
            }
            if(fighter->usage_row==SPARE_USAGE_ROW && !usage_owner)usage_owner=id;
            if(fighter->describe)fighter->describe(fighter->context,desc);
        } else tekken3_outfits_set_character_donor((int)id,(int)donor);
    }
    private_models=next_model-FIRST_PRIVATE_MODEL;
    layout_console();
    initialized=1;
    psx_mod_log("Roster: %u added fighter(s), character IDs %u..%u\n",
            extra_count,FIRST_ID,FIRST_ID+extra_count-1);
}

/* The memory-card format saves 22 usage counter rows; row 21 belongs to the
 * non-selectable Force enemies. One imported fighter may claim it. Records
 * and Character Usage (BNS overlays) show that row as the owner. */
static void patch_usage(void) {
    psx_mod_write_word(usage_mask,(psx_mod_read_word(T3_CHARACTER_MASK)&0x1fffff)|(1u<<SPARE_USAGE_ROW));
    if(psx_mod_read_word(0x800c19b8)==0x27bdff18 &&
       psx_mod_read_word(0x800c1a20)==0x3442ffff) {
        patch(0x800c1a1c,0x3c02001f,0x3c02003f); /* Include saved row 21. */
        /* The descriptor getters are RAM-patched and interpreted; route just
         * the usage name call through the native return stub at 80052948. */
        patch(0x800c1550,0x0c013bfc,0x0c014a52);
    }
    if(psx_mod_read_word(0x800e0860)==0x27bdfe60) {
        patch(0x800e0864,0x3c03ffdf,0x3c03001f);
        patch(0x800e0a38,0x3c02800f,0x3c020000|(usage_mask>>16));
        patch(0x800e0a3c,0x8c53c44c,0x8c530000|(usage_mask&65535));
        patch(0x800dffc0,0x0c013bf2,0x0c014a52);
    }
}

static void patch_executable(void) {
    unsigned limit=id_limit();
    patch(0x800616cc,0x3c04800a,0x3c040000|(cpu_profiles>>16));
    patch(0x800616d4,0x24848260,0x34840000|(cpu_profiles&65535));
    lui_ori(0x8003ec64,0x3c038009,0x8003ec68,0x24636f60,body_profiles);
    static const uint32_t descriptor_refs[]={0x8004efd8,0x8004f008,0x8004f29c,0x8004f374,0x8004f478,0x8004f580,0x8004f5e4,0x8004f648,0x8004f70c};
    for(unsigned i=0;i<sizeof descriptor_refs/sizeof *descriptor_refs;i++)
        indirect(descriptor_refs[i],descriptor_refs[i]==0x8004f648?0x24427d40:0x24637d40,metadata);
    indirect(0x8003618c,0x244258c4,model_map);indirect(0x800361c4,0x246358c4,model_map);
    indirect(0x800361f8,0x244658c4,model_map);indirect(0x8003623c,0x246358c4,model_map);
    immediate(0x800361b0,STOCK_SELECTIONS,limit*4);
    immediate(0x80036214,STOCK_SELECTIONS,limit*4);
    immediate(0x80036228,STOCK_SELECTIONS,limit*4);
    immediate(0x8002d1e8,21,limit);
    for(uint32_t a=0x8004efb0;a<0x8004f750;a+=4) {
        uint32_t w=psx_mod_read_word(a),op=w>>26;
        if((op==10 || op==11) && (w&65535)==0x5d)patch(a,w,(w&0xffff0000u)|limit*4);
        if((op==10 || op==11) && (w&65535)==0x16)patch(a,w,(w&0xffff0000u)|limit);
    }
    psx_mod_write_word(T3_CHARACTER_MASK,psx_mod_read_word(T3_CHARACTER_MASK)|extra_bits());
    /* The third-costume flag selects model slot 2, which some donors lack. */
    uint32_t third=psx_mod_read_word(0x80097ef4);
    for(unsigned k=0;k<extra_count;k++) {
        const Tekken3RosterImport *fighter=slots[k].import;
        int has_third=fighter && fighter->models?fighter->models>=3:
            (int)((third>>slots[k].donor)&1);
        if(has_third)third|=1u<<(FIRST_ID+k);
    }
    psx_mod_write_word(0x80097ef4,third);
    if(private_models) {
        indirect(0x800363a0,0x26315a9c,model_sizes);
        static const uint32_t face_refs[][2]={
            {0x80033d14,0x80033d20},{0x80034248,0x80034250},
            {0x8003435c,0x80034364},{0x800343e4,0x800343ec},
            {0x800344d8,0x800344e0}};
        for(unsigned i=0;i<sizeof face_refs/sizeof *face_refs;i++) {
            unsigned reg=i==4?2:3;
            lui_ori(face_refs[i][0],0x3c008009|(reg<<16),face_refs[i][1],
                    0x240059cc|(reg<<21)|(reg<<16),face_profiles);
        }
    }
    if(usage_owner)patch_usage();
    patch_console();
}

static void patch_force_bosses(void) {
    /* BNS record 7 indexes 800B6378[selection] for its {fighter, outfit}
     * routes and has only 92 pointers. Match the loaded code first and
     * rebuild the copy whenever the overlay restores its instructions. */
    if(psx_mod_read_word(0x800b2658)!=0x27bdffb8 ||
       psx_mod_read_word(0x800b2694)!=0x3c05800b ||
       psx_mod_read_word(0x800b2698)!=0x24a56378 ||
       psx_mod_read_word(0x800b26d4)!=0x84720002)return;
    copy_guest(force_bosses,0x800b6378,STOCK_SELECTIONS*4);
    for(unsigned k=0;k<extra_count;k++) {
        const Tekken3RosterImport *fighter=slots[k].import;
        if(fighter && fighter->force_route) {
            /* Four {fighter, outfit} stages; 0xFFFF keeps the native outfit. */
            uint32_t route=force_bosses+0x200+k*16;
            for(unsigned i=0;i<4;i++) {
                psx_mod_write_half(route+i*4,fighter->force_route[i]);
                psx_mod_write_half(route+i*4+2,0xffff);
                psx_mod_write_word(force_bosses+((FIRST_ID+k)*4+i)*4,route);
            }
        } else copy_guest(force_bosses+(FIRST_ID+k)*16,0x800b6378+slots[k].donor*16,16);
    }
    patch(0x800b2694,0x3c05800b,0x3c050000|(force_bosses>>16));
    patch(0x800b2698,0x24a56378,0x34a50000|(force_bosses&65535));
}

/* The selector overlay's 19 references to its node table at 801296C8, and
 * the 64 bytes after it hold other data. */
static const uint32_t node_refs[]={
    0x8010d348,0x8010d3e0,0x8010d604,0x8010d6ec,0x8010d9a4,0x8010da04,0x8010da64,
    0x8010deec,0x8010df10,0x8010df30,0x8010df54,0x8010dfa8,0x8010e0ac,0x8010e0d0,
    0x8010e0f0,0x8010e114,0x8010e168,0x8010f758,0x8010f944};
static void relocate_nodes(void) {
    if(psx_mod_read_word(0x8010d604)!=0x244796c8)return;
    copy_guest(nodes,0x801296c8,STOCK_NODES*NODE_SIZE);
    for(unsigned i=0;i<sizeof node_refs/sizeof *node_refs;i++) {
        uint32_t w=psx_mod_read_word(node_refs[i]);
        if((w&0xfc00ffffu)==0x240096c8u)indirect(node_refs[i],w,nodes);
    }
}
/* Table index of a grid cell. Index 22 is the selector's "no link" value,
 * so a grid larger than 22 cells skips it and leaves a hidden placeholder. */
static unsigned node_index(unsigned cell) {return cell<NO_NODE?cell:cell+1;}

static void write_node(unsigned index,const uint8_t links[4],int visible,unsigned id,int x,int y) {
    uint32_t n=nodes+index*NODE_SIZE;
    psx_mod_write_byte(n,(uint8_t)index);
    for(unsigned i=0;i<4;i++)psx_mod_write_byte(n+1+i,links[i]);
    psx_mod_write_byte(n+5,(uint8_t)visible);
    psx_mod_write_half(n+6,(uint16_t)id);
    psx_mod_write_half(n+8,(uint16_t)x);
    psx_mod_write_half(n+10,(uint16_t)y);
}

/* Lay out stock row 0, stock row 1 plus Gon, then the added fighters
 * alternating between rows so both stay filled. */
static void layout_cabinet(void) {
    int margin=psx_mod_widescreen_x_margin();
    unsigned total=21+extra_count;
    unsigned columns=margin>0?(total+1)/2:11;
    if(columns<11)columns=11;
    if(columns>14)columns=14;
    unsigned cells=2*columns<total?2*columns:total;
    visible_extras=cells-21;
    int pitch=33,x0=3;
    if(columns>11) {
        /* Compressed inside the 368px canvas; the renderer restores 35px. */
        pitch=(368-32-6)/(int)(columns-1);
        x0=(368-(pitch*(int)(columns-1)+32))/2;
    }
    /* The overlay builds its stock nodes from this character list; entries
     * 10 and 21 are Gon's two hidden end-of-row cells. */
    int ids[2][14];int count[2]={10,11};
    for(unsigned c=0;c<10;c++) {
        ids[0][c]=psx_mod_read_byte(0x800b9018+c);
        ids[1][c]=psx_mod_read_byte(0x800b9018+11+c);
    }
    ids[1][10]=GON;
    for(unsigned k=0;k<visible_extras;k++) {
        int row=count[0]<(int)columns && (count[0]<=count[1] || count[1]>=(int)columns)?0:1;
        ids[row][count[row]++]=FIRST_ID+(int)k;
    }
    node_count=cells>NO_NODE?cells+1:STOCK_NODES;
    static const uint8_t none[4]={NO_NODE,NO_NODE,NO_NODE,NO_NODE};
    write_node(NO_NODE,none,0,0xffff,0,0);
    for(unsigned row=0;row<2;row++)for(unsigned col=0;col<columns;col++) {
        unsigned cell=row*columns+col;
        int filled=(int)col<count[row];
        uint8_t links[4];
        /* Up, down, left, right; skip empty cells at the end of a row. */
        unsigned other=1-row,ocol=col<(unsigned)count[other]?col:(unsigned)count[other]-1;
        uint8_t vertical=(uint8_t)node_index(other*columns+ocol);
        links[0]=row?vertical:NO_NODE;
        links[1]=row?NO_NODE:vertical;
        unsigned n=(unsigned)count[row];
        links[2]=(uint8_t)node_index(row*columns+(col+n-1)%n);
        links[3]=(uint8_t)node_index(row*columns+(col+1)%n);
        if(filled)write_node(node_index(cell),links,1,(unsigned)ids[row][col],x0+pitch*(int)col,(int)row*ROW_Y);
        else write_node(node_index(cell),none,0,0xffff,0,0);
    }
    tekken3_selector_declare_cabinet_grid(columns>11?(int)columns:0,x0,pitch,(int)cells);
}

static void patch_cabinet(void) {
    unsigned limit=id_limit();
    relocate_nodes();
    uint32_t mask=0x001fffffu|extra_bits();
    immediate(0x8010d5f0,0x1f,mask>>16);
    immediate(0x8010d9b4,22,limit);immediate(0x8010da30,22,limit);
    immediate(0x8010db38,23,limit);immediate(0x8010dc0c,22,limit);
    immediate(0x8010f7b0,22,limit);
    if(psx_mod_read_half(T3_SCREEN_PHASE)<=1 && psx_mod_read_word(0x80118648)>=21) {
        layout_cabinet();
        immediate(0x8010d9d4,STOCK_NODES,node_count);
        immediate(0x8010da50,STOCK_NODES,node_count);
        immediate(0x8010f840,STOCK_NODES,node_count);
        psx_mod_write_word(0x80118648,21+visible_extras);
        psx_mod_write_word(0x80118644,psx_mod_read_word(0x80118644)|extra_bits());
    }
    /* The panel's descriptor lookup also picks the name strip (row = ID *
     * 16, width = byte 5). The getter is RAM-patched and interpreted, so
     * route this one call through the native return stub at 80052948. */
    patch(0x8010e4b4,0x0c013bfc,0x0c014a52);
    /* Extend the portrait archive directory, not its image payloads: a
     * stand-in points at its donor's record in the resident archive, and an
     * import with its own portrait at the blank one its hook replaces. */
    if(psx_mod_read_word(0x8010e49c)==0x3c02800c &&
       psx_mod_read_word(0x8010e4a4)==0x2455912c &&
       psx_mod_read_word(0x800b912c)==22) {
        psx_mod_write_word(portrait_bank,limit);
        for(unsigned i=0;i<limit;i++) {
            const Tekken3RosterImport *fighter=imported(i);
            unsigned native=i<22?i:i==22 || (fighter && fighter->portrait)?21:tekken3_roster_donor(i);
            uint32_t record=0x800b912c+4+native*8;
            psx_mod_write_word(portrait_bank+4+i*8,0x800b912c+psx_mod_read_word(record)-portrait_bank);
            psx_mod_write_word(portrait_bank+8+i*8,psx_mod_read_word(record+4));
        }
        patch(0x8010e49c,0x3c02800c,0x3c020000|(portrait_bank>>16));
        patch(0x8010e4a4,0x2455912c,0x34550000|(portrait_bank&65535));
    }
}

void tekken3_roster_tick(void) {
    extern uint64_t s_frame_count;
    static uint64_t last_frame=~0ull;
    if(s_frame_count==last_frame)return;
    last_frame=s_frame_count;
    /* The stock tables are copied once the executable is resident. */
    if(!psx_mod_game_started())return;
    if(!attempted)initialize();
    if(!initialized)return;
    patch_executable();
    roster_cpu_patch(cpu_pool);
    patch_force_bosses();
    int cabinet=psx_mod_read_word(T3_SCREEN)==T3_SCREEN_CABINET && tekken3_cabinet_selector_loaded();
    if(cabinet)patch_cabinet();
    else tekken3_selector_declare_cabinet_grid(0,0,0,0);
    /* The outfit carousel lists an import's own outfits under its cabinet cursor. */
    for(unsigned p=0;p<2;p++) {
        unsigned id=cabinet?psx_mod_read_word(0x80118668+p*0x7c):0;
        tekken3_outfits_set_character_override(p,imported(id)?(int)id:-1);
    }
    for(unsigned k=0;k<extra_count;k++)
        if(slots[k].import && slots[k].import->tick)slots[k].import->tick(slots[k].import->context);
}

/* Character-indexed lookups the stock tables cannot answer for IDs 23+.
 * Each runs only at its real entry, not at generated mid-function resumes. */
extern void __real_func_8004B928(CPUState*);
extern void __real_func_8004B790(CPUState*);
extern void __real_func_80052958(CPUState*);
extern void __real_func_80052990(CPUState*);
extern void __real_func_80052948(CPUState*);
extern void __real_func_80052938(CPUState*);
extern void __real_func_80052940(CPUState*);
extern void __real_func_8003F044(CPUState*);
extern void __real_func_800513C0(CPUState*);
extern void __real_func_80051464(CPUState*);
extern void __real_func_8005155C(CPUState*);
extern void __real_func_80051660(CPUState*);

extern void __real_func_80031BFC(CPUState*);
extern void __real_func_80052AD0(CPUState*);
extern void __real_func_8006BF20(CPUState*);
extern void __real_func_8003626C(CPUState*);
extern void __real_func_80036294(CPUState*);
extern void __real_func_800362C4(CPUState*);
extern int tekken3_anna_portrait_decode(CPUState*);

/* Roster icon: the selection word on the stack is character * 4. Import
 * hooks see every call (they may also restore a borrowed stock slot); a
 * stand-in shows its donor's icon. */
void __wrap_func_8004B928(CPUState *cpu) {
    if(initialized && (cpu->pc==0 || cpu->pc==0x8004b928)) {
        uint32_t slot=cpu->gpr[29]+16,word=psx_mod_read_word(slot);
        unsigned id=word>>2;
        int handled=0;
        for(unsigned k=0;k<extra_count;k++) {
            const Tekken3RosterImport *fighter=slots[k].import;
            if(fighter && fighter->icon && fighter->icon(fighter->context,cpu,id))handled=1;
        }
        if(!handled && extra(id) && !imported(id))
            psx_mod_write_word(slot,(tekken3_roster_donor(id)<<2)|(word&3));
    }
    __real_func_8004B928(cpu);
}
/* Portrait image-cache record; a stand-in shares its donor's art. */
void __wrap_func_8004B790(CPUState *cpu) {
    if(initialized && (cpu->pc==0 || cpu->pc==0x8004b790) && extra(cpu->gpr[4])) {
        const Tekken3RosterImport *fighter=imported(cpu->gpr[4]);
        if(fighter && fighter->ui_record) {
            cpu->gpr[2]=fighter->ui_record(fighter->context);cpu->pc=cpu->gpr[31];return;
        }
        cpu->gpr[4]=tekken3_roster_donor(cpu->gpr[4]);
    }
    __real_func_8004B790(cpu);
}
/* Portrait decode: the cabinet panel (RA 8010E574, panel+1C) and the loading
 * screen (RA 8004C814, player in S4) ask importers for their own TIMs. */
void __wrap_func_80031BFC(CPUState *cpu) {
    if(tekken3_anna_portrait_decode(cpu))return;
    if(initialized && (cpu->pc==0 || cpu->pc==0x80031bfc)) {
        unsigned id=0;
        if(cpu->gpr[31]==0x8010e574)id=psx_mod_read_word(cpu->gpr[16]+0x1c);
        else if(cpu->gpr[31]==0x8004c814 && cpu->gpr[20]<2)
            id=psx_mod_read_half(T3_SIDE_CHARACTERS+cpu->gpr[20]*2);
        const Tekken3RosterImport *fighter=imported(id);
        if(fighter && fighter->portrait && fighter->portrait(fighter->context,cpu))return;
    }
    __real_func_80031BFC(cpu);
}
/* Versus/loading loads, including stage data an import replaces. Hooks see
 * generated resumes too and check the entry themselves where it matters. */
void __wrap_func_80052AD0(CPUState *cpu) {
    if(initialized)
        for(unsigned k=0;k<extra_count;k++) {
            const Tekken3RosterImport *fighter=slots[k].import;
            if(fighter && fighter->loading && fighter->loading(fighter->context,cpu))return;
        }
    __real_func_80052AD0(cpu);
}
/* Private models have no disc records: the actor loader requests four
 * records per model (95 + model * 4), served from the donor's model. This
 * also runs during loading and Tekken Force transitions. */
void __wrap_func_8006BF20(CPUState *cpu) {
    if(initialized && private_models && (cpu->pc==0 || cpu->pc==0x8006bf20) && cpu->gpr[4]>=95) {
        const Slot *slot=model_owner((cpu->gpr[4]-95)/4);
        if(slot)cpu->gpr[4]=95+donor_model(FIRST_ID+(unsigned)(slot-slots))*4+(cpu->gpr[4]-95)%4;
    }
    __real_func_8006BF20(cpu);
}
void __wrap_func_8003626C(CPUState *cpu) {
    if(initialized && private_models && (cpu->pc==0 || cpu->pc==0x8003626c)) {
        const Slot *slot=model_owner(cpu->gpr[4]);
        if(slot)cpu->gpr[4]=donor_model(FIRST_ID+(unsigned)(slot-slots));
    }
    __real_func_8003626C(cpu);
}
/* Per-model property bytes at 80095950 and 80095984. */
static int model_property(CPUState *cpu,uint32_t entry,uint32_t table) {
    if(!initialized || !private_models || (cpu->pc!=0 && cpu->pc!=entry))return 0;
    const Slot *slot=model_owner(psx_mod_read_half(cpu->gpr[4]+28));
    if(!slot)return 0;
    cpu->gpr[2]=psx_mod_read_byte(table+donor_model(FIRST_ID+(unsigned)(slot-slots)));
    cpu->pc=cpu->gpr[31];return 1;
}
void __wrap_func_80036294(CPUState *cpu) {
    if(!model_property(cpu,0x80036294,0x80095950))__real_func_80036294(cpu);
}
void __wrap_func_800362C4(CPUState *cpu) {
    if(!model_property(cpu,0x800362c4,0x80095984))__real_func_800362C4(cpu);
}
void __wrap_func_80052958(CPUState *cpu) {
    if(initialized && (cpu->pc==0 || cpu->pc==0x80052958) && extra(cpu->gpr[5]))
        cpu->gpr[5]=tekken3_roster_donor(cpu->gpr[5]);
    __real_func_80052958(cpu);
}
void __wrap_func_80052990(CPUState *cpu) {
    if(initialized && (cpu->pc==0 || cpu->pc==0x80052990) && extra(cpu->gpr[5]))
        cpu->gpr[5]=tekken3_roster_donor(cpu->gpr[5]);
    __real_func_80052990(cpu);
}
/* Console confirmation (80052DE4, entered through the native return stub at
 * 80052940): an added fighter's cell is appended here, as the single added
 * fighter's roster did, then the game's own code continues just after its
 * append with the prologue's saved-register layout. Stock cells run the
 * native routine. */
void __wrap_func_80052940(CPUState *cpu) {
    if(initialized && (cpu->pc==0 || cpu->pc==0x80052940) &&
       (cpu->gpr[31]==0x80053a1c || cpu->gpr[31]==0x80053d90)) {
        uint32_t root=cpu->gpr[4],state=cpu->gpr[5],input=cpu->gpr[6];
        unsigned x=psx_mod_read_word(state+8),y=psx_mod_read_word(state+12);
        unsigned n=psx_mod_read_word(state+44);
        cpu->gpr[29]-=32;psx_mod_write_word(cpu->gpr[29]+20,cpu->gpr[17]);
        unsigned cell=y*console_columns+x;
        unsigned id=cell<console_count?psx_mod_read_byte(console_cells+cell*CELL_SIZE+4)>>2:0;
        if(y<3 && x<console_columns && extra(id) && (input&0xf0) && n<8 &&
           (psx_mod_read_word(state+36)&(1u<<id))) {
            /* Triangle is the third outfit when the fighter has one. */
            int third=(psx_mod_read_word(0x80097ef4)>>id)&1;
            unsigned costume=third && (input&0x10)?2:!!(input&0x60);
            psx_mod_write_word(cpu->gpr[29]+16,cpu->gpr[16]);
            psx_mod_write_word(cpu->gpr[29]+24,cpu->gpr[18]);
            psx_mod_write_word(cpu->gpr[29]+28,cpu->gpr[31]);
            cpu->gpr[17]=root;cpu->gpr[16]=state;cpu->gpr[18]=n;
            cpu->gpr[6]=id*4+costume;
            psx_mod_write_word(state+56+n*4,cpu->gpr[6]);
            cpu->pc=0x80052ed0;return;
        }
        cpu->pc=0x80052dec;return;
    }
    __real_func_80052940(cpu);
}
/* Console navigation: rows wrap horizontally, and moving onto the shorter
 * last row clamps to its end. Resumes after the native movement code. */
void __wrap_func_80052938(CPUState *cpu) {
    if(initialized && (cpu->pc==0 || cpu->pc==0x80052938) &&
       (cpu->gpr[31]==0x80053884 || cpu->gpr[31]==0x80053c10)) {
        int primary=cpu->gpr[31]==0x80053884;
        unsigned buttons=psx_mod_read_half(0x800adf00+cpu->gpr[primary?19:20]*2);
        uint32_t state=cpu->gpr[16];
        int x=(int)psx_mod_read_word(state+8),y=(int)psx_mod_read_word(state+12);
        if(y<0 || y>2)y=0;
        int width=(int)console_row_width(y);
        if(x<0 || x>=width)x=0;
        int dx=(int)((buttons>>13)&1)-(int)((buttons>>15)&1);
        int dy=(int)((buttons>>14)&1)-(int)((buttons>>12)&1);
        if(dx)x=(x+dx+width)%width;
        else if(dy) {
            y+=dy;if(y<0)y=0;if(y>2)y=2;
            width=(int)console_row_width(y);
            if(x>=width)x=width-1;
        }
        cpu->gpr[17]=(uint32_t)x;cpu->gpr[primary?18:19]=(uint32_t)y;
        cpu->pc=primary?0x80053910:0x80053c9c;return;
    }
    __real_func_80052938(cpu);
}
/* Descriptor getters routed through the native return stub at 80052948.
 * The cabinet panel names a stand-in by its donor's stock descriptor; the
 * usage screens show the spare row as its owner. Every other caller
 * continues into the real getter. */
void __wrap_func_80052948(CPUState *cpu) {
    if(initialized && (cpu->pc==0 || cpu->pc==0x80052948)) {
        if(cpu->gpr[31]==0x8010e4bc) {
            if(extra(cpu->gpr[4]) && !imported(cpu->gpr[4])) {
                unsigned selection=tekken3_roster_donor(cpu->gpr[4])*4+(cpu->gpr[5]&3);
                cpu->gpr[2]=psx_mod_read_word(0x80097d40+selection*4);
                cpu->pc=cpu->gpr[31];return;
            }
            cpu->pc=0x8004eff0;return;
        }
        if(usage_owner && cpu->gpr[31]==0x800c1558) {
            if(cpu->gpr[4]==SPARE_USAGE_ROW)cpu->gpr[4]=usage_owner;
            cpu->pc=0x8004eff0;return;
        }
        if(usage_owner && cpu->gpr[31]==0x800dffc8) {
            if(cpu->gpr[4]==SPARE_USAGE_ROW*4)cpu->gpr[4]=usage_owner*4;
            cpu->pc=0x8004efc8;return;
        }
    }
    __real_func_80052948(cpu);
}
/* Damage hurtbox radii: the stock 22-entry profile table at 80096FF0. */
void __wrap_func_8003F044(CPUState *cpu) {
    unsigned id=psx_mod_read_half(cpu->gpr[4]+24);
    if(initialized && (cpu->pc==0 || cpu->pc==0x8003f044) && extra(id)) {
        uint32_t profile=psx_mod_read_word(0x80096ff0+tekken3_roster_donor(id)*4);
        for(unsigned i=0;i<14;i++) {
            unsigned radius=psx_mod_read_half(profile+i*2);
            psx_mod_write_half(cpu->gpr[4]+0x218+i*20,(uint16_t)radius);
            psx_mod_write_word(cpu->gpr[4]+0x21c+i*20,radius*radius);
        }
        cpu->pc=cpu->gpr[31];return;
    }
    __real_func_8003F044(cpu);
}
/* Usage counters: the spare row's owner writes row 21; everyone else added
 * counts towards their donor. The write routines enter generated code just
 * after MULT, so the already-computed product for the division by 22 is
 * updated too. */
static void usage_arguments(CPUState *cpu,int two_players) {
    if(extra(cpu->gpr[4])) {
        unsigned row=usage_row(cpu->gpr[4]);
        uint64_t product=(uint64_t)row*0x2e8ba2e9u;
        cpu->gpr[4]=row;cpu->hi=(uint32_t)(product>>32);cpu->lo=(uint32_t)product;
    }
    if(two_players && extra(cpu->gpr[5]))cpu->gpr[5]=usage_row(cpu->gpr[5]);
}
void __wrap_func_800513C0(CPUState *cpu) {
    if(initialized && (cpu->pc==0 || cpu->pc==0x800513c0))usage_arguments(cpu,0);
    __real_func_800513C0(cpu);
}
void __wrap_func_80051464(CPUState *cpu) {
    if(initialized && (cpu->pc==0 || cpu->pc==0x80051464))usage_arguments(cpu,1);
    __real_func_80051464(cpu);
}
void __wrap_func_8005155C(CPUState *cpu) {
    if(initialized && (cpu->pc==0 || cpu->pc==0x8005155c))usage_arguments(cpu,1);
    __real_func_8005155C(cpu);
}
void __wrap_func_80051660(CPUState *cpu) {
    if(initialized && (cpu->pc==0 || cpu->pc==0x80051660) && extra(cpu->gpr[4]))
        cpu->gpr[4]=usage_row(cpu->gpr[4]);
    __real_func_80051660(cpu);
}

PSX_MOD_CONSTRUCTOR(tekken3_register_roster) {
    (void)psx_mod_register_vblank_plugin("tekken3.extra-roster",tekken3_roster_tick);
}
