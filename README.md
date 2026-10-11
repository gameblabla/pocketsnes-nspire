PocketSNES for TI-Nspire
========================

Play Super Nintendo games on a TI-Nspire CX or CX II calculator.

This is gameblabla's PocketSNES port of Snes9x 1.43, with a new front end:
a game list you can sort, zipped ROMs, a menu like gpSP's, 999 compressed
save state slots per game with a save manager, settings per game, smooth
tear-free drawing on the CX II, and the SNES CPU in ARM assembler (from
psnes) as well as in C. There's also an experimental
overclock setting, but it isn't reliable (see [Overclocking](#overclocking)).

There's no sound, because the calculator has no speaker.

**Start here:** [Getting started](#getting-started) ·
[Controls](#controls) · [The game list](#the-game-list) ·
[Saving](#saving) · [Speed and overclocking](#speed-and-overclocking) ·
[Troubleshooting](#troubleshooting)

**More detail:** [Every menu setting](#every-menu-setting) ·
[Files](#files) · [What runs](#what-runs) ·
[How it got faster](#how-it-got-faster) · [For developers](#for-developers) ·
[Future steps](#future-steps) · [Credits](#credits-and-license)


Getting started
---------------

You need a TI-Nspire with a colour screen (CX or CX II) and
[Ndless](https://ndless.me).

1. **Install Ndless** for your OS version (see [ndless.me](https://ndless.me)).
   On a CX II, Ndless has to be switched on again after every reset: just
   open the Ndless installer document.
2. **Copy `_PocketSNES.tns` to the calculator**, into any folder (a folder
   called `snes` works well). Use TI-Nspire Computer Link, the Student
   Software, TiLP, or any other transfer tool. The `_` keeps it at the top
   of the folder, above your games, and PocketSNES keeps its saves out of
   that folder (in My Documents), so opening the folder and pressing
   **enter** starts it.
3. **Copy your ROMs over and add `.tns` to their names.** The calculator only
   accepts `.tns` files. A zipped ROM works as it is, so you can download a
   ROM and just rename it: `Super Mario World.zip` becomes
   `Super Mario World.zip.tns`, and `Super Mario World.sfc` becomes
   `Super Mario World.sfc.tns`. Any folder works.
4. **Open `_PocketSNES.tns`.** The first time, a welcome screen shows the
   keys. Then the game list opens: pick a game and press **enter**, or press
   the **number** next to it.
5. That's it. After the first run, you can also open a ROM straight from the
   calculator's documents.

**Updating:** copy the new `_PocketSNES.tns` over the old one. Your
settings and saves stay (see [Files](#files)).

When you leave a game with **Q** (or *Exit PocketSNES* in the menu),
PocketSNES saves where you were, and the game carries on from there next
time.


Controls
--------

### In a game

| SNES button    | Calculator key                              |
|----------------|---------------------------------------------|
| D-pad          | arrows (touchpad), or 8 4 6 5               |
| A / B          | shift / del                                 |
| X / Y          | ctrl / var                                  |
| L / R          | tab / menu                                  |
| Start / Select | cat / frac                                  |

| Hotkey       | Key  | What it does                                        |
|--------------|------|-----------------------------------------------------|
| Menu         | esc  | opens the menu (the game waits)                     |
| Quit         | Q    | leaves the game, saving your place first            |
| Fast forward | F    | on/off: runs as fast as it can                      |
| Save state   | S    | saves a state (each save gets a new slot)           |
| Load state   | L    | loads the selected slot                             |
| Load newest  | N    | loads the newest state                              |
| Next / previous slot | ) / ( | picks the slot Load state loads             |
| Restart      | R    | resets the game                                     |
| Brightness   | + / − | brighter / dimmer (CX II), in menus too            |

A B X Y sit like the SNES's diamond on shift, del, ctrl and var. The FPS
counter hotkey has no key until you give it one. Every key can be changed in
the menu under *SNES buttons* and *Hotkeys*. Each action takes two keys, and
a key can be a combination (hold one key and press another). *Controls &
about* in the menu always shows your keys as they are now. To start over,
*Reset all settings* in the game list's settings (**menu** on a folder) puts
every setting and key back to these.

### In menus

| Key                   | Does                          |
|-----------------------|-------------------------------|
| arrows, or 8 5 4 6    | move                          |
| left / right          | change a setting              |
| enter (or click)      | select                        |
| esc                   | back                          |

### In the game list

| Key            | Does                                                   |
|----------------|--------------------------------------------------------|
| 1–9, 0         | start that game (the number is shown next to it)       |
| arrows         | move; left/right turn a page                           |
| enter          | start the game, or open a folder                       |
| tab            | pick up the game to move it (see below)                |
| menu           | settings for that game (or for all games, on a folder) |
| esc            | quit PocketSNES                                        |

In the game list the number keys start games, so moving is done with the
arrows there.


The game list
-------------

The list shows the folder PocketSNES was last in. Folders come first, then
your games. The first ten games have a number next to them: press it to
start that game right away.

**Putting your games in your own order:** press **tab** on a game (or pick
*Move in the list* from its menu). The game gets a gold frame. Move it with
up/down (left/right jump a page), then press **enter** to put it there, or
**esc** to put it back where it was. Each folder remembers its order. To go
back to alphabetical, press **menu** on a game and pick *Sort the list A to
Z*.

The game's menu (the **menu** key) can also start it without loading a
state (handy if a state is broken) and change its settings.


Saving
------

There are two kinds of saves, and both happen without you thinking about
them much:

* **In-game saves** are the game's own saves, like on a real SNES cartridge.
  PocketSNES writes them to the calculator a couple of seconds after the
  game saves.
* **Save states** freeze the whole game at any moment. **S** saves one, **L**
  loads one, and leaving a game saves one so you can carry on next time.
  Each game has 999 slots. Every save goes to a new slot, so an old save is
  never overwritten. A state takes about 5–30 KB compressed.

You can pick a slot in the menu (*Save state* / *Load state*, left/right
choose the slot). Everything a game saves goes in the `PocketSNES_saves`
folder in My Documents (the game list doesn't show it).

Where the save made when you leave goes is up to you (menu → *Saving*):
a new slot each time (the default), over your newest state (*New save on
quit* off), or always the same separate **Auto** slot, which keeps it out of
slots 001–999 (*Auto save slot* on). The Auto slot comes before 001 when you
pick a slot, and starting a game loads it when it's the newest.

**Compression.** States and in-game saves are compressed (*Saving →
Compress saves*, on by default): a state is about 7 KB instead of 356 KB.
Switched off, new saves are written as they are.

**The save manager** (*Saving → Save manager* in a game, or **menu** on a
game in the game list) lists the game's in-game save and states with their
sizes. **enter** marks one (**A** marks all); **C** compresses, **U**
uncompresses and **del** deletes the marked ones, or the selected one if
none is marked. Deleting asks first.

### Saves on a PC

* **In-game saves** work in PC emulators (Snes9x, bsnes, RetroArch and
  others), which read a plain `.srm` named like the ROM. Uncompress it in
  the save manager (or switch *Compress saves* off), copy
  `Super Mario World.sfc.srm.tns` from `PocketSNES_saves` to the PC, and
  rename it `Super Mario World.srm`, next to `Super Mario World.sfc`. A
  compressed one (`.srm.gz.tns`) works too: rename it to `.srm.gz` and
  unpack it with 7-Zip or any gzip tool.
* **From a PC:** rename `Super Mario World.srm` to
  `Super Mario World.srm.tns` and put it in `PocketSNES_saves` (in My
  Documents). PocketSNES takes it the next time the game starts, if the game has
  no in-game save of its own yet (otherwise it keeps the newer file).
* **Save states** don't move to PC emulators: they're Snes9x 1.43's format
  for this port (without the sound chip), which today's emulators can't
  read. Use in-game saves to move a game between the calculator and a PC.


Speed and overclocking
----------------------

On a CX II at its normal 396 MHz, Super Mario World runs at a full 60 frames
per second with every frame drawn. Games with extra chips (Super FX like
Yoshi's Island and Star Fox, or SA-1) are much heavier.

To help a slow game:

* **Keep frameskip on automatic** (the default). The game always runs at
  full speed and only skips drawing a frame when it has to.
* **Turn on the FPS counter** (menu → *Graphics & speed*) to see how it's
  doing: 60/60 is perfect.
* **Unplug the USB cable.** While it's plugged in, the calculator runs at
  288 MHz instead of 396.
* Overclocking is there, but it isn't reliable; read the section below
  first.

### Overclocking

**The overclock isn't reliable.** Use it at your own risk, and don't count
on a speed being safe just because it worked for a while.

*CPU speed* in *Graphics & speed* runs games faster than the calculator's
normal 396 MHz, in steps of 12 MHz up to 504 MHz (menus and saving stay at
normal speed). The clock really does change: the game shows the speed it
measured when it starts ("CPU 432 MHz"). The trouble is what happens next.
The memory speeds up together with the CPU but keeps the timings it was set
up with for normal speed, and how far each calculator copes with that
differs and isn't predictable. On the calculator this was developed on:

* 432 MHz ran fine,
* 456 MHz worked only sometimes,
* 480 MHz glitched and froze in games.

The *Overclock test* (in a game's menu, *Graphics & speed*) plays the game
you have open: a few seconds at normal speed, then the same seconds again
at 408, 420, … 504 MHz, twice at each, with memory checks after. The game
has to do exactly what it did at normal speed, frame by frame; the first
speed where it doesn't, or that freezes the calculator, ends the test, and
the speed before it becomes *highest tested*. (The first version only ran
memory and CPU checks, and passed 492 MHz on a calculator whose games froze
at 480.) A few seconds of one game still isn't hours of every game, so take
a step or two below its result to be safe.

If you try overclocking anyway:

* Start low (432 MHz) and play for a while before going higher.
* A freeze doesn't harm the calculator: reset it, open Ndless again, and
  PocketSNES puts *CPU speed* back to normal at the next start.
* A speed that *sometimes* glitches can quietly corrupt the game in memory,
  and that can end up in a save state. Keep a save state from normal speed
  of anything you care about.


Troubleshooting
---------------

* **My ROM isn't in the list.** Its name has to end in `.tns`
  (`game.sfc.tns`). Use *Up a folder* at the top of the list to get to other
  folders.
* **"Couldn't load ..."** The file isn't a SNES ROM, it's damaged, or it's
  too big for the free memory.
* **A game is slow.** Keep frameskip on automatic, unplug USB, and look at
  the FPS counter.
* **The calculator froze.** Press the reset button on the back with a paper
  clip, then open the Ndless installer again. If it froze at a raised CPU
  speed, PocketSNES goes back to normal speed by itself.
* **A save state is broken.** In the game list, press **menu** on the game
  and choose *Play without a state*.
* **I want the default settings back.** In the game list, press **menu** on
  a folder and choose *Reset all settings*.
* **Opening a ROM from the documents doesn't start PocketSNES.** PocketSNES
  must have run once (from any name: the documents then open ROMs in it).


---

*Everything below is reference. You don't need it to play.*


Every menu setting
------------------

Press **esc** in a game for the menu, or **menu** in the game list. Settings
can be for **all games** or for **one game only**: *Settings for* and *Keys
for* in the menu switch between the two. A game's own settings are used
whenever that game runs.

### Graphics & speed

| Setting | Choices (default first) | What it does |
|---|---|---|
| Frameskip | automatic, manual, **off** | Automatic skips drawing only when the game falls behind, so the game keeps full speed. Manual draws 1 frame in every N+1. Off draws every frame; the game slows down if drawing can't keep up. |
| Frames to skip | **5** (0–9) | Automatic: the most frames skipped in a row. Manual: N. |
| FPS counter | off, **on**, detailed | Frames drawn per second over the most there can be: 60/60 is full speed. Detailed adds how long each part of a frame takes and logs it to `pocketsnes_perf.txt.tns`. |
| Screen output (CX II) | **no tearing**, DMA | No tearing shows every frame whole. DMA is a little faster but can show a split line when the picture scrolls fast. |
| Brightness (CX II) | **calculator's**, 1 (dimmest) … 10 (brightest) | The screen's backlight while PocketSNES runs; the calculator's own comes back when PocketSNES closes. The + and − keys (Hotkeys) change it anywhere, a level at a time. |
| CPU speed (CX II) | **normal**, highest tested, 408 … 504 MHz | Experimental, not reliable: overclocks while a game runs. See [Overclocking](#overclocking). |
| Overclock test (CX II) | | In a game: plays it at 408 to 504 MHz until it runs differently from normal speed (or freezes), and keeps the highest that worked. About 3 minutes. See [Overclocking](#overclocking). |
| CPU core | **ARM (fast)**, C | Which emulation of the SNES CPU runs the game: ARM is psnes's, in ARM assembler; C is Snes9x's. They do exactly the same thing (see [The CPU cores](#the-cpu-cores)); try C if a game acts up. Games with an SA-1 chip always use C. |
| Run speed test | | In a game only: plays from where you are for about 40 seconds in six ways and shows the frame rates. The last two rows run the game without drawing on each CPU core, to compare them. Results also go to `pocketsnes_results.txt.tns`. |

### Saving

| Setting | Default | What it does |
|---|---|---|
| Auto-increment slot | on | Every save goes to a new slot, so nothing is overwritten. Off: saves go to the selected slot. |
| Save when leaving | on | Quitting, or going back to the game list, saves a state first. |
| Auto save slot | off | That save goes to its own slot, Auto (before 001), overwritten each time, so slots 001–999 only hold the saves you make. |
| New save on quit | on | That save goes to a new slot. Off: it saves over your newest state. (Doesn't matter with Auto save slot on.) |
| Load newest on start | on | Games start from their newest state. *Play without a state* in the game list skips it once. |
| Write in-game saves | automatic | When the game's own save is written to the calculator: a couple of seconds after the game saves, or only on exit. |
| Compress saves | on | New states and in-game saves are compressed (a state: about 7 KB instead of 356 KB). Off: in-game saves are the plain `.srm` PC emulators read. |
| Save manager | | This game's saves and their sizes; compress, uncompress or delete them. See [Saving](#saving). |

### SNES buttons and Hotkeys

Left/right pick key 1 or key 2, enter sets it (press a key, or hold one key
and press another for a combination), del clears it. *Reset to defaults* is
at the bottom. The arrows always work as the d-pad.

### The rest

* **In a game:** *Resume game*, *Save state* / *Load state* (left/right pick
  the slot, hold to go faster), *Restart game*, *Load another game*,
  *Controls & about*, *Exit PocketSNES*.
* **In the game list, on a game:** *Play*, *Play without a state*, *Move in
  the list*, *Sort the list A to Z*, and the game's settings.


Files
-----

In one `PocketSNES_saves` folder in My Documents, for every game wherever
its ROM is (named so you can tell it from other emulators' saves). It
isn't in the ROM folder, so `_PocketSNES.tns` is the first thing there (a
folder would be listed above it). Older versions kept a hidden
`.pocketsnes` folder (or a `PocketSNES_saves` folder) in each ROM folder,
and their own files next to the program: those move over by themselves.
Two ROMs with the same file name in different folders share their saves.

| File | What |
|---|---|
| `game.sfc.srm.gz.tns` | the in-game save, gzipped (Compress saves on) |
| `game.sfc.srm.tns` | the in-game save as it is (Compress saves off) |
| `game.sfc.sv001.tns` … `game.sfc.sv999.tns` | save states (compressed unless switched off) |
| `game.sfc.svauto.tns` | the Auto slot's state (Saving → Auto save slot) |
| `game.sfc.cfg.tns` | the game's own settings or keys, if it has any |
| `game_order_snes.txt.tns` | the order of the games in the `snes` folder, once you've moved one (`game_order.txt.tns` for My Documents itself) |

A zipped ROM's files are named after the zip (`game.zip.sv001.tns`); a zip
that replaces the unzipped ROM takes over its files.

**Settings for all games are kept inside `_PocketSNES.tns` itself**, in a
small area after the program, so they go wherever the program goes. A copy
is kept in `PocketSNES_saves`, so a new version copied over the old one
picks them up. That folder also has:

| File | What |
|---|---|
| `pocketsnes.cfg.tns` | the copy of the settings and keys for all games, and the overclock test's result |
| `pocketsnes_overclock.txt.tns` | what the overclock test found at each speed |
| `pocketsnes_results.txt.tns` | the last speed test's results |
| `pocketsnes_perf.txt.tns` | the detailed FPS log, if you turned it on |
| `pocketsnes_clock.tns` | only there while a game runs overclocked; if it's still there at the next start, that speed froze the calculator |
| `pocketsnes_octest.tns` | only there while the overclock test runs, for the same reason |


What runs
---------

* Most SNES games run, including ones with DSP-1, DSP-2, Super FX, SA-1,
  C4, OBC1 and Seta chips (the heavy ones slowly). SA-1 games run on the C
  CPU core whatever *CPU core* says.
* **S-DD1 and SPC7110 games don't run**: Star Ocean and Street Fighter Alpha
  2 (S-DD1), Far East of Eden Zero and Momotarou Dentetsu Happy (SPC7110).
* There's no sound emulation at all, which also saves time.
* Yoshi's Island shows glitches in the top rows of the picture. That comes
  from the emulator core; the original port has them too.
* ROMs can be up to 8 MB, if the calculator has the memory free.
* Calculators without a colour screen aren't supported.


How it got faster
-----------------

Measured on a CX II at 396 MHz with Super Mario World, every frame drawn:

| Version | Frames per second |
|---|---:|
| The original port (lcd_blit, two copies of each frame) | 40 |
| The game drawn straight into the frame, -O3 | 48 |
| Frames handed to the LCD instead of copied ("flip") | 49 |
| Frames copied by the DMA chip | 50 (no speed limit) |
| Faster tile drawing, and a shortcut for SMW's backdrop | 59 (no speed limit) |
| Only the 256×224 picture turned for the portrait LCD, tear-free (now) | 66–68 (no speed limit) |

Where a frame's time went, before and now (ms, no speed limit): running the
game 5.7 → 6.0, drawing 11.9 → 7.2, getting it on screen 2.7 → 1.8.

* **Drawing straight into the frame.** The original drew into one buffer,
  copied it to another, and then had Ndless's `lcd_blit` copy it again.
* **The CX II's screen is portrait.** Its LCD reads 320 lines of 240 pixels,
  so a landscape frame has to be turned. `lcd_blit` does that through the
  OS's special LCD memory, which is slow (4 ms a frame). PocketSNES turns
  only the SNES picture (and the text over it) into one of three buffers of
  its own and points the LCD at it. The LCD only switches buffers between
  refreshes, so there's no tearing.
* **DMA output** (optional): the calculator's DMA chip copies frames into
  the OS's LCD memory while the next frame is drawn.
* **Tile drawing** keeps the renderer's state in registers instead of
  reloading it for every pixel (14 → 9 instructions per pixel).
* **Backdrop shortcut**: games like Super Mario World put their background
  on the sub screen with colour maths over a black backdrop; that's now
  drawn directly instead of being fixed up pixel by pixel afterwards.
* **Timer fix**: the frame timer used to be "calibrated" while ROMs loaded,
  which could make games run 5–20% too slow while the counter still said 60.

Everything draws exactly the same pixels as the original renderer
(`tests/render_compare.sh` checks that on 16 games and demos). The full
measurements are in [FINDINGS.md](FINDINGS.md).

**The ARM CPU core** (*CPU core* in Graphics & speed) runs the SNES CPU in
hand-written ARM assembler instead of compiled C; the speed test's last two
rows compare the two on the game you're playing. How much it gains depends
on the game: games that spend their frame waiting for the screen (Super
Mario World does) already skip most of the CPU's work, so they gain little;
games that keep the CPU busy gain the most.


For developers
--------------

### Building

The calculator build needs the [Ndless SDK](https://github.com/ndless-nspire/Ndless)
toolchain on PATH (`nspire-gcc`, `nspire-g++`, `nspire-ld`, `genzehn`,
`make-prg`):

    make -f Makefile.nspire              # makes _PocketSNES.tns
    make -C tools/clocktest              # the stand-alone clock switch test

Variants each need their own `BUILD` folder, because make doesn't notice
changed flags:

    make -f Makefile.nspire OPT_LEVEL=-O2 BUILD=build/o2
    make -f Makefile.nspire BENCH=1 BUILD_NAME=mine BUILD=build/bench TARGET=pocketsnes_speedtest
    make -f Makefile.nspire ARM_CPU=0 BUILD=build/c-only    # without the ARM CPU core

`BENCH=1` makes a build that plays `Super Mario World (USA).sfc.tns` (next to
it) from its newest state, writes a hardware report
(`pocketsnes_diag.txt.tns`: clock, timer, LCD, DMA, memory), runs the speed
test and closes.

The PC test build needs g++ and zlib (and SDL2's headers for a window):

    make                                 # makes ./pocketsnes-host
    ./pocketsnes-host                    # the game list, in a window
    tests/run.sh                         # the test suite (80 checks)
    make unit-tests                      # tile drawing, frame turning, settings
    tests/asm/run.sh                     # the ARM CPU core against the C core, under qemu-arm

[TESTING.md](TESTING.md) explains the PC build, its script mode and the
tests.

### Source layout

| Path | What |
|---|---|
| `pocketsnes/snes9x/`, `pocketsnes/include/` | the Snes9x 1.43 core (65c816 CPU, PPU, renderer in `gfx.cpp` and `tile.cpp`, chips) |
| `pocketsnes/arm/`, `pocketsnes/snes9x/os9x_asm_cpu.cpp` | the 65c816 CPU in ARM assembler, from psnes, and its glue to the C core |
| `frontend/main.cpp` | startup: welcome, game list, game, repeat |
| `frontend/emu.cpp` | runs the core: frame pacing and frameskip, hotkeys, FPS counter and messages, save states, in-game saves, the speed test, CPU speed changes |
| `frontend/menu.cpp` | the menus, built from tables of options; the controls page and the welcome screen |
| `frontend/browser.cpp` | the game list: numbers, moving games, the order file |
| `frontend/overclock.cpp` | the overclock test |
| `frontend/config.cpp` | settings and keys: the file for all games and each game's own |
| `frontend/states.cpp` | save state slots and file names, in-game saves (plain or gzipped), what the save manager does |
| `frontend/savemanager.cpp` | the save manager screen |
| `frontend/paths.cpp` | where files go (`PocketSNES_saves`), moving older versions' files over |
| `frontend/selfstore.cpp` | the settings kept inside the program file |
| `frontend/romzip.cpp` | reading ROMs out of zips |
| `frontend/ui.cpp`, `gui.cpp`, `draw.cpp`, `fonts.h`, `icons.h`, `keys.cpp` | the look of every screen, menu input, text and icons, key names and bindings |
| `frontend/rotate.cpp` | turning frames for the CX II's portrait LCD |
| `frontend/platform.h` | everything hardware: screen, keys, timer, CPU clock |
| `frontend/platform_nspire.cpp`, `clock_switch_nspire.S` | … on the calculator |
| `frontend/platform_host.cpp` | … on a PC, for testing (SDL2 window, or scripted in virtual time) |
| `tests/` | `run.sh` and its scripts, `unit_tests.cpp`, `render_compare.sh` |
| `tests/asm/` | the CPU core tests under qemu-arm: opcode by opcode, and games frame by frame |
| `tools/` | the clock switch test, the font and icon generators |
| `FINDINGS.md` | what was measured and learned about the hardware |
| `menu/`, `sal/`, `sdl/`, `unused/` | the original port's front end, no longer built |

### How it works

**A frame.** The core draws the 256×224 picture straight into a 320×240
RGB565 buffer (with spare lines around it, because the renderer writes
whole 8×8 tiles). On a CX II the front end then turns the picture into one
of three portrait buffers and writes that buffer's address to the LCD
controller (a PL111 at 0xC0000000). With DMA output, the DMA controller (an
FTDMAC020 at 0xBC000000) copies the landscape frame to the OS's LCD memory
at 0xA8000000 instead. Other calculators use Ndless's `lcd_blit`.

**Timing.** The first SP804 timer (0x900C0000) runs free at 32768 Hz.
`emu.cpp` paces frames in 16.16 fixed-point ticks and decides for each frame
whether to draw it. Interrupts stay off while PocketSNES runs.

**Brightness.** On a CX II the OS sets the LCD backlight in 0x90130014,
from 0 (brightest) to 225 (darkest) (Hackspire). PocketSNES reads it at
startup, spreads its ten levels over that range (as lr-gpsp-nspire does),
writes the register only when the setting isn't "calculator's", and puts
the startup value back when it closes.

**CPU speed.** The CX II's power controller (0x90140000) holds the clock
multiplier in bits 24–29 of 0x90140030 (12 MHz × 33 = 396 MHz), but writing
the register does nothing by itself. PocketSNES switches the way the OS does
(its code lives in the on-chip SRAM): normal memory goes into self-refresh,
and for each step of the multiplier the register is written, the switch is
started through register 0x20, and the CPU waits for the power controller's
interrupt. That code runs from the SRAM (`clock_switch_nspire.S`), with the
LCD reading the OS's on-chip buffer meanwhile. After each switch, a loop of
known length is timed against the 32 kHz timer to check the clock really
changed. Marker files catch freezes. Details in FINDINGS.md.

**The overclock test** first plays 6 seconds of the open game at normal
speed (no keys pressed, every frame drawn) from a snapshot, and keeps a
fingerprint of the SNES's work RAM, video RAM, palette and sprite table
after every frame. Each speed then gets two rounds, each after its own
clock switch: the same frames again from the same snapshot, which must give
the same fingerprints (the emulation is deterministic, so any difference is
the overclock), then 2 seconds of memory checks (patterns over a buffer of
up to 8 MB, far bigger than the 8 KB data cache) and a CPU calculation.
The game part is the point: memory and CPU checks alone passed 492 MHz on
the development calculator, whose games froze at 480. Each run starts from
exactly the same point: the snapshot, plus what a snapshot leaves out (some
CPU state and open bus) and the same `rand()` seed, because this port
answers the game's reads of the sound chip with `rand()`; without that,
Super Mario World ran differently every time and the test stopped at 408.
`tests/asm/run.sh` checks that games replay identically on both CPU cores.
Afterwards the game is put back where it was.

**Settings file.** `name=value` lines (`config_version=3`, `frameskip_type`,
`cpu_speed`, `cpu_core`, `brightness`, `auto_slot`, `quit_new_save`,
`highest_tested_mhz`, … `key_a=122`, `key_a_2=121+20` for shift+3). A file
without `quit_new_save` gets it from `auto_increment`, which is how saving
when leaving behaved before. Key numbers are lr-gpsp-nspire's. A game's own file adds
`own_settings=1` / `own_keys=1`. Unknown lines are ignored, so older and
newer builds can share the file; version 2 files' CPU speeds are converted.

**Save states** are a small header and a Snes9x snapshot, compressed with
zlib unless the header says it's stored as it is. In-game saves are written
by the core and gzipped afterwards.

**Zipped ROMs** are read through the zip's central directory: the first
file with a ROM's extension, or else the biggest, stored or deflated, and
checked against its CRC (`frontend/romzip.cpp`).

**Settings in the program file:** an 8 KB area after the program (Ndless
loads the whole file; the Zehn loader only uses its own part) with a
footer, `PSNESCFG`, the text's length and the area's size. The first save
adds it, later ones write over it in place, so the program's own bytes
aren't written again (`frontend/selfstore.cpp`). File associations that
name a PocketSNES are pointed at the name it runs under.

### The CPU cores

The ARM core comes from psnes 7.2.0 (DrPocketSnes for the GP2X, Wiz and
Caanoo; gameblabla suggested it). It is Yoyofr's 65c816 core from
OpenSnes9x, with changes by Squidge, notaz and Bitrider. It keeps the SNES
registers in ARM registers, has a jump table of opcodes for each
combination of the M and X flags (and one for emulation mode), and handles
ordinary ROM and RAM itself.

It was changed to do exactly what the C core does, so the two can stand in
for each other mid-game and share save states:

* Everything that isn't plain ROM or RAM (I/O registers, SRAM, special
  chips, unmapped areas) goes through the C core's own `S9xGetByte` and
  friends, with the cycle count, idle loop state and "branch skip" request
  passed back and forth.
* Cycles are counted as the C core counts them (CATSFC's timings), and in
  the same order, so an I/O register sees the same cycle count on both.
* The open bus byte is kept up to date the same way. It lives inside the
  `CPU` struct, where the assembler reaches it, and `OpenBus` names that byte.
* Decimal mode ADC/SBC and the emulation mode stack opcodes run the C
  core's code; the stack wraps in bank 0 as in C.
* Fixed from psnes: XCE set the carry when staying in native mode,
  the idle loop flag was saved as the branch skip flag, and calls assumed
  the ARM9's way of storing PC (`STMFD {PC}`); calls into C now keep the
  stack 8-byte aligned, as the EABI needs.

`tests/asm/run.sh` checks this under qemu-arm: every opcode in every mode
from random states (registers, flags, cycles, memory and the open bus must
match), then games run on both cores with the same input, compared frame
by frame. See [TESTING.md](TESTING.md).


Future steps
------------

Ideas that haven't been done yet, roughly from most to least promising:

* **Tile drawing in ARM assembly.** Drawing is still the biggest part of a
  frame. snes9x2002 has an ARM assembly tile renderer that could be ported.
* **Profile-guided optimisation**: compile, record a run, compile again
  using the recording.
* **Rendering into the on-chip SRAM**, which is several times faster than
  normal memory for reads. Two earlier tries froze the calculator; it later
  turned out the MMU's page table lives in the SRAM at 0xA4004000, which
  those tries overwrote, so a retry that leaves the first 32 KB alone might
  work.
* **Overclocking is on hold.** It isn't reliable (see above), and there's
  no known way yet to make it run well. Picking it up again would take
  something concrete, such as working out the memory controller's timing
  registers so the memory can be given slower timings at higher speeds.
* **S-DD1 and SPC7110 support** (Star Ocean, Street Fighter Alpha 2, Far East
  of Eden Zero).
* **Yoshi's Island's top-row glitches** in the core's Super FX code.


Credits and license
-------------------

* Snes9x 1.43 by the Snes9x team; PocketSNES by Nebuleon; the TI-Nspire port
  by gameblabla.
* The ARM assembler CPU core: Yoyofr (OpenSnes9x), Squidge, notaz, Reesy
  and Bitrider (psnes / DrPocketSnes).
* The menu follows lr-gpsp-nspire (gpSP by Exophase, Nspire port by
  andymcca).
* Ndless by the Ndless team. The clock register was found by Wenting Zhang
  (zephray) and NoverII (Xavier Andreani); the switch follows the OS's own.
  Hardware details from Hackspire and the Firebird emulator.
* Fonts: Spleen by Frederic Cambus (BSD 2-Clause).

Snes9x is free for non-commercial use: "Permission to use, copy, modify and
distribute Snes9x in both binary and source form, for non-commercial
purposes, is hereby granted without fee, providing that this license
information and copyright notice appear with all copies and any derived
work." The full notice is at the top of `pocketsnes/include/snes9x.h`.
Super NES and Super Nintendo Entertainment System are trademarks of
Nintendo.
