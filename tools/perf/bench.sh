#!/bin/bash
# Benchmarks every scene in tools/perf/scenes.txt with the speed model
# (tests/calc_speed.sh) and prints one table; keeps it in build/perf/LABEL.txt.
#   tools/perf/bench.sh LABEL [MHZ] [BASELINE.txt]
# MHZ (default 132, the older CX) only scales the ms and fps columns; the
# instruction counts are exact. With BASELINE.txt (an earlier table) a last
# column shows the change in instructions per frame. The scenes run one
# after the other (calc_speed.sh shares one harness folder): about 30 s.
set -u
cd "$(dirname "$0")/../.."
LABEL=${1:-bench}
MHZ=${2:-132}
BASE=${3:-}
ROOT=$HOME/nspire/build
OUT=build/perf
mkdir -p "$OUT"
command -v arm-none-eabi-g++ >/dev/null || . "$ROOT/env.sh" >/dev/null 2>&1
export MHZ
trim() { echo "$1" | sed 's/^ *//; s/ *$//'; }
run_scene() {
    local name=$1 rom=$2 state=$3 frames=$4
    local log=$OUT/$LABEL.$name.log
    local st=-
    [ "$state" != - ] && st=$ROOT/$state
    tests/calc_speed.sh "$ROOT/$rom" "$st" "$frames" asm none > "$log" 2>&1
    local instr draw cpu ppu frame fps p95
    instr=$(grep -o 'ARM instructions per frame: [0-9]*' "$log" | grep -o '[0-9]*$')
    draw=$(grep '^  drawing' "$log" | awk '{print $(NF-1)}')
    cpu=$(grep '^  CPU (65c816)' "$log" | awk '{print $(NF-1)}')
    ppu=$(grep '^  PPU, DMA, timing' "$log" | awk '{print $(NF-1)}')
    frame=$(grep -o 'MHz: [0-9.]* ms per frame' "$log" | grep -o '[0-9.]* ms' | cut -d' ' -f1)
    fps=$(grep -o '= [0-9.]* fps with no speed limit' "$log" | grep -o '[0-9.]*' | head -1)
    p95=$(grep -o 'within [0-9.]* ms' "$log" | grep -o '[0-9.]*' | head -1)
    if [ -z "$instr" ]; then
        echo "$name  FAILED (see $log)"
        return
    fi
    local delta=""
    if [ -n "$BASE" ] && [ -f "$BASE" ]; then
        local was
        was=$(awk -v n="$name" '$1 == n {print $2}' "$BASE")
        if [ -n "$was" ] && [ "$was" -gt 0 ] 2>/dev/null; then
            delta=$(awk -v a="$instr" -v b="$was" 'BEGIN {printf "%+.2f%%", (a - b) * 100 / b}')
        fi
    fi
    printf '%-12s %9s %7s %7s %6s %8s %6s %7s  %s\n' "$name" "$instr" "$draw" "$cpu" "$ppu" "$frame" "$fps" "$p95" "$delta"
}
{
    printf '%-12s %9s %7s %7s %6s %8s %6s %7s  %s\n' scene instr/fr draw_ms cpu_ms ppu_ms frame_ms fps p95_ms vs_base
    while IFS='|' read -r name rom state frames; do
        name=$(trim "$name")
        case $name in ''|'#'*) continue ;; esac
        run_scene "$name" "$(trim "$rom")" "$(trim "$state")" "$(trim "$frames")"
    done < tools/perf/scenes.txt
} | tee "$OUT/$LABEL.txt"
echo "(speed model at $MHZ MHz, build $(git rev-parse --short HEAD)$(git diff --quiet HEAD -- pocketsnes frontend || echo -dirty); table in $OUT/$LABEL.txt)"
