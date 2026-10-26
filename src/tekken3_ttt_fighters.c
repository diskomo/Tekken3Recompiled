/* The TTT1 fighters this build can import. Only sizes, hashes and layout
 * coordinates of the private local packs are compiled in. */
#include "tekken3_ttt_fighter.h"
#include "tekken3_kazuya_layout.h"
#include "tekken3_kunimitsu_layout.h"
#include "tekken3_angel_layout.h"

/* Arcade face table 801945D8 for models 46, 47 and 118. */
static const TttFace jun_faces[3]={
    {16,64,1,{{0,0}}},{16,0,1,{{0,0}}},{48,0,1,{{0,0}}},
};

const TttFighter tekken3_ttt_fighters[TTT_FIGHTER_COUNT]={
    {.name="Jun",.dir="jun",.env="TEKKEN3_JUN_ASSETS",.roster_id=23,.source_key=28,.base_id=8192,
     .outfits=JUN_COSTUME_COUNT,.costumes=jun_costume_specs,.texture_counts=jun_texture_counts,
     .texture_tiles=jun_texture_tiles,.faces=jun_faces,
     /* Empty arcade placeholders 2, 5 and 28 are dropped. */
     .rows={0,1,3,4,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,29},
     .accessory_first=19,.accessory_last=22,.parts=22,
     .voice_size=527148,.voice_sha="a83562e4efd6d6d0db0e23ed8a7a6cb4cb5972e74fad66d873d3122ef81a0be3"},
    {.name="Kazuya",.dir="kazuya",.env="TEKKEN3_KAZUYA_ASSETS",.roster_id=24,.source_key=18,.base_id=9216,
     .outfits=KAZUYA_OUTFITS,.costumes=kazuya_costume_specs,.texture_counts=kazuya_texture_counts,
     .texture_tiles=kazuya_texture_tiles,.faces=kazuya_faces,.rows=KAZUYA_ROWS,
     .accessory_first=KAZUYA_ACCESSORY_FIRST,.accessory_last=KAZUYA_ACCESSORY_LAST,
     .parts=KAZUYA_PARTS,
     .voice_size=826192,.voice_sha="0ed82a63a93357c261eeed0121a18eb9e4d9e5b78b0b39934b8e6ebe22b3a7d4"},
    {.name="Kunimitsu",.dir="kunimitsu",.env="TEKKEN3_KUNIMITSU_ASSETS",.roster_id=25,.source_key=26,.base_id=10240,
     .outfits=KUNIMITSU_OUTFITS,.costumes=kunimitsu_costume_specs,.texture_counts=kunimitsu_texture_counts,
     .texture_tiles=kunimitsu_texture_tiles,.faces=kunimitsu_faces,.rows=KUNIMITSU_ROWS,
     .accessory_first=KUNIMITSU_ACCESSORY_FIRST,.accessory_last=KUNIMITSU_ACCESSORY_LAST,
     .parts=KUNIMITSU_PARTS,
     .voice_size=367632,.voice_sha="a639af2793c4893c5d402ea28e4909be56ec01b174aca6672f352b32a05db6e4"},
    {.name="Angel",.dir="angel",.env="TEKKEN3_ANGEL_ASSETS",.roster_id=26,.source_key=27,.base_id=11264,
     .outfits=ANGEL_OUTFITS,.costumes=angel_costume_specs,.texture_counts=angel_texture_counts,
     .texture_tiles=angel_texture_tiles,.faces=angel_faces,.rows=ANGEL_ROWS,
     .accessory_first=ANGEL_ACCESSORY_FIRST,.accessory_last=ANGEL_ACCESSORY_LAST,
     .parts=ANGEL_PARTS,
     .voice_size=567396,.voice_sha="2cedb528e5cb9f068e96ed2f8e5e4a4536939a54b6e48e449c744082d77decac"},
};
