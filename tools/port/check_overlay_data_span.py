#!/usr/bin/env python3
"""Check that the overlay data markers enclose exactly the overlays' writable data.

src/port/overlay_data.c restores everything between the overlay data markers on
each return to the title. This reads the linked executable's symbol table and
the overlay object files (base and variant units) with nm, and fails when an
overlay's initialized or zero-initialized symbol lies outside the markers, or
another object's symbol lies inside. Mach-O keeps zero-initialized external
definitions in __common, with their own markers; elsewhere they are in bss.
Overlay common symbols must have no storage of their own in the executable
(gen_overlay_commons.py defines them among the overlay data).

usage: check_overlay_data_span.py BUILD_DIR [--nm nm]
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

MARKERS = {'peOverlayDataBegin': 'data', 'peOverlayDataEnd': 'data',
           'peOverlayBssBeginMark': 'bss', 'peOverlayBssEndMark': 'bss',
           'peOverlayZeroBegin': 'zero', 'peOverlayZeroEnd': 'zero'}


def run_nm(nm, path, darwin):
    """Yield (address, storage, name); storage is data, bss, common or None."""
    args = [nm, '-m', str(path)] if darwin else [nm, str(path)]
    out = subprocess.run(args, check=True, capture_output=True, text=True).stdout
    for line in out.splitlines():
        if darwin:
            m = re.match(r'([0-9a-f]+) \(([^)]*)\) .*?(\S+)$', line)
            if not m:
                continue
            storage = {'__DATA,__data': 'data', '__DATA,__bss': 'bss', '__DATA,__common': 'zero',
                       'common': 'common'}.get(m.group(2))
            yield int(m.group(1), 16), storage, m.group(3)
        else:
            parts = line.split()
            if len(parts) != 3:
                continue
            kind = parts[1]
            storage = 'data' if kind in 'dD' else 'bss' if kind in 'bB' else 'common' if kind == 'C' else None
            if plain(parts[2]) in ('peOverlayZeroBegin', 'peOverlayZeroEnd'):
                storage = 'zero'
            yield int(parts[0], 16), storage, parts[2]


def plain(name):
    return name[1:] if name.startswith('_') else name


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('build', type=Path)
    parser.add_argument('--nm', default='nm')
    parser.add_argument('--binary', default='parasite-eve-ii-pc')
    args = parser.parse_args()
    exe = next((p for p in [args.build / args.binary, args.build / (args.binary + '.exe')] if p.exists()), None)
    if not exe:
        sys.exit(f'no {args.binary} in {args.build}')
    with exe.open('rb') as f:
        darwin = f.read(4) in (b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf')

    dirs = [args.build / 'CMakeFiles/game_overlays.dir', *(args.build / 'CMakeFiles').glob('pe_var_*.dir')]
    objects = [o for d in dirs for o in [*d.rglob('*.o'), *d.rglob('*.obj')]]
    if not objects:
        sys.exit('no overlay objects found')
    overlay = set()
    for obj in objects:
        overlay.update(name for _, storage, name in run_nm(args.nm, obj, darwin) if storage)

    table = list(run_nm(args.nm, exe, darwin))
    marks = {plain(n): (a, s) for a, s, n in table if plain(n) in MARKERS}
    if len(marks) != len(MARKERS):
        sys.exit(f'markers missing from {exe}: found {sorted(marks)}')
    for name, (_, storage) in marks.items():
        if storage != MARKERS[name]:
            sys.exit(f'{name} is in {storage} storage, expected {MARKERS[name]}')
    spans = {'data': (marks['peOverlayDataBegin'][0], marks['peOverlayDataEnd'][0]),
             'bss': (marks['peOverlayBssBeginMark'][0], marks['peOverlayBssEndMark'][0]),
             'zero': (marks['peOverlayZeroBegin'][0], marks['peOverlayZeroEnd'][0])}

    outside, foreign, common, checked = [], [], 0, 0
    for addr, storage, name in table:
        if plain(name) in MARKERS or storage is None:
            continue
        if storage == 'common':  # a common symbol left in the executable
            common += name in overlay
            continue
        begin, end = spans[storage]
        inside = begin <= addr < end
        if name in overlay:
            checked += 1
            if not inside:
                outside.append(name)
        elif inside:
            foreign.append(name)
    size = {k: e - b for k, (b, e) in spans.items()}
    print(f'{exe.name}: data {size["data"]}, bss {size["bss"]}, zero {size["zero"]} bytes; '
          f'{checked} overlay symbols checked, {len(outside)} outside, {len(foreign)} foreign inside, '
          f'{common} overlay common symbols left')
    for name in outside[:20]:
        print('  outside:', name)
    for name in foreign[:20]:
        print('  foreign:', name)
    return 1 if outside or foreign or common or not checked else 0


if __name__ == '__main__':
    sys.exit(main())
