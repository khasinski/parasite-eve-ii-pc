#!/usr/bin/env python3
"""Check that generated side artwork continues the native picture's lines across each seam.

A side can match the seam's brightness and still break the room: a floor joint,
ledge or bench edge that stops at the join, or reappears a few pixels higher.
This follows every strong horizontal-ish edge that reaches a seam in the native
picture, extrapolates it a few pixels into the side along its own slope, and
reports whether the side has a matching edge there. It measures geometry only;
colour and lighting steps are a separate check.

Input is a composite at native scale (one pixel per native pixel): the native
320-pixel picture with the side artwork on both sides, as packed for -LEFT..320+RIGHT.

usage: seam_continuity.py COMPOSITE.png --margin 64 [--json report.json]
"""
import argparse
import json
import sys

import numpy as np
from PIL import Image

TRACK = 12      # native columns used to follow an edge towards the seam
REACH = 4       # side columns where the continuation is looked for
TOLERANCE = 1.5 # allowed vertical miss of the continuation, in native pixels
STRONG = 0.25   # edge strength, as a fraction of the picture's strongest, that must continue


def vertical_gradient(gray):
    g = np.zeros_like(gray)
    g[1:-1] = gray[2:] - gray[:-2]
    return np.abs(g)


def column_peaks(column, threshold):
    """Rows where |d/dy| has a local maximum above threshold."""
    rows = []
    for y in range(1, len(column) - 1):
        v = column[y]
        if v >= threshold and v >= column[y - 1] and v > column[y + 1]:
            rows.append(y)
    return rows


def follow(gradient, row, columns, threshold):
    """Track an edge from `row` across `columns` (seam first); return [(x, y)]."""
    points, y = [], row
    for x in columns:
        lo, hi = max(1, y - 1), min(gradient.shape[0] - 2, y + 1)
        window = gradient[lo:hi + 1, x]
        best = int(np.argmax(window))
        if window[best] < threshold:
            break
        y = lo + best
        points.append((x, y))
    return points


def seam(gradient, native_columns, side_columns, threshold):
    """Edges reaching the seam through native_columns (seam column first) and their continuation."""
    edges = []
    seen = set()
    for row in column_peaks(gradient[:, native_columns[0]], threshold):
        if any(abs(row - r) <= 1 for r in seen):
            continue
        seen.add(row)
        track = follow(gradient, row, native_columns, threshold)
        if len(track) < TRACK // 2:
            continue  # too short to be a structural line
        xs = np.array([p[0] for p in track], float)
        ys = np.array([p[1] for p in track], float)
        slope = np.polyfit(xs, ys, 1)[0] if np.ptp(xs) else 0.0
        if abs(slope) > 1.0:
            continue  # steep: mostly vertical structure, its continuation is not a row match
        misses = []
        for x in side_columns:
            expected = row + slope * (x - native_columns[0])
            lo, hi = int(np.floor(expected - 3)), int(np.ceil(expected + 3))
            lo, hi = max(0, lo), min(gradient.shape[0] - 1, hi)
            window = gradient[lo:hi + 1, x]
            if not len(window) or window.max() < threshold * 0.5:
                misses.append(None)
                continue
            found = lo + int(np.argmax(window))
            misses.append(abs(found - expected))
        found = [m for m in misses if m is not None]
        continued = len(found) >= len(side_columns) - 1 and float(np.median(found)) <= TOLERANCE
        edges.append(dict(row=int(row), slope=round(float(slope), 3), continued=bool(continued),
                          miss=None if not found else round(float(np.median(found)), 2)))
    return edges


def check(composite, margin):
    gray = np.asarray(composite.convert('L'), dtype=float)
    width = gray.shape[1]
    if width != 320 + 2 * margin:
        raise ValueError(f'composite is {width} wide, expected {320 + 2 * margin} for margin {margin}')
    gradient = vertical_gradient(gray)
    threshold = STRONG * float(gradient[:, margin:margin + 320].max() or 1)
    left = seam(gradient, list(range(margin, margin + TRACK)), list(range(margin - 1, margin - 1 - REACH, -1)), threshold)
    right_edge = margin + 319
    right = seam(gradient, list(range(right_edge, right_edge - TRACK, -1)), list(range(right_edge + 1, right_edge + 1 + REACH)), threshold)
    report = {}
    for name, edges in [('left', left), ('right', right)]:
        broken = [e for e in edges if not e['continued']]
        report[name] = dict(edges=len(edges), broken=len(broken), broken_rows=[e['row'] for e in broken], detail=edges)
    report['passed'] = not report['left']['broken'] and not report['right']['broken']
    return report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('composite')
    parser.add_argument('--margin', type=int, default=64)
    parser.add_argument('--json')
    args = parser.parse_args()
    report = check(Image.open(args.composite), args.margin)
    for name in ['left', 'right']:
        r = report[name]
        print(f'{name}: {r["edges"]} edges reach the seam, {r["broken"]} not continued'
              + (f' (rows {r["broken_rows"]})' if r['broken'] else ''))
    if args.json:
        with open(args.json, 'w') as f:
            json.dump(report, f, indent=1)
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
