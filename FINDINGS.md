PocketSNES on the TI-Nspire CX II: findings
==========================================

What we learned while making PocketSNES faster, 2026-10-02 to 2026-10-04.
Calculator: TI-Nspire CX II, OS 6.40.74 (6.4.0.74), boot2 6.20.7, Ndless
4.5.5 (installer for OS 6.2/6.4). Test game: Super Mario World, World 1 pipe
level, from a save state, Mario standing still.

"Seen" means measured on the calculator; "guess" means not confirmed.


The screen
----------

* Seen: the LCD controller (a PL111 at 0xC0000000) reads 320 lines of 240
  pixels, 16-bit 5:6:5: timing register 2 gives 240 clocks per line, timing
  register 1 gives 320 lines (registers: 0x1425094C, 0x03780D3F, 0x00EF3802,
  control 0x392D). It reads 153,600 bytes per frame. Timing register 0 says
  320 pixels per line, but that's not what the DMA reads.
* Seen: landscape pixel (x, y) belongs at position 239 - y of line x. A frame
  turned that way into a buffer in normal memory shows upright when the LCD
  is pointed at it ("flip": each frame is turned into one of three such
  buffers and the buffer's address is written to the LCD, 0xC0000010).
  PocketSNES now only uses this if the DMA controller fails (below).
* Seen: the OS's own LCD buffer is at 0xA8000000. Memory there turns pixels
  as the CPU writes them: a plain landscape copy into it shows upright. CPU
  reads from it return pixels in the turned (portrait) order. The region is
  mapped write-through cached.
* Seen: Ndless matches its source for this OS. lcd_type() says
  SCR_320x240_565 (is_hww is 0), and lcd_blit is a plain 153,600-byte
  memcpy into 0xA8000000. The picture is right, but writing that memory is
  slow: 4.3 ms per frame at 287 MHz.
* Seen: pointing the LCD at a plain landscape buffer of our own gives a
  sideways, garbled picture.
* Seen: the program's code, static buffers and heap are mapped 1:1
  (virtual = physical) and write-back cached, so the LCD can read the
  program's buffers directly. The stack is the exception.
* Seen: Ndless doesn't put PocketSNES in its compatibility mode: the build
  uses lcd_blit, so genzehn marks it as supporting the CX II screen.
  (Compatibility mode, in Ndless's lcd_compat.c, sends the LCD panel a
  row/column swap over SPI so the LCD shows a plain landscape buffer. That is
  how lr-gpsp-nspire works on the CX II, and it tears.)
* Seen: the DMA controller at 0xBC000000 (a Faraday FTDMAC020, revision
  register 0x00011900, feature register 0x00006303: 6 channels, both bus
  ports, linked lists) copies frames into the OS's LCD buffer, as the OS
  does. At startup channel 0 still holds the OS's last copy: control
  0x00071206 (both ports AHB1, 32-bit, 256-word bursts), source in normal
  memory, destination ending at 0xA8025800. The source and destination
  registers count up and the size register counts down during a copy.
  PocketSNES can copy each frame this way on channel 1 ("dma"), with its
  interrupts masked, from one of two buffers, while the next frame is drawn
  into the other. Since 2026-10-05 that is a setting (Screen output: DMA);
  the default turns only the picture into one of three portrait buffers and
  flips the LCD to it (no tearing).
* Seen: a 320x240 frame copy by DMA takes 12.60 / 4.72 / 3.15 / 3.21 / 3.25 /
  3.58 ms with 1 / 4 / 8 / 16 / 32 / 64-word bursts (256-word, the OS's
  setting, wasn't timed), and 1.93 ms when the source is the on-chip SRAM.
  The LCD buffer limits it: the CPU's own copy into it takes 3.15 ms too.
* Seen: while a DMA copy runs, the CPU is slower: with DMA output the game's
  CPU time rose from 5.7 to 7.0 ms per frame and drawing from 11.9 to 12.4
  ms, which ate most of the 2.4 ms the copy saved (see the table below).


The CPU clock
-------------

* Seen: a timed loop of 80 million cycles measured 287 MHz in one speed test
  run, probably with the calculator plugged in over USB. The CX II is rated
  396 MHz, and the 48-49 fps seen while playing fits 396 MHz (34.8 fps x
  396 / 287 = 48).
* Reported (zephray, NoverII): the CPU runs at 288 MHz with USB connected
  and 396 MHz without, so run speed tests unplugged and check the "cpu ...
  MHz" in each results header. The clock is 12 MHz times a multiplier in
  bits 24-29 of 0x90140030 (x33 = 396, x24 = 288); the bus runs at half the
  CPU clock and memory goes up with it. NoverII's author found x41 (492 MHz)
  the highest safe setting; it varies by unit. Not tried here.
* Seen: the 0x900C0000 timer runs at 32768 Hz on the CX II (checked against
  the real-time clock).

### Switching the clock (2026-10-06)

* Seen (tools/clocktest, first version): writing a new multiplier into
  0x90140030 changes nothing. The register reads the new value back, but the
  CPU keeps its speed: x33 -> x35 stayed 396 MHz on battery, x24 -> x26
  stayed 288 MHz on USB, whether the write was followed by a busy wait, by
  idling the CPU (wait for interrupt), by clearing the power controller's
  flags, or done NoverII's way (interrupts on, Ndless's old msleep(1)).
  NoverII is reported working on OS 5.2; on this calculator (OS 6.4, boot2
  6.20.7) it can't be.
* Seen: the OS keeps its clock code in the on-chip SRAM (0xA4001000 to
  0xA40019FF, the same in every dump), called with the stack and the code
  away from normal memory:
  * 0xA4001640 puts normal memory into self-refresh: drain the write buffer;
    if bit 10 of memory controller register 4 (0x90120004) is clear, write 4
    there and wait until bit 2 clears. 0xA40016C4 wakes it: if bit 10 is
    set, write 8 and wait until bit 3 clears, then 8 NOPs.
  * 0xA4001678 is one step: write 0x90140030; save the interrupt
    controller's enables, enable only the power controller's interrupt (15);
    write 0x10000100 into 0x90140020; wait for interrupt; write 0x90140024
    with its own value (clears the flags); put the enables back.
  * 0xA40016F4, 0xA40017B0 and 0xA4001890 step through a table in normal
    memory (0x10AB9E3C: frequency, register value) inside one self-refresh:
    32 (0x04030003), 96 (0x08020103), 156 (0x0D020203), 240 (0x14020303),
    276 (0x17020303), 336 MHz (0x1C020303), then the target. Afterwards they
    write memory controller registers 0x74 (0x55 at 396 MHz, 0x11 at 32 MHz)
    and 0x1C (always 0x502), wait 550 us and wake the memory. 0xA4001890 is
    the OS idling: down to 32 MHz, wait for interrupt, back up.
  * Bits 8-9 of the register grow with the frequency (0, 1, 2, then 3 from
    240 MHz on); everything from 240 MHz up, overclocks included, uses
    0x..020303.
  * Other values for 0x90140020: 0x80000040 (with the low byte of 0x90140030
    set to 0x10 or 0x01, around standby), 0x80000008 (standby, wakes on an
    interrupt), 2 (off: 0xA4001000 cleans the cache, turns the MMU off,
    writes it and loops). 0x90140020 reads 0x10000000 otherwise.
  * 0xA400004C is an interrupt handler that only clears 0x90140024 and
    acknowledges the interrupt controller.
* Seen (tools/clocktest, second version, battery): doing the same (one
  multiplier step at a time, from the SRAM, with interrupts off) works:
  396 -> 372 -> 396 MHz and 396 -> 420 -> 396 MHz, each measured by timing
  a loop. A step takes about 0.15 ms; the whole switch with the 600 us wait
  about 0.9 ms. When a step is done, 0x90140024 has bit 8 set and the
  interrupt controller's raw status bit 15. Bit 28 of 0x90140024 was set
  before any switch on battery (not on USB); it isn't the switch.
* Seen: the interrupt controller's raw bit 19 (the second timer) can be on
  already when a switch starts, from the OS's tick (the OS runs that timer
  periodic, control 0xE6, with its interrupt on), so the switch only takes
  the timer's own flag (0x900D0010) as "time's up".
* PocketSNES does this (clock_switch_nspire.S, platform_nspire.cpp "CPU
  clock"), with the LCD pointed at the OS's buffer (on-chip memory) while
  normal memory sleeps, and measures the clock after every switch.
* Seen (user, in games, 2026-10-06): 432 MHz works, 456 MHz works only
  sometimes, 480 MHz glitches and freezes (the freeze marker named x40). One
  raise stopped at 420 MHz on the way up and fell back to normal (a step
  that didn't finish). Others report 432-444 and 492 MHz as their limits;
  the memory runs at half the CPU clock with timings set for 198 MHz, which
  is the likely limit.
* Seen (the overclock test, overclock.cpp, same calculator): 408 to 492 MHz
  passed (two rounds of 8 s each: 500-650 MB of memory pattern checks and a
  CPU calculation, no errors), 504 MHz failed with memory errors at once.
  So the test passed 492 MHz while games freeze at 480: it doesn't predict
  whether games run. Guess, not checked: its loops run from the caches,
  while the emulator's code (about 300 KB, against a 16 KB instruction
  cache) is fetched from memory all the time, and games also run longer and
  warmer. Overclocking is on hold: no known way to make it reliable.
* Changed (2026-10-10): the overclock test now replays a few seconds of the
  open game at each speed and compares the SNES's memory frame by frame
  with a run at normal speed, before the memory checks. Not yet run on the
  calculator: whether it catches what freezes games at 480 MHz is to be
  seen.


Where the time goes
-------------------

Speed test at 287 MHz, every frame drawn (ms per frame):

| Part | Run the game | Draw | Show (lcd_blit) | Total | fps |
|------|-------------:|-----:|----------------:|------:|----:|
| Every frame drawn | 7.5 | 16.2 | 4.8 | 28.5 | 34.8 |
| Nothing drawn | 7.6 | 0 | 0 | 7.6 | 130 |

At 396 MHz that's roughly 5.4 + 11.7 + 3.7 = 20.8 ms, about 48 fps. 60 fps
leaves 16.7 ms per frame, so running and drawing alone (about 17 ms) are
already just over.

Copying one 320x240 frame at 287 MHz: normal memory to normal memory
2.9 ms, into 0xA8000000 4.3 ms, out of 0xA8000000 1.1 ms. Normal memory is
slow enough that turning a frame into a flip buffer costs nearly as much as
lcd_blit, which is why flipping only gained about 1 fps.

Seen while playing SMW with frameskip off:

| Build | fps |
|-------|----:|
| Before (lcd_blit, two copies per frame) | 40 |
| Drawing straight into the frame, one lcd_blit, -O3 | 48 |
| Flip | 49 |

Speed test at 395 MHz with the DMA build, every frame drawn, no speed limit
(ms per frame):

| Output | Run the game | Draw | Show | fps |
|--------|-------------:|-----:|-----:|----:|
| dma | 7.0 | 12.4 | 0.3 | 50.3 |
| flip | 5.7 | 11.9 | 2.7 | 48.7 |
| lcd_blit | 5.7 | 11.9 | 3.5 | 47.0 |

Compiler settings: -O3 beat -O2 and -Os. Speed test, automatic frameskip,
first part only, probably at 396 MHz: running the game took 5.8 / 6.1 / 6.2
ms per frame and drawing 11.9 / 12.2 / 13.3 ms per drawn frame (-O3 / -O2 /
-Os). -O3 is the default now.

Frameskip: automatic frameskip kept the game at full speed (59.9 game fps)
at 287 MHz. Manual frameskip 1 (30/30) only managed 54 game fps there.


Memory
------

* Seen: the CPU (id 0x41069265, cache type 0x1D112152) has a 16 KB
  instruction cache and an 8 KB data cache, and no TCM (TCM status 0).
* Seen: the program runs in SVC mode with IRQs off (cpsr 0x20000093).
* Seen: the on-chip SRAM at 0xA4000000 (256 KB) is mapped 1:1,
  write-through cached (descriptor 0xA4000C1A, like the LCD buffer's). None
  of it changed in 2 seconds or while a file was written, and writing to it
  with interrupts off and putting it back did no harm.
* Seen: its first bytes are the CPU's exception vectors (ldr pc, [pc, #0x18]
  x 8, then the handler addresses: the OS at 0x1042xxxx, Ndless's system
  call and data abort handlers at 0x13A1xxxx, IRQ to a stub at 0x40). After
  them come the OS's interrupt entry stub and its sleep code (cleans the
  cache, turns the MMU off). Wiping them gives a black screen and a freeze
  at the next system call; the calculator needs a reset. So the first 16 KB
  must be left alone. The rest is the boot loader's leftovers (boot2
  5.0.0.42: its log, e.g. "Clocks: CPU = 396 MHz AHB = 198 MHz APB = 99
  MHz", and its Nucleus kernel's data), except:
* Seen (2026-10-06): the MMU's page table is in the SRAM, at 0xA4004000 to
  0xA4007FFF (translation table base 0xA4004000; its entries match what
  mmu_lookup reads, e.g. 0x10000C1E for normal memory, 0xA4000C1A for the
  SRAM, and the zero ones are unmapped addresses). The OS's clock code is at
  0xA4001000 (see "Switching the clock").
* Seen: speeds drawing 8x8 tiles the way the renderer does (CPU cycles per
  pixel, 395 MHz):

| | Normal memory | On-chip SRAM |
|---|---:|---:|
| 16-bit stores | 6.1 | 2.0 |
| read the line first, then 16-bit stores | 10.0 | 3.2 |
| 32-bit stores (2 pixels) | 3.0 | 1.0 |
| 32-bit reads (2 pixels) | 7.4 | 1.8 |
| memset (bursts) | 3.1 | 1.4 |

  Writes that miss the cache don't stall much: a whole screen of 16-bit
  stores costs 0.9 ms, and reading each line first only slows it down. Reads
  that miss are what's slow (a cache line from normal memory costs about
  120 cycles), and the SRAM is about 4 times faster for them.

Super Mario World
-----------------

* Seen: the main screen has BG1 (the level), BG3 (the status bar) and the
  sprites. The sub screen has BG2 (the hills), which shows through the
  backdrop with color math (add, backdrop only).
  Registers: $212C = 0x15, $212D = 0x02, $2130 = 0x02, $2131 = 0x20.
  So a "transparency off" mode would make the hills disappear.
* Seen: this Snes9x 1.43 port draws nothing at all with
  Settings.Transparency off: the non-transparency branch of S9xUpdateScreen
  is empty.
* Seen: each frame draws about 2,950 8x8 tiles (DrawTile16), and the screen
  is drawn in two batches (lines 0-36, the status bar, then 37-223). Drawing
  is the biggest cost.


Gotchas
-------

* The calculator only accepts files ending in .tns over USB ("Invalid
  input" otherwise). Files the program writes for reading on the PC
  (pocketsnes_results.txt.tns, pocketsnes_diag.txt.tns,
  pocketsnes_perf.txt.tns) end in .tns for that reason.
* Interlaced Mode 5 draws up to twice the lines, so the screen buffer has
  272 spare lines below the picture.
* The renderer keeps the distance between its buffers in a 32-bit int.
  Heap buffers on a 64-bit PC overflow it, so the render buffers are static.
* Firebird (the emulator) isn't set up: it needs files dumped from the
  calculator, and its speed wouldn't match the real thing anyway.


Tools
-----

* `make -f Makefile.nspire BENCH=1 BUILD=build/bench TARGET=pocketsnes_speedtest_O3`
  builds a speed test that runs on its own. It plays Super Mario World from
  its newest save state for 5 seconds, writes pocketsnes_diag.txt.tns
  (clock, LCD registers, memory mapping, copy speeds), runs the speed test,
  appends the results to pocketsnes_results.txt.tns, and closes.
* `~/nspire/build/tools/nspire-link`: info, ls, mkdir, put [-f], get, rm,
  shot over USB.
* The installed Ndless can be read from /ndless/ndless_resources.tns: a PRG
  loader followed by a Zehn binary at offset 0x1ec. Code starts after the
  header, relocations, flags and extra data.


Ideas not tried yet
-------------------

* Tried and switched off (SRAM_RENDERING in platform_nspire.cpp): the game
  screen and depth buffer in the on-chip SRAM (saved at start, put back at
  exit), frames copied to the LCD from there, the renderer waiting for the
  copy to pass the lines it draws. The first try wiped the exception vectors
  (above). The second left the first 16 KB alone and still gave a black
  screen and a freeze, in both the game and the speed test. Likely cause
  (found 2026-10-06): it started at 0xA4004000 and wiped the MMU's page
  table, which is there. Not tried again since. The speed test now checks,
  without writing to the SRAM, whether the DMA controller can read it at
  all.
* Seen (user): Yoshi's Island (Super FX) glitches in the top rows of the
  picture, in the old build too, so it comes from the emulator core.
  Fixed 2026-10-13 (core timing, not Super FX): see DEVNOTES.md, "Emulation
  core".

* Done 2026-10-05: only the 256x224 picture is turned; 8-word DMA bursts;
  DrawTile16 in faster C (ARM assembly could still go further).
* Profile-guided optimization (compile, record a run, compile again using
  the recording).
* The 6 ms of game emulation (the 65c816 core) is now close to the 7 ms
  of drawing.

Drawing (2026-10-05)
--------------------

* Seen (PC, gprof, SMW standing in the pipe level): per frame the renderer
  makes about 2,930 tile calls (2,690 for the backgrounds, 240 for sprite
  lines) and 115 clipped ones. The biggest costs were the tile pixel loop
  and the backdrop pass: SMW's backdrop is black with the sub screen (the
  hills) added, so after drawing, every pixel of every line was checked
  again and the hills or the fixed colour (the sky) copied in.
* Seen (ARM assembly, -O3): through WRITE_4PIXELS16 GCC reloaded GFX.Z1 and
  GFX.Z2 after every pixel and GFX.S and GFX.DB every 4 pixels, because the
  code is built with -fno-strict-aliasing and a byte store could change
  them: 14 instructions and two branches per pixel drawn. DrawTile16 and
  DrawClippedTile16 now keep them in local variables and skip transparent
  tile lines in one test: 9 instructions, no branch, per pixel drawn.
* The SMW backdrop case (no colour windows, black backdrop, sub screen added)
  is now done before the main screen is drawn: the sub screen line is filled
  with the fixed colour, drawn, and copied under the main screen. Same
  pixels, no per-pixel pass.
* Tried and dropped: band drawing (8 lines at a time into small buffers that
  fit the data cache, each band turned straight into a portrait buffer).
  Seen (speed test, SMW, 395 MHz, ms per frame, every frame drawn):

| | Run the game | Draw | Show | fps |
|---|---:|---:|---:|---:|
| Before (DMA) | 6.9 | 12.1 | 0.3 | 51.1 |
| New tile code, bands of 8 lines | 6.9 | 12.0 | 0.5 | 51.4 |
| ... bands of 4 / 16 lines | 6.9 | 13.5 / 12.5 | 0.5 | 47.4 / 50.1 |
| New tile code, whole frames, DMA | 7.5 | 9.0 | 0.3 | 58.8 |
| New tile code, whole frames, flip | 6.3 | 9.0 | 2.7 | 55.0 |

  So the tile code and the backdrop shortcut took drawing from 12.1 to 9.0
  ms, and the bands (tiles cut in two at band edges, per-band setup,
  turning) cost more than the cache saved. Whole frames are drawn again;
  only the 256x224 picture and the text over it are turned (in 8-line
  strips, see rotate_block) into the portrait buffer shown next, and the
  whole 320x240 frame only when that buffer is out of date. Not measured on
  the calculator yet (pocketsnes_speedtest_turn.tns).
* When nothing on the main screen has colour maths (SMW), the sub screen is
  now drawn straight into the main screen's buffer: no line copy at all.
* GCC writes 4 consecutive word stores as 4 STRs; the turning code uses one
  STM (inline asm, registers r4-r7) so the 16 bytes go out as one burst.
* All of this draws exactly the same pixels as the original renderer:
  tests/render_compare.sh plays each ROM with the reference build
  (pocketsnes-host-ref, the renderer before these changes) and the current
  one and compares a screenshot every 2 to 4 frames (12 test ROMs and SMW,
  plus Chrono Trigger, Zelda ALttP and Yoshi's Island from the calculator,
  in ~/nspire/build/testroms/calc). A coverage build showed every new path
  is exercised.
* Seen (PC only): the original PC build crashed in Chrono Trigger's Mode 7
  intro (DrawBGMode7Background16Add): GFX.DepthDelta is an unsigned 32-bit
  distance from the main to the sub depth buffer, and the static buffers
  happened to be in the other order, which a 64-bit pointer doesn't wrap.
  On the calculator it wraps and works. The PC build now keeps the buffers
  in order.
* Seen: pocketsnes_results.txt.tns and pocketsnes_diag.txt.tns came back
  garbled now and then (a block repeated, binary bytes at the start, the end
  cut off mid-line), always in files that were appended to ("a") over many
  runs. Both are now written as new files each run.
* Seen (calculator, pocketsnes_new.tns's menu speed test, SMW, cpu 432 MHz):
  normal play 59.9 fps (game 5.5, draw 6.6, show 1.7 ms); no speed limit
  71.6 fps (DMA output 73.1, whole-frame flip 67.7); nothing drawn 174.2.
  The turn speed test at 395 MHz: 65.7 / 67.1 / 62.1.
* Seen (USB, nspire-link): files of exactly 479 bytes wouldn't transfer in
  either direction. 479 bytes plus the CX II protocol's headers is one
  512-byte USB packet: the calculator doesn't end such a message with an
  empty packet, so libnspire's read ran on into the next one (and checked
  the checksum over the wrong length), and libnspire didn't send one after
  its own 512-byte messages either. Fixed in
  ~/nspire/build/tools/libnspire/src/cx2.cpp (readPacket, writePacket; the
  original is cx2.cpp.orig). nspire-link also got "cp REMOTE NEWREMOTE".

Where things stand (2026-10-05)
-------------------------------

* Seen (speed test, SMW pipe level, 396 MHz, ms per frame): game 6.0,
  drawing 7.2, showing 1.8 (turned picture); no speed limit 65.7 fps (DMA
  output 67.1, whole-frame flip 62.1); normal play holds 60. Before the
  drawing work: 51.1 fps, drawing 12.1 ms.
* Explained later the same day: the calculator's measured clock went
  395 -> 432 -> 475 MHz across runs (and 376 / 434 MHz in Yoshi's Island
  runs) without anyone overclocking it, with the frame rates rising by the
  same amount. None of those is 12 MHz times a whole number. The cause was
  the timer's rate, not the clock: it was "calibrated" against the
  real-time clock while the program ran, by watching for second boundaries
  at each key poll. Loading a ROM or a state holds up polling for a fraction
  of a second; when that happened next to a boundary, the measured rate came
  out 5-20% off and was adopted (anything over 2% off was). Everything timed
  with it scaled: the measured MHz, the speed test's fps and ms, the FPS
  counter, and the frame pacing itself, so those games ran 5-20% slow while
  the counter read 60/60. The rate is now fixed at 32768 Hz (the speed test
  build measures it against the real-time clock with nothing else running,
  for the report). One run with USB plugged in read 287 MHz (clock register
  x24), which is real: the OS lowers the clock on USB.
* CPU speed setting (432/456/480 MHz): it didn't work (writing the
  register isn't enough, see "Switching the clock"). Since 2026-10-06 it
  switches the OS's way, which tools/clocktest showed working on the
  calculator.
* NoverII also has a bus divider (0x90140020 bits 20-23) and a bit in
  0x90140810 (bit 4, set when that divider isn't 0); PocketSNES doesn't
  touch them.
* nspire-link (libnspire, CX II): the calculator streams messages back to
  back without ending a 512-byte one with an empty USB packet. Reading one
  USB packet first and then exactly the rest of a message overflows
  (LIBUSB_ERROR_OVERFLOW) on every message over 512 bytes, so the read
  keeps libnspire's single long read; the fix is to check the checksum
  over the message's own length and drop anything read past it. Writes add
  the empty packet after a message that fills whole USB packets. Checked
  with files of 478 to 40,000 bytes both ways.

Yoshi's Island (2026-10-14)
---------------------------

* The owner's speed test of Yoshi's Island (build eebf818, 396 MHz, DMA
  output, their save in the first world, every frame drawn): 53.0 fps;
  a frame took 10.3 ms running the game (cpu), 7.9 ms drawing (gfx) and
  0.5 ms getting it on screen (out). `tests/calc_speed.sh` with the same
  save said 53.3 fps, but 9.2 ms drawing and 9.0 ms for the rest.
* So per ARM instruction this game's drawing is cheaper than Super Mario
  World's (about 2.3 cycles instead of the fitted 2.64) and its Super FX
  interpreter dearer (about 3.2 cycles instead of 2.58; probably the
  indirect jump per GSU instruction, and plotting all over the GSU's 128 KB
  of RAM). Fitted with the CPU core and PPU parts at 2.58, as for SMW.
* The busiest code then, in ARM instructions a frame: the tile drawers
  963K, the Super FX 791K (632K after 82d4db8), the PPU, DMA and timing
  283K (VRAM DMA through the register functions 118K of it), the 65c816
  core 203K, `DrawBackground` 134K, the per-line buffer clears 149K.
* Yoshi's Island blanks its own top 8 and bottom 8 lines during play (the
  screen is off until its line-8 IRQ and again from line 216), so it shows
  208 lines of its 224; centred on the 240-line screen, that's 16 black
  rows above and below instead of SMW's 8. A real SNES does the same; a
  TV's overscan hides it.

Yoshi's Island, estimated (2026-10-16)
---------------------------------------

From `tests/calc_speed.sh` with the owner's saves (`build/calc-saves/yi/`),
the DMA output (`SHOW_MS=0.5`), 300 frames each, no keys after the start,
build 3129554. Not measured on the calculator yet.

| Save | Spot | ARM instructions a frame | Estimated fps (no speed limit) | 95% of frames at least |
|---|---|---:|---:|---:|
| sv010 | the busy spot measured on 2026-10-14 | 1,474,286 | 92.1 | 89.9 |
| sv005 | | 1,474,574 | 91.7 | 89.1 |
| sv007, sv008, sv009 | | 1,448,000–1,449,000 | 93.5–93.6 | 91.3–91.7 |
| sv006 | | 1,131,998 | 116.3 | 112.5 |
| sv001 | | 1,194,835 | 111.0 | 108.1 |
| sv011 | bubbles; one frame of 4.8 million | 1,020,103 | 127.2 | 81.2 |

* sv010's frame by part: drawing 806K instructions (5.37 ms), the Super FX
  277K (1.80 ms), the 65c816 core 203K (1.32 ms), PPU, DMA and timing 179K
  (1.17 ms). The busiest functions: the background rows' drawing pass 449K,
  the translated Super FX code 259K, the rows' listing pass 62K,
  `DrawOBJS` 52K, `S9xDoHDMA` 43K, `DrawBackground` 42K,
  `S9xDoHBlankProcessing_SFX` 40K, `S9xSetPPU` 37K, the ARM core's
  `mainLoop` 35K and `doHBlank` 34K, the clipped tile drawer 32K, the line
  fills 26K.
* What it draws (sv010, counted in the background rows over 300 frames):
  84 rows a frame, 1271 tiles drawn without the depth test and 93 with it;
  of the lines drawn without it, 89% have no transparent pixel, 2% are
  empty and 9% partly transparent. The rows start at an even pixel 37% of
  the time, a multiple of 4 20% (the depths' alignment for wider stores).
* Sprites (sv010): 244 sprite lines a frame, 465 calls of `DrawTile16` for
  8 pixels each; with `DrawOBJS`'s setup (121 instructions per sprite and
  line, 35 per 8 pixels) and the line drawer, about 100K instructions a
  frame for some 3700 pixels.
* The 65c816 waits for the next frame from line 31 to 216 in a loop at
  00:80f1 (`lda` of a RAM flag, `bmi` back). The idle loop detection skips
  to the next event, but each of the two events a line (H-blank start and
  the line's end) adds one to `WaitCounter`, so the loop runs twice before
  skipping again: 746 times in the frame traced (frame 10 from sv010).
* Yoshi's Island's per-frame DMA: 544 bytes to OAM, 512 to CGRAM and
  3584 to VRAM (the Super FX's picture). H-DMA: two channels on every line,
  one of them the fixed colour (the sky's bands); 630 writes of $2132 a
  frame.
* It turns the brightness down and back up in V-blank every frame; each
  change recomputed the 256 colours (about 5K instructions).
