#!/usr/bin/env python3
"""Generate a TTT1 fighter's runtime layout header from its local export.

Emits outfit hashes, the packed texture tiles (every polygon UV audited to lie
in one source tile), face variants from the arcade face table at 0x801945D8,
and how the 30 arcade model rows fill the 27-row SLUS-00402 header. No game
assets are embedded; only sizes, hashes and coordinates.

Rows start at +16: the vertex list (f0), its shared-vertex references and
weighted saves, then normals, vertices and mesh pointers. A mesh's first
reference pass copies the vertices of the mesh drawn just before it, so the
SLUS-00402 draw order (parts 0..23, each part's two meshes in turn) must keep
every such mesh right after its arcade predecessor. Arcade rows 24..29 are
rigid accessories on parts 18..23; SLUS-00402 itself stops at part 21, and
the runtime extends its two part loops (shards 06/07) for those fighters. A
used row 23, a third head mesh, takes the first accessory part.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct
import sys

try:
    from .plan_ttt1_texture_atlas import pack
    from .ttt1_fighters import FIGHTERS, WORK, select_ram
except ImportError:
    from plan_ttt1_texture_atlas import pack
    from ttt1_fighters import FIGHTERS, WORK, select_ram

# SLUS-00402 part -> header row (8001A05C) and second-mesh flag (8001A044).
PART_ROW = (0, 1, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 24, 25, 26)
TWO_MESH = {1, 2, 17}
ACCESSORY_PARTS = (18, 19, 20, 21, 22, 23)
FACE_TABLE, FACE_RECTS = 0x1945D8, 0x193FE0


def word(data: bytes, offset: int) -> int:
    return struct.unpack_from('<I', data, offset)[0]


def model_row(model: bytes, row: int) -> tuple:
    return struct.unpack_from('<14i', model, 24 + row * 56)


def texture_tiles(texture: bytes) -> list[dict]:
    tiles, o = [], 0
    while o + 8 < len(texture) and texture[o:o + 4] != b'\0\0\0\0':
        flags = word(texture, o + 4)
        o += 8
        o += word(texture, o)
        _, x, y, w, h = struct.unpack_from('<I4H', texture, o)
        tile = dict(x=x, y=y, w=w, h=h, mode=flags & 3, offset=o + 12)
        if not any(all(t[k] == tile[k] for k in ('x', 'y', 'w', 'h', 'mode')) for t in tiles):
            tiles.append(tile)
        o += word(texture, o)
    return tiles


def audit_uvs(model: bytes, tiles: list[dict]) -> int:
    """Every polygon of every row must lie within exactly one texture tile.
    Records each tile's used bounds (16-bit columns, rows) as tile['used']."""
    polygons = 0
    half = lambda a: struct.unpack_from('<H', model, a)[0]
    for row in range(30):
        m = word(model, 32 + row * 56)
        if not m:
            continue
        uv = m + word(model, m)
        src = uv + half(uv)
        for kind in range(4):
            count = model[src]
            src += 1
            for _ in range(count):
                mat = word(model, m + 4 + model[src] * 4)
                src += 1
                mode = (mat >> 23) & 3
                ppw = 4 >> mode
                points = []
                for _ in range(4 if kind & 1 else 3):
                    v = half(uv + 2 + model[src] * 2)
                    src += 1
                    points.append((((mat >> 16) & 15) * 64 + (v & 255) // ppw,
                                   (v >> 8) + ((mat >> 20) & 1) * 256))
                matches = [t for t in tiles if t['mode'] == mode and all(
                    t['x'] <= x < t['x'] + t['w'] and t['y'] <= y < t['y'] + t['h']
                    for x, y in points)]
                if len(matches) != 1:
                    raise ValueError(f'Row {row} polygon spans texture tiles: {points}')
                used = matches[0].get('used', (999, 999, -1, -1))
                xs, ys = [x for x, _ in points], [y for _, y in points]
                matches[0]['used'] = (min(used[0], *xs), min(used[1], *ys),
                                      max(used[2], *xs), max(used[3], *ys))
                polygons += 1
    return polygons


def cropped(tiles: list[dict], keep: set) -> list[dict]:
    """Trim each polygon tile to its used bounds; face tiles stay whole for
    16x64 expression copies, and tiles neither drawn nor a face are dropped.
    The runtime uploads only the planned texels."""
    out = []
    for t in tiles:
        if (t['x'], t['y']) in keep:
            out.append(dict(t))
            continue
        if 'used' not in t:
            continue
        x0, y0, x1, y1 = t['used']
        out.append(dict(t, x=x0, y=y0, w=x1 - x0 + 1, h=y1 - y0 + 1,
                        offset=t['offset'] + ((y0 - t['y']) * t['w'] + x0 - t['x']) * 2))
    return out


NO_FACE = 0xFFFF  # TTT_NO_FACE: the model has no face table entry (a mask)


def face(ram: bytes, model_id: int, tiles: list[dict]) -> dict:
    entry = struct.unpack_from('<8b', ram, FACE_TABLE + model_id * 8)
    if entry[0] < 0:
        return dict(x=NO_FACE, y=NO_FACE, sources=[], w=16, h=64)
    rect = lambda i: struct.unpack_from('<2H', ram, FACE_RECTS + i * 4)
    x, y = rect(entry[0])
    # Entry 2 is where the arcade backs up the destination; the runtime
    # restores variant 0 from the pack instead. Faces are 16x64, or 32x32
    # for Roger; every source matches the destination tile's size.
    dest = [t for t in tiles if t['mode'] == 1 and (t['x'], t['y']) == (x, y)]
    if len(dest) != 1:
        raise ValueError(f'Model {model_id}: no face tile at ({x},{y})')
    w, h = dest[0]['w'], dest[0]['h']
    sources = [rect(i) for i in entry[3:] if i >= 0]
    for sx, sy in sources:
        if not any(t['mode'] == 1 and t['x'] == sx and t['y'] == sy and t['w'] == w and t['h'] == h
                   for t in tiles):
            raise ValueError(f'Model {model_id}: no {w}x{h} face tile at ({sx},{sy})')
    return dict(x=x, y=y, sources=sources, w=w, h=h)


def predecessor_refs(model: bytes, row: int) -> int:
    """Shared vertices a row copies from the mesh drawn just before it."""
    vertices = word(model, 16 + row * 56)
    return word(model, vertices + 4) if vertices else 0


def draw_order(rows: list[int], parts: int) -> list[int]:
    order = []
    for part in range(1, parts):
        order.append(rows[PART_ROW[part]])
        if part in TWO_MESH:
            order.append(rows[PART_ROW[part] + 1])
    return order


def row_plan(models: list[bytes]) -> tuple[list[int], int, list[str]]:
    used = lambda r: any(word(m, 16 + r * 56) for m in models)
    rows = [0, 1, 2 if used(2) else 3, 4, 5 if used(5) else 6] + list(range(7, 23))
    if used(2) and used(3) or used(5) and used(6):
        raise ValueError('Rows 2/3 or 5/6 need more mesh slots than the native parts have')
    # Row 23 is a third head mesh (Kunimitsu's third outfit) beside rows 21
    # and 22, which fill the head's two native slots. It takes the first
    # accessory part, right after them, and the runtime turns it with the head.
    accessories = [r for r in range(23, 30) if used(r)]
    spare = [r for r in range(23, 30) if not used(r)]
    if len(accessories) > 6 or not spare:
        raise ValueError('More accessory rows than the six accessory parts')
    rows += accessories + [spare[0]] * (6 - len(accessories))
    parts = 24 if len(accessories) > 4 else 22
    notes = []
    for i, m in enumerate(models):
        drawn = [r for r in draw_order(rows, parts) if word(m, 16 + r * 56)]
        arcade = [r for r in range(30) if word(m, 16 + r * 56)]
        for k, r in enumerate(drawn):
            before = arcade[arcade.index(r) - 1] if arcade.index(r) else None
            if predecessor_refs(m, r) and (not k or drawn[k - 1] != before):
                raise ValueError(f'P{i + 1} row {r} would not follow row {before}')
        notes.append(f'P{i + 1}: draw order {drawn}')
    return rows, parts, notes


def header(key: str, outfits: list[int], work: Path = WORK) -> tuple[str, list[str]]:
    fighter = FIGHTERS[key]
    ram = select_ram(work, fighter)
    out = fighter.out(work)
    specs, tables, faces, models, notes = [], [], [], [], []
    for outfit in outfits:
        stem = out / f'{fighter.name}-TTT1-arcade-P{outfit}'
        model, texture, relocs = (stem.with_suffix(s).read_bytes() for s in ('.3dm', '.tim', '.relocs'))
        tiles = texture_tiles(texture)
        polygons = audit_uvs(model, tiles)
        if len(tiles) > 32:
            raise ValueError('More than 32 texture tiles')
        faces.append(face(ram, fighter.models[outfit - 1], tiles))
        try:
            tables.append(pack(tiles))
        except ValueError:
            # Expressions copy each source from host memory into the face
            # destination, so undrawn source tiles may also stay off VRAM.
            dest = {(faces[-1]['x'], faces[-1]['y'])}
            try:
                tables.append(pack(cropped(tiles, dest | set(faces[-1]['sources']))))
            except ValueError:
                tables.append(pack(cropped(tiles, dest)))
                notes.append(f'P{outfit}: face expression sources stay in host memory')
            dropped = len(tiles) - len(tables[-1])
            notes.append(f'P{outfit}: tiles trimmed to their used texels '
                         f'({sum(t["w"] * t["h"] for t in tables[-1])} of '
                         f'{sum(t["w"] * t["h"] for t in tiles)})'
                         + (f', {dropped} unused tiles dropped' if dropped else ''))
        sha = lambda d: hashlib.sha256(d).hexdigest()
        specs.append((len(model), len(texture), len(relocs) // 4, sha(model), sha(texture), sha(relocs)))
        models.append(model)
        notes.append(f'P{outfit}: {len(tiles)} tiles, {polygons} polygons in single tiles')
    rows, parts, row_notes = row_plan(models)
    notes += row_notes
    name, macro, n = fighter.key, fighter.key.upper(), len(outfits)
    lines = [f'/* Generated by tools/plan_ttt1_fighter_layout.py {key}; no game assets embedded. */',
             f'#ifndef TEKKEN3_{macro}_LAYOUT_H', f'#define TEKKEN3_{macro}_LAYOUT_H',
             '#include "tekken3_ttt_fighter.h"',
             f'enum {{ {macro}_OUTFITS={n} }};',
             f'static const TttCostumeSpec {name}_costume_specs[{n}]={{']
    lines += ['    {%u,%u,%u,"%s","%s","%s"},' % s for s in specs]
    lines += ['};', f'static const unsigned {name}_texture_counts[{n}]={{{",".join(str(len(t)) for t in tables)}}};',
              f'static const TttTextureTile {name}_texture_tiles[{n}][32]={{']
    for table in tables:
        lines.append('    {')
        lines += ['        {' + ','.join(str(t[k]) for k in ('x', 'y', 'w', 'h', 'mode', 'offset', 'dx', 'dy')) + '},'
                  for t in table]
        lines.append('    },')
    lines += ['};', f'static const TttFace {name}_faces[{n}]={{']
    for f in faces:
        sources = ','.join('{%u,%u}' % s for s in f['sources']) or '{0,0}'
        size = '' if (f['w'], f['h']) == (16, 64) else ',%u,%u' % (f['w'], f['h'])
        lines.append('    {%u,%u,%u,{%s}%s},' % (f['x'], f['y'], len(f['sources']), sources, size))
    lines += ['};', f'#define {macro}_ROWS {{{",".join(map(str, rows))}}}',
              f'#define {macro}_ACCESSORY_FIRST {ACCESSORY_PARTS[0]}',
              f'#define {macro}_ACCESSORY_LAST {ACCESSORY_PARTS[parts - 19]}',
              f'#define {macro}_PARTS {parts}']
    lines += ['/* ' + note + ' */' for note in notes]
    lines.append('#endif')
    return '\n'.join(lines) + '\n', notes


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('fighter', choices=sorted(FIGHTERS))
    p.add_argument('--outfits', default='1', help='comma-separated outfit numbers to ship')
    p.add_argument('--output', type=Path)
    a = p.parse_args()
    text, notes = header(a.fighter, [int(x) for x in a.outfits.split(',')])
    if a.output:
        a.output.write_text(text)
    else:
        sys.stdout.write(text)
    for note in notes:
        print(note, file=sys.stderr)


if __name__ == '__main__':
    main()
