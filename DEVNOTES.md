PocketSNES for the TI-Nspire: developer notes
============================================

The working notes for this port: where everything is, how to build, test
and put a build on the calculator, the decisions made along the way and
why, what has and hasn't been seen working on real hardware, and what's
next. Kept up to date with each change (last: 2026-10-11).

Details live in the other files, and this one points to them:

* [README.md](README.md): the user guide, then the developer reference
  (building, source layout, how each part works, the CPU cores).
* [FINDINGS.md](FINDINGS.md): everything measured on the calculator (LCD,
  DMA, clock switching, memory, speed tables, SRAM, MMU).
* [TESTING.md](TESTING.md): the PC build, its script mode and every test.

"Seen" below means it ran on the calculator; "PC only" means it passed the
PC tests but hasn't run on the calculator yet.


Contents
--------

1. [The setup](#the-setup)
2. [Build, test, deploy](#build-test-deploy)
3. [Files on the calculator](#files-on-the-calculator)
4. [Decisions and why](#decisions-and-why)
5. [Gotchas](#gotchas)
6. [Status on hardware](#status-on-hardware)
7. [History](#history)
8. [Open items and ideas](#open-items-and-ideas)
9. [Researched, not built](#researched-not-built)


The setup
---------

**Hardware.** TI-Nspire CX II, OS 6.4.0.74, boot2 6.20.7, Ndless 4.5.5
(installer for OS 6.2/6.4, release r2022). ARM926EJ-S at 396 MHz (288 MHz
while plugged into USB), 16 KB I-cache, 8 KB D-cache, no TCM, 256 KB
on-chip SRAM at 0xA4000000.

**Repositories** (under `~/nspire`):

| Path | What |
|---|---|
| `Ndless/`, `pocketsnes-nspire/`, `lr-gpsp-nspire/` | untouched clones, reference only: don't change them |
| `build/pocketsnes-nspire/` | main checkout, branch `nspire-frontend-speed` (has an uncommitted SRAM depth-buffer experiment; leave it) |
| `build/pocketsnes-asm/` | worktree, branch **`asm-cpu`**: all current work |
| `build/testroms/` | ROMs and states for tests (SMW, CT, ALttP, YI; `smw.sv001.tns`) |
| `build/tools/` | `nspire-link` (USB transfers), `qemu/` (static qemu-arm 10.2.1) |
| `psnes/` | gameblabla's psnes 7.2.0 zip (entries start with `../`: extract it safely) |

**Remotes.** `origin` = gameblabla/pocketsnes-nspire (upstream; no push
rights), `fork` = jadenmcdaniels/pocketsnes-nspire. PR
[gameblabla/pocketsnes-nspire#10](https://github.com/gameblabla/pocketsnes-nspire/pull/10)
is `fork/nspire-frontend-speed`; it was last pushed at 6c74b1a. Release
[v4.0](https://github.com/jadenmcdaniels/pocketsnes-nspire/releases/tag/v4.0)
on the fork is that build. Everything after 6c74b1a is local only, waiting
for a check on the calculator. Pushes and releases only happen when the
owner says so.

**Environment.** `. ~/nspire/build/env.sh` puts the Ndless toolchain (GCC
14.2) on PATH and unsets the `~/deps` paths that `~/.bashrc` adds with a
trailing `:` (that empty entry breaks GCC). `.bashrc` itself is left alone.
`gh` is in `~/.local/bin`.


Build, test, deploy
-------------------

### Build

    . ~/nspire/build/env.sh
    make -f Makefile.nspire          # _PocketSNES.tns, -O3
    make                             # ./pocketsnes-host, the PC build

Variants need their own `BUILD=` folder (make doesn't see changed flags):
`BENCH=1` (self-running speed test), `ARM_CPU=0` (C core only),
`OPT_LEVEL=-O2`. `make -C tools/clocktest` builds the clock switch test.
README.md → Building has the full list.

### Test (before every calculator build)

    tests/run.sh          # 80 checks: scripted runs of the PC build, screenshots in tests/out/
    make unit-tests       # 75 checks: tile drawing, frame turning, settings, program-file settings
    tests/asm/run.sh      # ARM core vs C core under qemu-arm: every opcode, 12 games x 3600 frames, replays
    tests/render_compare.sh   # pixel compare against ./pocketsnes-host-ref (old renderer)

Look at the screenshots of any screen that changed, not just the pass
count. The PC can't show cache, DMA, LCD or clock problems: review those
by hand. An untested calculator build that misbehaves costs a lot more
than the tests do.

### Put a build on the calculator

`~/nspire/build/tools/nspire-link` talks to the calculator over USB
(libnspire, no root needed):

    nspire-link info
    nspire-link ls /snes
    nspire-link put -f _PocketSNES.tns /snes/_PocketSNES.tns
    nspire-link get /snes/_PocketSNES.tns /tmp/check.tns && md5sum /tmp/check.tns
    nspire-link mkdir | rm | cp | shot

* Paths start at My Documents (`/snes` is My Documents/snes). Every
  uploaded name must end in `.tns`.
* **Always read an upload back and compare md5s.** Once a `get` returned
  the right length with 14 stray bytes at the front; a second `get`
  matched. Re-read before assuming the upload is bad.
* Before replacing `_PocketSNES.tns`, read the calculator's copy first: the
  settings live inside it (below). A fresh build picks them up again from
  `PocketSNES_saves/pocketsnes.cfg.tns`, so this only matters if that copy
  is missing.
* Keep the build being replaced: copies go in
  `~/nspire/build/pocketsnes-nspire/tests/calc/versions/` (older ones in
  `tests/calc/backup/`), named after their commit.
* libnspire was patched (2026-10-05): files of exactly 479 bytes (one full
  512-byte USB packet) failed both ways. 478 to 40,000 bytes pass now.

### When the calculator resets

A reset (a freeze, a crash in an overclock test) removes Ndless. Open
My Documents/ndless/`ndless_installer_4.5.5-6.2.0-6.4.0.tns` again.
`/ndless` needs the official r2022 files: the installer (8446 B, md5
1f7619f8982794a5f7e11ba0854e33ef) and `ndless_resources.tns` (193356 B, md5
d30ba5374125edbd231c9ba208b05577). The first run after a reset can reboot
the calculator; running it a second time is normal.

* **Never `get` the installer while it's open on the calculator.** The OS
  saves an open document before sending it and rewrites the file (it
  becomes `*TIMLP0600`, 8619 B). `put -f` over an open document makes the
  OS close it.
* `nspire-link get /NspireLogs.zip` has the OS's logs (Doc open,
  DOC_CLOSE, TI_ASSERT, "Launching OS" for resets). The calculator's clock
  is UTC.


Files on the calculator
-----------------------

Since 2026-10-11 (3eb26f2):

    My Documents/
      snes/
        _PocketSNES.tns            the program, first in the folder; settings for all games inside it
        Super Mario World (USA).sfc.tns
        Game.zip.tns               zipped ROMs work too
      PocketSNES_saves/            every game's saves, wherever its ROM is
        <rom>.sv001.tns … .sv999.tns, <rom>.svauto.tns    save states
        <rom>.srm.gz.tns / <rom>.srm.tns                  in-game saves
        <rom>.cfg.tns                                     a game's own settings and keys
        game_order_snes.txt.tns                           the snes folder's game order
        pocketsnes.cfg.tns                                copy of the settings for all games
        pocketsnes_overclock.txt.tns, pocketsnes_results.txt.tns, markers
      ndless/ndless.cfg.tns        file associations (ext.sfc=_PocketSNES …)

`<rom>` is the ROM's file name without `.tns` (`Super Mario World
(USA).sfc`). `frontend/paths.cpp` decides all of this; nothing else builds
a save path.

**Where the files used to be, and what moves them.** Each layout is moved
into the current one by `paths_move_old_files`, on every start (for the
program's folder) and whenever the game list opens a folder or a game
starts (for ROM folders). A file already at the destination wins; emptied
old folders are removed.

| Version | Layout |
|---|---|
| PocketSNES 3.0 and v4.0 | `pocketsnes.cfg.tns` and test files next to the program, saves in a hidden `.pocketsnes` folder in each ROM folder |
| 6018db6 (never released) | `PocketSNES_saves` in each ROM folder |
| 3eb26f2 on | one `PocketSNES_saves` in My Documents; `game_order.txt.tns` from a ROM folder becomes `game_order_<folder>.txt.tns` |

The PC build treats the folder above the program's as My Documents
(`--documents DIR` picks another one), which matches the calculator, where
the program sits in a folder of My Documents.


Decisions and why
-----------------

Screen and speed:

* **No tearing.** The default output turns only the 256x224 picture into
  one of three portrait buffers and flips the LCD to it. DMA output (a
  little faster, can tear) is an option. Ndless's compatibility mode was
  ruled out because it tears. Earlier, tearing was accepted for speed; that
  was reversed on 2026-10-05.
* **Fast flip is the only CPU output**; the compatible and mirrored modes
  were removed. `lcd_blit` stays as the automatic fallback on calculators
  that aren't a CX II, and in the speed test's comparison.
* **The FPS counter shows just "drawn/expected"** (e.g. 49/60).
* **The ARM CPU core is the default** (about 28% faster CPU emulation on
  the calculator); SA-1 games always use the C core.
* **Overclocking is on hold** as a recommended feature: 432 MHz works, 456
  sometimes, 480 freezes in games, while memory checks passed 492. The
  overclock test now plays the game itself to catch that. The defaults
  don't overclock: the right speed differs per calculator.
* **SRAM rendering is off** (`SRAM_RENDERING 0`): both tries froze the
  calculator. The MMU page table at 0xA4004000 is the likely reason.

Keys and settings:

* **Defaults are the owner's layout:** A shift, B del, X ctrl, Y var, L
  tab, R menu, Start cat, Select frac; N load newest, ) and ( next and
  previous slot, R restart, + and - brightness (as in gpSP); frameskip off,
  FPS counter on. *Reset all settings* (game list → menu on a folder) goes
  back to these. The owner's own file differs (Start enter, Select (-),
  CPU 480, brightness 8).
* **A newer hotkey's default key isn't given when the file already uses
  that key**, so an update never steals a key someone bound.
* **Settings for all games live inside the program file** (an 8 KB
  `PSNESCFG` trailer), so they travel with it; a copy in
  `PocketSNES_saves` carries them over to a new version copied on top.
* Two key slots per action, key combos (hold+press), one key on several
  buttons, and per-game settings and keys.

Saving:

* **Every save goes to a new slot** by default; *New save on quit* off
  writes the leaving save over the newest state; *Auto save slot* keeps it
  in its own slot. Load on start and save on exit are on by default.
* **Compression on by default** (a state: about 35-60 KB instead of 356 KB;
  measured on the calculator: Chrono Trigger 35 KB, SMW and Yoshi's Island
  57-61 KB).
  In-game saves are gzip, so a PC can unpack them.

Files and launching:

* **The program is `_PocketSNES.tns`** so it sorts above the ROMs, and
  **the saves are not in the ROM folder**, because a folder is listed
  above every file. Opening the `snes` folder and pressing enter starts
  PocketSNES. (First the saves moved from the hidden `.pocketsnes` to a
  visible `PocketSNES_saves` in the ROM folder; that put a folder above the
  program, so it moved to My Documents.)
* **File associations:** PocketSNES registers sfc, smc, fig, swc and zip
  with Ndless, and points existing PocketSNES associations at its current
  name, so opening a ROM in My Documents starts that game.
* **Zipped ROMs:** rename `Game.zip` to `Game.zip.tns`. A zip that replaces
  the unzipped ROM takes over its saves.
* **Number keys 1-9, 0 start the first ten games** in the game list; tab
  (or *Move in the list*) reorders; the order is kept per folder.


Gotchas
-------

* **The shell's `grep` is a ugrep wrapper** that silently skips some files.
  Use `command grep` in scripts and searches.
* **This port has no sound chip.** Reads of $2140-$2143 return `rand()`.
  Anything that replays a game (the overclock test, the core comparison)
  must reset the seed (`srand(1)`) and the CPU, ICPU, IAPU and OpenBus
  state, which a snapshot leaves out; otherwise SMW never runs the same
  twice.
* **ARM9 vs qemu:** `STMFD {PC}` stores PC+12 on the ARM926 but PC+8 in
  qemu. psnes relied on the ARM9 value; the port no longer stores PC.
* **qemu tests:** the newlib shim needs `-fno-builtin` (calloc recursion),
  semihosting splits argv at spaces, and `linux.specs` can't `fopen`; use
  `rdimon.specs`.
* **On-chip SRAM:** the first 16 KB hold exception vectors and the OS's
  clock code; the MMU page table is at 0xA4004000-0xA4007FFF. Writing over
  either freezes the calculator with a black screen (reset needed).
* **USB slows the CPU** to 288 MHz: run speed tests on battery and check
  the MHz in the results header.
* **The calculator's results files got garbled when appended to.** They're
  written fresh each run.
* **`pocketsnes-host-ref`** (the old renderer, for `render_compare.sh`)
  crashes in Chrono Trigger's intro: a 64-bit-only `DepthDelta` bug.
* Writing 0x90140030 alone never changes the clock on OS 6.4; the switch
  has to follow the OS's own sequence from SRAM (FINDINGS.md).


Status on hardware
------------------

Seen on the calculator:

* ARM core: SMW speed test 270 fps undrawn vs 210.8 on the C core; 59.9
  fps in normal play, 79.2 uncapped and drawn (no tearing), 79.9 with DMA.
* DMA output: 60/60 in lighter SMW scenes; speed test 50.3 fps (dma) vs
  48.7 (flip) vs 47.0 (lcd_blit) before the drawing work.
* Brightness (the owner used it and rebound its keys).
* Clock switching (clocktest: 396→372→396, 396→420→396 MHz); in games 432
  MHz works, 456 sometimes, 480 freezes.
* The menus, save states and the speed test, used since the 2026-10-05
  builds.
* The 6018db6 build's file move (2026-10-10/11): `.pocketsnes` and the
  files next to the program moved into `snes/PocketSNES_saves` and the old
  folder was removed, so `rename()` across folders and `rmdir()` work on
  the calculator.
* Settings inside the program file: the calculator's `_PocketSNES.tns` grew
  by exactly 8,208 bytes, and its stored text matched
  `pocketsnes.cfg.tns` byte for byte.
* Compressed saves: states of 35-61 KB and gzipped in-game saves
  (`.srm.gz.tns`) written on the calculator.

PC only, waiting for a run on the calculator (all built after 6c74b1a):

* 3eb26f2 (uploaded 2026-10-11, md5 2102a026…): the move from
  `snes/PocketSNES_saves` to My Documents/`PocketSNES_saves`, and
  `_PocketSNES.tns` then being first in `snes`. The calculator's copy of the
  program it replaced (with the settings inside) is in
  `tests/calc/versions/`.
* Zipped ROMs, the save manager.
* The reworked overclock test (game replay). Whether it catches the 480
  MHz freezes is unknown.
* Auto save slot.

To check after the next calculator run: `ls /snes` (no folders, no
`pocketsnes*.tns` files except the old `pocketsnes_asm.tns`), `ls
/PocketSNES_saves` (20 files, `game_order_snes.txt.tns` among them), the
old states load, the game order is kept, `_PocketSNES.tns` grows by 8,208
bytes again once a setting is saved.

Not PocketSNES's: `..cfg.tns` (64 bytes) in My Documents and in `/gba` are
per-game settings in the older gpSP build's format (`change_ext` of an
empty game name); the gpSP QoL build doesn't write them.
`pocketsnes_asm.tns` (an older build) still uses `.pocketsnes`: running it
again recreates that folder in `snes`, so it should be deleted once the new
build is confirmed.


History
-------

| Date | Commit | What |
|---|---|---|
| 2026-09-30 | | Clones; work copies in `~/nspire/build` |
| 2026-10-04 | | Speed and hardware research (FINDINGS.md); DMA output; BENCH build |
| 2026-10-05 | 8a19fbb | Core: faster tile drawing (locals), save states in memory, 64-bit fixes |
| 2026-10-05 | 235af82 | New front end: menu, save states, settings, tear-free output |
| 2026-10-05 | c7f7f0f | PC test suite, unit tests, renderer comparison |
| 2026-10-05 | 7f8cdcf | Fixed timer rate, measured CPU speed, first-start help |
| 2026-10-05 | 9d68fca | CPU speed menu with NoverII's method (it doesn't change the clock on OS 6.4); clock test tool |
| 2026-10-05 | 7034c08 | New look: themed screens, Spleen fonts, icons; non-ROM files don't crash |
| 2026-10-06 | a250ab6 | Clock switching the OS's way, from SRAM |
| 2026-10-06 | b9037ab | Game list: number keys, moving games |
| 2026-10-06 | cc4bbba | Overclock test (memory and CPU checks) |
| 2026-10-06 | 1ecc22a | README rewritten, beginners first; PR #10 opened |
| 2026-10-08 | 30aa430..c9e32d3 | ARM assembler CPU core from psnes, qemu tests, CPU core setting |
| 2026-10-10 | 2c6e3a4..e0cb368 | Brightness, Auto save slot, New save on quit; + / - brightness defaults |
| 2026-10-10 | 6c74b1a | Overclock test plays the game; PR #10 updated, release v4.0 |
| 2026-10-10 | 6018db6 | `PocketSNES_saves` in the ROM folder; program renamed `_PocketSNES.tns` |
| 2026-10-10 | ebe91b9 | Settings inside the program file |
| 2026-10-10 | d669a64 | Zipped ROMs |
| 2026-10-10 | bc7bec1 | Overclock test: identical replays (rand seed, CPU state) |
| 2026-10-10 | b178535 | Owner's layout as defaults; Reset all settings |
| 2026-10-10 | 4f06bb4, a2b99cf | Compressed saves and the save manager |
| 2026-10-10 | 5144665 | Docs for all of the above |
| 2026-10-11 | 3eb26f2 | Saves in My Documents/`PocketSNES_saves`, so `_PocketSNES.tns` is first in its folder |
| 2026-10-11 | | These notes |

`git log` has the details; every commit message says what changed and why.


Open items and ideas
--------------------

Waiting on the owner:

* Run the current `_PocketSNES.tns` on the calculator: settings, keys and
  saves still there; `_PocketSNES` first in `snes`; how far the overclock
  test gets in SMW.
* Then: push `asm-cpu` to the PR and make a release (only on their go).

Ideas, most promising first (see README → Future steps):

* Tile drawing in ARM assembly (snes9x2002's `tile16` renderer). Drawing is
  still the biggest part of a frame.
* A lighter HBlank sync for the ARM core (`doHBlank` save/load of registers
  is about 8% of an ARM frame), and calling `S9xGetPPU`/`S9xGetCPU`
  directly for I/O.
* Profile-guided optimisation.
* SRAM rendering again, leaving the first 32 KB alone.
* S-DD1 and SPC7110 chips; Yoshi's Island's top-row glitch (Super FX code).


Researched, not built
---------------------

**A home screen shortcut.** The TI-Nspire OS has no way to add an item to
its home screen, and Ndless doesn't add one. What Ndless does have:

* File associations (in use): opening a ROM in My Documents starts it.
* `ndless/startup/`: programs there run once when Ndless is installed (at
  each install after a reset), not on demand.
* Resident programs with OS hooks: a hook could start PocketSNES from a key
  on the home screen, but it needs addresses for this exact OS version,
  and a mistake freezes the calculator and removes Ndless. Not worth it.
* The OS's Recent documents list: Ndless starts programs from the OS's
  "unsupported format" path when a document is opened, so a program
  probably isn't added to that list. Not checked on the calculator.

**Toward 60 fps (2026-10-04).** Reported in chat at the time, not in
FINDINGS.md:

* Ndless's compatibility mode (`lcd_compat.c`) sends the panel a row/column
  swap over SPI so it shows a plain 320x240 buffer; lr-gpsp-nspire relies
  on it. Costs: a dialog at every start, and it tears. Ruled out.
* The OS copies frames into 0xA8000000 with the FTDMAC020 at 0xBC000000
  (Hackspire: AHB1 only; linked lists and 64-bit widths lock up). That is
  where the DMA output came from.
* The OS's LCD setup has a 120-line vertical front porch (about 28% of each
  refresh) and fires VComp at its start: probably meant for a tear-free
  copy. Not confirmed.
* The ARM926's data cache allocates on reads only, and its write buffer (4
  addresses, 16 words) doesn't merge stores, so every pixel store is its
  own memory write. Drawing costs about 24 cycles per tile pixel; write
  stalls are suspected, not measured. GCC compiled `rotate_frame`'s eight
  word stores as eight STRs, not one STM.
* COLOR_ADD/SUB use 128 KB tables (`GFX.X2`, `ZERO_OR_X2`), far bigger than
  the 8 KB data cache.

**Which Snes9x (2026-10-04).** This core's line: Snes9x 1.43 → NDSSFC →
CATSFC → Nebuleon's PocketSNES for the GCW Zero (comments in
`cpuexec.cpp`, `gfx.cpp`). libretro's snes9x2005 is the same family, so
switching would gain little; its portable speedups are Snes9x 1.60's colour
maths in place of the 128 KB tables (ours still uses the tables,
`include/gfx.h`), and SNESAdvance idle-loop ROM patches (we already have
CPU_SHUTDOWN). snes9x2002 (1.39 + DrPocketSNES) has an ARM-asm renderer
(`gfx16.c`, `tile16*.c`, `mode7*.c`, assumes GFX_PITCH 640) and an ARM-asm
CPU that libretro never turns on; it lacks OBC1 and DSP-4. Porting its
DrawTile16 into `tile.cpp` (same signature, same GFX fields) is the
suggested next speed step; the gain is a guess (20-40% of drawing time).
The CPU side was done instead with psnes's core (2026-10-08).
