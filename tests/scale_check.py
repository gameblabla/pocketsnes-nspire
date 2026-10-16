#!/usr/bin/env python3
"""Checks a stretched screenshot (Screen size full screen, frontend/scale.h)
against a screenshot of the same frame at the original size, pixel for
pixel, from scale.h's definition (not from scale.cpp's code). Prints
"same" or how many pixels differ.
  scale_check.py ORIGINAL.png STRETCHED.png CROP SMOOTH [LINES]
CROP 1: the stretched one left out 8 lines at the top and bottom.
LINES: the picture's lines at the original size (224)."""
import sys
from PIL import Image

orig = Image.open(sys.argv[1]).convert('RGB')
full = Image.open(sys.argv[2]).convert('RGB')
crop, smooth = int(sys.argv[3]), int(sys.argv[4])
lines = int(sys.argv[5]) if len(sys.argv) > 5 else 224
top = (240 - lines) // 2
if crop:
    top, lines = top + 8, lines - 16

def rgb565(p):
    return (p[0] >> 3) << 11 | (p[1] >> 2) << 5 | (p[2] >> 3)

src = [[rgb565(orig.getpixel((32 + x, top + y))) for x in range(256)] for y in range(lines)]

def average(p, q):
    return (p & q) + (((p ^ q) & 0xF7DE) >> 1)

def source_line(y):
    return (2 * y + 1) * lines // 480

def pixel(x, y):
    """Picture pixel x on output line y, lines stretched."""
    s = source_line(y)
    p = src[s][x]
    if smooth and y > 0 and s == source_line(y - 1) and s + 1 < lines:
        p = average(p, src[s + 1][x])
    return p

bad = 0
for y in range(240):
    for x in range(320):
        g, k = divmod(x, 5)
        if k < 3:
            expected = pixel(4 * g + k, y)
        elif k == 3:
            expected = average(pixel(4 * g + 2, y), pixel(4 * g + 3, y)) if smooth else pixel(4 * g + 2, y)
        else:
            expected = pixel(4 * g + 3, y)
        bad += rgb565(full.getpixel((x, y))) != expected
print('same' if bad == 0 else '%d pixels differ' % bad)
