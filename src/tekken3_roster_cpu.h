/* SLUS-00402 CPU opponent pools. Included after the roster's guarded patch
 * helper; the offline harness exercises these same patches on disc code. */
#ifndef TEKKEN3_ROSTER_CPU_H
#define TEKKEN3_ROSTER_CPU_H
#include <stdint.h>

/* Team Battle collects its candidates in 22 eight-byte stack slots and the
 * stock Team mask holds 20 fighters, so at most two added fighters fit. */
enum { TEKKEN3_ROSTER_CPU_POOL_MAX=2 };

/* The shared roster extends the per-character CPU tuning table (spacing and
 * response, indexed by both the CPU's and its opponent's IDs). These patches
 * let Arcade, Team Battle and Survival choose the fighters in `pool` (bits of
 * added character IDs, 23 and up) as opponents. Leave it 0 to keep stock. */
static void roster_cpu_patch(uint32_t pool) {
    if(!pool)return;
    unsigned limit=32;
    while(!(pool>>(limit-1)&1))limit--;  /* one past the highest pooled ID */
    unsigned high=pool>>16;

    /* BNS record 0: reset the per-run counters, one per ID below `limit`,
     * when starting a new Team Battle or Survival run. Team counters start
     * at +74 and Survival's at +60 in the mode union; with 25 IDs they end
     * at +A5 and +91, inside Arcade's opponent route (+90..BC), which
     * neither mode uses. Never change the value 22 used as the empty-
     * selection sentinel. */
    if(psx_mod_read_word(0x800ca4d0)==0xa4600074 &&
       psx_mod_read_word(0x800ca5a4)==0xa4800060 &&
       psx_mod_read_word(0x800ca5b8)==0x24030016) {
        patch(0x800ca490,0x24100015,0x24100000|(limit-1));
        patch(0x800ca494,0x2623002a,0x26230000|(limit-1)*2);
        patch(0x800ca544,0x24100015,0x24100000|(limit-1));
        patch(0x800ca548,0x2624002a,0x26240000|(limit-1)*2);
    }

    /* BNS record 5, NOT the Tekken Force overlay at the same addresses.
     * Keep all original exclusions, unlocked-fighter filtering, RNG,
     * anti-repeat weighting and fixed Arcade bosses. Only add the pool. */
    if(psx_mod_read_word(0x800b1fcc)!=0x27bdffb0 ||
       psx_mod_read_word(0x800b225c)!=0x27bdff00 ||
       psx_mod_read_word(0x800b26d0)!=0x27bdff30)return;
    patch(0x800b2030,0x3c03001f,0x3c03001f|high); /* Arcade */
    patch(0x800b2034,0x3c020015,0x3c020015|high); /* Time Attack */
    patch(0x800b20bc,0x28620016,0x28620000|limit);
    patch(0x800b2164,0x28620016,0x28620000|limit);
    patch(0x800b2268,0x3c02001f,0x3c02001f|high); /* Team Battle */
    patch(0x800b2344,0x2a020016,0x2a020000|limit);
    patch(0x800b23e4,0x28620016,0x28620000|limit);
    patch(0x800b24c0,0x28620016,0x28620000|limit);
    patch(0x800b2574,0x28620016,0x28620000|limit);
    /* Fold the native 0x157fff intersection into the three tier masks,
     * then OR the pool into the result before recent-opponent exclusions.
     * This stays entirely in the loaded overlay: Expansion 1 allocations
     * are DATA at 0x9F..., outside a MIPS J's 0x8... target segment.
     * Native pools: 0x3ff, 0xfbfff & 0x157fff, 0x1fffff & 0x157fff. */
    patch(0x800b26f8,0x3c08001f,0x3c080015);
    patch(0x800b2704,0x3508ffff,0x35087fff);
    patch(0x800b2708,0x3c08000f,0x3c080005);
    patch(0x800b270c,0x3508bfff,0x35083fff);
    patch(0x800b2710,0x3c020015,0x3c020000|high);
    patch(0x800b2714,0x34427fff,0x00000000);
    patch(0x800b2718,0x01024024,0x01024025);
    patch(0x800b2798,0x28620016,0x28620000|limit); /* Survival candidates */
    patch(0x800b16c8,0x28a20016,0x28a20000|limit); /* Survival totals */
}
#endif
