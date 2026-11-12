#!/usr/bin/env python3
"""Offscreen GPU control for decorative side artwork (requires Pillow/NumPy)."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import numpy as np
from PIL import Image

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary', type=Path, default=Path('build-hd-debug/native_polygon_clip_probe'))
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
proof = a.output / 'proof.json'
proof.unlink(missing_ok=True)
cases = []
for y in (0, 272):
    for wide in (0, 1):
        frames, native = [], []
        for clip in (0, 1):
            stem = a.output / f'{y}-{wide}-{clip}'
            with stem.with_suffix('.log').open('w') as log:
                subprocess.run([str(a.binary.resolve()), str(clip), str(wide), str(y),
                                str(stem.with_suffix('.ppm')), str(stem.with_suffix('.vram'))],
                               env=dict(os.environ, PSYZ_HEADLESS='gpu', SDL_AUDIODRIVER='dummy'),
                               stdout=log, stderr=log, check=True, timeout=30)
            frames.append(np.array(Image.open(stem.with_suffix('.ppm'))))
            native.append(stem.with_suffix('.vram').read_bytes())
        assert native[0] == native[1], 'Native VRAM changed'
        if not wide:
            assert np.array_equal(*frames), 'Classic presentation changed'
        else:
            red = [np.all(f == (255, 0, 0), axis=2).sum() for f in frames]
            green = [np.all(f == (0, 255, 0), axis=2).sum() for f in frames]
            assert red[0] > 0 and red[1] == 0, 'Outside polygon not clipped'
            assert 0 < green[1] < green[0], 'Crossing polygon not clipped'
            # Quantized RGB555 blue sprites remain unchanged on both sides.
            for blue in (206,):
                assert np.array_equal(np.all(frames[0] == (0, 0, blue), axis=2),
                                      np.all(frames[1] == (0, 0, blue), axis=2))
            assert np.any(np.all(frames[1][:, :150] == (0, 0, 99), axis=2))
        cases.append(dict(buffer_y=y, widescreen=wide, passed=True))
proof.write_text(json.dumps(dict(passed=True, cases=cases), indent=2) + '\n')
print('4 paired GPU cases passed')
