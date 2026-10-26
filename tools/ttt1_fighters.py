"""Per-fighter facts for TTT1 imports, shared by the converters.

IDs come from the arcade's own tables, recovered from local MAME captures:
select_id indexes the move-index tables at 0x80093780 and the cancel/hit
character rules; model_index is the select record's second ID (the outfit
lookup row at 0x8001110C); sound_profile indexes the voice profiles at
0x80011E1C. The select records
at 0x8001EAC0 list, after each name, model_index, sound_profile and select_id.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / 'workspace/jun-import'
ALIASES, ALIAS_COUNT = 0x2A7350, 5515
MOVE_TABLES, MOVE_BASE, MOVE_SIZE = 0x93780, 0x80036768, 52
PREVIEW_MODEL = 0x354108


@dataclass(frozen=True)
class Fighter:
    name: str                 # file prefix, e.g. Jun-TTT1-combat.jmv
    select_id: int
    model_index: int
    sound_profile: int
    roster_id: int            # SLUS-00402 character ID
    base_id: int              # first imported move ID; ranges never overlap
    models: tuple[int, ...]   # arcade model per outfit
    idle_clip: int | None = None  # neutral-only reference stance (Jun)

    @property
    def key(self) -> str:
        return self.name.lower()

    @property
    def ram(self) -> str:
        return f'{self.key}-select-ram.bin'

    def out(self, work: Path) -> Path:
        return work / self.key


JUN = Fighter('Jun', 28, 30, 19, 23, 8192, (46, 47, 118), 0x102B58)
FIGHTERS = {f.key: f for f in (JUN,)}


def aliases_for(ram: bytes, select_id: int) -> bytes:
    """The alias table the arcade writes when select_id is highlighted."""
    if not 0 <= select_id < 33:
        raise ValueError('TTT1 select IDs are 0..32')
    row = struct.unpack_from(f'<{ALIAS_COUNT}H', ram, MOVE_TABLES + select_id * ALIAS_COUNT * 2)
    return struct.pack(f'<{ALIAS_COUNT}I', *(MOVE_BASE + i * MOVE_SIZE for i in row))


def select_ram(work: Path, fighter: Fighter) -> bytes:
    """Jun's highlight capture, or a synthesised one for a locked fighter.

    A synthesised dump must agree with Jun's verified capture in every other
    range the converters read, and carry this fighter's rebuilt alias table.
    """
    try:
        from .prepare_jun_import import RAM_SHA, verified
    except ImportError:
        from prepare_jun_import import RAM_SHA, verified
    if fighter is JUN:
        return verified(work / JUN.ram, RAM_SHA)
    ram = (work / fighter.ram).read_bytes()
    ranges = json.loads((ROOT / 'tools/data/jun_capture_ranges.json').read_text())[JUN.ram]
    for r in ranges:
        if r['offset'] in (ALIASES, PREVIEW_MODEL):
            continue
        if hashlib.sha256(ram[r['offset']:r['offset'] + r['size']]).hexdigest() != r['sha256']:
            raise ValueError(f'{fighter.ram}: shared arcade data differs at 0x{r["offset"]:x}')
    if ram[ALIASES:ALIASES + ALIAS_COUNT * 4] != aliases_for(ram, fighter.select_id):
        raise ValueError(f'{fighter.ram}: alias table is not {fighter.name}\'s')
    return ram
