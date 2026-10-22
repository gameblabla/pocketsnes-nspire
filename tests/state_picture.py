#!/usr/bin/env python3
"""A save state's picture (Saving > State pictures, frontend/states.cpp).
  state_picture.py has STATE              yes or no
  state_picture.py png STATE OUT.png      the picture as a PNG, to look at
  state_picture.py frame STATE SHOT.png   same or different: the picture
      against SHOT's game picture (224 lines in the middle of the screen)
      shrunk the way states.h says: each pixel the average of the source
      pixels it covers, 256x224 -> 112x98
  state_picture.py card STATE SHOT.png X Y  same or different: SHOT's
      pixels at X, Y (the menu's picture card) against the picture"""
import struct
import sys
import zlib
from PIL import Image

W, H = 112, 98
STORED, PICTURE = 1, 2


def picture(path):
    """The picture as rows of RGB565 values, or None."""
    data = open(path, 'rb').read()
    magic, version, counter, raw, packed, crc, flags, _ = struct.unpack_from('<4s7I', data)
    if magic != b'PSST' or not flags & PICTURE:
        return None
    at = 32 + packed
    pmagic, width, height, size, pflags = struct.unpack_from('<4sHHII', data, at)
    if pmagic != b'PSPC' or (width, height) != (W, H):
        return None
    pixels = data[at + 16:at + 16 + size]
    if not pflags & STORED:
        pixels = zlib.decompress(pixels)
    values = struct.unpack('<%dH' % (W * H), pixels)
    return [values[y * W:(y + 1) * W] for y in range(H)]


def rgb565(p):
    return (p[0] >> 3) << 11 | (p[1] >> 2) << 5 | (p[2] >> 3)


def rgb(v):
    r, g, b = v >> 11, (v >> 5) & 63, v & 31
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)


def shrink(shot):
    img = Image.open(shot).convert('RGB')
    src = [[rgb565(img.getpixel((32 + x, 8 + y))) for x in range(256)] for y in range(224)]
    rows = []
    for y in range(H):
        y0, y1 = y * 224 // H, (y + 1) * 224 // H
        row = []
        for x in range(W):
            x0, x1 = x * 256 // W, (x + 1) * 256 // W
            box = [src[j][i] for j in range(y0, y1) for i in range(x0, x1)]
            n = len(box)
            row.append((sum(p >> 11 for p in box) // n) << 11 |
                       (sum((p >> 5) & 63 for p in box) // n) << 5 |
                       (sum(p & 31 for p in box) // n))
        rows.append(tuple(row))
    return rows


def main():
    mode, state = sys.argv[1], sys.argv[2]
    pic = picture(state)
    if mode == 'has':
        print('yes' if pic else 'no')
    elif mode == 'png':
        img = Image.new('RGB', (W, H))
        for y in range(H):
            for x in range(W):
                img.putpixel((x, y), rgb(pic[y][x]))
        img.save(sys.argv[3])
    elif mode == 'frame':
        print('same' if pic and pic == shrink(sys.argv[3]) else 'different')
    elif mode == 'card':
        img = Image.open(sys.argv[3]).convert('RGB')
        x0, y0 = int(sys.argv[4]), int(sys.argv[5])
        shown = [tuple(rgb565(img.getpixel((x0 + x, y0 + y))) for x in range(W)) for y in range(H)]
        print('same' if pic and [tuple(r) for r in pic] == shown else 'different')


main()
