#!/usr/bin/env python3
"""Check that the overlay data markers enclose exactly the overlays' writable data.

src/port/overlay_data.c restores everything between the overlay data markers on
each return to the title. This reads the linked executable's symbol table and
the overlay object files (base and variant units), and fails when an overlay
symbol in writable data lies outside every marker span, or another object's
symbol lies inside one. Writable data is Mach-O __DATA __data/__bss/__common, or
the .data and .bss output sections elsewhere (located with objdump -h; MinGW
merges .bss into .data). Read-only-after-relocation data (.data.rel.ro, .rdata,
__DATA_CONST) is never restored and is not checked. Overlay common symbols must
have no storage of their own (gen_overlay_commons.py defines them).

usage: check_overlay_data_span.py BUILD_DIR [--nm nm] [--objdump objdump]
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

SPANS = [('peOverlayDataBegin', 'peOverlayDataEnd'), ('peOverlayBssBeginMark', 'peOverlayBssEndMark'),
         ('peOverlayZeroBegin', 'peOverlayZeroEnd')]
MARKERS = {name for span in SPANS for name in span}
WRITABLE_SECTIONS = {'.data', '.bss'}
DARWIN_WRITABLE = {'__DATA,__data', '__DATA,__bss', '__DATA,__common'}


def plain(name):
    return name[1:] if name.startswith('_') else name


def run(args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def darwin_symbols(nm, path):
    """(address, writable, common, name) from nm -m."""
    for line in run([nm, '-m', str(path)]).splitlines():
        m = re.match(r'([0-9a-f]+) \(([^)]*)\) .*?(\S+)$', line)
        if m:
            yield int(m.group(1), 16), m.group(2) in DARWIN_WRITABLE, m.group(2) == 'common', m.group(3)


def sections(objdump, path):
    """Writable output sections as (begin, end)."""
    found = []
    for line in run([objdump, '-h', str(path)]).splitlines():
        m = re.match(r'\s*\d+\s+(\S+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)', line)
        if m and m.group(1) in WRITABLE_SECTIONS:
            begin = int(m.group(3), 16)
            found.append((begin, begin + int(m.group(2), 16)))
    return found


def other_symbols(nm, path, writable):
    """(address, writable, common, name) from plain nm; writable by output-section address."""
    for line in run([nm, str(path)]).splitlines():
        parts = line.split()
        if len(parts) != 3:
            continue
        address, kind, name = int(parts[0], 16), parts[1], parts[2]
        if kind in 'Cc':
            yield address, False, True, name
            continue
        inside = writable is None and kind in 'dDbB' or writable and any(b <= address < e for b, e in writable)
        yield address, bool(inside), False, name


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('build', type=Path)
    parser.add_argument('--nm', default='nm')
    parser.add_argument('--objdump', default=None, help='default: objdump, or llvm-objdump with an llvm-nm')
    parser.add_argument('--binary', default='parasite-eve-ii-pc')
    args = parser.parse_args()
    objdump = args.objdump or ('llvm-objdump' if 'llvm' in Path(args.nm).name else 'objdump')
    exe = next((p for p in [args.build / args.binary, args.build / (args.binary + '.exe')] if p.exists()), None)
    if not exe:
        sys.exit(f'no {args.binary} in {args.build}')
    with exe.open('rb') as f:
        darwin = f.read(4) in (b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf')

    dirs = [args.build / 'CMakeFiles/game_overlays.dir', *(args.build / 'CMakeFiles').glob('pe_var_*.dir')]
    objects = [o for d in dirs for o in [*d.rglob('*.o'), *d.rglob('*.obj')]]
    if not objects:
        sys.exit('no overlay objects found')
    overlay, overlay_common = set(), set()
    for obj in objects:
        symbols = darwin_symbols(args.nm, obj) if darwin else other_symbols(args.nm, obj, None)
        for _, writable, common, name in symbols:
            (overlay_common if common else overlay).add(name) if (writable or common) else None

    if darwin:
        table = list(darwin_symbols(args.nm, exe))
    else:
        writable = sections(objdump, exe)
        if not writable:
            sys.exit(f'no .data/.bss sections found in {exe} with {objdump}')
        table = list(other_symbols(args.nm, exe, writable))
    marks = {plain(n): (a, w) for a, w, _, n in table if plain(n) in MARKERS}
    missing = MARKERS - marks.keys()
    if missing:
        sys.exit(f'markers missing from {exe}: {sorted(missing)}')
    for name, (_, writable_mark) in marks.items():
        if not writable_mark:
            sys.exit(f'{name} is not in writable data')
    spans = [(marks[b][0], marks[e][0]) for b, e in SPANS]
    for (b, e), (bn, en) in zip(spans, SPANS):
        if e <= b:
            sys.exit(f'{en} does not follow {bn}')

    outside, foreign, common, checked = [], [], 0, 0
    for addr, writable_symbol, is_common, name in table:
        if plain(name) in MARKERS:
            continue
        if is_common:
            common += name in overlay or name in overlay_common
            continue
        if not writable_symbol:
            continue
        inside = any(b <= addr < e for b, e in spans)
        if name in overlay or name in overlay_common:
            checked += 1
            if not inside:
                outside.append(name)
        elif inside:
            foreign.append(name)
    size = [e - b for b, e in spans]
    print(f'{exe.name}: data {size[0]}, bss {size[1]}, zero {size[2]} bytes; '
          f'{checked} overlay symbols checked, {len(outside)} outside, {len(foreign)} foreign inside, '
          f'{common} overlay common symbols left')
    for name in outside[:20]:
        print('  outside:', name)
    for name in foreign[:20]:
        print('  foreign:', name)
    return 1 if outside or foreign or common or not checked else 0


if __name__ == '__main__':
    sys.exit(main())
