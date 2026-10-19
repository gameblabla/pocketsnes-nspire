#!/usr/bin/env python3
"""Turns BDF bitmap fonts into frontend/fonts.h.

    python3 tools/fonts/fontconv.py spleen-6x12.bdf spleen-8x16.bdf spleen-12x24.bdf > frontend/fonts.h

Spleen (https://github.com/fcambus/spleen, BSD 2-Clause, see SPLEEN-LICENSE)
is what PocketSNES uses. Each font keeps ASCII 32-126 and Latin-1 160-255
(where the font has them); every glyph is a cell of the font's size, one
uint16_t per row with the leftmost pixel in bit 15. 'map' gives a character's
glyph number, 0 (the space) when the font doesn't have it."""
import sys

def parse(path):
    props, glyphs = {}, {}
    with open(path, encoding='latin-1') as f:
        lines = iter(f.read().splitlines())
    for line in lines:
        words = line.split()
        if not words:
            continue
        if words[0] == 'FONTBOUNDINGBOX':
            props['bbox'] = tuple(int(w) for w in words[1:5])
        elif words[0] == 'FONT_ASCENT':
            props['ascent'] = int(words[1])
        elif words[0] == 'STARTCHAR':
            code, bbx, rows = None, None, []
            for line in lines:
                words = line.split()
                if words[0] == 'ENCODING':
                    code = int(words[1])
                elif words[0] == 'BBX':
                    bbx = tuple(int(w) for w in words[1:5])
                elif words[0] == 'BITMAP':
                    for line in lines:
                        if line.strip() == 'ENDCHAR':
                            break
                        rows.append(int(line.strip(), 16) << (8 * ((16 - 4 * len(line.strip())) // 8) if len(line.strip()) <= 4 else 0))
                    break
            glyphs[code] = (bbx, rows, len(rows))
    return props, glyphs

def cell(props, glyph):
    w, h, xoff, yoff = props['bbox']
    ascent = props.get('ascent', h + yoff)
    (gw, gh, gx, gy), rows, _ = glyph
    out = [0] * h
    top = ascent - gy - gh
    for i, bits in enumerate(rows):
        r = top + i
        if 0 <= r < h:
            # bits is left-aligned in 16 bits (BDF rows are padded to bytes)
            out[r] |= (bits >> (gx - xoff)) & 0xFFFF
    return out

def main():
    print('/* Bitmap fonts made by tools/fonts/fontconv.py from Spleen by Frederic')
    print(' * Cambus (https://github.com/fcambus/spleen).')
    print(' *')
    for line in open(__file__.replace('fontconv.py', 'SPLEEN-LICENSE')).read().splitlines():
        print(' * ' + line if line else ' *')
    print(' */')
    print('#ifndef FONTS_H\n#define FONTS_H\n\n#include <stdint.h>\n')
    print('struct Font\n{\n    int w, h;\n    const uint16_t *rows;   /* h per glyph */\n    const uint8_t *map;     /* character -> glyph */\n};\n')
    for path in sys.argv[1:]:
        props, glyphs = parse(path)
        w, h = props['bbox'][0], props['bbox'][1]
        name = 'font_%dx%d' % (w, h)
        codes = [c for c in list(range(32, 127)) + list(range(160, 256)) if c in glyphs]
        if 32 not in codes:
            raise SystemExit('no space in ' + path)
        print('static const uint16_t %s_rows[%d] =\n{' % (name, len(codes) * h))
        for c in codes:
            rows = cell(props, glyphs[c])
            print('    ' + ', '.join('0x%04x' % r for r in rows) + ',  /* %d */' % c)
        print('};\n')
        index = {c: i for i, c in enumerate(codes)}
        mapping = [index.get(c, 0) for c in range(256)]
        print('static const uint8_t %s_map[256] =\n{' % name)
        for i in range(0, 256, 16):
            print('    ' + ', '.join('%d' % m for m in mapping[i:i + 16]) + ',')
        print('};\n')
        print('static const struct Font %s = { %d, %d, %s_rows, %s_map };\n' % (name, w, h, name, name))
    print('#endif')

main()
