#ifndef TEKKEN3_ROSTER_H
#define TEKKEN3_ROSTER_H
#include <stdint.h>
/* SLUS-00402 extra roster slots. The game keeps character availability in
 * 32-bit masks: IDs 0..20 are stock fighters, 21 the Force enemies and 22 the
 * empty-selection sentinel, which leaves IDs 23..31 for added fighters. */
enum { TEKKEN3_ROSTER_FIRST_ID=23, TEKKEN3_ROSTER_MAX_EXTRA=9 };

struct CPUState;

/* An imported fighter keeps a fixed character ID and may bring its own
 * models and art. Anything it leaves unset is borrowed from its donor, the
 * stock fighter whose disc records, tuning and hit tables back it. */
typedef struct Tekken3RosterImport {
    unsigned id;
    unsigned donor;
    const char *name;            /* descriptor name, at most 15 characters */
    unsigned models;             /* private model IDs, one per outfit; 0 = donor's */
    unsigned usage_row;          /* saved statistics row; 0 = donor's (only 21 is spare) */
    const uint8_t *force_route;  /* four Tekken Force bosses; NULL = donor's route */
    /* Optional hooks, each passed `context`. Those taking a CPU state return
     * nonzero when they handled the call and set its result. */
    void *context;
    void (*describe)(void *context, uint32_t descriptor);       /* adjust the copied descriptor */
    void (*tick)(void *context);                                /* once per frame while live */
    int (*portrait)(void *context, struct CPUState *cpu);       /* 80031BFC: decode its portrait TIM */
    int (*loading)(void *context, struct CPUState *cpu);        /* 80052AD0: versus/loading loads */
    int (*icon)(void *context, struct CPUState *cpu, unsigned id); /* 8004B928: any roster icon */
    uint32_t (*ui_record)(void *context);                       /* 8004B790: image-cache record */
} Tekken3RosterImport;

/* Register from an activation plugin. Imports keep their IDs; stand-ins take
 * the remaining IDs in order. Returns 1 when accepted. */
int tekken3_roster_add_import(const Tekken3RosterImport *fighter);
/* Add a fighter that borrows a stock character's models, moves, art and
 * tuning under its own character ID. */
int tekken3_roster_add_proxy(unsigned donor);
/* Per-frame work; registrants with their own VBlank plugin call this, and a
 * second call in the same frame is ignored. */
void tekken3_roster_tick(void);
int tekken3_roster_live(void);
unsigned tekken3_roster_extra_count(void);
/* The stock character an added fighter borrows from; stock IDs map to
 * themselves. */
unsigned tekken3_roster_donor(unsigned id);
/* First private model ID of an imported fighter, or 0. */
unsigned tekken3_roster_first_model(unsigned id);
#endif
