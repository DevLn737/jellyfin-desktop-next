#!/usr/bin/env python3
"""Reads the frame-number barcode of the test clip from an X11 screen.

Works for any client that shows the clip on an X display (Xvfb in CI), so a
native player can be measured without hooks. Protocol: one line on stdin per
sample; one JSON line on stdout: {"atMs", "frame", "captureMs"}. atMs is the
Unix time in ms halfway through the capture. frame is null when the strip is
not on screen (cells must be clearly black or white).

Usage: screen_probe.py --display :99 --x X --y Y --cell CELL_WIDTH_PX
  X, Y: screen position of the top-left corner of the barcode strip
  CELL_WIDTH_PX: on-screen width of one of the 16 cells (40 px at 640x360)
"""
import argparse
import json
import sys
import time

from Xlib import X, display

BITS = 16


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--display', default=':99')
    parser.add_argument('--x', type=float, required=True)
    parser.add_argument('--y', type=float, required=True)
    parser.add_argument('--cell', type=float, required=True)
    parser.add_argument('--row', type=float, default=0.5,
                        help='where to read inside the strip, 0 = top edge, 1 = bottom edge')
    args = parser.parse_args()
    root = display.Display(args.display).screen().root
    row = int(args.y + args.cell * args.row)
    # Up to the centre of the last cell, so a strip at the screen edge still fits.
    width = int(args.cell * (BITS - 0.5)) + 1
    for _ in sys.stdin:
        t0 = time.time()
        image = root.get_image(int(args.x), row, width, 1, X.ZPixmap, 0xFFFFFFFF)
        t1 = time.time()
        data = image.data  # BGRX
        lums = []
        for bit in range(BITS):
            px = int(args.cell * bit + args.cell / 2) * 4
            b, g, r = data[px], data[px + 1], data[px + 2]
            lums.append(0.299 * r + 0.587 * g + 0.114 * b)
        # Adaptive threshold: player overlays (title bars, gradients) dim the
        # strip, so compare cells with each other instead of fixed levels.
        low, high = min(lums), max(lums)
        mid = (low + high) / 2
        valid = high - low > 60 and all(abs(v - mid) > (high - low) * 0.25 for v in lums)
        frame = sum(1 << bit for bit, v in enumerate(lums) if v > mid)
        print(json.dumps({'atMs': (t0 + t1) / 2 * 1000, 'frame': frame if valid else None,
                          'captureMs': (t1 - t0) * 1000}), flush=True)


if __name__ == '__main__':
    main()
