Testing without a calculator
============================

`make` builds `pocketsnes-host`: the same core, menu, save states and frame
pacing as the calculator build, with `frontend/platform_host.cpp` standing in
for the calculator's screen, keys and timer. What it can't tell you is how fast
the calculator is, or anything about its screen and keypad hardware.

Playing it in a window
----------------------

    make
    ./pocketsnes-host                      # starts in the game list
    ./pocketsnes-host path/to/game.sfc     # or straight into a game

Settings and the game list start from the program's folder (`--exe-dir DIR`
picks another one). Saves go in `zPocketSNES_saves` in the folder above it,
which stands for the calculator's My Documents (`--documents DIR` picks
another one). Plain `.sfc`/`.smc` files work as well as `.tns` ones.
Calculator keys on the PC keyboard:

| PC key                 | Calculator key  | Default use           |
|------------------------|-----------------|-----------------------|
| Shift / Backspace      | shift / del     | SNES A / B            |
| Ctrl / F2              | ctrl / var      | SNES X / Y            |
| Tab / F1               | tab / menu      | SNES L / R            |
| F5 / F6                | cat / frac      | SNES Start / Select   |
| arrows, 8 5 4 6        | same            | d-pad                 |
| Esc                    | esc             | menu                  |
| Q / F / S / L / N / R  | same            | quit / fast forward / save / load / load newest / restart |
| ] / [                  | ) / (           | next / previous slot  |
| keypad + / -           | + / -           | brightness            |

F3 is doc, F4 scratchpad, Home the on key; letters and digits are
themselves. `--program FILE` keeps the settings in FILE as the calculator
keeps them in the program (`--glitch-above N` and the others are listed at
the top of `frontend/platform_host.cpp`).

Scripted runs
-------------

`--script FILE` runs without a window, drives the keys from FILE and uses
virtual time, so every run is the same. `--shots DIR` is where `shot`
screenshots go. The commands are listed at the top of
`frontend/platform_host.cpp`; for example:

    wait 120
    press esc
    wait 5
    shot menu.png
    press S 2
    quit

`PSNES_TRACE=1` prints the keys seen at every poll, which helps when a script
doesn't do what you expect.

Two more options are for the tests: `--log FILE` appends a line to FILE for
every CPU clock change asked for (`clock x40`) and every message shown in the
game (`message: CPU 480 MHz`), and `--stuck-clock` acts like a calculator
whose clock doesn't change when asked (the PC build otherwise reports the
clock asked for as the one it got).

The test suite
--------------

    tests/run.sh

Downloads a few free homebrew demos (PeterLemon's SNES test programs) into
`tests/roms`, then checks that loading a state gives back exactly the same
game (pixel for pixel, 150 frames later), auto-increment, saving when leaving
and loading on start, old settings files, key combinations, settings for one
game, the CPU speed setting (the menu's choice reaches the clock when the
game resumes; a clock that doesn't move goes back to normal and says so; a
freeze at a raised speed is undone at the next start), that in-game saves
are written without quitting, the menu, the welcome screen on the first
start, the game list and its menu (number keys starting games, moving a game
with tab or the menu, the order kept, esc putting it back, sorting A to Z
again), and the overclock test (run in a game; with `--clock-limit 37` it
must find 444 MHz, with `--glitch-above 35`, which makes the PC report the
game running differently above 420 MHz, it must stop at 432; from the game
list it only asks for a game;
a freeze marker left by the test is picked up at the next start; "highest
tested" runs games at the result; old settings files keep their CPU speed),
where the save made when leaving goes (the Auto slot, over the newest
state, older settings files), and brightness (the + and - keys in a game
and in the game list, the menu line, and the calculator's own brightness
back at the end; `--backlight N` sets the pretend calculator's own value,
and `--log` records every change as `backlight N`), zipped ROMs (deflated
and stored zips play exactly like the ROM, a zip takes over the unzipped
ROM's saves, a zip without a ROM says so), saves (gzipped and plain in-game
saves, one from a PC emulator taken in, stored states loading back exactly,
the save manager uncompressing and deleting), and files from older versions
moving into `zPocketSNES_saves` in My Documents (the `snesfolder` test sets
out the calculator's layout, program and ROMs in one folder, and checks the
folder has no folder left in it; `docsfolder` moves the earlier
My Documents/`PocketSNES_saves` over and checks it's gone), and full screen
(*Screen size*: each size and smooth setting, turned as on the CX II and
shown whole with `--landscape` as on other calculators; `tests/scale_check.py`
works out every stretched pixel from the original size's screenshot by its
own reading of the definition and compares).
`make unit-tests` also checks the settings kept inside the program file
and *Reset all settings*.
Results and screenshots go to
`tests/out/`. Every test starts with a settings file (so not on a "first
start") unless it removes it.

Unit tests
----------

    make unit-tests

`tests/unit_tests.cpp`: the tile drawers against the original Snes9x 1.43
ones on 20,000 random tiles (flips, clipping, depths, line ranges), the
frame turning for the CX II LCD, the full screen stretch (`scale_turn` and
`scale_frame` against `scale_reference` at every height from 120 to 240
lines, sharp and smooth), the settings files (defaults, old files,
key combinations, settings for one game, the CPU speed reset after a
freeze) and the key bindings. tests/run.sh runs them first.

The S-DD1 test cartridge
------------------------

`tests/make_sdd1_rom.py` writes `tests/roms/SDD1Test.sfc` (tests/run.sh does,
when it's missing or older than the script): a 2 MB cartridge with an S-DD1
header that checks the bank registers ($4804-$4807, reading through banks
c0-ff), decompresses eight streams (every bit plane and context mode, 1 to
65536 bytes) by DMA into WRAM and compares them with the expected bytes,
checks that $4801 is clear afterwards, and then keeps checking the banks.
The backdrop is green when all of that passed, red when something failed
(WRAM $0000 holds which test). The expected bytes come from the script's own
S-DD1 decompressor, written for the test from the algorithm's description,
not from the emulator's. run.sh plays it, and plays it again loading the
state it saved when it closed (which only stays green if loading a state
restores the S-DD1 banks); tests/asm/run.sh plays it on both CPU cores.

The Super FX interpreters and translator
----------------------------------------

    make fx-test                # PC: the C interpreter
    tests/asm/run.sh            # ARM, under qemu: the C one, the ARM assembler one, the translator

`tests/fx_test.cpp` runs every one of the 1024 opcode and ALT mode
combinations as a single instruction from random GSU states (all registers,
the B flag, banks, flags, the ROM buffer, junk in the bits above 16), and
random programs of up to 200 instructions, once on the interpreter being
tested and once on `fx_run_reference` (the original Snes9x loop, a call
through `fx_apfOpcodeTable` per instruction, built only for the test), and
compares the whole GSU struct, its RAM and the register space byte for
byte. On ARM it tests `fx_run` (the C interpreter), `fx_run_arm_counted`
(the assembler one built to stop after a given number of instructions)
and, with `jit`, the translator (`fxjit.cpp`): parts 1 and 2 through
`fx_jit_run_counted`, which translates blocks that stop after the given
number of instructions, and a part 3 that runs programs until the GSU
stops, as games do, with the blocks kept and linked. Part 3 runs each
program from 6 states and shrinks the block table's hash now and then
(`fx_jit_test_hash_bits` 0 or 1) so blocks collide, plus directed programs
for what random ones rarely hit: loops, branches into code already
translated, delay slots, iwt/ibt operands after a jump, computed jumps
(`add r15`, `lms r15`), calls and returns. Planting a bug in either
interpreter (a flag, an order of reads, the ROM buffer, the careful table)
makes it fail; bugs planted in the translator showed where random programs
weren't enough, which is what the shrunken hash and the directed programs
are for.

Estimating the speed on the calculator
--------------------------------------

    . ~/nspire/build/env.sh
    tests/calc_speed.sh ROM [STATE] [FRAMES] [CORE] [INPUT]
    tests/calc_speed.sh build/asm-test/roms/yi.sfc build/asm-test/roms/yi.sv001

    FUNCS=40 tests/calc_speed.sh ...            # also the 40 busiest functions
    ANNOTATE='_Z10DrawTile16jjjj' tests/calc_speed.sh ...
                                                # that function's instructions, each
                                                # with how many times a frame it ran

Builds `tests/asm/game_test` with the calculator build's compiler flags,
runs it under a qemu-arm with plugin support and `tools/calc_speed/arm926.c`
(an ARM926EJ-S model: the pipeline's extra cycles, 16 KB / 8 KB caches, the
write buffer; it also counts the instructions of every frame), and prints
the ARM instructions per frame by part of the emulator (drawing, Super FX,
CPU, PPU/DMA) and the estimated milliseconds and fps at 396 MHz. Like the
calculator's speed test it starts from the state with no keys pressed;
`game_test`'s `-timing` input draws every frame without printing checksums,
and `timing_frame_mark` marks the frames. The Super FX's translated code has
no symbols, so its instructions are counted as `fx_jit_code` (the code
buffer), under Super FX.

The milliseconds come from instruction counts with numbers fitted to the
calculator's own speed tests of Super Mario World (2026-10-10, 396 MHz:
3.70 ms per frame with nothing drawn on the ARM CPU core, 4.74 ms on the C
core, 79.2 fps drawn with the no-tearing output): 2.58 cycles per
instruction running the game, 2.64 drawing it, 0.63 ms more per frame, and
2.0 ms to show a drawn frame. It reproduces those within 1%. Yoshi's
Island checked it (2026-10-14): from the owner's save, with the DMA output
(0.5 ms a frame instead of 2.0, `SHOW_MS=0.5`), the calculator's speed test
measured 53.0 fps and the script said 53.3. The split was further off: the
calculator measured 7.9 ms a frame drawing and 10.3 ms the rest, the
script 9.2 and 9.0. Fitted to that game, drawing takes about 2.3 cycles an
instruction and the Super FX interpreter about 3.2 (2.64 and 2.58 in the
script), so expect a bit less from saving drawing instructions than the
script says and a bit more from saving Super FX ones. To refit, run the
speed test on the calculator, run the same game and state here, and solve
for the four numbers (they're variables at the top of the script). The plugin's own cycle model didn't fit
the calculator as well (its memory timings can't make both the C and ARM
cores come out right), so it's only used to count.

The qemu with plugins (the Ubuntu one has none) is built from source, ARM
user mode only, without root:

    mkdir -p ~/nspire/build/tools/qemu-plugins && cd ~/nspire/build/tools/qemu-plugins
    apt-get download meson ninja-build && for f in *.deb; do dpkg-deb -x $f root; done
    curl -LO https://download.qemu.org/qemu-10.2.1.tar.xz && tar xJf qemu-10.2.1.tar.xz
    mkdir qemu-10.2.1/build && cd qemu-10.2.1/build
    export PATH=$HOME/nspire/build/tools/qemu-plugins/root/usr/bin:$PATH
    export PYTHONPATH=$HOME/nspire/build/tools/qemu-plugins/root/usr/lib/python3/dist-packages
    ../configure --target-list=arm-linux-user --enable-plugins --disable-system --disable-docs \
                 --disable-tools --disable-werror
    ninja qemu-arm

(glib's development files have to be installed; they were.)

Drawing the same pixels as before
---------------------------------

    tests/render_compare.sh [ROM...]

Plays each ROM with the same keys in `./pocketsnes-host-ref` (a PC build of
the renderer before the speed work of October 2026; keep it) and in the
current build, with a screenshot every 2 to 4 frames, and checks they are
identical. Without arguments it uses the test ROMs, Super Mario World with
its save state, and the games in `~/nspire/build/testroms/calc` (with their
saves). The reference build crashes in Chrono Trigger's intro (a 64-bit PC
bug, see FINDINGS.md), so that game only compares up to there.

The ARM CPU core against the C core
-----------------------------------

    . ~/nspire/build/env.sh      # the Ndless toolchain
    tests/asm/run.sh [frames]    # 600 frames per game if not given

The ARM assembler CPU core (`pocketsnes/arm`) can't run on a PC, so
`tests/asm/Makefile` builds the core for ARM with the Ndless toolchain and
newlib's semihosting library (file access through the host), and the tests
run under `qemu-arm` (by default the one unpacked from Ubuntu's `qemu-user`
package into `~/nspire/build/tools/qemu`; `QEMU=` picks another). Both cores
must do exactly the same thing, so everything is compared, not just the
picture:

* **opcode_test** runs every opcode in emulation mode and in the four M/X
  modes from random registers, flags and memory, once on the C core and
  once on the assembler core (built to stop after one opcode), and compares
  the registers, flags, cycle count, idle loop state, open bus byte and
  memory, and that no stray bits are left in the assembler's registers. Its
  memory map has RAM, ROM, SRAM and unmapped blocks, so the calls into C
  are covered too. `opcode_test 100 1 all openbus` is what run.sh uses;
  `opcode_test 500 7 6D openbus print=20` looks at one opcode.
* **game_test** plays a ROM (from power-on or from a PocketSNES state) for
  some frames on one core with scripted input, printing checksums of RAM,
  VRAM, palette and sprites, SRAM, I/O registers and the picture, and the
  registers and cycle count, for every frame. run.sh runs it on both cores
  for the homebrew demos and, when they're in `~/nspire/build/testroms`,
  Super Mario World, Chrono Trigger, Zelda: A Link to the Past and Yoshi's
  Island (Super FX; also from the owner's two saves, `yi_1_1` and
  `yi_bubble`), and the lines must be identical. The CPU core setting also
  picks the Super FX translator, the tile drawers' and line fills' ARM
  assembler (`pocketsnes/arm/gfx_arm.s`), the background rows' two passes,
  the depth ceilings, the VRAM DMA copy and the brightness rebuilt when
  used, against the C code with *C*, so the games compare those too.
* **tile_test** draws random tiles with `DrawTile16` and
  `DrawClippedTile16` on both settings (the assembler's inner loop and the
  C one) and with Snes9x 1.43's drawers (`tests/tile_ref.h`, which the PC
  unit tests use too), and compares the whole screen and depth buffers:
  blank, opaque and partly transparent tiles, every flip, palettes, places
  on the screen, 0 to 8 lines, clips, depths and Z values from 0 to 255,
  and line lengths of 256 to 512 pixels. run.sh runs 20000 cases;
  `tile_test 100000 1` runs more. Each of 12 bugs planted in the assembler
  (a condition, the compare's order, a byte's shift, a mask, a flip, the
  next line's depth, Z2, the line count) made it fail. Then the untested
  drawers (`DrawTileLines16_u*`) against the tested ones on depths all
  below Z1, where they must draw the same. Then a quarter as many rows of
  background tiles: `DrawBGTiles16` (its assembler lister and, with
  `BGListInC`, the C one) against Snes9x 1.43's loop from `DrawBackground`
  with the reference drawer: random map rows, 8x8 and 16x16 tiles (tile
  numbers often near 1023, where a 16x16 quarter carries into the palette),
  runs across the halves of the map row, tile data anywhere in VRAM (tile
  numbers wrapping), 2, 4 and 8 bits a pixel, tiles still to be converted
  from VRAM, depths under a ceiling; the screen, depths, t after the row,
  GFX.Z1/Z2 and the highest depth drawn must match. Each of 11 planted bugs
  (in the run logic, the quarters, the wraps, the carry check, converted
  tiles, the ceiling, the flipped line, the highest depth) made it fail.
* **scale_test** checks the full screen loops in ARM assembler
  (`frontend/scale_arm.S`) against `scale_reference`, turned, on random
  pictures at every height from 120 to 240 lines, sharp and smooth.
* **game_trace** is game_test printing every opcode of chosen frames with
  the registers, on either core, so the first instruction where the two
  differ can be found with `diff`:

      Q=~/nspire/build/tools/qemu/root/usr/bin/qemu-arm
      $Q build/asm-test/game_trace build/asm-test/roms/smw.sfc 298 c build/asm-test/roms/smw.sv001 - walk 298 298 > c.txt
      $Q build/asm-test/game_trace build/asm-test/roms/smw.sfc 298 asm build/asm-test/roms/smw.sv001 - walk 298 298 > asm.txt
      diff c.txt asm.txt | head

  (Semihosting splits the command line at spaces, so run.sh links the ROMs
  into `build/asm-test/roms` under names without them.)

qemu says nothing about speed on the calculator: the speed test's last two
rows compare the cores there.
