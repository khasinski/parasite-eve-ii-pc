#!/usr/bin/env python3
"""Start a new game at every warp entry of every area, headless, and report the runs that fail.

The binary lists each area's room and warp counts (PE2_LOCATION_AUDIT=1, which also checks every warp trigger);
each location then runs PE2_START_LOCATION=stage,area,1,1,warp,1 for a few thousand pad frames from a scripted
start. With --probe, one warp past each table is also tried: it must be rejected, not crash. Use an ASan build to
catch silent out-of-bounds reads. Needs both disc images; nothing is written outside --out.

  tools/port/location_sweep.py build/parasite-eve-ii-pc --disc rom/image/SLUS-01042.cue \\
      --disc2 rom/image/SLUS-01055.cue --out /tmp/sweep [--stage 1] [--probe] [--jobs 8]
"""
import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path


def startup_replay(path, frames):
    """Start at the title, then tap Cross: the new game opens at PE2_START_LOCATION."""
    with path.open('w') as output:
        output.write('PE2PAD 1\n')
        for index in range(frames):
            mask = 8 if 150 <= index < 158 else 0
            if 200 <= index < 4000 and index % 60 < 8:
                mask |= 0x4000
            reply = bytes([0, 0x41, (~mask) & 255, ((~mask) >> 8) & 255]) + bytes(30)
            output.write(f'{index} {reply.hex()}\n')


def base_env(profile):
    env = {k: v for k, v in os.environ.items() if not k.startswith('PE2_')}
    env.update(PSYZ_HEADLESS='1', PSYZ_RASTER_CLOCK='virtual', SDL_AUDIODRIVER='dummy', PE2_SKIPINTRO='1',
               PE2_FAST='1', PE2_CONFIG_DIR=str(profile), PE2_MEMORY_CARD_DIR=str(Path(profile) / 'cards'))
    return env


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--disc', type=Path, required=True)
    parser.add_argument('--disc2', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--stage', type=int, action='append', help='only these stages (repeatable)')
    parser.add_argument('--frames', type=int, default=3000, help='pad frames per run (default 3000)')
    parser.add_argument('--timeout', type=float, default=180)
    parser.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    parser.add_argument('--probe', action='store_true', help='also try one warp past each table (must be rejected)')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    command = [str(args.binary.resolve()), '--disc', str(args.disc.resolve()), '--disc2', str(args.disc2.resolve())]

    with tempfile.TemporaryDirectory(dir=args.out) as profile:
        env = base_env(profile)
        env['PE2_LOCATION_AUDIT'] = '1'
        audit = subprocess.run(command, env=env, capture_output=True, text=True, timeout=args.timeout)
    tables = [tuple(map(int, m)) for m in re.findall(r'\[location-table\] stage (\d+) area (\d+) rooms (\d+) warps (\d+)', audit.stderr)]
    for line in audit.stderr.splitlines():
        if line.startswith('[location-audit]'):
            print(line)
    if not tables:
        sys.exit('the binary listed no location tables (is it built with src/port/location_tables.c?)')

    replay = args.out / 'startup.pad'
    startup_replay(replay, args.frames)
    cases = [(stage, area, warp, warps) for stage, area, rooms, warps in tables if not args.stage or stage in args.stage
             for warp in range(1, warps + 1 + (1 if args.probe else 0))]

    def run(case):
        stage, area, warp, warps = case
        location = f'{stage},{area},1,1,{warp},1'
        run_dir = args.out / location.replace(',', '-')
        run_dir.mkdir(exist_ok=True)
        env = base_env(run_dir)
        env.update(PE2_PAD_REPLAY=str(replay), PE2_START_LOCATION=location)
        try:
            result = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=args.timeout)
            code, log = result.returncode, result.stdout.decode(errors='replace')
        except subprocess.TimeoutExpired as expired:
            code, log = 'timeout', (expired.stdout or b'').decode(errors='replace')
        (run_dir / 'run.log').write_text(log)
        rejected = '[location] PE2_START_LOCATION' in log and 'rejected' in log
        problem = None
        if warp > warps and not rejected:
            problem = 'out-of-range warp was not rejected'
        elif warp <= warps and rejected:
            problem = 'valid warp was rejected'
        elif code != 0:
            asan = re.search(r'ERROR: AddressSanitizer: (\S+).*?\n.*?#0 \S+ in (\S+) (\S+)', log, re.S)
            problem = f'exit {code}' + (f' ({asan.group(1)} in {asan.group(2)} {asan.group(3)})' if asan else '')
        return location, problem

    failures = 0
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for location, problem in pool.map(run, cases):
            if problem:
                failures += 1
                print(f'{location}: {problem}', flush=True)
    print(f'{len(cases)} locations, {failures} failed; logs in {args.out}')
    sys.exit(1 if failures or 'problems' in audit.stderr and ' 0 problems' not in audit.stderr else 0)


if __name__ == '__main__':
    main()
