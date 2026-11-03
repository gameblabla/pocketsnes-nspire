#!/bin/bash
# The busiest functions of one scene from tools/perf/scenes.txt:
#   FUNCS=25 tools/perf/profile.sh SCENE            (default FUNCS=20)
#   ANNOTATE='DrawBGCommands16 ...' tools/perf/profile.sh SCENE
set -u
cd "$(dirname "$0")/../.."
ROOT=$HOME/nspire/build
command -v arm-none-eabi-g++ >/dev/null || . "$ROOT/env.sh" >/dev/null 2>&1
line=$(grep -v '^ *#' tools/perf/scenes.txt | awk -F'|' -v OFS='|' -v n="$1" '{gsub(/^ +| +$/, "", $1)} $1 == n')
[ -n "$line" ] || { echo "no scene $1 in tools/perf/scenes.txt"; exit 1; }
IFS='|' read -r name rom state frames <<< "$line"
trim() { echo "$1" | sed 's/^ *//; s/ *$//'; }
rom=$(trim "$rom"); state=$(trim "$state"); frames=$(trim "$frames")
st=-
[ "$state" != - ] && st=$ROOT/$state
FUNCS=${FUNCS:-20} MHZ=${MHZ:-132} tests/calc_speed.sh "$ROOT/$rom" "$st" "$frames" asm none
