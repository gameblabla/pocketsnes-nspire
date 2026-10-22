#!/bin/bash
# Automated checks with the PC test build; no calculator needed.
#   tests/run.sh
# Builds ./pocketsnes-host, downloads a few free homebrew demos (PeterLemon's
# SNES test programs) into tests/roms, then drives the emulator with scripts
# and checks save states, auto-increment, auto-resume, in-game saves, the
# menu and the ROM browser. See TESTING.md.
set -u
cd "$(dirname "$0")/.."

make -s -j"$(nproc)" >/dev/null || { echo "build failed"; exit 1; }

ROMS=tests/roms
OUT=tests/out/$(date +%Y%m%d-%H%M%S)
mkdir -p "$ROMS" "$OUT"
HOST=$PWD/pocketsnes-host

PASS=0
FAIL=0
check() {
    if [ "$1" = "$2" ]; then echo "PASS  $3"; PASS=$((PASS + 1))
    else echo "FAIL  $3  (got '$1', expected '$2')"; FAIL=$((FAIL + 1)); fi
}

for path in PPU/Mode7/RotZoom/RotZoom.sfc PPU/Mode7/Perspective/Perspective.sfc \
            PPU/HDMA/Mode7HDMA/Mode7HDMA.sfc PPU/Rings/Rings.sfc \
            Games/MonsterFarmJump/MonsterFarmJump.sfc; do
    name=$(basename "$path")
    [ -f "$ROMS/$name" ] || curl -sSLf --max-time 60 -o "$ROMS/$name" \
        "https://raw.githubusercontent.com/PeterLemon/SNES/master/$path" ||
        { echo "couldn't download $name"; exit 1; }
done
[ -f "$ROMS/SramWrite.sfc" ] || python3 tests/make_sram_rom.py "$ROMS/SramWrite.sfc"
[ -f "$ROMS/SDD1Test.sfc" ] && [ "$ROMS/SDD1Test.sfc" -nt tests/make_sdd1_rom.py ] ||
    python3 tests/make_sdd1_rom.py "$ROMS/SDD1Test.sfc"

# setup NAME ROM [config lines...]: a fake calculator folder layout in $OUT/NAME,
# with a settings file (just "config_version=2" if no lines are given), so
# PocketSNES isn't on its first start and doesn't show the welcome screen
setup() {
    local dir=$OUT/$1 rom=$2
    shift 2
    mkdir -p "$dir/zPocketSNES_saves" "$dir/ndless" "$dir/roms" "$dir/shots"
    cp "$ROMS/$rom" "$dir/roms/$rom.tns"
    [ $# -gt 0 ] || set -- config_version=2
    for line in "$@"; do echo "$line" >> "$dir/zPocketSNES_saves/pocketsnes.cfg.tns"; done
}

# play NAME SCRIPT [ROM]: runs a script in $OUT/NAME (ROM omitted: start in the browser)
play() {
    local dir=$OUT/$1 script
    script=$(realpath "$2")
    shift 2
    (cd "$dir" && "$HOST" --script "$script" --shots shots --exe-dir ndless \
        ${1:+roms/$1.tns} >/dev/null 2>&1)
}

# play_logged NAME SCRIPT ROM [options]: like play, with the PC build's test
# log (clock changes, backlight changes and messages) in $OUT/NAME/log.txt;
# ROM - starts in the game list
play_logged() {
    local dir=$OUT/$1 script rom=$3 rom_arg=()
    script=$(realpath "$2")
    shift 3
    [ "$rom" = - ] || rom_arg=("roms/$rom.tns")
    (cd "$dir" && "$HOST" --script "$script" --shots shots --exe-dir ndless --log log.txt "$@" \
        "${rom_arg[@]}" >/dev/null 2>&1)
}
# The clock changes and CPU messages in a test log, on one line.
clock_log() { grep -e '^clock' -e '^message: CPU' "$1" | tr '\n' '|'; }

same() { cmp -s "$1" "$2" && echo same || echo different; }
exists() { [ -e "$1" ] && echo yes || echo no; }
order() { tr '\n' ' ' < "$1" 2>/dev/null; }

echo "== Unit tests (tile drawing, frame turning, settings files, key bindings)"
make -s unit-tests > "$OUT/unit_tests.txt" 2>&1 && r=pass || r=fail
check "$r" pass "$(tail -1 "$OUT/unit_tests.txt")"

echo "== Save states restore the game exactly"
for rom in RotZoom Mode7HDMA Perspective Rings; do
    setup "rt_$rom" "$rom.sfc"
    play "rt_$rom" tests/scripts/roundtrip_input.txt "$rom.sfc"
    d=$OUT/rt_$rom/shots
    check "$(same "$d/A.png" "$d/B.png")" same "$rom: same picture 150 frames after saving and after loading"
    check "$(same "$d/A.png" "$d/C.png")" different "$rom: the picture moved (the check means something)"
done
setup rt_game MonsterFarmJump.sfc
play rt_game tests/scripts/roundtrip.txt MonsterFarmJump.sfc
d=$OUT/rt_game/shots
check "$(same "$d/A.png" "$d/B.png")" same "Monster Farm Jump: same picture after saving and after loading"
check "$(same "$d/A.png" "$d/C.png")" different "Monster Farm Jump: the picture moved"

echo "== Auto-increment, saving when leaving, loading on start"
setup auto MonsterFarmJump.sfc config_version=2 auto_increment=1 save_on_exit=1 load_on_start=1
play auto tests/scripts/save3_quit.txt MonsterFarmJump.sfc
s=$OUT/auto/zPocketSNES_saves/MonsterFarmJump.sfc
check "$(exists "$s.sv001.tns")$(exists "$s.sv002.tns")$(exists "$s.sv003.tns")" yesyesyes "three saves went to slots 1, 2 and 3"
check "$(exists "$s.sv004.tns")" yes "quitting with Q saved slot 4 (save when leaving)"
play auto tests/scripts/wait_quit.txt MonsterFarmJump.sfc
check "$(exists "$s.sv005.tns")" yes "the next run loaded the newest and saved slot 5 when it closed"

# Files where older versions kept them: the settings next to the program,
# states in a hidden .pocketsnes folder. They move into zPocketSNES_saves.
setup oldfolders MonsterFarmJump.sfc
rm -r "$OUT/oldfolders/zPocketSNES_saves"
printf 'config_version=3\nshow_fps=1\nsave_on_exit=0\n' > "$OUT/oldfolders/ndless/pocketsnes.cfg.tns"
mkdir -p "$OUT/oldfolders/roms/.pocketsnes"
cp "$s.sv001.tns" "$OUT/oldfolders/roms/.pocketsnes/"
play oldfolders tests/scripts/wait_quit.txt MonsterFarmJump.sfc
d=$OUT/oldfolders
check "$(exists "$d/zPocketSNES_saves/MonsterFarmJump.sfc.sv001.tns")$(exists "$d/roms/.pocketsnes")$(grep -c '^show_fps=1$' "$d/zPocketSNES_saves/pocketsnes.cfg.tns")$(exists "$d/ndless/pocketsnes.cfg.tns")" \
    yesno1no "older files move into zPocketSNES_saves: the states, and the settings with their values"

# The calculator's way: the program in the ROM folder (My Documents/snes),
# with the files of the two older layouts in it (.pocketsnes, and a
# PocketSNES_saves folder in the ROM folder). Everything moves to
# zPocketSNES_saves in My Documents, and the snes folder is left with no
# folder in it, so _PocketSNES.tns is the first thing there.
d=$OUT/snesfolder
mkdir -p "$d/snes/.pocketsnes" "$d/snes/PocketSNES_saves" "$d/shots"
cp "$ROMS/MonsterFarmJump.sfc" "$d/snes/MonsterFarmJump.sfc.tns"
cp "$ROMS/Rings.sfc" "$d/snes/Rings.sfc.tns"
printf 'config_version=3\nshow_fps=1\nsave_on_exit=1\nload_on_start=0\n' > "$d/snes/pocketsnes.cfg.tns"
cp "$s.sv001.tns" "$d/snes/.pocketsnes/MonsterFarmJump.sfc.sv001.tns"
printf 'Rings.sfc.tns\nMonsterFarmJump.sfc.tns\n' > "$d/snes/.pocketsnes/game_order.txt.tns"
cp "$s.sv001.tns" "$d/snes/PocketSNES_saves/MonsterFarmJump.sfc.sv002.tns"
script=$(realpath tests/scripts/list_number.txt)
(cd "$d" && "$HOST" --script "$script" --shots shots --exe-dir snes >/dev/null 2>&1)
p=$d/zPocketSNES_saves
check "$(exists "$p/MonsterFarmJump.sfc.sv001.tns")$(exists "$p/MonsterFarmJump.sfc.sv002.tns")$(grep -c '^show_fps=1$' "$p/pocketsnes.cfg.tns")$(order "$p/game_order_snes.txt.tns")" \
    "yesyes1Rings.sfc.tns MonsterFarmJump.sfc.tns " \
    "program in the ROM folder: states, settings and the game order move to My Documents/zPocketSNES_saves"
check "$(find "$d/snes" -mindepth 1 -type d | wc -l)$(ls "$d/snes" | tr '\n' ' ')" \
    "0MonsterFarmJump.sfc.tns Rings.sfc.tns " "... and the ROM folder has no folder (or settings file) left in it"
check "$(exists "$p/MonsterFarmJump.sfc.sv003.tns")" yes "... and the 2 key started the second game in the moved order (MonsterFarmJump)"

# The saves folder before it had the z: My Documents/PocketSNES_saves, listed
# above the snes folder (the calculator's layout on 2026-10-11). It moves into
# zPocketSNES_saves, below the game folders, and the old folder goes.
d=$OUT/docsfolder
o=$d/PocketSNES_saves
mkdir -p "$d/snes" "$o" "$d/shots"
cp "$ROMS/MonsterFarmJump.sfc" "$d/snes/MonsterFarmJump.sfc.tns"
cp "$ROMS/Rings.sfc" "$d/snes/Rings.sfc.tns"
printf 'config_version=3\nshow_fps=1\nsave_on_exit=1\nload_on_start=0\n' > "$o/pocketsnes.cfg.tns"
printf 'Rings.sfc.tns\nMonsterFarmJump.sfc.tns\n' > "$o/game_order_snes.txt.tns"
cp "$s.sv001.tns" "$o/MonsterFarmJump.sfc.sv001.tns"
printf 'sram' > "$o/Rings.sfc.srm.tns"
(cd "$d" && "$HOST" --script "$script" --shots shots --exe-dir snes >/dev/null 2>&1)
p=$d/zPocketSNES_saves
check "$(exists "$p/MonsterFarmJump.sfc.sv001.tns")$(exists "$p/Rings.sfc.srm.tns")$(grep -c '^show_fps=1$' "$p/pocketsnes.cfg.tns")$(order "$p/game_order_snes.txt.tns")$(exists "$o")" \
    "yesyes1Rings.sfc.tns MonsterFarmJump.sfc.tns no" \
    "My Documents/PocketSNES_saves moves into zPocketSNES_saves (states, in-game saves, settings, game order) and goes"
check "$(exists "$p/MonsterFarmJump.sfc.sv002.tns")$(LC_ALL=C ls "$d" | tr '\n' ' ')" "yesshots snes zPocketSNES_saves " \
    "... the 2 key started the second game in the kept order, and My Documents lists the saves folder last"

setup noinc MonsterFarmJump.sfc config_version=2 auto_increment=0 save_on_exit=0 load_on_start=0
play noinc tests/scripts/save3_quit.txt MonsterFarmJump.sfc
s=$OUT/noinc/zPocketSNES_saves/MonsterFarmJump.sfc
check "$(exists "$s.sv001.tns")$(exists "$s.sv002.tns")" yesno "without auto-increment, saves overwrite slot 1"

# Old (version 1) settings files: auto-increment, saving when leaving and
# loading on start come on; the rest is kept.
setup old MonsterFarmJump.sfc auto_increment=0 auto_resume=0 show_fps=1 key_a=6
play old tests/scripts/wait_quit.txt MonsterFarmJump.sfc
cfg=$OUT/old/zPocketSNES_saves/pocketsnes.cfg.tns
check "$(grep -c -e '^config_version=3$' -e '^auto_increment=1$' -e '^save_on_exit=1$' -e '^load_on_start=1$' \
    -e '^show_fps=1$' -e '^key_a=6$' "$cfg")" 6 "an old settings file is brought up to date and keeps its choices"

echo "== Where saving when leaving goes"
# The auto save slot: three saves go to slots 1-3, quitting to the auto slot.
setup autoslot MonsterFarmJump.sfc config_version=3 auto_increment=1 save_on_exit=1 auto_slot=1 load_on_start=1
play autoslot tests/scripts/save3_quit.txt MonsterFarmJump.sfc
s=$OUT/autoslot/zPocketSNES_saves/MonsterFarmJump.sfc
check "$(exists "$s.sv003.tns")$(exists "$s.sv004.tns")$(exists "$s.svauto.tns")" yesnoyes \
    "with Auto save slot on, quitting saved to the auto slot, not to slot 4"
play_logged autoslot tests/scripts/wait_quit.txt MonsterFarmJump.sfc
check "$(grep -c '^message: Resumed from the auto slot$' "$OUT/autoslot/log.txt")$(exists "$s.sv004.tns")" 1no \
    "... the next start resumed from the auto slot, and saved there again"

# New save on quit off: quitting saves over the newest state.
counter() { od -An -t u4 -j 8 -N 4 "$1" | tr -d ' '; }
setup overnewest MonsterFarmJump.sfc config_version=3 auto_increment=1 save_on_exit=1 quit_new_save=0 load_on_start=0
play overnewest tests/scripts/save3_quit.txt MonsterFarmJump.sfc
s=$OUT/overnewest/zPocketSNES_saves/MonsterFarmJump.sfc
check "$(exists "$s.sv004.tns") $(counter "$s.sv003.tns")" "no 4" \
    "with New save on quit off, quitting saved over the newest state (slot 3, now the 4th save)"

# Settings files from before New save on quit: it follows auto-increment, so
# with auto-increment off, quitting saves over the newest, as it did.
setup quitold MonsterFarmJump.sfc config_version=3 auto_increment=0 save_on_exit=1 load_on_start=0
play quitold tests/scripts/save3_quit.txt MonsterFarmJump.sfc
s=$OUT/quitold/zPocketSNES_saves/MonsterFarmJump.sfc
check "$(exists "$s.sv001.tns")$(exists "$s.sv002.tns")$(grep -c '^quit_new_save=0$' "$OUT/quitold/zPocketSNES_saves/pocketsnes.cfg.tns")" \
    yesno1 "an older settings file with auto-increment off gets New save on quit off"

echo "== Leaving: the OS's file list"
# On the calculator the OS reads all its files again at exit (slow with many
# files), only when PocketSNES added, removed or renamed one; the PC build
# logs "file list refreshed" there.
refreshed() { cat "$1" 2>/dev/null | grep -c '^file list refreshed$'; }
setup listnone MonsterFarmJump.sfc
play_logged listnone tests/scripts/wait_quit.txt -
check "$(refreshed "$OUT/listnone/log.txt")" 0 "opening the game list and leaving: no new files, no refresh"
setup liststate MonsterFarmJump.sfc config_version=3 auto_increment=0 save_on_exit=1 quit_new_save=0 load_on_start=0
play_logged liststate tests/scripts/wait_quit.txt MonsterFarmJump.sfc
s=$OUT/liststate/zPocketSNES_saves/MonsterFarmJump.sfc
check "$(exists "$s.sv001.tns")$(refreshed "$OUT/liststate/log.txt")" yes1 "a game saved a new state when it closed: refresh"
rm "$OUT/liststate/log.txt"
play_logged liststate tests/scripts/wait_quit.txt MonsterFarmJump.sfc
check "$(exists "$s.sv002.tns")$(refreshed "$OUT/liststate/log.txt")" no0 "... saved over it the next time: no refresh"
setup listold MonsterFarmJump.sfc
mv "$OUT/listold/zPocketSNES_saves/pocketsnes.cfg.tns" "$OUT/listold/ndless/"
play_logged listold tests/scripts/wait_quit.txt -
check "$(refreshed "$OUT/listold/log.txt")" 1 "an older version's file moved into the saves folder: refresh"

echo "== Brightness"
# The pretend calculator's own backlight is 112 (level 6); levels 7, 8, 3
# are 75, 50 and 175 (0 brightest, 225 darkest).
backlight_log() { grep -e '^backlight' -e '^message: Brightness' "$1" | tr '\n' '|'; }
setup bright MonsterFarmJump.sfc
play_logged bright tests/scripts/brightness_keys.txt MonsterFarmJump.sfc
check "$(backlight_log "$OUT/bright/log.txt")" \
    "backlight 75|message: Brightness 7/10|backlight 50|message: Brightness 8/10|backlight 75|message: Brightness 7/10|backlight 112|" \
    "+ and - in a game step the brightness from the calculator's own, and quitting puts it back"
check "$(grep -c '^brightness=7$' "$OUT/bright/zPocketSNES_saves/pocketsnes.cfg.tns")" 1 "... and the level is saved"
setup brightmenu MonsterFarmJump.sfc
play_logged brightmenu tests/scripts/brightness_menu.txt MonsterFarmJump.sfc
check "$(backlight_log "$OUT/brightmenu/log.txt")" "backlight 225|backlight 200|backlight 175|backlight 112|" \
    "Brightness in Graphics & speed sets the backlight as it changes (1, 2, 3), and quitting puts it back"
setup brightlist MonsterFarmJump.sfc
play_logged brightlist tests/scripts/brightness_list.txt -
check "$(backlight_log "$OUT/brightlist/log.txt")" "backlight 75|backlight 112|" "+ works in the game list too"

echo "== Keys"
# S alone is Next slot, shift+S (second key of Save state) saves.
setup combo MonsterFarmJump.sfc config_version=2 auto_increment=0 save_on_exit=0 load_on_start=0 \
    key_save_state=0 key_save_state_2=121+23 key_next_slot=23
play combo tests/scripts/combo_hotkey.txt MonsterFarmJump.sfc
s=$OUT/combo/zPocketSNES_saves/MonsterFarmJump.sfc
check "$(exists "$s.sv001.tns")$(exists "$s.sv002.tns")$(exists "$s.sv003.tns")" noyesno \
    "S moved to slot 2, shift+S saved there and didn't also move the slot"

echo "== Settings for one game"
setup pergame MonsterFarmJump.sfc config_version=2 auto_increment=1 save_on_exit=0 load_on_start=0
mkdir -p "$OUT/pergame/zPocketSNES_saves"
printf 'config_version=2\nown_settings=1\nauto_increment=0\nsave_on_exit=0\nload_on_start=0\n' \
    > "$OUT/pergame/zPocketSNES_saves/MonsterFarmJump.sfc.cfg.tns"
play pergame tests/scripts/save3_quit.txt MonsterFarmJump.sfc
s=$OUT/pergame/zPocketSNES_saves/MonsterFarmJump.sfc
check "$(exists "$s.sv001.tns")$(exists "$s.sv002.tns")" yesno "the game's own setting (auto-increment off) won over the one for all games"
check "$(grep -c '^auto_increment=1$' "$OUT/pergame/zPocketSNES_saves/pocketsnes.cfg.tns")" 1 "the setting for all games was kept"

echo "== CPU speed"
forever=$(realpath tests/scripts/forever.txt)
setup clock MonsterFarmJump.sfc config_version=2 cpu_speed=3 save_on_exit=0 load_on_start=0
play clock tests/scripts/wait_quit.txt MonsterFarmJump.sfc
marker=$OUT/clock/zPocketSNES_saves/pocketsnes_clock.tns
check "$(exists "$marker")$(grep -c '^cpu_speed=8$' "$OUT/clock/zPocketSNES_saves/pocketsnes.cfg.tns")" no1 \
    "leaving a game normally removes the raised-speed marker and keeps the setting (480 MHz, renumbered)"
# A freeze while playing at a raised speed: the run is killed.
(cd "$OUT/clock" && timeout -s KILL 3 "$HOST" --script "$forever" \
    --shots shots --exe-dir ndless roms/MonsterFarmJump.sfc.tns >/dev/null 2>&1)
check "$(exists "$marker")" yes "a freeze at a raised speed leaves the marker"
play clock tests/scripts/after_crash.txt MonsterFarmJump.sfc
check "$(exists "$marker")$(grep -c '^cpu_speed=0$' "$OUT/clock/zPocketSNES_saves/pocketsnes.cfg.tns")" no1 \
    "the next start puts the CPU speed back to normal"

# The same with the raised speed in the game's own settings.
setup clockgame MonsterFarmJump.sfc config_version=2 cpu_speed=0 save_on_exit=0 load_on_start=0
mkdir -p "$OUT/clockgame/zPocketSNES_saves"
game_cfg=$OUT/clockgame/zPocketSNES_saves/MonsterFarmJump.sfc.cfg.tns
printf 'config_version=2\nown_settings=1\ncpu_speed=3\nsave_on_exit=0\nload_on_start=0\n' > "$game_cfg"
(cd "$OUT/clockgame" && timeout -s KILL 3 "$HOST" --script "$forever" \
    --shots shots --exe-dir ndless roms/MonsterFarmJump.sfc.tns >/dev/null 2>&1)
play clockgame tests/scripts/after_crash.txt MonsterFarmJump.sfc
check "$(grep -c -e '^own_settings=1$' -e '^cpu_speed=0$' "$game_cfg")" 2 \
    "... also when it was the game's own setting"

# Changing the setting in the in-game menu changes the clock when the game
# resumes, and the game says what it got.
setup clockmenu MonsterFarmJump.sfc config_version=2 save_on_exit=0 load_on_start=0
play_logged clockmenu tests/scripts/cpu_speed_menu.txt MonsterFarmJump.sfc
check "$(clock_log "$OUT/clockmenu/log.txt")" \
    "clock x40|message: CPU 480 MHz|clock x0|message: CPU 396 MHz (normal)|" \
    "480 MHz set in the menu raises the clock when the game resumes; normal puts it back"
# A calculator whose clock doesn't move: back to normal, and the game says so.
setup clockstuck MonsterFarmJump.sfc config_version=2 cpu_speed=3 save_on_exit=0 load_on_start=0
play_logged clockstuck tests/scripts/wait_quit.txt MonsterFarmJump.sfc --stuck-clock
check "$(clock_log "$OUT/clockstuck/log.txt")" \
    "clock x40|clock x0|message: CPU speed not raised (still 396 MHz)|" \
    "a clock that doesn't reach the speed goes back to normal and says so"
check "$(exists "$OUT/clockstuck/zPocketSNES_saves/pocketsnes_clock.tns")" no "... and leaves no freeze marker"

echo "== Overclock test"
# In a game, on a PC acting like a calculator that doesn't go past 444 MHz:
# 408 to 444 pass, 456 doesn't switch.
setup octest MonsterFarmJump.sfc config_version=3 save_on_exit=0 load_on_start=0
play_logged octest tests/scripts/overclock_test.txt MonsterFarmJump.sfc --clock-limit 37
d=$OUT/octest
check "$(grep -c '^highest_tested_mhz=444$' "$d/zPocketSNES_saves/pocketsnes.cfg.tns")" 1 "the overclock test finds 444 MHz"
check "$(grep -c -e '^456 MHz: didn.t switch' -e '^Highest tested speed: 444 MHz$' "$d/zPocketSNES_saves/pocketsnes_overclock.txt.tns")" 2 \
    "... and writes what happened at each speed"
check "$(exists "$d/zPocketSNES_saves/pocketsnes_octest.tns")" no "... and leaves no marker"
check "$(grep -c '^message: Overclock test: 444 MHz, 2 of 2' "$OUT/octest/log.txt")" 1 \
    "... and played the game again at each speed (the game ran the same at normal speed and raised)"
# Super Mario World talks to the sound chip, which this port answers with
# rand(): the game check has to start each run the same way, or it sees a
# difference at every speed (it stopped at 408 MHz on the calculator).
SMW="$HOME/nspire/build/testroms/Super Mario World (USA).sfc.tns"
if [ -e "$SMW" ]; then
    setup ocsmw MonsterFarmJump.sfc config_version=3 save_on_exit=0 load_on_start=1
    cp "$SMW" "$OUT/ocsmw/roms/SMW.sfc.tns"
    mkdir -p "$OUT/ocsmw/zPocketSNES_saves"
    cp "$HOME/nspire/build/testroms/smw.sv001.tns" "$OUT/ocsmw/zPocketSNES_saves/SMW.sfc.sv001.tns"
    play_logged ocsmw tests/scripts/overclock_test.txt SMW.sfc --clock-limit 37
    check "$(grep -c '^Highest tested speed: 444 MHz$' "$OUT/ocsmw/zPocketSNES_saves/pocketsnes_overclock.txt.tns")" 1 \
        "Super Mario World (sound chip reads) runs the same at every speed in the overclock test"
fi
# A calculator that runs the game wrongly above 420 MHz: the game check
# notices at 432, although the memory checks would pass there.
setup ocglitch MonsterFarmJump.sfc config_version=3 save_on_exit=0 load_on_start=0
play_logged ocglitch tests/scripts/overclock_test.txt MonsterFarmJump.sfc --glitch-above 35
d=$OUT/ocglitch
check "$(grep -c -e '^432 MHz: game went wrong' -e '^Highest tested speed: 420 MHz$' "$d/zPocketSNES_saves/pocketsnes_overclock.txt.tns")$(grep -c '^highest_tested_mhz=420$' "$d/zPocketSNES_saves/pocketsnes.cfg.tns")" 21 \
    "a game that runs differently at 432 MHz stops the test there: 420 MHz is the highest"
# The game list has no game to play: the test only says to open one.
setup oclist MonsterFarmJump.sfc config_version=3 highest_tested_mhz=444
play oclist tests/scripts/overclock_list.txt
check "$(grep -c '^highest_tested_mhz=444$' "$OUT/oclist/zPocketSNES_saves/pocketsnes.cfg.tns")$(exists "$OUT/oclist/zPocketSNES_saves/pocketsnes_overclock.txt.tns")" 1no \
    "from the game list the overclock test asks for a game and changes nothing"
# The test froze the calculator at 456 MHz: the next start keeps 444.
setup ocfreeze MonsterFarmJump.sfc config_version=3 highest_tested_mhz=0
printf 'testing=456\npassed=444\n' > "$OUT/ocfreeze/zPocketSNES_saves/pocketsnes_octest.tns"
play ocfreeze tests/scripts/after_crash.txt
check "$(grep -c '^highest_tested_mhz=444$' "$OUT/ocfreeze/zPocketSNES_saves/pocketsnes.cfg.tns")$(exists "$OUT/ocfreeze/zPocketSNES_saves/pocketsnes_octest.tns")" 1no \
    "after a freeze in the overclock test, the next start keeps the highest speed that passed"
# "highest tested" as the CPU speed uses the test's result.
setup octested MonsterFarmJump.sfc config_version=3 cpu_speed=1 highest_tested_mhz=444 save_on_exit=0 load_on_start=0
play_logged octested tests/scripts/wait_quit.txt MonsterFarmJump.sfc
check "$(clock_log "$OUT/octested/log.txt")" "clock x37|message: CPU 444 MHz|clock x0|" \
    "CPU speed \"highest tested\" runs the game at the tested 444 MHz"
# Old settings files: 456 MHz was cpu_speed=2.
setup ocold MonsterFarmJump.sfc config_version=2 cpu_speed=2 save_on_exit=0 load_on_start=0
play ocold tests/scripts/wait_quit.txt MonsterFarmJump.sfc
check "$(grep -c -e '^config_version=3$' -e '^cpu_speed=6$' "$OUT/ocold/zPocketSNES_saves/pocketsnes.cfg.tns")" 2 \
    "an older settings file keeps its CPU speed (456 MHz) under the new numbering"

echo "== In-game saves (SRAM)"
setup sram SramWrite.sfc
(cd "$OUT/sram" && timeout -s KILL 4 "$HOST" --script "$forever" \
    --shots shots --exe-dir ndless roms/SramWrite.sfc.tns >/dev/null 2>&1)
srm=$OUT/sram/zPocketSNES_saves/SramWrite.sfc.srm
check "$(gzip -dc "$srm.gz.tns" 2>/dev/null | xxd -l 2 -p)$(exists "$srm.tns")" 4299no \
    "the game's save was written without quitting, gzipped (Compress saves is on by default)"
setup sramplain SramWrite.sfc config_version=3 compress_saves=0
(cd "$OUT/sramplain" && timeout -s KILL 4 "$HOST" --script "$forever" \
    --shots shots --exe-dir ndless roms/SramWrite.sfc.tns >/dev/null 2>&1)
srm=$OUT/sramplain/zPocketSNES_saves/SramWrite.sfc.srm
check "$(xxd -l 2 -p "$srm.tns" 2>/dev/null)$(exists "$srm.gz.tns")" 4299no \
    "with Compress saves off it's the plain file a PC emulator reads"
# One from a PC emulator, named after the ROM without .sfc, is taken in,
# and comes back out gzipped.
setup srampc SramWrite.sfc config_version=3 save_on_exit=0 load_on_start=0
mkdir -p "$OUT/srampc/zPocketSNES_saves"
head -c 8192 /dev/zero | tr '\0' '\101' > "$OUT/srampc/zPocketSNES_saves/SramWrite.srm.tns"
play srampc tests/scripts/wait_quit.txt SramWrite.sfc
s=$OUT/srampc/zPocketSNES_saves
check "$(exists "$s/SramWrite.srm.tns")$(gzip -dc "$s/SramWrite.sfc.srm.gz.tns" 2>/dev/null | xxd -s 2 -l 4 -p)" no41414141 \
    "an in-game save from a PC emulator (Game.srm.tns) is taken in"

echo "== Save manager"
# Four compressed states (three saves and the one made when quitting); the
# game list's menu on the game, Save manager: A marks all, U uncompresses
# them, then del deletes the selected one after asking.
setup manager MonsterFarmJump.sfc config_version=3
play manager tests/scripts/save3_quit.txt MonsterFarmJump.sfc
s=$OUT/manager/zPocketSNES_saves/MonsterFarmJump.sfc
# A state's "stored" flag (not compressed) as 00 or 01.
flags() { local v; v=$(xxd -s 24 -l 1 -p "$1" 2>/dev/null) && printf '%02x' $((0x$v & 1)); }
check "$(flags "$s.sv001.tns")$(flags "$s.sv004.tns")" 0000 "new states are compressed by default"
size_before=$(stat -c %s "$s.sv002.tns")
echo "rom_dir=$(realpath "$OUT")/manager/roms" >> "$OUT/manager/zPocketSNES_saves/pocketsnes.cfg.tns"
play manager tests/scripts/save_manager.txt
check "$(flags "$s.sv002.tns")$(flags "$s.sv003.tns")$(flags "$s.sv004.tns")" 010101 \
    "the save manager uncompressed all the states"
check "$([ "$(stat -c %s "$s.sv002.tns")" -gt "$size_before" ] && echo bigger)$(exists "$s.sv001.tns")" biggerno \
    "... they're bigger now, and del deleted the selected one (slot 001) after asking"
setup storedrt MonsterFarmJump.sfc config_version=2 compress_saves=0 auto_increment=0 save_on_exit=0 load_on_start=0
play storedrt tests/scripts/roundtrip.txt MonsterFarmJump.sfc
d=$OUT/storedrt/shots
check "$(same "$d/A.png" "$d/B.png")$(flags "$OUT/storedrt/zPocketSNES_saves/MonsterFarmJump.sfc.sv001.tns")" same01 \
    "with Compress saves off a state is stored as it is, and loads back exactly"

echo "== Save state pictures"
# tests/state_picture.py reads a state's picture; the card's pixels start at
# 194, 79 (menu.cpp draw_state_picture under the Load state row).
pic() { python3 tests/state_picture.py "$@"; }
setup pic MonsterFarmJump.sfc config_version=3 show_fps=0 save_on_exit=0 load_on_start=0
play pic tests/scripts/state_picture.txt MonsterFarmJump.sfc
p=$OUT/pic/zPocketSNES_saves/MonsterFarmJump.sfc.sv001.tns
check "$(pic has "$p")$(pic frame "$p" "$OUT/pic/shots/saved.png")" yessame \
    "a state keeps a picture of the frame it was saved at, shrunk to 112x98"
check "$(pic card "$p" "$OUT/pic/shots/card.png" 194 79)" same "... and Load state in the menu shows it for that slot"
setup picfps MonsterFarmJump.sfc config_version=3 show_fps=1 save_on_exit=0 load_on_start=0
play picfps tests/scripts/state_picture.txt MonsterFarmJump.sfc
pic png "$p" "$OUT/pic/picture.png"
pic png "$OUT/picfps/zPocketSNES_saves/MonsterFarmJump.sfc.sv001.tns" "$OUT/picfps/picture.png"
check "$(same "$OUT/pic/picture.png" "$OUT/picfps/picture.png")" same \
    "with the FPS counter on the picture is the same: what's drawn over the game isn't in it"
setup picoff MonsterFarmJump.sfc config_version=3 state_pictures=0 save_on_exit=0 load_on_start=0
play picoff tests/scripts/state_picture.txt MonsterFarmJump.sfc
check "$(pic has "$OUT/picoff/zPocketSNES_saves/MonsterFarmJump.sfc.sv001.tns")" no "with State pictures off a state has none"
check "$(pic has "$s.sv002.tns")$(pic has "$OUT/storedrt/zPocketSNES_saves/MonsterFarmJump.sfc.sv001.tns")" yesyes \
    "the save manager kept the pictures when it uncompressed the states; Compress saves off stores one too"
# Save state shows its card in the same place as Load state's.
setup picsave MonsterFarmJump.sfc config_version=3 show_fps=0 save_on_exit=0 load_on_start=0
play picsave tests/scripts/state_picture_save.txt MonsterFarmJump.sfc
# cardbox SHOT: the card's area as a PNG next to it (SHOT.card.png)
cardbox() { python3 -c "
from PIL import Image
Image.open('$1').crop((191, 76, 309, 180)).save('$1.card.png')"; echo "$1.card.png"; }
check "$(pic card "$OUT/picsave/zPocketSNES_saves/MonsterFarmJump.sfc.sv001.tns" "$OUT/picsave/shots/savecard.png" 194 79)" different \
    "Save state on a new slot shows no picture (it's empty)"
setup picover MonsterFarmJump.sfc config_version=3 show_fps=0 save_on_exit=0 load_on_start=0 auto_increment=0
cp "$p" "$OUT/picover/zPocketSNES_saves/"
play picover tests/scripts/state_picture_save.txt MonsterFarmJump.sfc
check "$(pic card "$p" "$OUT/picover/shots/savecard.png" 194 79)" same \
    "with auto-increment off Save state shows the state it replaces"
setup savepick MonsterFarmJump.sfc config_version=3 show_fps=0 save_on_exit=0 load_on_start=0
cp "$p" "$OUT/savepick/zPocketSNES_saves/"
play_logged savepick tests/scripts/save_slot_pick.txt MonsterFarmJump.sfc
s2=$OUT/savepick/zPocketSNES_saves/MonsterFarmJump.sfc
check "$(exists "$s2.sv002.tns")$(exists "$s2.sv003.tns")$(grep -c '^message: Loaded slot 001' "$OUT/savepick/log.txt")" noyes1 \
    "Save state's right goes from new slot 002 to 003, the save goes there, and Load state's slot didn't move"
check "$(pic card "$s2.sv003.tns" "$OUT/savepick/shots/pick.png" 194 79)$(same "$(cardbox "$OUT/savepick/shots/pick.png")" "$(cardbox "$OUT/picsave/shots/savecard.png")")" differentsame \
    "... an empty slot it picks shows the same empty card as a new slot"
play_logged savepick tests/scripts/list_play_state.txt -
check "$(grep -c '^message: Started from slot 001' "$OUT/savepick/log.txt")$(pic card "$s2.sv001.tns" "$OUT/savepick/shots/card.png" 194 64)" 1same \
    "the game list's Play from a state starts the game from the slot picked (001, not the newest), with its picture"

echo "== S-DD1 (Star Ocean, Street Fighter Alpha 2)"
# backdrop NAME: green, red or other, from the centre of shots/end.png
backdrop() {
    python3 -c "
from PIL import Image
p = Image.open('$OUT/$1/shots/end.png').convert('RGB').getpixel((160, 120))
print('green' if p[1] > 200 and p[0] < 50 else 'red' if p[0] > 200 and p[1] < 50 else 'other')" 2>/dev/null || echo none
}
setup sdd1 SDD1Test.sfc config_version=3 show_fps=0 save_on_exit=1 load_on_start=0
play sdd1 tests/scripts/wait_shot_quit.txt SDD1Test.sfc
check "$(backdrop sdd1)" green "the S-DD1 test cartridge passes (bank registers, 8 decompressing DMAs, \$4801 cleared)"
mv "$OUT/sdd1/shots/end.png" "$OUT/sdd1/shots/first.png"
sed -i 's/^load_on_start=0$/load_on_start=1/' "$OUT/sdd1/zPocketSNES_saves/pocketsnes.cfg.tns"
play sdd1 tests/scripts/wait_shot_quit.txt SDD1Test.sfc
check "$(backdrop sdd1)" green "loading its state on a fresh start puts the S-DD1 banks back"

echo "== Menu"
setup menu MonsterFarmJump.sfc config_version=2
play menu tests/scripts/menu_options.txt MonsterFarmJump.sfc
cfg=$OUT/menu/zPocketSNES_saves/pocketsnes.cfg.tns
check "$(grep -c -e '^auto_increment=0$' -e '^save_on_exit=0$' -e '^load_on_start=0$' -e '^show_fps=2$' "$cfg")" 4 \
    "menu turned off auto-increment, saving when leaving and loading on start, and the FPS counter to detailed"
check "$(grep -c -e '^screen_size=1$' -e '^screen_dma=1$' -e '^cpu_speed=4$' "$cfg")" 3 \
    "menu set the screen size to full screen, the screen output to DMA and the CPU speed to 432 MHz"
check "$(grep -c -e '^key_a=6$' -e '^key_a_2=121+20$' -e '^key_save_state_2=121+23$' "$cfg")" 3 \
    "menu set A to Z and shift+3, and shift+S as a second key for Save state"
play menu tests/scripts/menu_game_settings.txt MonsterFarmJump.sfc
game_cfg=$OUT/menu/zPocketSNES_saves/MonsterFarmJump.sfc.cfg.tns
check "$(grep -c -e '^own_settings=1$' -e '^auto_increment=1$' "$game_cfg" 2>/dev/null)" 2 \
    "settings for this game only: auto-increment went back on for the game"
check "$(grep -c '^auto_increment=0$' "$cfg")" 1 "... and stayed off for all games"

echo "== Full screen (Screen size)"
# The same frame of a demo at the original size and stretched (sharp, smooth,
# cropped), turned like on the CX II and shown whole (--landscape, as on
# other calculators); tests/scale_check.py works out each stretched pixel
# from the original screenshot.
script=$(realpath tests/scripts/wait_shot_quit.txt)
for lcd in turned landscape; do
    opt=; [ $lcd = landscape ] && opt=--landscape
    for size in 0 1 2; do
        for smooth in 0 1; do
            [ $size = 0 ] && [ $smooth = 1 ] && continue
            name=full_${lcd}_${size}_${smooth}
            setup $name RotZoom.sfc config_version=3 show_fps=0 screen_size=$size smooth=$smooth
            (cd "$OUT/$name" && "$HOST" --script "$script" --shots shots \
                --exe-dir ndless $opt roms/RotZoom.sfc.tns >/dev/null 2>&1)
        done
    done
    o=$OUT/full_${lcd}_0_0/shots/end.png
    for size in 1 2; do
        for smooth in 0 1; do
            check "$(python3 tests/scale_check.py "$o" "$OUT/full_${lcd}_${size}_${smooth}/shots/end.png" \
                $((size - 1)) $smooth)" same "full screen, $lcd LCD, crop $((size - 1)), smooth $smooth: each pixel as defined"
        done
    done
done
check "$(same "$OUT/full_turned_1_1/shots/end.png" "$OUT/full_landscape_1_1/shots/end.png")" same \
    "full screen looks the same turned and shown whole"

echo "== Files that aren't ROMs"
# The core crashes on files under 32 KB; documents and programs are .tns
# files too. Opening one shows a message and goes on to the game list.
setup notrom MonsterFarmJump.sfc
printf 'tiny' > "$OUT/notrom/roms/Tiny.sfc.tns"
{ printf '*TIMLP0500'; head -c 40000 /dev/zero; } > "$OUT/notrom/roms/Notes.tns"
message_script=$(realpath tests/scripts/after_crash.txt)
for rom in Tiny.sfc Notes; do
    (cd "$OUT/notrom" && timeout 20 "$HOST" --script "$message_script" --shots shots --exe-dir ndless \
        "roms/$rom.tns" >/dev/null 2>&1)
    check "$?" 0 "opening $rom.tns (not a ROM) shows a message instead of crashing"
done

echo "== Zipped ROMs"
# A ROM in a zip (Game.zip.tns) plays the same as the ROM itself, deflated
# or stored, also with other files in the zip.
setup unzipped MonsterFarmJump.sfc config_version=3 save_on_exit=0 load_on_start=0
play unzipped tests/scripts/zip_play.txt MonsterFarmJump.sfc
for kind in deflate store; do
    setup "zip$kind" MonsterFarmJump.sfc config_version=3 save_on_exit=0 load_on_start=0
    printf 'read me\n' > "$OUT/zip$kind/readme.txt"
    python3 tests/make_zip.py "$OUT/zip$kind/roms/Monster.zip.tns" $kind "$OUT/zip$kind/readme.txt" \
        "$ROMS/MonsterFarmJump.sfc=Monster Farm Jump/MonsterFarmJump.sfc"
    rm "$OUT/zip$kind/roms/MonsterFarmJump.sfc.tns"
    play "zip$kind" tests/scripts/zip_play.txt Monster.zip
    check "$(same "$OUT/unzipped/shots/game.png" "$OUT/zip$kind/shots/game.png")" same \
        "a zipped ROM ($kind) plays the same as the ROM itself"
done
# A zip in place of the ROM takes over the ROM's saves.
setup ziptake MonsterFarmJump.sfc config_version=3 save_on_exit=0 load_on_start=0
mkdir -p "$OUT/ziptake/zPocketSNES_saves"
printf 'state' > "$OUT/ziptake/zPocketSNES_saves/MonsterFarmJump.sfc.sv003.tns"
printf 'sram' > "$OUT/ziptake/zPocketSNES_saves/MonsterFarmJump.sfc.srm.tns"
python3 tests/make_zip.py "$OUT/ziptake/roms/MonsterZip.zip.tns" deflate "$ROMS/MonsterFarmJump.sfc"
rm "$OUT/ziptake/roms/MonsterFarmJump.sfc.tns"
play ziptake tests/scripts/wait_quit.txt "MonsterZip.zip"
s=$OUT/ziptake/zPocketSNES_saves
check "$(exists "$s/MonsterZip.zip.sv003.tns")$(exists "$s/MonsterZip.zip.srm.tns")$(exists "$s/MonsterFarmJump.sfc.sv003.tns")" yesyesno \
    "a zip that replaced the ROM takes over its saves"
# A zip with no ROM in it says so.
setup zipnorom MonsterFarmJump.sfc
python3 tests/make_zip.py "$OUT/zipnorom/roms/Empty.zip.tns" deflate tests/make_zip.py
(cd "$OUT/zipnorom" && timeout 20 "$HOST" --script "$message_script" --shots shots --exe-dir ndless \
    "roms/Empty.zip.tns" >/dev/null 2>&1)
check "$?" 0 "a zip without a ROM shows a message instead of crashing"
# A GBA game's zip (PocketSNES has the .zip files, so one opened from My
# Documents comes here): a message, not the GBA ROM run as a SNES game.
setup zipgba MonsterFarmJump.sfc config_version=3 save_on_exit=1
python3 tests/make_zip.py "$OUT/zipgba/roms/Advance.zip.tns" deflate "$ROMS/Rings.sfc=Advance Wars.gba"
(cd "$OUT/zipgba" && timeout 20 "$HOST" --script "$message_script" --shots shots --exe-dir ndless \
    "roms/Advance.zip.tns" >/dev/null 2>&1)
check "$?$(ls "$OUT/zipgba/zPocketSNES_saves" | grep -c '^Advance')" 00 \
    "a zip with a GBA game says to start it from gpSP, and saves nothing for it"

echo "== First start"
# No settings file yet: a welcome screen first, then the game list; the next
# start goes straight to the game list.
setup first MonsterFarmJump.sfc
rm "$OUT/first/zPocketSNES_saves/pocketsnes.cfg.tns"
play first tests/scripts/first_start.txt
d=$OUT/first/shots
check "$(same "$d/start.png" "$d/list.png")" different "the first start shows a welcome screen, a key goes on to the game list"
check "$(exists "$OUT/first/zPocketSNES_saves/pocketsnes.cfg.tns")" yes "... and the settings file is written then"
mv "$d/list.png" "$d/list_first.png"
play first tests/scripts/first_start.txt
check "$(same "$d/start.png" "$d/list_first.png")" same "the next start goes straight to the game list"

echo "== ROM browser"
setup browser MonsterFarmJump.sfc
play browser tests/scripts/browser.txt
d=$OUT/browser/shots
check "$(same "$d/browser.png" "$d/game.png")" different "picking a ROM in the browser starts it"
check "$(grep -c '^rom_dir=.*/browser/roms$' "$OUT/browser/zPocketSNES_saves/pocketsnes.cfg.tns")" 1 "the ROM folder is remembered"

# With a saved state and loading on start on, the game list's menu can still
# start the game without it: the picture matches a game that has no states.
setup fresh MonsterFarmJump.sfc config_version=2 save_on_exit=0 load_on_start=1
play fresh tests/scripts/save_quit.txt MonsterFarmJump.sfc
play fresh tests/scripts/browser_start.txt
mv "$OUT/fresh/shots/game.png" "$OUT/fresh/shots/resumed.png"
play fresh tests/scripts/browser_start_fresh.txt
setup nostate MonsterFarmJump.sfc config_version=2 save_on_exit=0 load_on_start=1
play nostate tests/scripts/browser_start.txt
check "$(same "$OUT/fresh/shots/game.png" "$OUT/nostate/shots/game.png")" same \
    "Start without loading a state (game list menu) starts the game from the beginning"
check "$(same "$OUT/fresh/shots/resumed.png" "$OUT/fresh/shots/game.png")" different \
    "... while a plain start loaded the saved state"

echo "== Game list: number keys and the order of the games"
# setup_list NAME: a folder of three games, A to Z MonsterFarmJump, Rings and
# RotZoom, that the game list starts in; quitting a game saves a state, so
# the state file says which game ran.
setup_list() {
    setup "$1" MonsterFarmJump.sfc config_version=2 save_on_exit=1 load_on_start=0 \
        "rom_dir=$(realpath "$OUT")/$1/roms"
    cp "$ROMS/Rings.sfc" "$OUT/$1/roms/Rings.sfc.tns"
    cp "$ROMS/RotZoom.sfc" "$OUT/$1/roms/RotZoom.sfc.tns"
}
setup_list number
play number tests/scripts/list_number.txt
s=$OUT/number/zPocketSNES_saves
check "$(exists "$s/Rings.sfc.sv001.tns")$(exists "$s/MonsterFarmJump.sfc.sv001.tns")" yesno \
    "the 2 key in the game list starts the second game"

setup_list move
play move tests/scripts/list_move.txt
s=$OUT/move/zPocketSNES_saves
check "$(order "$s/game_order_roms.txt.tns")" "Rings.sfc.tns RotZoom.sfc.tns MonsterFarmJump.sfc.tns " \
    "tab, down, down, enter moved the first game to third place, and the order was saved"
check "$(exists "$s/MonsterFarmJump.sfc.sv001.tns")" yes "... then the 3 key started it there"
play move tests/scripts/list_number.txt
check "$(exists "$s/RotZoom.sfc.sv001.tns")" yes "the next start keeps the order (2 is now RotZoom)"
play move tests/scripts/list_menu_sort.txt
check "$(exists "$s/game_order_roms.txt.tns")" no "Sort the list A to Z (game list menu) goes back to A to Z"

setup_list cancel
play cancel tests/scripts/list_move_cancel.txt
check "$(exists "$OUT/cancel/zPocketSNES_saves/game_order_roms.txt.tns")" no "esc while moving puts the game back and saves nothing"

setup_list menumove
play menumove tests/scripts/list_menu_move.txt
check "$(order "$OUT/menumove/zPocketSNES_saves/game_order_roms.txt.tns")" \
    "Rings.sfc.tns MonsterFarmJump.sfc.tns RotZoom.sfc.tns " "Move in the list (game list menu) moves the game"

echo
echo "$PASS passed, $FAIL failed. Screenshots and files are in $OUT"
[ "$FAIL" = 0 ]
