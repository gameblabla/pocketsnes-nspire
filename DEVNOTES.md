PocketSNES for the TI-Nspire: developer notes
=====================================
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
is `fork/nspire-frontend-speed`, pushed from `asm-cpu` (last on
2026-10-11). Release
[v4.0](https://github.com/jadenmcdaniels/pocketsnes-nspire/releases/tag/v4.0)
on the fork was first published on 2026-10-10 from 6c74b1a (asset
`pocketsnes.tns`, archived in `tests/calc/versions/`), then updated in
place on 2026-10-11: tag moved to the branch head, asset `_PocketSNES.tns`
built from 3eb26f2 (md5 2102a026…; the later commits are docs only).
Pushes and releases only happen when the owner says so.

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

    tests/run.sh          # 103 checks: scripted runs of the PC build, screenshots in tests/out/
    make unit-tests       # 102 checks: tile drawing, frame turning, settings, program-file settings, ndless.cfg.tns, file list
    tests/asm/run.sh      # ARM core vs C core under qemu-arm: every opcode, 12 games x 3600 frames, replays
    tests/render_compare.sh   # pixel compare against ./pocketsnes-host-ref (old renderer)

Look at the screenshots of any screen that changed, not just the pass
count. The PC can't show cache, DMA, LCD or clock problems: review those
by hand. An untested calculator build that misbehaves costs a lot more
than the tests do.

### Put a build on the calculator

`tools/deploy.sh` does all of the below in one step and prints a few
lines: PC tests (`--no-test` skips them), the build (named after the
commit), a backup of the calculator's copy in `tests/calc/versions/`, its
settings area carried over, the upload and an md5 read-back.
`tools/deploy.sh --check` only reads the copy back again. Details go to
`build/deploy.log`.

`~/nspire/build/tools/nspire-link` talks to the calculator over USB
(libnspire, no root needed):

    nspire-link info
    nspire-link ls /snes
    nspire-link put -f _PocketSNES.tns /snes/_PocketSNES.tns
    nspire-link get /snes/_PocketSNES.tns /tmp/check.tns && md5sum /tmp/check.tns
    nspire-link mkdir | rm | rmdir | cp | mv | shot

* Paths start at My Documents (`/snes` is My Documents/snes). Every
  uploaded name must end in `.tns`.
* `mv` renames or moves files and whole folders in one step (it renamed
  `/PocketSNES_saves` to `/zPocketSNES_saves` with its 21 files);
  `rmdir` removes an empty folder. Both added 2026-10-11 (the binary before
  them is `nspire-link.before-mv`).
* **`ls` quirks:** never pipe it into `head`. A listing stopped partway
  leaves the calculator's listing open, and the next `ls` of another folder
  printed that folder's leftover entries after its own (it looked as if
  `/snes` held every save). It also sometimes prints the first entry twice:
  compare listings with `sort -u`. The listing order is the calculator's
  storage order, not the file browser's.
* **Always read an upload back and compare md5s.** Once a `get` returned
  the right length with 14 stray bytes at the front; a second `get`
  matched. Re-read before assuming the upload is bad.
* Before replacing `_PocketSNES.tns`, read the calculator's copy first: the
  settings live inside it (below). A fresh build picks them up again from
  `zPocketSNES_saves/pocketsnes.cfg.tns`, so this only matters if that copy
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

Since 2026-10-11 (3eb26f2; the z since 19a516b):

    My Documents/
      ndless/ndless.cfg.tns        file associations (ext.sfc=_PocketSNES …)
      snes/
        _PocketSNES.tns            the program, first in the folder; settings for all games inside it
        Super Mario World (USA).sfc.tns
        Game.zip.tns               zipped ROMs work too
      zPocketSNES_saves/           every game's saves, wherever its ROM is; last in My Documents
        <rom>.sv001.tns … .sv999.tns, <rom>.svauto.tns    save states
        <rom>.srm.gz.tns / <rom>.srm.tns                  in-game saves
        <rom>.cfg.tns                                     a game's own settings and keys
        game_order_snes.txt.tns                           the snes folder's game order
        pocketsnes.cfg.tns                                copy of the settings for all games
        pocketsnes_overclock.txt.tns, pocketsnes_results.txt.tns, markers

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
| 3eb26f2 | one `PocketSNES_saves` in My Documents; `game_order.txt.tns` from a ROM folder becomes `game_order_<folder>.txt.tns` |
| 19a516b on | `zPocketSNES_saves` in My Documents; the old My Documents folder moves in at program start (its game orders already have folder names) |

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
* **Full screen is a setting, the original size the default** (*Screen
  size*, 2026-10-15). Stretching 4 pixels to 5 and 224 lines to 240 can't
  keep every pixel the same size, so *smooth stretching* (on by default)
  makes the doubled column and line averages of their neighbours instead
  of copies. Output line y shows source line (2y+1)·lines/480, which
  spreads the repeated lines evenly. *Full, crop edges* drops 8 lines at
  the top and bottom first (Yoshi's Island blanks 16 of its own). The
  stretch and the turn for the portrait LCD are one pass into the
  portrait buffer, so a stretched frame is always shown that way; the DMA
  output, which copies a landscape frame, only applies at the original
  size.

Keys and settings:

* **Defaults are the owner's layout:** A shift, B del, X ctrl, Y var, L
  tab, R menu, Start cat, Select frac; N load newest, ) and ( next and
  previous slot, R restart, + and - brightness (as in gpSP); frameskip off,
  FPS counter on. *Reset all settings* (game list → menu on a folder) goes
  back to these. The owner's own settings keep a few personal choices on
  top (brightness 8).
* **A newer hotkey's default key isn't given when the file already uses
  that key**, so an update never steals a key someone bound.
* **Settings for all games live inside the program file** (an 8 KB
  `PSNESCFG` trailer), so they travel with it; a copy in
  `zPocketSNES_saves` carries them over to a new version copied on top.
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
* **The saves folder is `zPocketSNES_saves`** so it's the last folder in
  My Documents, below the game folders. As `PocketSNES_saves` the owner
  saw it above `snes`: the browser lists folders A to Z ignoring case
  (gbc, ndless, PocketSNES_saves, snes on the calculator), folders above
  files. `_PocketSNES` coming first in `snes` means `_` sorts before
  letters there; whether `~` would sort last or first is unknown, and a
  leading z is last either way. gpSP's saves folder became `zgpSP_saves`
  at the same time (agreed between the two work sessions).
* **The OS's file list is a cached copy.** It never re-reads a folder after
  a program renames, moves or deletes files, and a USB rename doesn't
  update it either (USB puts and deletes only add or drop that one file,
  with a "Document Received/Deleted" box on the calculator). After the
  saves-folder moves the owner still saw `PocketSNES_saves`, `..cfg` and
  old saves in `/gba`, all gone. `platform_shutdown` calls Ndless's
  `refresh_osscr()` last (gpSP too, in `nspire_restore`). Its syscalls are
  in Ndless's table for OS 6.4.0.74; whether it redraws the list there is
  waiting on the first quit with 7834655. The OS reads every file again for
  it, which is slow on a calculator with many files (issue #11: slow to
  close), so it only runs when PocketSNES added, removed or renamed a file:
  `paths_file_list_start` notes the names in the saves folder at start,
  `paths_file_list_changed` compares at exit (a file written over doesn't
  count), and moves of older files call `paths_file_list_touched`.
* **GBA zips:** PocketSNES owns `.zip`, so a GBA zip opened from My
  Documents starts PocketSNES. Other consoles' ROMs inside a zip never
  count as the ROM; a GBA one says "It's a GBA game: start it in gpSP."
  (Before 7834655 the .gba ran as a SNES game and saved junk states.)
* **Save state pictures** (*Saving → State pictures*, on by default; asked
  for 2026-10-19, "like RetroArch or TI-Boy CE"). A state file can carry a
  112x98 RGB565 picture after the snapshot (header flag `STATE_PICTURE`,
  then `struct PictureHeader` "PSPC" and the pixels, zlib unless Compress
  saves is off); older versions read only the header and snapshot, so they
  still load these states (checked with the pre-picture PC build). The
  save manager carries the picture along. The picture is the SNES picture's
  middle 224 lines shrunk to 7/16 with a box filter (`take_picture`), taken
  in `S9xDeinitUpdate` before the FPS counter and messages are drawn over
  it: a save, the menu or quitting has one more frame drawn first (forced
  even with frameskip), so a hotkey save now happens on the frame after
  the key, the frame its picture shows (`roundtrip*.txt` wait 151). The
  menu draws the selected slot's picture on a card under *Load state*
  (`draw_state_picture`), over the values of the rows below.
* **File associations:** PocketSNES registers sfc, smc, fig, swc and zip
  with Ndless, and points existing PocketSNES associations at its current
  name, so opening a ROM in My Documents starts that game. It edits
  `My Documents/ndless/ndless.cfg.tns` itself (`romtypes.cpp`), reading it
  once and writing only when a line changes. Ndless's
  `cfg_register_fileext` finds the file with `locate()`, which walks every
  folder of My Documents (stat on each file, A to Z by strcmp, so
  capitalised folders before `ndless`) on every call; five calls at every
  start froze a calculator with many files for most of a minute (issue
  #11, CX II CAS-T). Opening a ROM from the OS list still makes Ndless
  walk the files to find the program (`ploaderhook.c`, every associated
  program has that).
* **Zipped ROMs:** rename `Game.zip` to `Game.zip.tns`. A zip that replaces
  the unzipped ROM takes over its saves.
* **Number keys 1-9, 0 start the first ten games** in the game list; tab
  (or *Move in the list*) reorders; the order is kept per folder.

Emulation core:

* **H-blank starts at dot 274 and each line is taken at dot 48**, as in
  Snes9x 1.52 and later (Snes9x 1.43 had H-blank, its HDMA and the $4212
  flag at dot 256, and took a line's settings at its first cycle). This
  fixed Yoshi's Island's top line (2026-10-13). Its IRQs at lines 6 and 8
  wait for the next H-blank on $4212 and then switch HDMA on and the
  screen to full brightness. Their first poll lands close to dot 256, so
  with the old timing the IRQ's few cycles of jitter decided whether they
  caught this line's H-blank or the next. The brightness write also
  slipped to dot 7-10 of line 9 when the line's HDMA ran first, which the
  console still shows on line 9 (pixels start at dot 22). In 400 frames
  the first picture line was lit in 124 and dark in 276; the sky's colour
  bands also jumped a line in about 4% of frames (HDMA switched on a line
  late). Both changes are needed (each alone still flickers). A register
  write before dot 48 now changes the line it's in: the end of H-blank
  queues the line (`S9xQueueRenderLine`), and it's taken at the first PPU
  register write or VRAM/CG-RAM flush from dot 48 on, or at the next
  H-blank before HDMA. Against the old build, every test game and demo is
  pixel-identical except Yoshi's Island, the Rings demo (the first line,
  where the demo writes the second half of a scroll value at dot 8; the
  old build drew that line with a half-written value) and Chrono Trigger
  (one line of one loading frame that is blanked at dot 34, now blank).
* **Each line keeps its own fixed colour** ($2132), taken with the line in
  `RenderLine` (`LineFixedColour`), and the backdrop passes in `DrawLines`
  use it. A $2132 write then only makes the lines above it be drawn first
  when a layer has colour maths ($2131 & $1f) or pseudo hi-res ($2133 bit 3,
  which now also redraws when it changes) is on, because the tile drawers
  use one fixed colour for everything they draw. Yoshi's Island writes $2132
  on nearly every line through HDMA (the sky) with colour maths on the
  backdrop only, and was drawn in about 210 one-line pieces a frame; the
  pixels are the same as before for every test game.
* **S-DD1 is on** (`SDD1_DECOMP` in `pocketsnes/linux/port.h`; the port had
  it commented out and `sdd1.cpp`/`sdd1emu.cpp` in `unused/`). $4804-$4807
  map a megabyte of ROM into banks c0-ff (`S9xSetSDD1MemoryMap`), a DMA with
  a fixed source address while $4801 is set gets `SDD1_decompress`'s output
  instead of the ROM, and loading a state now puts the banks back
  (`S9xSDD1PostLoadState`, which Snes9x 1.43 and snes9x2005 have but never
  call). `sdd1emu.cpp` gives the same bytes as bsnes's decompressor for
  3000 random streams (checked once, in a scratch harness, not kept: bsnes
  is GPL) and as `tests/make_sdd1_rom.py`'s own decompressor.
* **Two Super FX interpreters.** `fx_run` in C keeps the status register,
  the source and destination register numbers, the pipe byte and the bank
  pointers in locals and jumps from one instruction's code to the next
  through a table of labels (GCC's computed goto). `fx_run_arm`
  (`pocketsnes/arm/fx_run_arm.s`) keeps them, and R15, in ARM registers. Both
  run the rare instructions through `fx_apfOpcodeTable` (the original C
  functions) with the state written back first. The assembler one also
  sends any instruction that would read or write R15 by register number
  there, and while the source or the destination is R15 it uses a
  "careful" table that sends everything there. While the destination is
  R14 it uses a third table of the handlers' `_d14` versions, which reload
  the ROM buffer after writing R14 (the handlers are macros assembled
  twice). So its own code never needs those checks. It runs with *CPU core:
  ARM* (the C one with *C*, as a fallback). `tests/fx_test.cpp` checks both
  against the original loop, byte for byte.
* **Fast paths go with *CPU core: ARM*, the C code with *C*.** The tile
  drawers' inner loop and the line fills in ARM assembler
  (`pocketsnes/arm/gfx_arm.s`, `ARM_ASM_GFX`) and the word-at-a-time VRAM
  DMA loop (`dma.cpp`) check `Settings.AsmCPU`, like the Super FX
  interpreter. That leaves *C* as a fallback with the original code, and
  lets `tests/asm/run.sh` compare every frame of the test games with and
  without them (the C setting's run is the reference).
* **The Super FX translator** (`fxjit.cpp`, 2026-10-15) replaced the
  assembler interpreter for *CPU core: ARM*; *C* runs the C interpreter,
  and the translator falls back to it (`fx_step`) an instruction at a time.
  The assembler interpreter is no longer called by the emulator
  (`fx_run_arm_interpreter` is left in; `tests/fx_test.cpp` still checks
  it). Blocks are keyed
  by bank, address and prefix state, so the prefixes cost nothing when a
  block runs; R15 is a constant in each instruction; a flag is stored only
  if something can read it before it's set again (a block's end reads
  them all). Exits are patched into direct branches once their target is
  translated; computed jumps look the target up in the block table from
  the translated code. What it doesn't do goes to `fx_step` (code in the
  GSU's RAM, ljmp, a delay slot holding a jump, the C functions with R15 as
  destination, the first instruction of a run whose pipe isn't the byte
  before R15), and an address that failed is remembered. On the
  calculator the new code is written back from the data cache and the
  instruction cache invalidated (`FX_JIT_CACHE_OPS`); under qemu the buffer
  is made executable with `mprotect` (`FX_JIT_MPROTECT`).
* **Depth ceilings** (`LineMaxDepth`, gfx.cpp, 2026-10-15): for each line
  of the main and sub screen, a depth at least as high as anything drawn
  on it so far (the clears set 0 and 1). `DrawOBJS` and `DrawBackground`
  raise it as they draw (by what each row really drew; mosaic, offset-per-
  tile and mode 7 raise it by their highest depth), and a background tile
  above every ceiling on its lines is drawn with the untested drawers
  (`DrawTileLines16_u*`, no depth compare: the result is the same). Off
  for interlace and double-height pixels.
* **Background rows in two passes** (`ListBGTiles16` and
  `DrawBGCommands16` in `gfx_arm.s`, `DrawBGTiles16` in tile.cpp,
  2026-10-16): the first lists a row's unclipped tiles with everything in
  registers (tables by the map entry's bits for depth, flips and palette;
  blank tiles dropped; 16x16 quarters; tile numbers wrapping past the last
  tile), the second draws the list. One pass in assembler ran out of
  registers and reloaded the row's values for every tile; in C, GCC
  spilled them. A row where a 16x16 quarter's sum may carry into the
  palette bits (DrawBackground adds the quarter to the whole map entry) is
  listed by the C lister instead, which adds as DrawBackground does.
* **The tile drawers' assembler** (`DrawTileLines16_*`, 8 versions: flips,
  clipped or not) gets each pixel's palette index already doubled with one
  `ands` (mask 0x1fe, shifted), which also says whether it's 0; the depth
  compare and the three loads/stores after it are conditional, and the next
  pixel's depth is loaded between the colour's load and its store, so no
  load result is waited for (byte and halfword loads come 2 cycles late on
  the ARM926). `DrawTile16`'s C part (the tile cache lookup and palette)
  calls it through a table indexed by the flip bits; it reads Z1, Z2 and
  PPL from GFX itself (offsets checked with `static_assert` in tile.cpp).
* **A new brightness rebuilds the colours only when they're drawn with**
  (*CPU core: ARM*, 2026-10-16): `S9xUpdateScreen` does it, and not at all
  if the brightness is back to what they were built with; CGRAM writes in
  between use the old brightness and are rebuilt with the rest. Yoshi's
  Island turns it down and up in every V-blank.
* **VRAM DMA with source and address both moving on by one is a
  `memcpy`** (*CPU core: ARM*), with the tile caches marked a block at a
  time; anything that would wrap goes through the word loop.


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

* 3eb26f2's move (seen 2026-10-11 evening): `/snes` held only
  `_PocketSNES.tns` and the ROMs, My Documents/`PocketSNES_saves` had the
  21 files (`game_order_snes.txt.tns` among them), and the program had
  grown by 8,208 bytes again with the same settings as the copy.
* Renaming a whole folder over USB (`nspire-link mv`), 2026-10-11:
  `/PocketSNES_saves` became `/zPocketSNES_saves` with the same 21 files.

PC only, waiting for a run on the calculator (all built after 6c74b1a):

* 19a516b (uploaded 2026-10-11 evening, 446,241 bytes = the build plus the
  owner's settings area from the program it replaced, md5 98976401…): the
  saves folder was renamed over USB first, so the program's own move from
  `PocketSNES_saves` hasn't run on the calculator (PC test `docsfolder`).
  The replaced program and every file of the old folder are in
  `tests/calc/versions/2026-10-11-before-zsaves/`.
* 7834655 (uploaded 2026-10-11 ~19:52, 446,837 bytes with the owner's
  settings area, md5 383ac6c4…): `refresh_osscr()` at exit, the GBA zip
  message. The program it replaced is in
  `tests/calc/versions/2026-10-11-before-refresh/` with the 3 junk
  Advance Wars states it had saved (deleted from the calculator).
* Zipped ROMs, the save manager.
* The reworked overclock test (game replay). Whether it catches the 480
  MHz freezes is unknown.
* Auto save slot.

To check after the next calculator run: `ls /` (`zPocketSNES_saves`, no
`PocketSNES_saves`), `ls /snes` (the program and the zips only), the old
states load from `zPocketSNES_saves` (the zips take over the `.sfc` saves on
each game's first open), the game order is kept.

Not PocketSNES's: `..cfg.tns` (64 bytes) in My Documents and in `/gba` are
per-game settings in the older gpSP build's format (`change_ext` of an
empty game name); the gpSP QoL build doesn't write them.
`pocketsnes_asm.tns` (an older build that used `.pocketsnes`) is no longer
on the calculator (gone by 2026-10-11 evening).


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
| 2026-10-11 | | Pushed to PR #10; release v4.0 updated in place with all of the above |
| 2026-10-11 | 19a516b | Saves folder `zPocketSNES_saves`, below the game folders in My Documents |
| 2026-10-11 | 7834655 | OS file list refreshed at exit; GBA zips refused |
| 2026-10-13 | ee26428 | Yoshi's Island's top line steady: H-blank at dot 274, lines taken at dot 48 |
| 2026-10-13 | 84f8ed7..05b6cf7 | Per-line fixed colour; Super FX interpreters in C and ARM assembler; `tests/calc_speed.sh` |
| 2026-10-14 | | S-DD1 (Star Ocean, Street Fighter Alpha 2) and its test cartridge |
| 2026-10-15 | 82d4db8 | Super FX assembler: R14 destination table, plot, merge, color |
| 2026-10-15 | 71bef17..ee6d762 | Tile drawing and line fills in ARM assembler, VRAM DMA a word at a time, `calc_speed.sh` FUNCS/ANNOTATE |
| 2026-10-15 | 042079c | Screen size: full screen, full with the edges cropped, sharp or smooth |
| 2026-10-15 | 30c3a33, 8465004 | The Super FX translated into ARM code |
| 2026-10-15 | 0e3c338 | Background tiles nothing on their lines can hide drawn without the depth test |
| 2026-10-16 | b61b1c2, 3129554 | Background rows in two assembler passes; VRAM DMA a copy; brightness rebuilt when used |
| 2026-10-19 | 394535e | Issue #11: no folder walk at start (ndless.cfg.tns edited directly), OS file list refreshed only after file changes |
| 2026-10-19 | 28a086f | Save state pictures, shown at Load state |
| 2026-10-20 | 208ed5e | `tools/deploy.sh`: tests, build, backup, settings kept, upload and check in one step |
| 2026-10-20 | 1d9faa6 | Speed model with caches and memory, fitted to the calculator (`tools/calc_speed/calibrate.py`) |
| 2026-10-20 | 4031b0b | The busiest code runs from the on-chip SRAM (CX II) |
| 2026-10-20 | | Everything since PR #10 pushed (branch asm-cpu) as a new PR |

`git log` has the details; every commit message says what changed and why.


Open items and ideas
--------------------

Waiting on the owner:

* A speed test of Yoshi's Island on the calculator with 4031b0b (uploaded
  2026-10-20), from the owner's busiest save (state 14, in
  `build/calc-saves/yi/`; the FPS counter showed 57-58 there with the
  2026-10-16 build). The results file now says where the code ran ("code
  in the SRAM: N KB" or why not). The model with caches
  (`tools/calc_speed/calibrate.py`) estimates 62.6 fps with no speed limit
  without the SRAM code and 70.2 with it; the fill time from the SRAM (30
  cycles) is a guess from FINDINGS.md's measured 4x. If the calculator
  froze, the next start keeps the code in normal memory
  (`pocketsnes_sramcode_off.tns`).

* Run v4.0's `_PocketSNES.tns` on the calculator (uploaded 2026-10-11):
  settings, keys and saves still there; `_PocketSNES` first in `snes`; how
  far the overclock test gets in SMW.

Ideas, most promising first (see README → Future steps):

* The Super FX translator's code buffer in the SRAM too: a quarter of
  Yoshi's Island's instruction cache misses (93 KB of translated code runs
  every frame). The model: 13.0 ms a frame in save 14 with 128 KB of the
  buffer and the best 96 KB of code there, against 13.8 with code only.
  The assembler 65c816 core (121 KB) has only 24 KB of hot code, spread
  out: its hot handlers in one section would leave room.
* Data cache misses (about 2.6 ms a frame in save 14: background drawing,
  sprites): hot buffers in the SRAM.
* Saving and loading states faster (the owner asked, 2026-10-20): not
  looked into yet beyond reading `states.cpp` (compress2 at Z_BEST_SPEED,
  a temporary file then rename).

* Sprites: `DrawOBJS` calls `DrawTile16` for every 8 pixels of a sprite on
  a line, about 150 instructions each with the setup per sprite and line
  (Yoshi's Island sv010: 465 calls, about 100K instructions a frame). A loop
  over a sprite's tiles in assembler.
* Opaque lines: 89% of the untested background lines Yoshi's Island draws
  have no pixel 0 (FINDINGS.md). A per-tile flag from `ConvertTile` could
  pick drawers without the per-pixel test that store the depths 2 or 4 at
  a time (the row's alignment decides).
* The Super FX translator: GSU registers kept in ARM registers across
  blocks, flags dropped across jumps (its code is 260K instructions a frame
  in sv010).
* Idle frames: Yoshi's Island waits for the next frame in a two-instruction
  loop that the core's idle loop detection skips, but every H-blank event
  wakes it and it runs twice before skipping again (`WaitCounter`), plus
  the events themselves: about 50K instructions a frame between
  `doHBlank`, `S9xDoHBlankProcessing_SFX` and the loop.
* H-DMA: Yoshi's Island writes the fixed colour on every line through
  `S9xSetPPU` (about 30K instructions a frame with the H-DMA loop).
* A lighter HBlank sync for the ARM core (`doHBlank` save/load of
  registers), and calling `S9xGetPPU`/`S9xGetCPU` directly for I/O.
* Profile-guided optimisation.
* SRAM rendering again, leaving the first 32 KB alone.
* The SPC7110 chip.
* Star Ocean and Street Fighter Alpha 2 themselves (only the S-DD1 test
  cartridge has run).


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
