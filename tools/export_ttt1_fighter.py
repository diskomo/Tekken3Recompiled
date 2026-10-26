#!/usr/bin/env python3
"""Export any TTT1 fighter's outfits and move aliases from local arcade data.

Works for fighters still locked on a fresh NVRAM. Every fighter's move
records stay resident in arcade RAM; highlighting one only rebuilds the
alias table at 0x802A7350 from its row of 16-bit move indices at
0x80093780. Outfit relocations come from tools/capture_ttt1_model.lua.
Outputs go to the ignored workspace/jun-import directory; no ROM data is committed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

try:
    from .prepare_jun_import import BANK_SHA, check_textures, model_relocations, verified
    from .plan_ttt1_texture_atlas import pack
    from .ttt1_fighters import ALIASES, ALIAS_COUNT, FIGHTERS, ROOT, WORK, aliases_for
except ImportError:
    from prepare_jun_import import BANK_SHA, check_textures, model_relocations, verified
    from plan_ttt1_texture_atlas import pack
    from ttt1_fighters import ALIASES, ALIAS_COUNT, FIGHTERS, ROOT, WORK, aliases_for

MODEL_ARC, EXPANSION_ARC = 0x014DA698, 0x0320A760
TEXTURE_ARC, EXPANSION_TEXTURE_ARC = 0x111BF04, 0x0316FCAC
# The model ID each expansion member replaces, as 23 halfwords in arcade RAM:
# member 7 is model 110 (Devil's second outfit), 16 is Jun's 118, and some
# members update main-archive models such as 76.
EXPANSION_IDS, EXPANSION_COUNT = 0x19E3B4, 23
PREVIEW = 0x80354108


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def arc_member(bank: bytes, arc: int, index: int) -> bytes:
    count = struct.unpack_from("<I", bank, arc)[0]
    if not 0 <= index < count:
        raise ValueError(f"ARC 0x{arc:x} has no member {index}")
    offset, size = struct.unpack_from("<II", bank, arc + 4 + index * 8)
    return bank[arc + offset:arc + offset + size]


def expansion_member(ram: bytes, model: int) -> int | None:
    ids = struct.unpack_from(f"<{EXPANSION_COUNT}H", ram, EXPANSION_IDS)
    return ids.index(model) if model in ids else None


def model_member(bank: bytes, ram: bytes, model: int) -> bytes:
    member = expansion_member(ram, model)
    if member is not None:
        return arc_member(bank, EXPANSION_ARC, member)
    return arc_member(bank, MODEL_ARC, model)


def texture_member(bank: bytes, ram: bytes, model: int) -> bytes:
    """Expansion models keep their textures in a second archive under the
    same member index (Jun's 118 is member 16 at 0x3200FDC)."""
    member = expansion_member(ram, model)
    if member is not None:
        return arc_member(bank, EXPANSION_TEXTURE_ARC, member)
    return arc_member(bank, TEXTURE_ARC, model)


def audit_aliases(work: Path) -> list[str]:
    """Check the rebuild against real highlight captures when they exist."""
    checked = []
    for name, select_id in (("jun-select-ram.bin", 28), ("jin-select-ram.bin", 9)):
        path = work / name
        if not path.is_file():
            continue
        ram = path.read_bytes()
        if aliases_for(ram, select_id) != ram[ALIASES:ALIASES + ALIAS_COUNT * 4]:
            raise ValueError(f"{name}: alias rebuild disagrees with the arcade")
        checked.append(name)
    return checked


def texture_tiles(texture: bytes) -> list[dict]:
    """Unique tiles in the form plan_ttt1_texture_atlas.pack expects."""
    tiles, o = [], 0
    while o + 8 < len(texture) and texture[o:o + 4] != b"\0\0\0\0":
        flags = struct.unpack_from("<I", texture, o + 4)[0]
        o += 8
        o += struct.unpack_from("<I", texture, o)[0]
        _, x, y, w, h = struct.unpack_from("<I4H", texture, o)
        tile = dict(x=x, y=y, w=w, h=h, mode=flags & 3, offset=o + 12)
        if not any(all(t[k] == tile[k] for k in ("x", "y", "w", "h", "mode")) for t in tiles):
            tiles.append(tile)
        o += struct.unpack_from("<I", texture, o)[0]
    return tiles


def export(name: str, select_id: int, models: list[int], captures: Path, work: Path) -> dict:
    bank = verified(work / "ttt1/bankedroms.bin", BANK_SHA)
    audited = audit_aliases(work)
    out = work / name.lower()
    out.mkdir(parents=True, exist_ok=True)
    outfits = []
    for outfit, model_id in enumerate(models, 1):
        capture = (captures / f"ttt1-model-{model_id}-select.bin").read_bytes()
        model = model_member(bank, capture, model_id)
        texture = texture_member(bank, capture, model_id)
        if struct.unpack_from("<I", model)[0] != 30:
            raise ValueError(f"Model {model_id} is not a 30-part fighter")
        live = capture[PREVIEW & 0x3FFFFF:(PREVIEW & 0x3FFFFF) + len(model)]
        offsets = model_relocations(model, live, PREVIEW)
        relocations = struct.pack(f"<{len(offsets)}I", *offsets)
        try:
            check_textures(texture)
            native_layout = True
        except ValueError:
            native_layout = False
        tiles = texture_tiles(texture)
        try:
            pack(tiles)
            fits = True
        except ValueError:
            fits = False
        stem = f"{name}-TTT1-arcade-P{outfit}"
        for suffix, data in (("3dm", model), ("tim", texture), ("relocs", relocations)):
            (out / f"{stem}.{suffix}").write_bytes(data)
        outfits.append(dict(outfit=outfit, arcade_model=model_id, model_size=len(model),
                            model_sha256=sha(model), texture_size=len(texture),
                            texture_sha256=sha(texture), relocation_count=len(offsets),
                            relocation_sha256=sha(relocations), texture_tiles=len(tiles),
                            texture_area=sum(t["w"] * t["h"] for t in tiles),
                            native_texture_layout=native_layout, fits_jun_atlas=fits))
    # The first capture stands in for a highlight capture: only the alias
    # table differs between highlighted fighters in the ranges consumed.
    ram = bytearray((captures / f"ttt1-model-{models[0]}-select.bin").read_bytes())
    ram[ALIASES:ALIASES + ALIAS_COUNT * 4] = aliases_for(ram, select_id)
    (work / f"{name.lower()}-select-ram.bin").write_bytes(ram)
    report = dict(schema_version=1, name=name, select_id=select_id, bank_sha256=BANK_SHA,
                  alias_audit=audited, outfits=outfits)
    (out / "export-report.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("fighter", choices=sorted(FIGHTERS))
    p.add_argument("--captures", type=Path, required=True,
                   help="directory of ttt1-model-<id>-select.bin dumps")
    p.add_argument("--work-dir", type=Path, default=WORK,
                   help="arcade workspace holding ttt1/ and the Jun and Jin captures")
    a = p.parse_args()
    f = FIGHTERS[a.fighter]
    report = export(f.name, f.select_id, list(f.models), a.captures.resolve(), a.work_dir.resolve())
    for o in report["outfits"]:
        print(f"P{o['outfit']}: model {o['arcade_model']}, {o['relocation_count']} relocations, "
              f"{o['texture_tiles']} tiles / {o['texture_area']} texels, "
              f"{'fits' if o['fits_jun_atlas'] else 'exceeds'} the Jun atlas")
    print("Alias rebuild audited against:", ", ".join(report["alias_audit"]) or "nothing")


if __name__ == "__main__":
    main()
