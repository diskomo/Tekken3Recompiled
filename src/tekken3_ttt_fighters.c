/* The TTT1 fighters this build can import. Only sizes, hashes and layout
 * coordinates of the private local packs are compiled in. */
#include "tekken3_ttt_fighter.h"

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
};
