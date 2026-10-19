/* Runs a game for some frames on one CPU core and prints a line per frame
 * with checksums of the machine's state and of the picture; tests/asm/run.sh
 * runs it once with the C core and once with the assembler core and
 * compares the lines.
 *
 *   game_test ROM FRAMES c|asm [STATE] [PNG_PREFIX] [INPUT]
 * STATE is a PocketSNES state file (.svNNN.tns) to start from ("-" for
 * none). PNG_PREFIX names pictures of the last frame and of every 60th
 * frame ("-" for none). INPUT picks the scripted controller: "none",
 * "start" (Start now and then), "walk" (right, jumps and runs) or "busy"
 * (all sorts of buttons); with "-norender" after it nothing is drawn and
 * nothing is printed (to time the CPU cores).
 * Built as game_trace (GAME_TRACE), two more arguments, FROM and TO, print
 * every opcode of those frames.
 * Runs on ARM (under qemu-arm on a PC). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "snes9x.h"
#include "memmap.h"
#include "ppu.h"
#include "gfx.h"
#include "cpuexec.h"
#include "snapshot.h"
#include "display.h"
#include "cpuops.h"
#include "apu.h"

#define SCREEN_W 320
#define SCREEN_H 240
#define PITCH (SCREEN_W * 2)
#define RENDER_LINES 512
#define GUARD_LINES 16
#define PICTURE_LEFT ((SCREEN_W - SNES_WIDTH) / 2)

static uint8 *screen_buffer, *sub_screen, *zbuffer, *sub_zbuffer;
static uint8 *screen_start;
static int frame_number;
static const char *input_kind = "none";
/* INPUT ending in "-replay": after the frames, the overclock test's game
 * check (emu.cpp): a snapshot, then the same frames twice from it with no
 * keys, which must leave the same memory after every frame. */
static bool replay;
static bool no_render;   /* INPUT ends in "-norender": nothing drawn (for timing) */

extern "C"
{
bool8 S9xInitUpdate ()
{
    GFX.Screen = screen_start;
    GFX.RenderRows = RENDER_LINES;
    return TRUE;
}

uint32 S9xReadJoypad (int which)
{
    if (which != 0)
	return 0;
    uint32 keys = 0x80000000;   /* "connected" */
    int f = frame_number;
    if (strcmp (input_kind, "start") == 0)
    {
	if (f % 120 >= 100 && f % 120 < 106)
	    keys |= SNES_START_MASK;
	if (f % 90 >= 40 && f % 90 < 44)
	    keys |= SNES_A_MASK;
    }
    else if (strcmp (input_kind, "walk") == 0)
    {
	keys |= SNES_RIGHT_MASK;
	if (f % 50 < 18)
	    keys |= SNES_B_MASK;
	if (f % 200 >= 120)
	    keys |= SNES_Y_MASK;
	if (f % 300 >= 280)
	    keys = (keys & ~SNES_RIGHT_MASK) | SNES_LEFT_MASK;
    }
    else if (strcmp (input_kind, "busy") == 0)
    {
	static const uint32 pattern [] = {
	    SNES_START_MASK, 0, SNES_A_MASK, SNES_DOWN_MASK, SNES_RIGHT_MASK | SNES_B_MASK,
	    SNES_UP_MASK, SNES_X_MASK, SNES_LEFT_MASK | SNES_Y_MASK, SNES_TL_MASK, SNES_TR_MASK,
	    SNES_A_MASK, 0, SNES_SELECT_MASK, SNES_B_MASK, SNES_DOWN_MASK | SNES_A_MASK,
	};
	keys |= pattern [(f / 7) % (sizeof (pattern) / sizeof (pattern [0]))];
    }
    return keys;
}
}

#ifdef GAME_TRACE
/* game_trace: a line for every opcode of frames trace_from to trace_to, at
 * the same point on both cores (after the interrupt checks, before the
 * opcode is fetched), so the two traces can be compared. */
static int trace_from, trace_to, trace_raw;

static void trace_line (uint8 pb, uint16 pc, uint8 op, uint16 a, uint16 x, uint16 y, uint16 s,
			uint16 d, uint8 db, uint16 p, long cycles, bool wait_address, bool branch_skip)
{
    if (frame_number < trace_from || frame_number > trace_to)
	return;
    printf ("T %d %02x:%04x op %02x a %04x x %04x y %04x s %04x d %04x db %02x p %03x c %ld v %ld f %x"
	    " wa %d wc %u bs %d\n", frame_number, pb, pc, op, a, x, y, s, d, db, p, cycles,
	    (long) CPU.V_Counter, (unsigned) CPU.Flags, wait_address, CPU.WaitCounter, branch_skip);
}

/* Called by the assembler core (built with ASM_TRACE) before each opcode,
 * with its ARM registers r0-r12 and r14 as they are (see the register list
 * at the start of os9x_65c816_common.s). */
extern "C" void asm_trace (const uint32 *regs)
{
    uint32 status = regs [4], reg_a = regs [5], reg_d = regs [6], reg_x = regs [7];
    uint32 reg_s = regs [8], reg_y = regs [9], cycles = regs [11];
    const uint8 *pc = (const uint8 *) regs [10], *pc_base = (const uint8 *) regs [12];
    uint16 p = (status >> 24) | ((status & (1 << 23)) ? 0x100 : 0);
    uint16 a = (p & MemoryFlag) ? (reg_a >> 24) | (CPU.AsmRegisters.A & 0xff00) : reg_a >> 16;
    uint16 x = (p & IndexFlag) ? reg_x >> 24 : reg_x >> 16;
    uint16 y = (p & IndexFlag) ? reg_y >> 24 : reg_y >> 16;
    trace_line (reg_d & 0xff, pc - pc_base, *pc, a, x, y, reg_s, reg_d >> 16, status & 0xff, p,
		(long) (int32) cycles, (status & (1 << 21)) != 0, (status & (1 << 22)) != 0);
    if (trace_raw && frame_number >= trace_from && frame_number <= trace_to)
	printf ("R status %08x a %08x d %08x x %08x s %08x y %08x mem.bs %d mem.wa %p\n", status, reg_a,
		reg_d, reg_x, reg_s, reg_y, CPU.BranchSkip, (void *) CPU.WaitAddress);
}

/* One frame of cpuexec.cpp's S9xMainLoop_NoSA1_SFX / _NoSFX, with the
 * trace line. */
static void c_traced_frame (void)
{
    for (;;)
    {
	if (CPU.Flags)
	{
	    if (CPU.Flags & NMI_FLAG)
	    {
		if (--CPU.NMICycleCount == 0)
		{
		    CPU.Flags &= ~NMI_FLAG;
		    if (CPU.WaitingForInterrupt)
		    {
			CPU.WaitingForInterrupt = FALSE;
			CPU.PC++;
		    }
		    S9xOpcode_NMI ();
		}
	    }
	    if (CPU.Flags & IRQ_PENDING_FLAG)
	    {
		if (CPU.IRQCycleCount == 0)
		{
		    if (CPU.WaitingForInterrupt)
		    {
			CPU.WaitingForInterrupt = FALSE;
			CPU.PC++;
		    }
		    if (CPU.IRQActive && !Settings.DisableIRQ)
		    {
			if (!CheckFlag (IRQ))
			    S9xOpcode_IRQ ();
		    }
		    else
			CPU.Flags &= ~IRQ_PENDING_FLAG;
		}
		else
		{
		    if (--CPU.IRQCycleCount == 0 && CheckFlag (IRQ))
			CPU.IRQCycleCount = 1;
		}
	    }
	    if (CPU.Flags & SCAN_KEYS_FLAG)
		break;
	}

	S9xPackStatus ();
	const struct SRegisters &r = ICPU.Registers;
	trace_line (r.PB, CPU.PC - CPU.PCBase, *CPU.PC, r.A.W, r.X.W, r.Y.W, r.S.W, r.D.W, r.DB, r.P.W,
		    CPU.Cycles, CPU.WaitAddress != NULL, CPU.BranchSkip);

	CPU.PCAtOpcodeStart = CPU.PC;
	CPU.Cycles += CPU.MemSpeed;
	(*ICPU.S9xOpcodes [*CPU.PC++].S9xOpcode) ();
	if (Settings.SuperFX)
	{
	    DO_HBLANK_CHECK_SFX ();
	}
	else
	{
	    DO_HBLANK_CHECK_NoSFX ();
	}
    }

    ICPU.Registers.PC = CPU.PC - CPU.PCBase;
    S9xPackStatus ();
    IAPU.Registers.PC = IAPU.PC - IAPU.RAM;
    S9xAPUPackStatus ();
    if (CPU.Flags & SCAN_KEYS_FLAG)
    {
	S9xSyncSpeed ();
	CPU.Flags &= ~SCAN_KEYS_FLAG;
    }
}
#endif

/* A PNG of the picture: 8-bit RGB, rows filtered with "none". */
static void write_png (const char *path, const uint16 *pixels, int width, int height, int pitch)
{
    FILE *f = fopen (path, "wb");
    if (!f)
	return;
    uLong raw_size = (uLong) (width * 3 + 1) * height;
    uint8 *raw = (uint8 *) malloc (raw_size);
    uint8 *p = raw;
    for (int y = 0; y < height; y++)
    {
	*p++ = 0;
	for (int x = 0; x < width; x++)
	{
	    uint16 c = pixels [y * pitch + x];
	    *p++ = ((c >> 11) & 31) * 255 / 31;
	    *p++ = ((c >> 5) & 63) * 255 / 63;
	    *p++ = (c & 31) * 255 / 31;
	}
    }
    uLongf packed_size = compressBound (raw_size);
    uint8 *packed = (uint8 *) malloc (packed_size);
    compress (packed, &packed_size, raw, raw_size);

    static const uint8 signature [8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
    fwrite (signature, 1, 8, f);
    struct { const char *type; const uint8 *data; uint32 size; } chunks [3];
    uint8 ihdr [13] = { 0, 0, (uint8) (width >> 8), (uint8) width, 0, 0, (uint8) (height >> 8),
			(uint8) height, 8, 2, 0, 0, 0 };
    chunks [0].type = "IHDR"; chunks [0].data = ihdr; chunks [0].size = 13;
    chunks [1].type = "IDAT"; chunks [1].data = packed; chunks [1].size = packed_size;
    chunks [2].type = "IEND"; chunks [2].data = NULL; chunks [2].size = 0;
    for (int i = 0; i < 3; i++)
    {
	uint8 head [8] = { (uint8) (chunks [i].size >> 24), (uint8) (chunks [i].size >> 16),
			   (uint8) (chunks [i].size >> 8), (uint8) chunks [i].size };
	memcpy (head + 4, chunks [i].type, 4);
	fwrite (head, 1, 8, f);
	if (chunks [i].size)
	    fwrite (chunks [i].data, 1, chunks [i].size, f);
	uLong crc = crc32 (0, head + 4, 4);
	if (chunks [i].size)
	    crc = crc32 (crc, chunks [i].data, chunks [i].size);
	uint8 tail [4] = { (uint8) (crc >> 24), (uint8) (crc >> 16), (uint8) (crc >> 8), (uint8) crc };
	fwrite (tail, 1, 4, f);
    }
    fclose (f);
    free (raw);
    free (packed);
}

/* A PocketSNES state file: a header, then the zlib-packed snapshot. */
static bool load_state (const char *path)
{
    struct
    {
	char magic [4];
	uint32 version, counter, raw_size, packed_size, rom_crc, reserved [2];
    } header;
    FILE *f = fopen (path, "rb");
    if (!f)
	return false;
    bool ok = false;
    if (fread (&header, sizeof (header), 1, f) == 1 && memcmp (header.magic, "PSST", 4) == 0)
    {
	uint8 *packed = (uint8 *) malloc (header.packed_size);
	uint8 *raw = (uint8 *) malloc (header.raw_size);
	uLongf raw_size = header.raw_size;
	ok = packed && raw && fread (packed, 1, header.packed_size, f) == header.packed_size &&
	     uncompress (raw, &raw_size, packed, header.packed_size) == Z_OK &&
	     S9xUnfreezeFromMemory (raw, raw_size);
	free (packed);
	free (raw);
    }
    fclose (f);
    return ok;
}

static void init_settings (void)
{
    memset (&Settings, 0, sizeof (Settings));
    Settings.CyclesPercentage = 100;
    Settings.DisableSoundEcho = TRUE;
    Settings.H_Max = SNES_CYCLES_PER_SCANLINE;
    Settings.SkipFrames = AUTO_FRAMERATE;
    Settings.Shutdown = Settings.ShutdownMaster = TRUE;
    Settings.FrameTimePAL = 20000;
    Settings.FrameTimeNTSC = 16667;
    Settings.FrameTime = Settings.FrameTimeNTSC;
    Settings.DisableMasterVolume = TRUE;
    Settings.InterpolatedSound = TRUE;
    Settings.StarfoxHack = TRUE;
    Settings.Transparency = TRUE;
    Settings.AutoSaveDelay = 1;
    Settings.TurboSkipFrames = 15;
    Settings.SoundSync = 1;
    Settings.SuperFX = TRUE;
    Settings.DSP1Master = TRUE;
    Settings.SA1 = TRUE;
    Settings.C4 = TRUE;
    Settings.SDD1 = TRUE;
    Settings.HBlankStart = (256 * Settings.H_Max) / SNES_HCOUNTER_MAX;
}

int main (int argc, char **argv)
{
    if (argc < 4)
    {
	printf ("usage: game_test ROM FRAMES c|asm [STATE] [PNG_PREFIX] [INPUT]\n");
	return 2;
    }
    const char *rom = argv [1];
    int frames = atoi (argv [2]);
    bool asm_core = strcmp (argv [3], "asm") == 0;
    const char *state = argc > 4 && strcmp (argv [4], "-") != 0 ? argv [4] : NULL;
    const char *png_prefix = argc > 5 && strcmp (argv [5], "-") != 0 ? argv [5] : NULL;
    if (argc > 6)
    {
	static char kind [32];
	snprintf (kind, sizeof (kind), "%s", argv [6]);
	char *suffix = strstr (kind, "-norender");
	if (suffix)
	{
	    *suffix = 0;
	    no_render = true;
	}
	suffix = strstr (kind, "-replay");
	if (suffix)
	{
	    *suffix = 0;
	    replay = true;
	}
	input_kind = kind;
    }
#ifdef GAME_TRACE
    trace_from = argc > 7 ? atoi (argv [7]) : 1;
    trace_to = argc > 8 ? atoi (argv [8]) : trace_from;
    trace_raw = argc > 9 && strcmp (argv [9], "raw") == 0;
#endif
    setvbuf (stdout, NULL, _IOLBF, 0);

    init_settings ();
    size_t screen_bytes = PITCH * (RENDER_LINES + 2 * GUARD_LINES);
    screen_buffer = (uint8 *) calloc (1, screen_bytes);
    sub_screen = (uint8 *) calloc (1, screen_bytes);
    zbuffer = (uint8 *) calloc (1, screen_bytes / 2);
    sub_zbuffer = (uint8 *) calloc (1, screen_bytes / 2);
    screen_start = screen_buffer + PITCH * GUARD_LINES + PICTURE_LEFT * 2;
    GFX.RealPitch = GFX.Pitch = PITCH;
    GFX.Screen = screen_start;
    GFX.SubScreen = sub_screen + PITCH * GUARD_LINES;
    GFX.ZBuffer = zbuffer + PITCH / 2 * GUARD_LINES;
    GFX.SubZBuffer = sub_zbuffer + PITCH / 2 * GUARD_LINES;
    GFX.Delta = (GFX.SubScreen - GFX.Screen) >> 1;
    GFX.PPL = GFX.Pitch >> 1;
    GFX.PPLx2 = GFX.Pitch;
    GFX.ZPitch = GFX.Pitch >> 1;
    if (!Memory.Init () || !S9xGraphicsInit ())
    {
	printf ("init failed\n");
	return 1;
    }
    if (!Memory.LoadROM (rom))
    {
	printf ("can't load %s\n", rom);
	return 1;
    }
    S9xReset ();
    if (state && !load_state (state))
    {
	printf ("can't load the state %s\n", state);
	return 1;
    }
    srand (1);
    Settings.AsmCPU = asm_core;
    printf ("%s: %s, SuperFX %d, SA-1 %d, %s core%s\n", rom, Memory.ROMName, Settings.SuperFX,
	    Settings.SA1, asm_core ? "assembler" : "C",
	    asm_core && Settings.SA1 ? " (the C core runs SA-1 games)" : "");

    for (frame_number = 1; frame_number <= frames; frame_number++)
    {
	IPPU.RenderThisFrame = !no_render;
#ifdef GAME_TRACE
	if (!asm_core && !Settings.SA1)
	    c_traced_frame ();
	else
#endif
	S9xMainLoop ();

	if (no_render)
	    continue;   /* timing: no checksums either */
	uint32 ram = crc32 (0, Memory.RAM, 0x20000);
	uint32 vram = crc32 (0, Memory.VRAM, 0x10000);
	uint32 sram = crc32 (0, Memory.SRAM, 0x20000);
	uint32 ppu = crc32 (crc32 (0, (const Bytef *) PPU.CGDATA, sizeof (PPU.CGDATA)),
			    PPU.OAMData, sizeof (PPU.OAMData));
	uint32 fill = crc32 (0, Memory.FillRAM, 0x8000);
	uint32 picture = 0;
	for (int y = 0; y < SNES_HEIGHT_EXTENDED; y++)
	    picture = crc32 (picture, screen_start + y * PITCH, SNES_WIDTH * 2);
	const struct SRegisters &r = ICPU.Registers;
	printf ("frame %d ram %08x vram %08x sram %08x ppu %08x io %08x picture %08x"
		" pc %02x:%04x a %04x x %04x y %04x s %04x d %04x db %02x p %03x cycles %ld vc %ld\n",
		frame_number, ram, vram, sram, ppu, fill, picture, r.PB, r.PC, r.A.W, r.X.W, r.Y.W,
		r.S.W, r.D.W, r.DB, r.P.W, (long) CPU.Cycles, (long) CPU.V_Counter);

	if (png_prefix && (frame_number % 60 == 0 || frame_number == frames))
	{
	    char path [600];
	    snprintf (path, sizeof (path), "%s%04d.png", png_prefix, frame_number);
	    write_png (path, (const uint16 *) screen_start, SNES_WIDTH, PPU.ScreenHeight, SCREEN_W);
	}
    }

    if (replay)
    {
	static uint32 prints [2][360];
	uint8 *snapshot;
	uint32 snapshot_size;
	input_kind = "none";
	if (!S9xFreezeToMemory (&snapshot, &snapshot_size))
	    return 1;
	/* What a snapshot leaves out, as emu.cpp keeps it. */
	struct SCPUState cpu = CPU;
	struct SICPU icpu = ICPU;
	struct SIAPU iapu = IAPU;
	uint8 open_bus = OpenBus;
	int first_different = -1;
	for (int pass = 0; pass < 2; pass++)
	{
	    S9xUnfreezeFromMemory (snapshot, snapshot_size);
	    CPU = cpu;
	    ICPU = icpu;
	    IAPU = iapu;
	    OpenBus = open_bus;
	    srand (1);
	    for (int i = 0; i < 360; i++)
	    {
		IPPU.RenderThisFrame = TRUE;
		S9xMainLoop ();
		uint32 h = crc32 (0, Memory.RAM, 0x20000);
		h = crc32 (h, Memory.VRAM, 0x10000);
		h = crc32 (h, (const Bytef *) PPU.CGDATA, sizeof (PPU.CGDATA));
		prints [pass][i] = crc32 (h, PPU.OAMData, sizeof (PPU.OAMData));
		if (pass == 1 && first_different < 0 && prints [1][i] != prints [0][i])
		    first_different = i + 1;
	    }
	}
	if (first_different < 0)
	    printf ("replay: the same for 360 frames\n");
	else
	    printf ("replay: different from frame %d\n", first_different);
    }
    return 0;
}
