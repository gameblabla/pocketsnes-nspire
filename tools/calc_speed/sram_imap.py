#!/usr/bin/env python3
"""An imap (tools/calc_speed/imap.py) with code moved into a fast region,
for trying out code in the on-chip SRAM with the ARM926 plugin
(fastlo=/fasthi=/fastfill=):
    tools/calc_speed/sram_imap.py IMAP MISSES BASE BUDGET [--block LO:HI ...]
        [--jit LO:KB] > new.imap
First each --block (harness addresses, hex: e.g. a whole assembler object,
which can't be split) is moved as one piece; then --jit moves KB of the
Super FX translator's code buffer starting at harness address LO; then the
other ranges of the imap, ranked by instruction misses per byte (MISSES is a
run's FILE.misses with the same harness), 32-byte aligned, while they fit in
BUDGET bytes. Prints to stderr what was moved."""
import sys
args = sys.argv[1:]
imap = [list(int(x, 16) for x in l.split()) for l in open(args[0]) if l.strip()]
miss = {}
for l in open(args[1]).read().split('\n')[1:]:
    if l.strip():
        pc, im, dm = l.split()
        miss[int(pc, 16)] = int(im)
base, budget = int(args[2], 16), int(args[3], 0)
blocks, jit = [], None
i = 4
while i < len(args):
    lo, hi = args[i + 1].split(':')
    if args[i] == '--block':
        blocks.append((int(lo, 16), int(hi, 16)))
    else:
        jit = (int(lo, 16), int(hi) * 1024)
    i += 2
total = sum(miss.values())
at, got, moved = base, 0, {}
def take(s, e):
    return sum(v for pc, v in miss.items() if s <= pc < e)
for lo, hi in blocks:
    for k, (s, e, c) in enumerate(imap):
        if lo <= s < hi:
            moved[k] = at + (s - lo)
    got += take(lo, hi)
    print('block %x-%x (%.1f KB) -> %x' % (lo, hi, (hi - lo) / 1024, at), file=sys.stderr)
    at += (hi - lo + 31) & ~31
if jit:
    lo, size = jit
    for k, (s, e, c) in enumerate(imap):
        if s <= lo < e:     # split the buffer: its first KB moved, the rest stays
            imap.append([lo + size, e, c + (lo + size - s)])
            imap[k][1] = lo + size
            moved[k] = at
    got += take(lo, lo + size)
    print('jit %x +%d KB -> %x' % (lo, size // 1024, at), file=sys.stderr)
    at += size
rank = sorted(((take(s, e), k) for k, (s, e, c) in enumerate(imap) if k not in moved),
              key=lambda x: -x[0] / max(1, imap[x[1]][1] - imap[x[1]][0]))
for m, k in rank:
    s, e, c = imap[k]
    if m == 0 or any(lo <= s < hi for lo, hi in blocks):
        continue
    size = (e - s + 31) & ~31
    if at + size > base + budget:
        continue
    moved[k] = at + (c & 31)
    at += size
    got += m
print('used %.1f KB, %.1f%% of the instruction misses' % ((at - base) / 1024, 100.0 * got / max(1, total)), file=sys.stderr)
for k, (s, e, c) in sorted(enumerate(imap), key=lambda x: x[1][0]):
    print('%x %x %x' % (s, e, moved.get(k, c)))
