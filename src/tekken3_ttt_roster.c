/* The TTT1 imports' entries in the shared extra roster, one row each. Each
 * fighter keeps its tekken3_ttt_fighters roster ID with one private model per
 * imported outfit. The roster (tekken3_roster.c) extends the game's tables
 * and both selectors; this file supplies the menu art: the TTT1 portrait,
 * thumbnails cropped from it and a Tekken 3 name strip
 * (tools/prepare_ttt1_ui.py). Each keeps Jin's statistics row and Tekken
 * Force route. Jun, with her own stage and route, is tekken3_jun_roster.c. */
#include "mod_plugins.h"
#include "psx_runtime.h"
#include "tekken3_roster.h"
#include "tekken3_ttt_fighter.h"
#include "tekken3_ttt_ui.h"
#include <stdio.h>

typedef struct TttRosterEntry {
    unsigned fighter;            /* tekken3_ttt_fighters index */
    const char *title;           /* descriptor name, at most 15 characters */
    TttUi ui;                    /* pack and name files are set on activation */
    char plugin[48], pack[48], name[48];
    Tekken3RosterImport import;
    int registered;
} TttRosterEntry;

/* Thumbnail scratch columns pair up in two bands, x 384..511: row 160 holds
 * Jun (384, 400), Kazuya, Kunimitsu and Angel; row 96 holds Devil. CLUT rows
 * run down from Jun's 501..503, three per fighter. Both are restored when
 * menus close (TTT1_FIGHTERS.md lists the slots). */
static TttRosterEntry entries[]={
    {TTT_KAZUYA,"KAZUYA",{
        .pack_sha="947319f0c87670fab34df78262ad1ce1481017408327229984550017d4be9011",
        .name_sha="d86a31394f6234b059b3c8b37de0deb15ed25766ed56fee24ca05bca2959688e",
        .name_width=52,.icon_x=416,.loading_x=432,.grey_row=498,.selector_row=499,.loading_row=500,
        .color=0x6b617a75}},
    {TTT_KUNIMITSU,"KUNIMITSU",{
        .pack_sha="0d3f762c47249b1de473cc6b1a4ce75a38155bf269c4e544987dd131fdf50cb5",
        .name_sha="d79385735df8baa762a854faf4c80f22645a59f48df93bc72687fa8b1baec1c2",
        .name_width=68,.icon_x=448,.loading_x=464,.grey_row=495,.selector_row=496,.loading_row=497,
        .color=0x6b756e4b}},
    {TTT_ANGEL,"ANGEL",{
        .pack_sha="3ddcc5ee45df41afce8fce1c1183259bad4bbefae93876367a92fbdfe2a2cfee",
        .name_sha="a9f4d5f2a8b623ecfaee3bfe5654ad7ba120daf469a43380daadfe78fd2403f8",
        .name_width=40,.icon_x=480,.loading_x=496,.grey_row=492,.selector_row=493,.loading_row=494,
        .color=0x6c65674e}},
    {TTT_DEVIL,"DEVIL",{
        .pack_sha="a090e64dadaf3bd52af92e82a59c9281e409ccd31d0936894917fcdcf3bb8c7e",
        .name_sha="6fa8a7eb488f294c1b5cb97f1e706714fc3fc5479d8ce09bc3e25d8ee7260410",
        .name_width=44,.icon_x=384,.loading_x=400,.icon_y=96,.grey_row=489,.selector_row=490,.loading_row=491,
        .color=0x6c697665}},
};
enum { ENTRY_COUNT=sizeof entries/sizeof entries[0] };

void tekken3_ttt_roster_tick(unsigned fighter) {
    for(unsigned i=0;i<ENTRY_COUNT;i++)
        if(entries[i].fighter==fighter && entries[i].registered)tekken3_roster_tick();
}
static void describe(void *e,uint32_t desc) {tekken3_ttt_ui_describe(&((TttRosterEntry *)e)->ui,desc);}
static void tick(void *e) {tekken3_ttt_ui_tick(&((TttRosterEntry *)e)->ui);}
static int portrait(void *e,CPUState *cpu) {return tekken3_ttt_ui_portrait(&((TttRosterEntry *)e)->ui,cpu);}
static int loading(void *e,CPUState *cpu) {return tekken3_ttt_ui_loading(&((TttRosterEntry *)e)->ui,cpu);}
static int icon(void *e,CPUState *cpu,unsigned id) {return tekken3_ttt_ui_icon(&((TttRosterEntry *)e)->ui,cpu,id);}
static uint32_t ui_record(void *e) {return ((TttRosterEntry *)e)->ui.record;}

static void activate(void *context) {
    TttRosterEntry *e=context;
    const TttFighter *f=&tekken3_ttt_fighters[e->fighter];
    snprintf(e->pack,sizeof e->pack,"%s-TTT1-ui.jui",f->name);
    snprintf(e->name,sizeof e->name,"%s-TTT1-name.4bpp",f->name);
    e->ui.fighter=f;
    e->ui.pack=e->pack;
    e->ui.name=e->name;
    if(!tekken3_ttt_ui_load(&e->ui)){psx_mod_log("%s roster: private UI pack failed validation\n",f->name);return;}
    e->import=(Tekken3RosterImport){
        .id=f->roster_id,.donor=9,.name=e->title,.models=f->outfits,.context=e,
        .describe=describe,.tick=tick,.portrait=portrait,.loading=loading,.icon=icon,
        .ui_record=ui_record};
    e->registered=tekken3_roster_add_import(&e->import);
}
PSX_MOD_CONSTRUCTOR(register_ttt_roster) {
    for(unsigned i=0;i<ENTRY_COUNT;i++) {
        TttRosterEntry *e=&entries[i];
        snprintf(e->plugin,sizeof e->plugin,"tekken3.%s-roster",tekken3_ttt_fighters[e->fighter].dir);
        (void)psx_mod_register_activation_plugin_with(e->plugin,activate,e);
    }
}
