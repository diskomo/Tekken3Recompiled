#!/usr/bin/env python3
"""Build a TTT1 fighter's PS1 menu art from local arcade and disc data.

The portrait is the fighter's own TTT1 select portrait (152x256, file 40 +
alphabetical index in the arcade file directory at 0x80302B40). Tekken 3's
selector and loading thumbnails are cropped from it, as TTT1's own selector
does, on a plain backdrop. The name strip is composed in Tekken 3's HUD font
from stock name bitmaps; letters absent from the stock roster are drawn in
the same outline and shading. Outputs the same pack format as Jun's
(prepare_jun_ui.py) into the ignored workspace.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import sys

from PIL import Image

try:
    from .prepare_jun_ui import name_bitmap, ps1_tim
    from .ttt1_fighters import FIGHTERS, ROOT, WORK, select_ram
    from .prepare_jun_import import BANK_SHA, verified
    from . import bns_tool
except ImportError:
    from prepare_jun_ui import name_bitmap, ps1_tim
    from ttt1_fighters import FIGHTERS, ROOT, WORK, select_ram
    from prepare_jun_import import BANK_SHA, verified
    import bns_tool

DIRECTORY, PORTRAITS = 0x302B40, 40
FONT = '.123456789ABCDEF'
# Hand-drawn in the stock glyphs' outline (A), fill (1) and shading levels.
GLYPHS = {'Z': '''AAAAAAAAA A11111116 A11111116 A66666116 AAAAA8115 AAAA9114A AAAA4116A AAA8115AA
                  AAA4116AA AA8115AAA AA4116AAA A8115AAAA A11666668 A11111116 A11111116 AAAAAAAAA''',
          'V': '''AAAAA.AAAAA A911A.A911A A911A.A911A A911A.A911A A911A.A911A A9116A6911A AA911A911AA
                  .A911A911A. .A9116911A. .AA91119AA. ..A91119A.. ..A91119A.. ..AA911AA..
                  ...A911A... ...A911A... ...AAAAA...'''}
FIGHTER_UI = {
    'kazuya': dict(portrait=17, name='KAZUYA',
                   # Stock name bundle and column span for each other letter.
                   letters={'K': (161, 0, 8), 'U': (161, 8, 16), 'A': (161, 26, 35), 'Y': (169, 16, 24)},
                   backdrop=((112, 16, 16), (24, 4, 8)),
                   crops={'selector-tall': (24, 12, 140, 191), 'selector': (24, 18, 140, 171),
                          'loading-tall': (24, 60, 140, 150), 'loading': (24, 66, 140, 142)}),
    'kunimitsu': dict(portrait=20, name='KUNIMITSU',
                      # Whole stock pieces: KU from KUMA, N from NINA, IMITSU from YOSHIMITSU.
                      pieces=[(161, 0, 16), (113, 0, 10), (105, 37, 76)],
                      backdrop=((72, 24, 96), (16, 4, 24)),
                      crops={'selector-tall': (28, 8, 144, 187), 'selector': (28, 14, 144, 167),
                             'loading-tall': (28, 48, 144, 138), 'loading': (28, 54, 144, 130)}),
    'devil': dict(portrait=7, name='DEVIL',
                  # D from DOCTOR B., E from EDDY, a drawn V, I from LEI, L from LAW.
                  pieces=[(249, 0, 9), (137, 0, 8), 'V', (89, 15, 20), (81, 0, 9)],
                  backdrop=((96, 24, 120), (20, 4, 32)),
                  crops={'selector-tall': (24, 12, 140, 191), 'selector': (24, 18, 140, 171),
                         'loading-tall': (24, 60, 140, 150), 'loading': (24, 66, 140, 142)}),
    'angel': dict(portrait=2, name='ANGEL',
                  # AN from ANNA, GE from TIGER, L from LAW.
                  pieces=[(217, 0, 16), (221, 12, 27), (81, 0, 9)],
                  backdrop=((112, 128, 168), (24, 28, 56)),
                  crops={'selector-tall': (24, 12, 140, 191), 'selector': (24, 18, 140, 171),
                         'loading-tall': (24, 60, 140, 150), 'loading': (24, 66, 140, 142)}),
}
SIZES = {'selector-tall': (44, 68), 'selector': (44, 58), 'loading-tall': (44, 34), 'loading': (44, 29)}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def decode_tim(data, offset):
    """An 8bpp TIM with its CLUT, as RGBA; colour 0 is transparent."""
    magic, flags = struct.unpack_from('<II', data, offset)
    if (magic, flags) != (16, 9):
        raise ValueError('Expected an 8bpp TIM with a CLUT')
    q = offset + 8
    size, _, _, cw, ch = struct.unpack_from('<I4H', data, q)
    clut = struct.unpack_from(f'<{cw * ch}H', data, q + 12)
    q += size
    size, _, _, w, h = struct.unpack_from('<I4H', data, q)
    rgba = lambda c: (((c & 31) * 255) // 31, (((c >> 5) & 31) * 255) // 31,
                      (((c >> 10) & 31) * 255) // 31, 0 if c == 0 else 255)
    image = Image.new('RGBA', (w * 2, h))
    image.putdata([rgba(clut[i]) for i in data[q + 12:q + 12 + w * 2 * h]])
    return image


def thumbnail(portrait, box, size, backdrop):
    top, bottom = backdrop
    w, h = size
    base = Image.new('RGBA', size)
    for y in range(h):
        t = y / max(1, h - 1)
        base.paste(tuple(round(a + (b - a) * t) for a, b in zip(top, bottom)) + (255,), (0, y, w, y + 1))
    base.alpha_composite(portrait.crop(box).resize(size, Image.Resampling.LANCZOS))
    return base


def drawn_glyphs():
    glyphs = {k: Image.new('L', (len(v.split()[0]), 16)) for k, v in GLYPHS.items()}
    for k, rows in GLYPHS.items():
        for y, row in enumerate(rows.split()):
            for x, c in enumerate(row):
                glyphs[k].putpixel((x, y), FONT.index(c))
    return glyphs


def name_strip(spec, bundles):
    glyphs = drawn_glyphs()
    if 'pieces' in spec:
        # Each piece is a stock name span (bundle, x0, x1), a drawn letter,
        # or a number of blank columns between words.
        parts = [glyphs[p] if isinstance(p, str) else Image.new('L', (p, 16)) if isinstance(p, int)
                 else bundles[p[0]].crop((p[1], 0, p[2], 16)) for p in spec['pieces']]
        return strip_bytes(parts)
    for letter, (entry, x0, x1) in spec['letters'].items():
        glyphs[letter] = bundles[entry].crop((x0, 0, x1, 16))
    return strip_bytes([glyphs[c] for c in spec['name']])


def strip_bytes(parts):
    width = (sum(p.width for p in parts) + 3) // 4 * 4
    strip = Image.new('L', (width, 16))
    x = 0
    for p in parts:
        strip.paste(p, (x, 0))
        x += p.width
    pixels = strip.tobytes()
    return bytes(pixels[i] | pixels[i + 1] << 4 for i in range(0, len(pixels), 2)), strip


def build(key, work=WORK, disc=ROOT / 'disc/Tekken 3 (USA).cue', exe=ROOT / 'disc/SLUS_004.02'):
    fighter, spec = FIGHTERS[key], FIGHTER_UI[key]
    bank = verified(work / 'ttt1/bankedroms.bin', BANK_SHA)
    ram = select_ram(work, fighter)
    offset, size = struct.unpack_from('<2I', ram, DIRECTORY + (PORTRAITS + spec['portrait']) * 8)
    source = bank[offset:offset + size]
    portrait = decode_tim(bank, offset)
    if portrait.size != (152, 256):
        raise ValueError('Unexpected TTT1 portrait size')
    out = fighter.out(work)
    out.mkdir(parents=True, exist_ok=True)
    stem = f'{fighter.name}-TTT1'
    portrait.save(out / f'{stem}-portrait-source.png')
    # Tekken 3's 168x252 portrait frame: scale to its height, centre.
    frame = Image.new('RGBA', (168, 252))
    scaled = portrait.resize((150, 252), Image.Resampling.LANCZOS)
    frame.alpha_composite(scaled, ((168 - 150) // 2, 0))
    images = {'portrait': frame}
    images.update({name: thumbnail(portrait, spec['crops'][name], SIZES[name], spec['backdrop'])
                   for name in ('selector-tall', 'selector', 'loading-tall', 'loading')})
    payloads, manifest = [], dict(portrait_bank_offset=offset, portrait_sha256=sha(source), assets=[])
    for name, image in images.items():
        image.save(out / f'{stem}-{name}.png')
        converted = ps1_tim(image, name == 'portrait')
        payloads.append(converted)
        manifest['assets'].append(dict(name=name, ps1_sha256=sha(converted)))
    pack = bytearray(struct.pack('<4I', 0x3149554a, 1, 5, 0) + bytes(5 * 8))
    for i, payload in enumerate(payloads):
        struct.pack_into('<2I', pack, 16 + i * 8, len(pack), len(payload))
        pack.extend(payload)
    struct.pack_into('<I', pack, 12, len(pack))
    (out / f'{stem}-ui.jui').write_bytes(pack)
    table, _, _ = bns_tool.load_us_table(Path(exe))
    entries = sorted({p[0] for p in spec.get('pieces') or spec['letters'].values() if isinstance(p, tuple)})
    with bns_tool.open_bns_source(Path(disc)) as archive:
        bundles = {e: name_bitmap(archive.read_at(table[e].offset, table[e].size)) for e in entries}
    name, strip = name_strip(spec, bundles)
    (out / f'{stem}-name.4bpp').write_bytes(name)
    strip.point(lambda p: p * 17).save(out / f'{stem}-name.png')
    manifest.update(pack_sha256=sha(pack), pack_size=len(pack), name_size=[strip.width, 16],
                    name_sha256=sha(name), name_entries=entries)
    (out / 'ui-report.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('fighter', choices=sorted(FIGHTER_UI))
    report = build(ap.parse_args().fighter)
    print(json.dumps({k: report[k] for k in ('pack_sha256', 'pack_size', 'name_size', 'name_sha256')}))


if __name__ == '__main__':
    main()
