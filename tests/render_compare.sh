#!/bin/bash
# Checks that the renderer still draws exactly what the original one does:
# each ROM is played with the same keys by the reference build (the original
# renderer, ./pocketsnes-host-ref) and by the current build, with a
# screenshot every 2 frames, and the screenshots must be identical.
#   tests/render_compare.sh [ROM...]      (default: the test ROMs, SMW and the
#                                          games in ~/nspire/build/testroms/calc)
# A ROM's in-game save (NAME.srm.tns) and save state (NAME.sv001.tns) are used
# when they sit next to it. Games with a state walk right from it; the others
# start from power-on with a longer script that presses Start and A to get
# through the title screens and menus.
#   REF=path tests/render_compare.sh      another reference build
set -u
cd "$(dirname "$0")/.."
make -s -j"$(nproc)" >/dev/null || { echo "build failed"; exit 1; }
HOST=$PWD/pocketsnes-host
REF=$(realpath "${REF:-pocketsnes-host-ref}")
[ -x "$REF" ] || { echo "no reference build at $REF"; exit 1; }
OUT=${OUT:-tests/out/render-$(date +%Y%m%d-%H%M%S)}
mkdir -p "$OUT"
TESTROMS=${TESTROMS:-$HOME/nspire/build/testroms}   # test ROMs outside the repo

roms=("$@")
if [ ${#roms[@]} -eq 0 ]; then
    roms=(tests/roms/*.sfc "$TESTROMS/Super Mario World (USA).sfc.tns")
    for r in "$TESTROMS"/*.sfc; do [ -e "tests/roms/$(basename "$r")" ] || roms+=("$r"); done
    for r in "$TESTROMS"/calc/*.sfc.tns; do roms+=("$r"); done
fi

# Keys: walk right, jump now and then, with a shot every 2 frames.
script=$(realpath "$OUT")/script.txt
{
    echo "wait 130"   # past the 2-second "Resumed from slot" message
    echo "down right"
    for i in $(seq 1 300); do
        echo "shot s$(printf %04d "$i").png"
        [ $((i % 40)) -eq 10 ] && echo "down ctrl"
        [ $((i % 40)) -eq 25 ] && echo "up ctrl"
        [ $((i % 60)) -eq 30 ] && echo "down shift"
        [ $((i % 60)) -eq 50 ] && echo "up shift"
        echo "wait 2"
    done
    echo "quit"
} > "$script"

# From power-on: Start every 3 seconds, A every second, walking about.
boot_script=$(realpath "$OUT")/boot_script.txt
{
    echo "wait 4"   # the screen before the game's first frame differs between front ends
    for i in $(seq 1 600); do
        [ $((i % 45)) -eq 1 ] && echo "press enter 3"
        [ $((i % 15)) -eq 8 ] && echo "press ctrl 3"
        [ $((i % 60)) -eq 20 ] && echo "down right"
        [ $((i % 60)) -eq 40 ] && echo "up right"
        [ $((i % 120)) -eq 70 ] && echo "press up 10"
        echo "shot s$(printf %04d "$i").png"
        echo "wait 4"
    done
    echo "quit"
} > "$boot_script"

fail=0
for rom in "${roms[@]}"; do
    name=$(basename "$rom" .tns)
    for mode in ref new; do
        dir=$OUT/$name/$mode
        mkdir -p "$dir/ndless" "$dir/roms/.pocketsnes" "$dir/shots"
        cp "$rom" "$dir/roms/$name.tns"
        echo "auto_resume=1" > "$dir/ndless/pocketsnes.cfg.tns"
        rom_dir=$(dirname "$rom")
        run_script=$boot_script
        [ -e "$rom_dir/$name.srm.tns" ] && cp "$rom_dir/$name.srm.tns" "$dir/roms/.pocketsnes/"
        if [ -e "$rom_dir/$name.sv001.tns" ]; then
            cp "$rom_dir/$name.sv001.tns" "$dir/roms/.pocketsnes/"
            run_script=$script
        fi
        case "$name" in
            "Super Mario World (USA).sfc") cp "$TESTROMS/smw.sv001.tns" "$dir/roms/.pocketsnes/$name.sv001.tns"
                                           run_script=$script ;;
            *.sfc) [ "$rom_dir" = "$TESTROMS/calc" ] || run_script=$script ;;
        esac
        bin=$HOST
        [ $mode = ref ] && bin=$REF
        (cd "$dir" && "$bin" --script "$run_script" --shots shots \
            --exe-dir ndless "roms/$name.tns" >/dev/null 2>&1)
    done
    a=$OUT/$name/ref/shots b=$OUT/$name/new/shots
    n=$(ls "$a" | wc -l)
    diffs=0
    for f in "$a"/*.png; do cmp -s "$f" "$b/$(basename "$f")" || diffs=$((diffs + 1)); done
    distinct=$(md5sum "$a"/*.png | awk '{print $1}' | sort -u | wc -l)
    if [ "$n" -gt 0 ] && [ "$diffs" -eq 0 ] && [ "$(ls "$b" | wc -l)" -eq "$n" ]; then
        echo "PASS  $name: $n shots identical ($distinct different pictures)"
    else
        echo "FAIL  $name: $diffs of $n shots differ"; fail=1
    fi
done
exit $fail
