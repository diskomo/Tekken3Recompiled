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
| 3rd | 448, 464 | 160 | 495..497 |
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
  mirror, Kazuya against a CPU Jun and CPU Kazuya in Arcade and Survival were
  played.
- Accessories are rigid (no secondary motion), as for Jun's hair and bow.
