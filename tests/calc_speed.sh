#!/bin/bash
# Estimates how fast a game runs on the calculator (TI-Nspire CX II, 396 MHz,
# no overclock), from a PC:
#   tests/calc_speed.sh ROM [STATE] [FRAMES] [CORE] [INPUT]
#     STATE   a PocketSNES state (.svNNN.tns) to start from, or - (power-on)
#     FRAMES  frames to time (default 300; the first 10 are left out)
#     CORE    asm (the ARM CPU core, the default setting) or c
#     INPUT   none (default, like the speed test), walk, start or busy
# FUNCS=N also lists the N functions that run the most instructions.
# ANNOTATE='SYMBOL ...' prints those functions' instructions with how many
# times a frame each ran (symbols as arm-none-eabi-nm prints them, mangled).
# It builds tests/asm/game_test with the calculator build's compiler flags
# (Makefile.nspire), runs it under a qemu-arm with plugin support and the
# ARM926 plugin in tools/calc_speed, and turns the ARM instructions run per
# frame into milliseconds with numbers fitted to three speed tests run on
# the calculator (Super Mario World, 396 MHz, 2026-10-10: 3.70 ms per frame
# with nothing drawn on the ARM CPU core, 4.74 on the C core, 79.2 fps with
# every frame drawn and the no-tearing output): 2.58 cycles per instruction
# running the game, 2.64 drawing it, 0.63 ms more per frame (the speed
# test's own work, interrupts), and 2.0 ms per drawn frame to show it. It's
# an estimate: run the speed test on the calculator to be sure (TESTING.md).
#
# Needs: the Ndless toolchain (. ~/nspire/build/env.sh) and a qemu-arm built
# with --enable-plugins (QEMU=, default the one in
# ~/nspire/build/tools/qemu-plugins; TESTING.md says how to build it).
set -u
cd "$(dirname "$0")/.."
ROM=$1
STATE=${2:--}
FRAMES=${3:-300}
CORE=${4:-asm}
INPUT=${5:-none}
QEMU=${QEMU:-$HOME/nspire/build/tools/qemu-plugins/qemu-10.2.1/build/qemu-arm}
QEMU_INCLUDE=${QEMU_INCLUDE:-$(dirname "$(dirname "$QEMU")")/include/qemu}
BUILD=${BUILD:-build/calc-speed}
CPI_GAME=${CPI_GAME:-2.58}       # cycles per ARM instruction running the game
CPI_DRAW=${CPI_DRAW:-2.64}       # ... drawing it
FRAME_MS=${FRAME_MS:-0.63}       # ms more per frame
SHOW_MS=${SHOW_MS:-2.0}          # ms per frame to show the picture (no-tearing output)
MHZ=${MHZ:-396}
FUNCS=${FUNCS:-0}                # list the busiest functions too
ANNOTATE=${ANNOTATE:-}

command -v arm-none-eabi-g++ >/dev/null || { echo "no Ndless toolchain: . ~/nspire/build/env.sh"; exit 1; }
[ -x "$QEMU" ] || { echo "no qemu-arm with plugins at $QEMU (see TESTING.md)"; exit 1; }

# The game harness, built like the calculator build
make -s -f tests/asm/Makefile BUILD="$BUILD" ARCH='-marm -march=armv5te -mtune=arm926ej-s' \
    CORE_FLAGS='$(ARCH) -O3 -fomit-frame-pointer -fno-strict-aliasing -ffunction-sections -fdata-sections $(DEFINES) $(INCLUDE) -w' \
    "$BUILD/game_test" || exit 1
PLUGIN=$BUILD/arm926.so
if [ ! -e "$PLUGIN" ] || [ tools/calc_speed/arm926.c -nt "$PLUGIN" ]; then
    gcc -O2 -shared -fPIC -I"$QEMU_INCLUDE" $(pkg-config --cflags glib-2.0) tools/calc_speed/arm926.c \
	-o "$PLUGIN" $(pkg-config --libs glib-2.0) || exit 1
fi

# Semihosting splits the command line at spaces: link the files under plain names
mkdir -p "$BUILD/files"
ln -sf "$(realpath "$ROM")" "$BUILD/files/rom"
state_arg=-
if [ "$STATE" != - ]; then
    ln -sf "$(realpath "$STATE")" "$BUILD/files/state"
    state_arg=$BUILD/files/state
fi
OUT=$BUILD/files/model.out
MARK=$(arm-none-eabi-nm "$BUILD/game_test" | awk '/ timing_frame_mark$/ {print $1}')
"$QEMU" -cpu arm926 -plugin "$PLUGIN,mark=$MARK,out=$OUT" "$BUILD/game_test" "$BUILD/files/rom" \
    "$FRAMES" "$CORE" "$state_arg" - "$INPUT-timing" > "$BUILD/files/run.txt" 2>&1 ||
    { cat "$BUILD/files/run.txt"; exit 1; }
head -1 "$BUILD/files/run.txt"

# Instructions per frame, overall and by part of the emulator
arm-none-eabi-nm -n -C "$BUILD/game_test" > "$BUILD/files/symbols.txt"
python3 - "$OUT" "$BUILD/files/symbols.txt" "$CPI_GAME" "$CPI_DRAW" "$FRAME_MS" "$SHOW_MS" "$MHZ" "$FUNCS" <<'EOF'
import sys, bisect, collections
out, symfile = sys.argv[1], sys.argv[2]
cpi_game, cpi_draw, frame_ms, show_ms, mhz = map(float, sys.argv[3:8])
funcs = int(sys.argv[8])
syms = []
for l in open(symfile):
    p = l.rstrip('\n').split(' ', 2)
    if len(p) == 3 and p[1] in 'tTwW':
        syms.append((int(p[0], 16), p[2]))
syms.sort(); addrs = [a for a, _ in syms]
lines = open(out).read().split('\n')
frames = [int(x) for x in lines[3].split()[1:]]   # instructions per frame
per_pc = []
for l in lines[4:]:
    if l.strip():
        pc, n, c = l.split()
        per_pc.append((int(pc, 16), int(n)))
def part(name):
    if name.startswith(('fx_', 'FxEmulate', 'S9xSuperFX')):
        return 'Super FX'
    if any(k in name for k in ('Draw', 'Render', 'S9xUpdateScreen', 'Tile', 'S9xSetupOBJ', 'Mode7', 'ComputeClip',
                               'S9xBuildDirect', 'FLUSH_REDRAW', 'FillLine', 'S9xEndScreenRefresh', 'FixedColourOfLine',
                               'FillWords', 'FillDepth', 'FillPattern')) \
       or name in ('memset', 'memcpy'):
        return 'drawing'
    if name.startswith(('Op', 'S9xOpcode', 'S9xMainLoop', 'CPUShutdown', 'S9xGetByte', 'S9xSetByte', 'S9xGetWord',
                        'S9xSetWord', 'S9xSetPCBase', 'asm', 'os9x', 'Asm', 'lbl', 'mainLoop', 'doHBlank')):
        return 'CPU (65c816)'
    if name.startswith(('S9xSetPPU', 'S9xGetPPU', 'S9xSetCPU', 'S9xGetCPU', 'S9xDoHDMA', 'S9xDoDMA', 'REGISTER_',
                        'S9xDoHBlank', 'S9xFixColour', 'S9xStartHDMA', 'S9xQueueRenderLine', 'S9xFlushQueued')):
        return 'PPU, DMA, timing'
    if name.startswith(('SA1', 'S9xSA1')):
        return 'SA-1'
    return 'other'
total = sum(n for _, n in per_pc)
parts = collections.Counter()
by_func = collections.Counter()
for pc, n in per_pc:
    i = bisect.bisect_right(addrs, pc) - 1
    name = syms[i][1] if i >= 0 else '?'
    parts[part(name)] += n
    by_func[name] += n
nframes = len(frames)
skip = min(10, max(0, nframes - 1))     # the first frames after loading a state
timed = sorted(frames[skip:])
draw_share = parts['drawing'] / max(1, total)
def frame_ms_of(insns):
    """ms per frame for this many instructions, split as on average between drawing and the rest"""
    return insns * (draw_share * cpi_draw + (1 - draw_share) * cpi_game) / (mhz * 1000.0) + frame_ms
per_frame = sum(timed) / len(timed)
print("%d frames timed; ARM instructions per frame: %.0f (slowest frame %d; 95%% of frames %d or fewer)"
      % (len(timed), per_frame, timed[-1], timed[int(len(timed) * 0.95) - 1]))
for name, n in parts.most_common():
    cpi = cpi_draw if name == 'drawing' else cpi_game
    print("  %-18s %8.0f per frame  %5.2f ms" % (name, n / nframes, n / nframes * cpi / (mhz * 1000.0)))
if funcs:
    print("The busiest functions (ARM instructions per frame, part):")
    for name, n in by_func.most_common(funcs):
        print("  %8.0f  %-16s %s" % (n / nframes, part(name), name[:80]))
avg_ms = frame_ms_of(per_frame) + show_ms
slow_ms = frame_ms_of(timed[int(len(timed) * 0.95) - 1]) + show_ms
print("Estimated on the calculator at %.0f MHz: %.2f ms per frame for the game (running and drawing it,"
      " %.2f of that the emulator's fixed cost) + %.1f ms showing it = %.1f fps with no speed limit"
      % (mhz, frame_ms_of(per_frame), frame_ms, show_ms, 1000.0 / avg_ms))
print("95%% of frames within %.1f ms (%.1f fps). Normal play holds 60 fps while frames take 16.7 ms or less."
      % (slow_ms, 1000.0 / slow_ms))
EOF

# The instructions of chosen functions, with how often each ran a frame
for sym in $ANNOTATE; do
    arm-none-eabi-objdump -d --no-show-raw-insn --disassemble="$sym" "$BUILD/game_test" |
    python3 -c '
import sys, re
lines = open(sys.argv[1]).read().split("\n")
nframes = len(lines[3].split()) - 1
counts = {}
for l in lines[4:]:
    if l.strip():
        pc, n, c = l.split()
        counts[int(pc, 16)] = int(n)
total = 0
for l in sys.stdin:
    m = re.match(r"\s*([0-9a-f]+):\s*(.*)", l)
    if m:
        n = counts.get(int(m.group(1), 16), 0)
        total += n
        print("%9.0f  %s" % (n / nframes, l.strip()))
    elif l.strip().endswith(">:"):
        print(l.strip())
print("%9.0f  in all" % (total / nframes))' "$OUT"
done
