/* Jun's entry in the shared extra roster: character 23, models 52..54. The
 * roster (tekken3_roster.c) extends the game's tables, both selectors and the
 * saved statistics and adds her to the CPU opponent pools; this file supplies
 * her private art, stage and Tekken Force route. Original disc data is never
 * changed. */
#include "mod_plugins.h"
#include "psx_runtime.h"
#include "tekken3_jun_stage.h"
#include "tekken3_jun_ui_layout.h"
#include "tekken3_ttt_ui.h"
#include "tekken3_roster.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { JUN_ID=23 };
static TttUi ui={.pack="Jun-T3-ui.jui",.name="Jun-T3-name.4bpp",
    .pack_sha="f4c2f82a5712cbb35b4f038e09b8c0dea57b3b684975fe9ce2f23ed83f91fe5d",
    .name_sha="76d2af59e794ad6dc8b4eeaf78f39a0cc96b3636e08266208a72b67c692e5c81",
    .name_width=24,.icon_x=ICON_X,.loading_x=LOADING_ICON_X,.grey_row=JUN_GREY_ROW,
    .selector_row=JUN_SELECTOR_ROW,.loading_row=JUN_LOADING_ROW,.color=0xead8b397,.usage_owner=1};
static int registered,enabled=-1;

int tekken3_jun_roster_enabled(void) {
    return registered && tekken3_roster_live();
}

static void describe(void *context,uint32_t desc) {
    (void)context;
    tekken3_ttt_ui_describe(&ui,desc);
    /* A distinct arena ID preserves stock Jin and Forest stages. Mode-owned
     * arenas (Ball/Force) retain their native overrides. Keep Jin's music. */
    psx_mod_write_byte(desc+10,(uint8_t)tekken3_jun_stage_initialize());
    psx_mod_write_byte(desc+11,JUN_STAGE_MUSIC);
}
static void tick(void *context) {
    (void)context;
    tekken3_jun_stage_tick();
    tekken3_ttt_ui_tick(&ui);
}
static int portrait(void *context,CPUState *cpu) {(void)context;return tekken3_ttt_ui_portrait(&ui,cpu);}
static int loading(void *context,CPUState *cpu) {
    (void)context;
    return tekken3_jun_stage_load(cpu) || tekken3_ttt_ui_loading(&ui,cpu);
}
static int icon(void *context,CPUState *cpu,unsigned id) {(void)context;return tekken3_ttt_ui_icon(&ui,cpu,id);}
static uint32_t ui_record(void *context) {(void)context;return ui.record;}

/* Xiaoyu, Yoshimitsu, Jin, Heihachi. */
static const uint8_t force_route[4]={7,4,9,13};
static const Tekken3RosterImport jun={
    .id=JUN_ID,.donor=9,.name="JUN",.models=3,.usage_row=21,.force_route=force_route,
    .describe=describe,.tick=tick,.portrait=portrait,.loading=loading,.icon=icon,
    .ui_record=ui_record};

void tekken3_jun_roster_tick(void) {
    if(registered)tekken3_roster_tick();
}
static void activate(void) {
    if(enabled<0){const char *p=getenv("TEKKEN3_JUN_ROSTER");enabled=!p || strcmp(p,"0");}
    if(!enabled)return;
    ui.fighter=&tekken3_ttt_fighters[TTT_JUN];
    if(!tekken3_ttt_ui_load(&ui)){psx_mod_log("Jun roster: private UI pack failed validation\n");return;}
    registered=tekken3_roster_add_import(&jun);
}
PSX_MOD_CONSTRUCTOR(register_jun_roster) {
    (void)psx_mod_register_activation_plugin("tekken3.jun-roster",activate);
}
