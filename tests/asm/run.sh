#!/bin/bash
# The ARM assembler CPU core (pocketsnes/arm) against the C core, on a PC:
# both are built for ARM and run under qemu-arm.
#   tests/asm/run.sh [frames]
# 0. fx_test: the Super FX interpreters (C and ARM assembler) against the
#    original C loop, on random instructions and programs (tests/fx_test.cpp).
# 1. opcode_test: every opcode in emulation mode and the four M/X modes, from
#    random registers and memory; registers, cycles and memory must match.
# 2. game_test: games run once on each core with the same scripted input;
#    every frame's RAM, VRAM, palette, sprites, I/O registers, picture and
#    registers must match. Uses the homebrew demos tests/run.sh downloads and,
#    if they are there, the commercial ROMs in ~/nspire/build/testroms.
# Needs the Ndless toolchain (. ~/nspire/build/env.sh) and qemu-arm (QEMU=,
# by default the one unpacked in ~/nspire/build/tools/qemu).
set -u
cd "$(dirname "$0")/../.."

FRAMES=${1:-600}
QEMU=${QEMU:-$HOME/nspire/build/tools/qemu/root/usr/bin/qemu-arm}
TESTROMS=${TESTROMS:-$HOME/nspire/build/testroms}
BUILD=build/asm-test
OUT=$BUILD/out/$(date +%Y%m%d-%H%M%S)

[ -x "$QEMU" ] || { echo "no qemu-arm at $QEMU"; exit 1; }
make -s -f tests/asm/Makefile -j"$(nproc)" >/dev/null || { echo "build failed"; exit 1; }
mkdir -p "$OUT" "$BUILD/roms"

PASS=0
FAIL=0
result() {
    if [ "$1" = pass ]; then echo "PASS  $2"; PASS=$((PASS + 1))
    else echo "FAIL  $2"; FAIL=$((FAIL + 1)); fi
}

echo "== Super FX interpreters against the original loop (random instructions and programs)"
for which in c asm; do
    "$QEMU" -cpu arm926 $BUILD/fx_test 40 7 $which > "$OUT/fx_$which.txt" 2>&1
    last=$(tail -1 "$OUT/fx_$which.txt")
    case "$last" in PASS*) r=pass ;; *) r=fail ;; esac
    result $r "Super FX, $which: $last"
done

echo "== Opcodes (both cores, one opcode at a time)"
for seed in 1 2; do
    "$QEMU" -cpu arm926 $BUILD/opcode_test 100 $seed all openbus > "$OUT/opcodes_$seed.txt" 2>&1
    last=$(tail -1 "$OUT/opcodes_$seed.txt")
    case "$last" in PASS*) r=pass ;; *) r=fail ;; esac
    result $r "opcodes, seed $seed: $last"
done

# Semihosting splits the command line at spaces: link the ROMs to plain names.
link() { [ -e "$2" ] && ln -sf "$(realpath "$2")" "$BUILD/roms/$1"; }
for rom in tests/roms/*.sfc; do link "$(basename "$rom")" "$rom"; done
link smw.sfc "$TESTROMS/Super Mario World (USA).sfc.tns"
link smw.sv001 "$TESTROMS/smw.sv001.tns"
link ct.sfc "$TESTROMS/calc/Chrono Trigger (USA).sfc.tns"
link alttp.sfc "$TESTROMS/calc/Legend of Zelda, The - A Link to the Past (USA).sfc.tns"
link yi.sfc "$TESTROMS/calc/Super Mario World 2 - Yoshi's Island.sfc.tns"
link yi.sv001 "$TESTROMS/calc/Super Mario World 2 - Yoshi's Island.sfc.sv001.tns"

# game NAME ROM STATE INPUT: both cores, frame by frame
game() {
    local name=$1 rom=$BUILD/roms/$2 state=$3 input=$4
    if [ ! -e "$rom" ]; then echo "SKIP  $name (no $2)"; return; fi
    [ "$state" = - ] || state=$BUILD/roms/$state
    for core in c asm; do
        "$QEMU" -cpu arm926 $BUILD/game_test "$rom" "$FRAMES" $core "$state" \
            "$OUT/${name}_${core}_" "$input" > "$OUT/${name}_$core.txt" 2>&1
    done
    local frames_c frames_asm first
    frames_c=$(grep -c '^frame' "$OUT/${name}_c.txt")
    frames_asm=$(grep -c '^frame' "$OUT/${name}_asm.txt")
    first=$(diff <(grep '^frame' "$OUT/${name}_c.txt") <(grep '^frame' "$OUT/${name}_asm.txt") |
            grep -m1 '^<' | cut -d' ' -f3)
    if [ "$frames_c" -eq "$FRAMES" ] && [ "$frames_asm" -eq "$FRAMES" ] && [ -z "$first" ]; then
        result pass "$name: $FRAMES frames the same"
    else
        result fail "$name: C ran $frames_c frames, assembler $frames_asm, first different frame ${first:-none}"
    fi
}

echo "== Games ($FRAMES frames on each core)"
game rotzoom RotZoom.sfc - none
game perspective Perspective.sfc - none
game mode7hdma Mode7HDMA.sfc - none
game rings Rings.sfc - none
game monsterfarm MonsterFarmJump.sfc - busy
game sram SramWrite.sfc - none
game smw_state smw.sfc smw.sv001 walk
game smw_boot smw.sfc - start
game ct_boot ct.sfc - start
game alttp_boot alttp.sfc - start
game yi_state yi.sfc yi.sv001 walk
game yi_boot yi.sfc - start
# Super FX demos (plotting polygons and lines; the ARM core runs the ARM
# Super FX interpreter, the C core the C one)
game gsu_fillpoly GSU4BPP256x192FillPoly.sfc - none
game gsu_plotline GSU8BPP256x192PlotLine.sfc - none

# The overclock test's game check (emu.cpp): from a snapshot, the same
# frames twice must leave the same memory, on each core.
replay() {
    local name=$1 rom=$BUILD/roms/$2 state=$3 input=$4
    if [ ! -e "$rom" ]; then echo "SKIP  replay $name (no $2)"; return; fi
    [ "$state" = - ] || state=$BUILD/roms/$state
    for core in c asm; do
        local last
        last=$("$QEMU" -cpu arm926 $BUILD/game_test "$rom" 120 $core "$state" - "$input-replay" 2>&1 | tail -1)
        case "$last" in "replay: the same"*) result pass "replay $name ($core): $last" ;;
            *) result fail "replay $name ($core): $last" ;; esac
    done
}

echo "== Replays from a snapshot (the overclock test)"
replay smw_state smw.sfc smw.sv001 walk
replay ct_boot ct.sfc - start
replay monsterfarm MonsterFarmJump.sfc - busy

echo "== $PASS passed, $FAIL failed (output in $OUT)"
[ "$FAIL" -eq 0 ]
