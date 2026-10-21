#!/usr/bin/env python3
"""Writes the linker's section ordering file (frontend/hot_sections.ld) from
the ARM926 plugin's cache misses, so the code that misses the instruction
cache most sits together and can run from the on-chip SRAM
(platform_nspire.cpp, "Code in the SRAM"):
    tools/calc_speed/hot_sections.py HARNESS_DIR MISSES[:FRAMES] ... > frontend/hot_sections.ld
HARNESS_DIR is the harness build (tests/calc_speed.sh's build/calc-speed):
its game_test and object files name each function's section (the core is
built with -ffunction-sections, the assembler files have one section each).
Each MISSES file (a run's FILE.misses, with the frames it ran, default 300)
counts the same, per frame. Sections are listed by instruction misses per
byte, between .text.sram_code_begin and .text.sram_code_end, as lines for
the .text of frontend/pocketsnes.ld; the program
moves as much of that stretch into the SRAM as fits."""
import bisect, glob, os, subprocess, sys

hdir = sys.argv[1]
elf = os.path.join(hdir, 'game_test')
section_of, size_of = {}, {}
for o in glob.glob(os.path.join(hdir, '**', '*.o'), recursive=True):
    for l in subprocess.check_output(['arm-none-eabi-objdump', '-t', o], text=True).split('\n'):
        p = l.split()
        # "00000000 g     F .text._Z9S9xSetPPUth	00001234 _Z9S9xSetPPUth"
        if len(p) >= 5 and p[-3].startswith('.text') and p[-1] != p[-3]:
            section_of.setdefault(p[-1], p[-3])
    for l in subprocess.check_output(['arm-none-eabi-objdump', '-h', o], text=True).split('\n'):
        p = l.split()
        if len(p) >= 3 and p[1].startswith('.text'):
            size_of[p[1]] = max(size_of.get(p[1], 0), int(p[2], 16))
syms = []
for l in subprocess.check_output(['arm-none-eabi-nm', '-n', elf], text=True).split('\n'):
    p = l.split()
    if len(p) == 3 and p[1] in 'tTwW' and not p[2].startswith('$') and p[2] in section_of:
        syms.append((int(p[0], 16), p[2]))
addrs = [a for a, _ in syms]
misses = {}
for arg in sys.argv[2:]:
    path, _, frames = arg.partition(':')
    frames = int(frames or 300)
    for l in open(path).read().split('\n')[1:]:
        if not l.strip():
            continue
        pc, im, dm = l.split()
        i = bisect.bisect_right(addrs, int(pc, 16)) - 1
        if i >= 0:
            sec = section_of[syms[i][1]]
            misses[sec] = misses.get(sec, 0) + int(im) / frames
ranked = sorted((s for s in misses if size_of.get(s)), key=lambda s: -misses[s] / size_of[s])
total = sum(misses.values())
# Lines of frontend/pocketsnes.ld's .text (it INCLUDEs the file)
print('/* Made by tools/calc_speed/hot_sections.py */')
print('*(.text.sram_code_begin)')
cum = size = 0
for s in ranked:
    cum += misses[s]
    size += size_of[s]
    print('*(%s)' % s)
    print('%7.1f KB %5.1f%%  %s' % (size / 1024, 100 * cum / total, s), file=sys.stderr)
print('*(.text.sram_code_end)')
