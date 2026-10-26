#ifndef TEKKEN3_GUEST_H
#define TEKKEN3_GUEST_H
/* SLUS-00402 guest state that several mods read. Addresses one file uses
 * stay beside the code that uses them. */
#include <stdint.h>
#include "mod_plugins.h"

/* Current screen (word) and the phase within it (half). */
#define T3_SCREEN 0x800ae204u
#define T3_SCREEN_PHASE 0x800ae224u
enum {
    T3_SCREEN_FIGHT=8,
    T3_SCREEN_CABINET=9,     /* arcade-style selector with large portraits */
    T3_SCREEN_GRID=10,       /* VS and Team Battle grid selector */
    T3_SCREEN_LOADING=11,
};
/* Battle mode (word): 0..5 regular battles (5 = Practice), 7/8 own their stage. */
#define T3_GAME_MODE 0x800afa88u

/* The two player actors. */
#define T3_ACTORS 0x800a9228u
#define T3_ACTOR_STRIDE 0x188cu
#define T3_ACTOR(player) (T3_ACTORS+(uint32_t)(player)*T3_ACTOR_STRIDE)
#define T3_ACTOR_P2 T3_ACTOR(1)
enum {
    T3_ACTOR_COSTUME=0x14,   /* half; low two bits */
    T3_ACTOR_CHARACTER=0x18, /* half */
};

/* Character ID per side (half each), read by loading and versus screens. */
#define T3_SIDE_CHARACTERS 0x800add5cu
/* Character availability mask (word): bit per character ID. */
#define T3_CHARACTER_MASK 0x80097ef0u
/* P1's character on the selector (byte). */
#define T3_P1_CHARACTER 0x80098106u
/* Sine table indexed by ((angle>>3)&0x1ffe). */
#define T3_SINE_TABLE 0x8001e8c4u

/* The cabinet selector overlay is resident: its entry still begins
 * addiu sp,sp,-48. */
static inline int tekken3_cabinet_selector_loaded(void) {
    return psx_mod_read_word(0x8010ff4cu)==0x27bdffd0u;
}
#endif
