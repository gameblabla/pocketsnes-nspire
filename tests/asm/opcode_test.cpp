/* The assembler CPU core against the C core, one opcode at a time.
 *
 * For every opcode, in emulation mode and in each of the four M/X modes,
 * random register values and random memory are set up; the opcode is run
 * once on the C core (cpuops.cpp) and once, from the same state, on the
 * assembler core (built with ASM_SINGLE_STEP, entry asmStepOpcode); then the
 * registers, cycle count, CPU fields and memory are compared.
 *
 * The memory map is made up: every bank is RAM or ROM from a 128 KB buffer
 * (even and odd banks differ), with some 4 KB blocks of LoROM SRAM and some
 * unmapped, so the assembler core's calls into the C core are exercised too.
 *
 *   opcode_test [cases per opcode and mode] [seed] [opcode in hex, or all] [options]
 * Options: "openbus" also compares the open bus byte; "mapped" leaves out the
 * unmapped blocks; "verbose" names each case; "print=N" prints N differences
 * per opcode (6).
 * Prints each difference (up to a limit per opcode) and a summary; returns 1
 * if anything differed. Runs on ARM (under qemu-arm on a PC, tests/asm/run.sh). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snes9x.h"
#include "memmap.h"
#include "ppu.h"
#include "cpuexec.h"
#include "cpuops.h"
#include "sa1.h"
#include "getset.h"

extern "C" void asmStepOpcode (struct SCPUState *cpu);
/* Only the single-step build of the assembler core is linked here. */
extern "C" void asmMainLoop (struct SCPUState *) { abort (); }
extern "C" bool8 S9xInitUpdate () { return TRUE; }
extern "C" uint32 S9xReadJoypad (int) { return 0; }

extern "C" uint8 OpenBus;

static uint32 rng = 1;
static uint32 rnd (void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

#define BANK_BYTES 0x10000
#define BUFFER_BYTES (2 * BANK_BYTES)
/* Room after the buffer: an instruction at the end of bank 1 reads on. */
static uint8 *buffer;
static uint8 *before_buffer, *c_buffer;
static const uint32 sram_mask = 0x7ff;
static uint8 before_sram [0x800], c_sram [0x800];

enum BlockKind { RAM_BLOCK, ROM_BLOCK, SRAM_BLOCK, NONE_BLOCK };
static uint8 block_kind [MEMMAP_NUM_BLOCKS];
static int no_unmapped;   /* option "mapped": no unmapped blocks (no open bus reads) */

static void make_memory_map (void)
{
    static const uint8 speeds [3] = { ONE_CYCLE, SLOW_ONE_CYCLE, TWO_CYCLES };
    for (int b = 0; b < MEMMAP_NUM_BLOCKS; b++)
    {
	int bank = b >> 4, sub = b & 15;
	uint8 *base = buffer + (bank & 1) * BANK_BYTES;
	int kind;
	/* Bank 0 holds the stack, direct page and vectors: keep it plain. */
	if (bank == 0)
	    kind = sub >= 12 ? ROM_BLOCK : RAM_BLOCK;
	else if (sub == 6)
	    kind = SRAM_BLOCK;
	else if (sub == 7)
	    kind = no_unmapped ? RAM_BLOCK : NONE_BLOCK;
	else
	    kind = sub >= 12 ? ROM_BLOCK : RAM_BLOCK;
	block_kind [b] = kind;
	switch (kind)
	{
	case RAM_BLOCK:
	    Memory.Map [b] = Memory.WriteMap [b] = base;
	    break;
	case ROM_BLOCK:
	    Memory.Map [b] = base;
	    Memory.WriteMap [b] = (uint8 *) CMemory::MAP_NONE;
	    break;
	case SRAM_BLOCK:
	    Memory.Map [b] = Memory.WriteMap [b] = (uint8 *) CMemory::MAP_LOROM_SRAM;
	    break;
	default:
	    Memory.Map [b] = Memory.WriteMap [b] = (uint8 *) CMemory::MAP_NONE;
	    break;
	}
	Memory.BlockIsRAM [b] = kind == RAM_BLOCK || kind == SRAM_BLOCK;
	Memory.BlockIsROM [b] = kind == ROM_BLOCK;
	Memory.MemorySpeed [b] = speeds [(bank * 7 + sub / 4) % 3];
    }
    Memory.SRAMMask = sram_mask;
}

/* The state both cores start from. */
struct Start
{
    struct SCPUState cpu;
    struct SRegisters regs;
    uint8 open_bus;
};

struct Result
{
    struct SRegisters regs;
    uint32 pc_offset;     /* CPU.PC - CPU.PCBase */
    uint8 *pc, *pc_base;
    long cycles, mem_speed, mem_speed_x2;
    uint8 *wait_address;
    uint32 wait_counter, flags;
    bool8 waiting_for_interrupt, branch_skip, brk_triggered, sram_modified;
    uint8 open_bus;
    uint32 format;   /* from the assembler core: 1 A, 2 X, 4 Y, 8 D, 16 S, 32 and 64 status */
};

static void set_up (struct Start *s, int mode, int opcode)
{
    memset (&CPU, 0, sizeof (CPU));
    memset (&ICPU.Registers, 0, sizeof (ICPU.Registers));

    struct SRegisters &r = ICPU.Registers;
    r.PB = rnd () & 0xff;
    r.DB = rnd () & 0xff;
    r.A.W = rnd ();
    r.D.W = (rnd () & 3) ? rnd () : (rnd () & 0xff00);
    r.X.W = rnd ();
    r.Y.W = rnd ();
    r.S.W = rnd ();
    r.P.W = rnd () & 0xff;
    /* mode 0: emulation; 1-4: M1X1, M1X0, M0X1, M0X0 */
    if (mode == 0)
    {
	r.P.W |= Emulation | MemoryFlag | IndexFlag;
	r.SH = 1;
    }
    else
    {
	r.P.W &= ~(MemoryFlag | IndexFlag);
	if (mode == 1 || mode == 2)
	    r.P.W |= MemoryFlag;
	if (mode == 1 || mode == 3)
	    r.P.W |= IndexFlag;
    }
    if (r.P.W & IndexFlag)
    {
	r.XH = 0;
	r.YH = 0;
    }

    /* The instruction, somewhere in RAM or ROM (not in the SRAM or unmapped
     * blocks: the C core runs those from Memory.SRAM). */
    uint32 offset;
    do
	offset = rnd () & 0xffff;
    while (block_kind [(((uint32) r.PB << 16) | offset) >> 12] >= SRAM_BLOCK);
    r.PC = offset;
    uint8 *at = buffer + (r.PB & 1) * BANK_BYTES + offset;
    at [0] = opcode;

    /* Some operands pointing at block ends, where words cross blocks. */
    if ((rnd () & 7) == 0)
	at [1] = 0xff, at [2] = (rnd () & 0xf0) | 0x0f;

    S9xUnpackStatus ();
    S9xSetPCBase (((uint32) r.PB << 16) + r.PC);   /* CPU.PC, PCBase, MemSpeed */
    CPU.MemSpeedx2 = CPU.MemSpeed << 1;
    CPU.Cycles = rnd () % 1000;
    CPU.NextEvent = CPU.Cycles + 100 + rnd () % 1000;
    CPU.V_Counter = rnd () % 262;
    CPU.WaitCounter = rnd () % 4;
    switch (rnd () % 4)
    {
    case 0: CPU.WaitAddress = NULL; break;
    case 1: CPU.WaitAddress = CPU.PC; break;
    /* likely branch targets: the opcode itself (a loop on one instruction) */
    default: CPU.WaitAddress = CPU.PC; break;
    }
    CPU.Flags = 0;
    switch (rnd () % 8)
    {
    case 0: CPU.Flags = IRQ_PENDING_FLAG; break;
    case 1: CPU.Flags = NMI_FLAG; break;
    }
    CPU.BranchSkip = (rnd () & 3) == 0;
    CPU.IRQActive = rnd () & 1;
    CPU.FastROMSpeed = SLOW_ONE_CYCLE;
    OpenBus = rnd ();

    s->cpu = CPU;
    s->regs = ICPU.Registers;
    s->open_bus = OpenBus;
}

static void restore (const struct Start *s)
{
    CPU = s->cpu;
    ICPU.Registers = s->regs;
    OpenBus = s->open_bus;
    S9xUnpackStatus ();
    ICPU.ShiftedPB = ICPU.Registers.PB << 16;
    ICPU.ShiftedDB = ICPU.Registers.DB << 16;
    S9xFixCycles ();
    memcpy (buffer, before_buffer, BUFFER_BYTES);
    memcpy (Memory.SRAM, before_sram, sizeof (before_sram));
}

static void record (struct Result *res)
{
    res->regs = ICPU.Registers;
    res->pc_offset = CPU.PC - CPU.PCBase;
    res->pc = CPU.PC;
    res->pc_base = CPU.PCBase;
    res->cycles = CPU.Cycles;
    res->mem_speed = CPU.MemSpeed;
    res->mem_speed_x2 = CPU.MemSpeedx2;
    res->wait_address = CPU.WaitAddress;
    res->wait_counter = CPU.WaitCounter;
    res->flags = CPU.Flags;
    res->waiting_for_interrupt = CPU.WaitingForInterrupt;
    res->branch_skip = CPU.BranchSkip;
    res->brk_triggered = CPU.BRKTriggered;
    res->sram_modified = CPU.SRAMModified;
    res->open_bus = OpenBus;
    res->format = 0;
}

static void run_c (void)
{
    CPU.PCAtOpcodeStart = CPU.PC;
    CPU.Cycles += CPU.MemSpeed;
    (*ICPU.S9xOpcodes [*CPU.PC++].S9xOpcode) ();
    S9xPackStatus ();
    ICPU.Registers.PC = CPU.PC - CPU.PCBase;
}

static void run_asm (void)
{
    S9xAsmRegistersIn ();
    asmStepOpcode (&CPU);
    S9xAsmRegistersOut ();
}

static uint32 asm_format (void)
{
    return (uint32) CPU.AsmStatus;
}

static const char *mode_name [5] = { "E1", "M1X1", "M1X0", "M0X1", "M0X0" };

/* How often each field differed, for the summary line of an opcode. */
#define MAX_FIELDS 32
static const char *field_names [MAX_FIELDS];
static int field_counts [MAX_FIELDS], mode_counts [5];

static void count_field (const char *name)
{
    for (int i = 0; i < MAX_FIELDS; i++)
    {
	if (!field_names [i])
	    field_names [i] = name;
	if (strcmp (field_names [i], name) == 0)
	{
	    field_counts [i]++;
	    return;
	}
    }
}
static int compare_open_bus, verbose, max_print = 6;

/* Prints the differences; returns how many fields differed. */
static int compare (const struct Start *s, const struct Result *c, const struct Result *a,
		    int opcode, int mode, bool print)
{
    char text [2048];
    int len = 0, diffs = 0;
#define DIFF(name, cv, av, fmt) \
    if ((cv) != (av)) \
    { \
	diffs++; \
	count_field (name); \
	len += snprintf (text + len, sizeof (text) - len, " " name " C=" fmt " asm=" fmt, cv, av); \
    }
    DIFF ("PB", c->regs.PB, a->regs.PB, "%02x");
    DIFF ("DB", c->regs.DB, a->regs.DB, "%02x");
    DIFF ("P", c->regs.P.W, a->regs.P.W, "%03x");
    DIFF ("A", c->regs.A.W, a->regs.A.W, "%04x");
    DIFF ("D", c->regs.D.W, a->regs.D.W, "%04x");
    DIFF ("X", c->regs.X.W, a->regs.X.W, "%04x");
    DIFF ("Y", c->regs.Y.W, a->regs.Y.W, "%04x");
    DIFF ("S", c->regs.S.W, a->regs.S.W, "%04x");
    DIFF ("PC", c->pc_offset, a->pc_offset, "%04x");
    DIFF ("PCptr", (unsigned) (c->pc - c->pc_base), (unsigned) (a->pc - a->pc_base), "%x");
    DIFF ("PCBase", (void *) c->pc_base, (void *) a->pc_base, "%p");
    DIFF ("cycles", (int) (c->cycles - s->cpu.Cycles), (int) (a->cycles - s->cpu.Cycles), "%+d");
    DIFF ("MemSpeed", (int) c->mem_speed, (int) a->mem_speed, "%d");
    DIFF ("MemSpeedx2", (int) c->mem_speed_x2, (int) a->mem_speed_x2, "%d");
    DIFF ("WaitAddress", (void *) c->wait_address, (void *) a->wait_address, "%p");
    DIFF ("WaitCounter", c->wait_counter, a->wait_counter, "%u");
    DIFF ("Flags", c->flags, a->flags, "%x");
    DIFF ("WFI", c->waiting_for_interrupt, a->waiting_for_interrupt, "%d");
    DIFF ("BranchSkip", c->branch_skip, a->branch_skip, "%d");
    DIFF ("BRK", c->brk_triggered, a->brk_triggered, "%d");
    DIFF ("SRAMModified", c->sram_modified, a->sram_modified, "%d");
    if (compare_open_bus)
	DIFF ("OpenBus", c->open_bus, a->open_bus, "%02x");
    /* Stray bits in the assembler core's registers (asmStepOpcode's check). */
    DIFF ("format", 0, a->format, "%x");

    if (memcmp (c_buffer, buffer, BUFFER_BYTES) != 0)
    {
	count_field ("mem");
	for (int i = 0; i < BUFFER_BYTES; i++)
	    if (c_buffer [i] != buffer [i])
	    {
		diffs++;
		len += snprintf (text + len, sizeof (text) - len, " mem[%02x:%04x] C=%02x asm=%02x was=%02x",
				 i >> 16, i & 0xffff, c_buffer [i], buffer [i], before_buffer [i]);
		if (len > 1500)
		    break;
	    }
    }
    if (memcmp (c_sram, Memory.SRAM, sizeof (c_sram)) != 0)
    {
	count_field ("sram");
	for (int i = 0; i < (int) sizeof (c_sram); i++)
	    if (c_sram [i] != Memory.SRAM [i])
	    {
		diffs++;
		len += snprintf (text + len, sizeof (text) - len, " sram[%03x] C=%02x asm=%02x", i,
				 c_sram [i], Memory.SRAM [i]);
		if (len > 1500)
		    break;
	    }
    }

    if (diffs && print)
    {
	const struct SRegisters &r = s->regs;
	const uint8 *pc = s->cpu.PC;
	printf ("%02X %-4s at %02x:%04x [%02x %02x %02x %02x] P=%03x A=%04x X=%04x Y=%04x S=%04x D=%04x DB=%02x"
		" WA=%s WC=%u F=%x BS=%d:%s\n",
		opcode, mode_name [mode], r.PB, r.PC, pc [0], pc [1], pc [2], pc [3], r.P.W, r.A.W, r.X.W,
		r.Y.W, r.S.W, r.D.W, r.DB, s->cpu.WaitAddress ? "pc" : "-", s->cpu.WaitCounter,
		s->cpu.Flags, s->cpu.BranchSkip, text);
    }
    return diffs;
}

int main (int argc, char **argv)
{
    int cases = argc > 1 ? atoi (argv [1]) : 2000;
    rng = argc > 2 ? strtoul (argv [2], NULL, 0) : 1;
    int only = argc > 3 && strcmp (argv [3], "all") != 0 ? (int) strtol (argv [3], NULL, 16) : -1;
    /* Options after the opcode (semihosting passes no environment):
     * "openbus" also compares the open bus byte, "verbose" names each case. */
    for (int i = 4; i < argc; i++)
    {
	if (strcmp (argv [i], "openbus") == 0)
	    compare_open_bus = 1;
	else if (strcmp (argv [i], "verbose") == 0)
	    verbose = 1;
	else if (strcmp (argv [i], "mapped") == 0)
	    no_unmapped = 1;
	else if (strncmp (argv [i], "print=", 6) == 0)
	    max_print = atoi (argv [i] + 6);
    }
    if (rng == 0)
	rng = 1;
    setvbuf (stdout, NULL, _IOLBF, 0);

    memset (&Settings, 0, sizeof (Settings));
    Settings.Shutdown = Settings.ShutdownMaster = TRUE;
    Settings.H_Max = SNES_CYCLES_PER_SCANLINE;
    Settings.HBlankStart = (256 * Settings.H_Max) / SNES_HCOUNTER_MAX;
    Settings.CyclesPercentage = 100;
    if (!Memory.Init ())
    {
	printf ("Memory.Init failed\n");
	return 1;
    }
    buffer = (uint8 *) calloc (1, BUFFER_BYTES + 16);
    before_buffer = (uint8 *) malloc (BUFFER_BYTES);
    c_buffer = (uint8 *) malloc (BUFFER_BYTES);
    make_memory_map ();

    int total_diffs = 0, failed_opcodes = 0;
    for (int opcode = 0; opcode < 256; opcode++)
    {
	if (only >= 0 && opcode != only)
	    continue;
	int opcode_diffs = 0, printed = 0;
	for (int mode = 0; mode < 5; mode++)
	    for (int n = 0; n < cases; n++)
	    {
		/* Fresh random memory now and then; it is put back between cases. */
		if (n % 64 == 0)
		{
		    for (int i = 0; i < BUFFER_BYTES; i++)
			buffer [i] = rnd ();
		    for (int i = 0; i < (int) sizeof (before_sram); i++)
			Memory.SRAM [i] = rnd ();
		}
		/* The memory before the instruction is placed; set_up writes it. */
		memcpy (before_buffer, buffer, BUFFER_BYTES);
		struct Start s;
		set_up (&s, mode, opcode);
		memcpy (before_buffer, buffer, BUFFER_BYTES);
		memcpy (before_sram, Memory.SRAM, sizeof (before_sram));

		struct Result c, a;
		restore (&s);
		run_c ();
		record (&c);
		memcpy (c_buffer, buffer, BUFFER_BYTES);
		memcpy (c_sram, Memory.SRAM, sizeof (c_sram));

		restore (&s);
		if (verbose)
		    printf ("opcode %02X mode %s case %d\n", opcode, mode_name [mode], n);
		run_asm ();
		record (&a);
		a.format = asm_format ();

		int d = compare (&s, &c, &a, opcode, mode, printed < max_print);
		if (d)
		{
		    printed++;
		    opcode_diffs++;
		    mode_counts [mode]++;
		}
		/* Leave the buffer as it was before the opcode. */
		memcpy (buffer, before_buffer, BUFFER_BYTES);
		memcpy (Memory.SRAM, before_sram, sizeof (before_sram));
	    }
	if (opcode_diffs)
	{
	    failed_opcodes++;
	    total_diffs += opcode_diffs;
	    printf ("opcode %02X: %d of %d cases differ; by mode", opcode, opcode_diffs, cases * 5);
	    for (int m = 0; m < 5; m++)
		if (mode_counts [m])
		    printf (" %s %d", mode_name [m], mode_counts [m]);
	    printf ("; by field");
	    for (int i = 0; i < MAX_FIELDS && field_names [i]; i++)
		if (field_counts [i])
		    printf (" %s %d", field_names [i], field_counts [i]);
	    printf ("\n");
	}
	memset (field_counts, 0, sizeof (field_counts));
	memset (mode_counts, 0, sizeof (mode_counts));
    }
    printf ("%s: %d opcodes differ, %d cases in all (%d cases per opcode and mode)\n",
	    failed_opcodes ? "FAIL" : "PASS", failed_opcodes, total_diffs, cases);
    return failed_opcodes ? 1 : 0;
}
