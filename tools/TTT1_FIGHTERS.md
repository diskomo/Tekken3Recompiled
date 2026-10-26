# Further TTT1 fighters

Kazuya Mishima joins the shared extra roster as character 24, imported from
the same local Tekken Tag Tournament arcade set as Jun (`tools/JUN_IMPORT.md`).
Jun's import must be done first; it verifies the ROMs and captures the shared
arcade tables.

```bash
python3 tools/import_ttt1_fighter.py kazuya --ttt1 tektagt.zip
cmake -S . -B build-release -DTEKKEN3_JUN_EXPERIMENTAL=ON
cmake --build build-release --target psx-runtime
```

The build stages `workspace/jun-import/kazuya` beside the executable when it
exists, with the **Kazuya Mishima (TTT1)** package. In 4:3 he is selectable from
the console grids (Team Battle); the 11-column cabinet grid shows him in 16:9.
He has both arcade outfits, **Karate Pants** and **Purple Suit**, in the outfit
picker, each with its own card art.

Kunimitsu follows the same way, as character 25 with all three arcade outfits
(**Purple Ninja**, **Gold Ninja**, **Purple Bodysuit**); see
[Kunimitsu](#kunimitsu) for what she needed beyond Kazuya.

```bash
python3 tools/import_ttt1_fighter.py kunimitsu --ttt1 tektagt.zip
```

Angel, Devil, Armor King and Roger follow as characters 26..29 (see their
sections); import each the same way by name (`angel`, `devil`, `armorking`,
`roger`).

## How a locked fighter is imported

Kazuya is locked on fresh arcade NVRAM, but nothing needs unlocking:

- Every fighter's move records stay resident in arcade RAM. Highlighting a
  fighter only rebuilds the alias table at `0x802A7350` from that fighter's
  row of 5515 16-bit move indices at `0x80093780` (row = TTT1 select ID;
  Kazuya is 18). `tools/ttt1_fighters.py` rebuilds it and checks the method
  against the real Jun and Jin highlight captures.
- `tools/capture_ttt1_model.lua` loads any model number into Jun's select
  preview, which yields each outfit's pointer relocations (models 64, 65).
- `tools/capture_ttt1_voice_oracle.lua` requests sound IDs through the H8
  driver and records the C352 key-on registers. Kazuya's eleven samples (sound
  profile 18, shared with Devil) are in `tools/data/ttt1_voices.json`.

`tools/import_ttt1_fighter.py` runs these, then the same motion, combat and
voice converters as Jun, and checks the outputs against
`tools/data/kazuya_runtime_hashes.json`.

## Runtime

The import runtime (`src/tekken3_jun_*.c`) is shared by every fighter in
`src/tekken3_ttt_fighters.c`:

- Imported move IDs start at each fighter's own base (Jun 8192, Kazuya 9216),
  so both can share a match. Each pack's appended reaction rows, pushback
  curves and counter entries are rebased and merged into one table set.
- Cancel rows that require a particular TTT1 opponent (Kazuya has six) are
  enabled per match through the native character byte.
- `tools/plan_ttt1_fighter_layout.py` generates `src/tekken3_kazuya_layout.h`:
  outfit hashes, the texture atlas, face variants from the arcade face table
  `0x801945D8`, and how the 30 arcade model rows fill SLUS-00402's 27.
- Like Jun, he joins the CPU opponent pools (`src/tekken3_roster_cpu.h`) of
  Arcade, Time Attack, Team Battle and Survival.

A fighter's texture atlas normally holds 16384 texels per player: the native
fighter page (x 384..447, rows 0..223 of the player's half of VRAM) and the
face backup (x 448..463, rows 0..127). The Purple Suit needs 20208. For a
costume that does not fit, the planner also uses x 448..495, rows 128..223,
which fights against stock fighters 0..9 left blank apart from those
fighters' own textures (the other eleven are unchecked), and trims every non-face tile to the texels its
polygons use (19024). The runtime uploads and checks only the trimmed part.

Arcade model rows start at +16 with a vertex list whose first reference pass
copies vertices from the mesh drawn just before it, so the draw order must
follow the arcade order. Kazuya's six accessories (arcade rows 24..29) use
actor parts 18..23. SLUS-00402 draws only parts 0..21 and solves accessories
only for 18..21, so the build compiles copies of generated shards 06 and 07
whose two part loops ask `tekken3_ttt_part_limit` for the bound.

## Menu art

`tools/prepare_ttt1_ui.py` builds the same five-image pack and 4bpp name strip
as Jun's. The portrait is Kazuya's own TTT1 select portrait (152x256, arcade
file 57 in the directory at `0x80302B40`; portraits are files 40..77 in
alphabetical order). Tekken 3's selector and loading thumbnails are cropped
from it, as TTT1's own selector does, on a dark red backdrop. The name strip
uses Tekken 3's HUD font: K, U and A from KUMA, Y from BRYAN, and a Z drawn in
the same outline and shading, since no stock name contains one.

`src/tekken3_ttt_ui.c` serves every imported fighter's art; Jun's roster uses
it with her original values. Kazuya's thumbnails use menu scratch columns
416 and 432 (beside Jun's 384 and 400) and CLUT rows 498..500, restored when
menus close. He keeps Jin's statistics row and Tekken Force route.

Every selector and loading screen leaves fighter page `0x86` blank at x
384..511 above row 224, and CLUT rows 470..495. Each imported fighter takes a
16-column pair (selector, loading) 58 rows tall, set by `icon_x`, `loading_x`
and `icon_y` in its row of `src/tekken3_ttt_roster.c`, and three CLUT rows:

| Fighter | Columns | Rows | CLUT rows |
|---|---|---|---|
| Jun | 384, 400 | 160 | 501..503 |
| Kazuya | 416, 432 | 160 | 498..500 |
| Kunimitsu | 448, 464 | 160 | 495..497 |
| 4th | 480, 496 | 160 | 492..494 |
| 5th..8th | as above | 96 | 489..491, 486..488, 483..485, 480..482 |

The roster's selectors already fit more fighters: the VS and Team Battle grid
grows to ten columns (30 cells), and the 16:9 cabinet grid to fourteen (28).

### Adding a fighter

After `tools/ttt1_fighters.py` and `tools/prepare_ttt1_ui.py` have entries for
it and its import has run, a fighter needs:

1. Its generated `src/tekken3_<dir>_layout.h`, an enum value in
   `src/tekken3_ttt_fighter.h` and a row in `tekken3_ttt_fighters`
   (`src/tekken3_ttt_fighters.c`).
2. A row in `src/tekken3_ttt_roster.c`: descriptor name, UI hashes, thumbnail
   slot and CLUT rows from the table above.
3. An entry in `TTT1_IMPORTS` in `CMakeLists.txt`, which stages its assets.
4. A package, `mods/local-packages/tekken3.character.<dir>-probe`, enabling
   the `tekken3.<dir>-probe` and `tekken3.<dir>-roster` plugins. Both are
   registered from the tables, so no code names the fighter.

## Kunimitsu

Her IDs come from the select records at `0x8001EAC0`, which list after each
name the model row, sound profile and select ID: model row 28 (models 58, 59
and 115), sound profile 31, select ID 26. Moves start at 10240.

- **Third outfit.** Model 115 is in the expansion archives, whose textures are
  at bank offset `0x316FCAC` (`export_ttt1_fighter.texture_member`), as Jun's
  third outfit is. A table at `0x8019E3B4` gives the model ID each expansion
  member replaces.
- **No face.** Her mask leaves no face-table entry (`-1`), so her layout has
  `TTT_NO_FACE` and no expression copies. Each outfit's four 16x64 face tiles
  are then unused; the planner drops tiles that no polygon or face uses, and
  the runtime skips their TIM blocks. Trimmed, her outfits need 14216, 13896
  and 15984 texels, inside the native 16384. Tile tables now hold 32 tiles.
- **Blade limb.** One move (alias 1594) sweeps a capsule from limb 6 to arcade
  limb 22, the blade, which SLUS-00402's skeleton lacks. The converter keeps
  the limb 6 sweep and reports `extended_attack_limb`.
- **Third head mesh.** The Purple Bodysuit draws arcade row 23 beside head rows
  21 and 22. It must follow row 22 in the draw order (it copies 46 of its
  vertices), so it takes accessory part 18 and turns with the head joint
  (`head_mesh` in `tekken3_ttt_import.c`). Her hair rows name their parents by
  arcade part (row = part + 6); the runtime resolves those to the parts that
  actually hold the rows.
- **Voices.** Profile 31 has eight samples, 213..220; the voice oracle measured
  frequency 5965 for all of them.
- **Menu art.** Portrait 20 (file 60). The name strip joins whole stock pieces:
  KU from KUMA, N from NINA and IMITSU from YOSHIMITSU. Her thumbnails use menu
  scratch columns 448 and 464 and CLUT rows 495..497.
- **Not a CPU opponent.** Team Battle's candidate list holds two imports, Jun
  and Kazuya, so the roster leaves her out of the random opponent pools.

## Angel

Character 26, one outfit (**White Robe**, model 67). Her select record (model
ID 34, sound profile 24) shares Devil's select ID 27, so she has Devil's move
list under her own move IDs from 11264; model 67 is the fifth entry of
Devil's outfit row. Her nine voice samples (758..766) play at frequency 16384.
Her model needs 19454 texels once trimmed, so it uses the extended texture
space, and her row 23 is a third head mesh like Kunimitsu's. Seven of her hit
capsules reach wing limbs that SLUS-00402 lacks and sweep the remaining limb.
Portrait 2; the name strip joins AN (ANNA), GE (TIGER) and L (LAW). Her
thumbnails take the last pair at row 160 (480, 496) and CLUT rows 492..494.
Like Kunimitsu, she is not a random CPU opponent.

## Devil

Character 27, two outfits (**Purple Devil**, model 66; **Magenta Devil**, model
110). He has Angel's select ID 27, so the same move list, under move IDs from
12288; voice profile 28 has nine samples (612..620) at frequency 16384.

- **Expansion models.** Model 110 is expansion member 7, not 8: a table of 23
  halfwords at `0x8019E3B4` gives the model ID each expansion member replaces
  (members 13 and 16 are Kunimitsu's 115 and Jun's 118, and some members
  update main-archive models). `export_ttt1_fighter.py` reads it.
- **Textures.** Trimmed, each outfit needs 18422 texels plus his face. Only the
  face's destination tile stays in VRAM; expressions copy their two source
  tiles from host memory, as they always have.
- **Accessories.** His wings, tail and horns are five accessory rows, 23..27,
  on parts 18..23, so he draws all 24 parts.
- **Menu art.** Portrait 7. The name strip is D (DOCTOR B.), E (EDDY), a V
  drawn in the HUD font's outline and shading, I (LEI) and L (LAW). His
  thumbnails open the band at row 96 (384, 400) with CLUT rows 489..491.

## Armor King

Character 28, two outfits (**Grey Armor**, model 52; **Blue Armor**, model 53),
select ID 24, move IDs from 13312. He has no face entry (his mask) and five
accessory rows, 24..28, on parts 18..23.

- **Voices.** Profile 25's nine roles use eight samples, 196..203, stored as
  signed 8-bit linear PCM (C352 flag 0) at four frequencies (5630..7093), which
  `prepare_ttt1_voices.py` now decodes per sample. Sample 200 is both his
  second damage cry and his victory voice, so the pack holds an entry for
  each role.
- **Opponent-specific damage.** Three throws hit some opponents with another
  reaction and 19 instead of 20 damage. Those DYNH rows carry both damages in
  their high halves, and the runtime swaps damage with the reaction.
- **Input windows.** Four commands allow 94 or 114 frames of input. SLUS-00402
  keeps 60 frames of input history and its matcher wraps that ring, so the
  converter clamps them to 60 (`input_window_clamped`); they may need slightly
  faster input than in the arcade.
- **Menu art.** Portrait 0. The name strip is AR (HWOARANG), M (KUMA), OR
  (DOCTOR B.), a four-column gap and KING. His thumbnails are 416 and 432 at
  row 96 with CLUT rows 486..488.

## Roger

Character 29, select ID 31 (shared with Alex), move IDs from 14336. His
record precedes Jack-2's at `0x8001EA64`; outfit row 17 holds models 68
(Roger) and 69, which is Alex, so his outfits are **Roger** and **Alex**. Both
use Roger's voice profile 36: six linear-PCM samples, 248..253, at 16871 and
6319.

- **32x32 faces.** His face tiles are 32x32, not 16x64, and the arcade backs
  them up at (176,64). The planner takes the face size from the destination
  tile and records it in `TttFace` when it is not 16x64; expressions copy that
  size.
- **Textures.** 11776 and 9504 texels fit the native space untrimmed.
- **Menu art.** Portrait 33. The name strip is R (BRYAN), O (OGRE) and GER
  (TIGER). His thumbnails are 448 and 464 at row 96 with CLUT rows 483..485.

With him the 16:9 cabinet grid is full (28 cells) and the console grid uses
28 of 30.

## Outfit picker

Every imported fighter's outfits are in the inline carousel (catalog entries
`<fighter>-arcade-<n>`), on the cabinet and console grids. Choosing a fighter
with an attack button opens it, L1/R1 browse and Cross confirms; the chosen
outfit is the costume the fight installs. Each card shows that outfit in game:
a 448x672 full-body crop from a fight start (`mods/assets/outfit-menu`). An
outfit without card art falls back to the fighter's select portrait, labelled
PORTRAIT.

## Not yet done

- VS mode with two imported fighters on two pads is untested; a VS Kazuya
  mirror, Kazuya against a CPU Jun, CPU Kazuya in Arcade and Survival, and
  Kunimitsu against Jin and in all-outfit mirrors were played.
- Only Jun and Kazuya are random CPU opponents: Team Battle's candidate list
  holds two imports, so Kunimitsu, Angel, Devil, Armor King and Roger are left
  out.
- Roger's Alex outfit uses Roger's voice samples, not Alex's profile 23.
- Accessories are rigid (no secondary motion), as for Jun's hair and bow.
