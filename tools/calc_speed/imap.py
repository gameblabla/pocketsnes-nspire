#!/usr/bin/env python3
"""Where each function of the harness is in the calculator build, for the
ARM926 plugin's imap= (instruction cache at the calculator's addresses):
    tools/calc_speed/imap.py HARNESS_ELF CALCULATOR_ELF > imap.txt
Lines "start end calc_start" (hex) for the code symbols both have (and the
Super FX translator's code buffer, which runs); a symbol ends at its size or
at the next symbol. Prints to stderr how many matched and how many differ in
size (code built differently in the two)."""
import subprocess, sys

def symbols(elf):
    out = subprocess.check_output(['arm-none-eabi-nm', '-n', '-S', '--defined-only', elf], text=True)
    syms = []
    for l in out.split('\n'):
        p = l.split()
        if len(p) == 4:
            addr, size, kind, name = int(p[0], 16), int(p[1], 16), p[2], p[3]
        elif len(p) == 3:
            addr, size, kind, name = int(p[0], 16), None, p[1], p[2]
        else:
            continue
        if kind in 'tTwW' or name == 'fx_jit_code':
            if name.startswith('$'):
                continue
            syms.append([addr, size, name])
    syms.sort(key=lambda s: (s[0], s[2]))
    # no size: up to the next symbol
    for i, s in enumerate(syms):
        nxt = syms[i + 1][0] if i + 1 < len(syms) else s[0] + 4
        if s[1] is None or s[1] == 0:
            s[1] = max(4, nxt - s[0])
    return {name: (addr, size) for addr, size, name in syms}

h = symbols(sys.argv[1])
c = symbols(sys.argv[2])
rows, differ = [], 0
for name, (addr, size) in h.items():
    if name in c:
        caddr, csize = c[name]
        if csize != size:
            differ += 1
        rows.append((addr, addr + size, caddr))
rows.sort()
# drop overlaps (aliases at one address)
last = -1
for a, b, ca in rows:
    if a < last:
        continue
    print('%x %x %x' % (a, b, ca))
    last = b
sys.stderr.write('imap: %d of %d harness code symbols found in the calculator build, %d differ in size\n'
                 % (len(rows), len(h), differ))
