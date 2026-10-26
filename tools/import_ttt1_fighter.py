#!/usr/bin/env python3
"""Import a further TTT1 fighter from the local Jun import.

Run tools/import_jun.py first: it verifies the arcade ROMs and captures the
shared Jun and Jin tables this importer reuses. A fresh, muted MAME run then
previews each of the fighter's outfit models, which also works for fighters
still locked on fresh NVRAM. Outputs stay in the ignored workspace.
"""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess, sys, tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from ttt1_fighters import FIGHTERS, JUN, WORK
import export_ttt1_fighter as export, ttt1_motion as motion
import convert_ttt1_moves as combat, prepare_ttt1_voices as voices, prepare_ttt1_ui as ui


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('fighter', choices=sorted(set(FIGHTERS) - {JUN.key}))
    ap.add_argument('--ttt1', type=Path, default=ROOT / 'tektagt.zip',
                    help='Non-merged tektagt.zip, World TEG2/VER.C1')
    ap.add_argument('--mame', type=Path, help='MAME 0.289 executable')
    args = ap.parse_args()
    fighter = FIGHTERS[args.fighter]
    if not (WORK / 'ttt1/bankedroms.bin').is_file() or not (WORK / 'jin-select-ram.bin').is_file():
        raise ValueError('Import Jun first: python tools/import_jun.py')
    found = shutil.which('mame')
    mame = (args.mame or (Path(found) if found else None))
    if not mame:
        raise ValueError('MAME 0.289 is required; pass --mame')
    # A private fresh directory prevents reads/writes to the player's emulator saves.
    capture = Path(tempfile.mkdtemp(prefix='local-capture-', dir=WORK))
    (capture / 'roms').mkdir()
    shutil.copyfile(args.ttt1, capture / 'roms/tektagt.zip')
    for model in fighter.models:
        print(f'Capturing {fighter.name} model {model} in a muted MAME run...', flush=True)
        subprocess.run([str(mame), 'tektagt', '-rompath', str(capture / 'roms'), '-video', 'none',
                        '-sound', 'none', '-nothrottle', '-skip_gameinfo', '-autoboot_script',
                        str(ROOT / 'tools/capture_ttt1_model.lua'), '-autoboot_delay', '0',
                        '-nvram_directory', str(capture / f'nvram{model}'),
                        '-cfg_directory', str(capture / f'cfg{model}'),
                        '-snapshot_directory', str(capture / f'snap{model}'), '-seconds_to_run', '50'],
                       cwd=capture, check=True, timeout=240,
                       env=dict(os.environ, TTT_CAPTURE_MODEL=str(model)),
                       **({'creationflags': subprocess.CREATE_NO_WINDOW} if os.name == 'nt' else {}))
    print('Converting outfits, moves, voices and menu art...', flush=True)
    export.export(fighter.name, fighter.select_id, list(fighter.models), capture, WORK)
    motion.export(WORK, fighter=fighter)
    combat.build(WORK, fighter)
    voices.build(WORK, fighter)
    ui.build(fighter.key, WORK)
    expected = json.loads((ROOT / f'tools/data/{fighter.key}_runtime_hashes.json').read_text())
    for name, digest in expected.items():
        if hashlib.sha256((fighter.out(WORK) / name).read_bytes()).hexdigest() != digest:
            raise ValueError(f'{name}: converted output differs from the tested version')
    print(f'All {len(expected)} {fighter.name} runtime files match the tested version. '
          'Rebuild with -DTEKKEN3_JUN_EXPERIMENTAL=ON to stage them.', flush=True)


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print(f'Import failed: {error}', file=sys.stderr)
        sys.exit(1)
