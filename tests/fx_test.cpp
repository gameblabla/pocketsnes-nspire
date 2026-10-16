/* Super FX (GSU) interpreter test: fx_run (pocketsnes/snes9x/fxinst.cpp, the
 * interpreter with its state in local variables) against fx_run_reference
 * (the original loop, a call through fx_apfOpcodeTable per instruction).
 * Both start from the same random GSU state and memory, and after each run
 * the whole GSU struct, the GSU RAM and the register space must be the same,
 * byte for byte (registers keep bits above 16 too, so these are compared as
 * well).
 *
 *   make fx-test            builds and runs tests/fx_test
 *   tests/fx_test [N] [SEED] [c|asm]
 * On ARM (tests/asm/run.sh, under qemu) "asm" tests the ARM assembler
 * interpreter (pocketsnes/arm/fx_run_arm.s, its FX_COUNTED build) instead of
 * fx_run.
 *
 * Part 1 runs every one of the 1024 opcode/ALT combinations N times as a
 * single instruction (with the B flag on and off, every source and
 * destination register, all banks); part 2 runs N random programs of up to
 * 200 instructions each, the opcodes weighted towards the common ones and
 * towards prefix sequences (with/to/from/alt). */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ARM_ASM_FX
#include "snes9x.h"
struct SSettings Settings;	/* (fxinst.cpp reads Settings.AsmCPU) */
extern "C"
{
    void fx_run_arm_counted (uint32 nInstructions);
    extern uint32 fx_arm_handlers_counted [];
    extern uint32 fx_arm_tables_counted [3072];
    void fx_arm_setup_tables (uint32 *tables, const uint32 *handlers);
    void fx_arm_plot_entries (uint32 *tables, const uint32 *handlers);
}
#endif
#include "fxemu.h"
#include "fxinst.h"

extern struct FxRegs_s GSU;
void fx_run_reference(uint32 nInstructions);
void fx_computeScreenPointers ();
static void (*run_new) (uint32) = fx_run;
static bool asm_counted;

#define ROM_SIZE (0x600000)
#define RAM_SIZE (0x20000)

static uint8 *rom, *ram, regs[0x300];
static uint32 rng_state = 1;

static uint32 rnd()
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

/* A register value: mostly 16-bit, sometimes with junk above bit 15 (as the
 * interpreter leaves there), sometimes one of the edge values. */
static uint32 reg_value()
{
    static const uint32 edges[] = { 0, 1, 0x7f, 0x80, 0xff, 0x100, 0x7fff, 0x8000, 0x8001, 0xfffe, 0xffff,
				    0x10000, 0xffffffff, 0xffff8000, 0x1ffff };
    switch (rnd() % 8)
    {
    case 0: return edges[rnd() % (sizeof edges / sizeof edges[0])];
    case 1: return rnd();
    default: return rnd() & 0xffff;
    }
}

/* Banks the GSU can run from: ROM (00-3f, 40-5f) and its RAM (70-71). */
static uint32 code_bank()
{
    switch (rnd() % 4)
    {
    case 0: return rnd() & 0x3f;
    case 1: return 0x40 + (rnd() & 0x1f);
    case 2: return 0x70 + (rnd() & 1);
    default: return 0x01;
    }
}

static void random_state(uint32 op_index, bool single)
{
    int i;
    for (i = 0; i < 16; i++)
	GSU.avReg[i] = reg_value();
    /* loop's counter near its end (with and without bits above 16) */
    if (rnd() % 4 == 0)
	GSU.avReg[12] = (rnd() % 3) | (rnd() % 2 ? 0 : rnd() << 16);
    /* Keep the pointers mostly into memory that's set up */
    GSU.vStatusReg = (rnd() & 0x9f1e) | FLG_G;	/* Z CY S OV R ALT1 ALT2 IL IH B IRQ */
    if (single)
	GSU.vStatusReg = (GSU.vStatusReg & ~0x300) | (op_index & 0x300);
    if (rnd() % 64 == 0)
	GSU.vStatusReg &= ~FLG_G;		/* a run that mustn't do anything */
    GSU.vSign = rnd() % 3 ? reg_value() : rnd();
    GSU.vZero = rnd() % 3 ? reg_value() : rnd();
    GSU.vCarry = rnd() & 1;
    GSU.vOverflow = rnd() % 2 ? (int32)(rnd() & 0x8000) : (int32)rnd();
    GSU.pvSreg = &GSU.avReg[rnd() & 15];
    GSU.pvDreg = rnd() % 2 ? &GSU.avReg[0] : &GSU.avReg[rnd() & 15];
    GSU.vRomBuffer = rnd();
    GSU.vColorReg = rnd() & 0xff;
    GSU.vPlotOptionReg = rnd() & 0x1f;
    GSU.vLastRamAdr = rnd() & 0xffff;
    GSU.vPrgBankReg = code_bank();
    GSU.pvPrgBank = GSU.apvRomBank[GSU.vPrgBankReg];
    GSU.vRomBankReg = rnd() & 0x7f;
    GSU.pvRomBank = GSU.apvRomBank[GSU.vRomBankReg];
    GSU.vRamBankReg = rnd() & 3;
    GSU.pvRamBank = GSU.apvRamBank[GSU.vRamBankReg];
    /* A screen mode and height, as fx_readRegisterSpace sets them (plot and
     * rpix through the table, the screen's pointers for them) */
    if (rnd() % 4 == 0)
    {
	static const uint32 heights[] = { 128, 160, 192, 256 };
	GSU.vMode = rnd() & 3;
	GSU.vScreenHeight = GSU.vScreenRealHeight = heights[rnd() & 3];
	fx_apfOpcodeTable[0x24c] = fx_apfOpcodeTable[0x04c] = fx_apfPlotTable[GSU.vMode];
	fx_apfOpcodeTable[0x14c] = fx_apfOpcodeTable[0x34c] = fx_apfPlotTable[GSU.vMode + 5];
	fx_computeScreenPointers();
    }
    GSU.vCacheBaseReg = rnd() & 0xfff0;
    GSU.bCacheActive = rnd() & 1;
    GSU.pvRegisters[GSU_CFGR] = rnd();
    if (single)
	GSU.vPipe = op_index & 0xff;
    else
	GSU.vPipe = rnd();
}

/* A program for part 2: the common opcodes more often, and prefixes before
 * them (so the B flag and ALT modes reach the instructions they change). */
static uint8 random_opcode()
{
    static const uint8 common[] = {
	0x01, 0x03, 0x04, 0x05, 0x08, 0x09, 0x0a, 0x0c, 0x0d, 0x3c, 0x3d, 0x3e, 0x3f, 0x95, 0x96, 0x97,
	0x9e, 0x9f, 0xc0, 0x4d, 0x4f, 0x70, 0x90, 0x91, 0x4e, 0xdf, 0xef, 0x4c, 0x4c, 0x70, 0x4e,
	0x1e, 0x2e };
    uint32 k = rnd() % 16;
    if (k < 3)
	return 0x10 + (rnd() % 0x30);		/* to, with, stw/stb */
    if (k < 5)
	return 0xb0 + (rnd() & 15);		/* from */
    if (k < 6)
	return 0x3d + (rnd() % 3);		/* alt1-3 */
    if (k < 8)
	return common[rnd() % sizeof common];
    if (k < 9)
	return 0x40 + (rnd() % 12);		/* ldw/ldb */
    uint8 op = rnd();
    if (op == 0x00 && rnd() % 4)		/* stop now and then */
	op = 0x01;
    return op;
}

/* What fx_run does before running the assembler interpreter */
static void plot_entries()
{
#ifdef ARM_ASM_FX
    if (asm_counted)
	fx_arm_plot_entries(fx_arm_tables_counted, fx_arm_handlers_counted);
#endif
}

static uint8 *program_at(uint32 r15)
{
    return &GSU.pvPrgBank[r15 & 0xffff];
}

static void write_program(int length)
{
    uint32 pc = GSU.avReg[15];
    for (int i = 0; i < length * 3 + 16; i++)
	program_at(pc + i)[0] = random_opcode();
}

/* The GSU only writes its RAM (and the register space), never the ROM. */
struct Snapshot
{
    struct FxRegs_s gsu;
    uint8 *ram;
    uint8 regs[0x300];
};

static void take(Snapshot &s)
{
    memcpy(&s.gsu, &GSU, sizeof GSU);
    memcpy(s.ram, ram, RAM_SIZE);
    memcpy(s.regs, regs, sizeof regs);
}

static void put_back(const Snapshot &s)
{
    memcpy(&GSU, &s.gsu, sizeof GSU);
    memcpy(ram, s.ram, RAM_SIZE);
    memcpy(regs, s.regs, sizeof regs);
}

static const char *field_name(size_t offset)
{
    static char buf[64];
#define F(name) if (offset >= offsetof(FxRegs_s, name) && offset < offsetof(FxRegs_s, name) + sizeof(GSU.name)) return #name;
    F(avReg) F(vColorReg) F(vPlotOptionReg) F(vStatusReg) F(vPrgBankReg) F(vRomBankReg) F(vRamBankReg)
    F(vCacheBaseReg) F(vCacheFlags) F(vLastRamAdr) F(pvDreg) F(pvSreg) F(vRomBuffer) F(vPipe) F(vPipeAdr)
    F(vSign) F(vZero) F(vCarry) F(vOverflow) F(pvRamBank) F(pvRomBank) F(pvPrgBank) F(bCacheActive)
    F(vScreenHeight) F(vSCBRDirty)
#undef F
    snprintf(buf, sizeof buf, "byte %u", (unsigned) offset);
    return buf;
}

/* Compares the state after the reference run (ref) with the one now. */
static bool same(const Snapshot &ref, const char *what, uint32 op_index, int *reported)
{
    const uint8 *a = (const uint8 *) &ref.gsu, *b = (const uint8 *) &GSU;
    bool ok = true;
    for (size_t i = 0; i < sizeof GSU; i++)
	if (a[i] != b[i])
	{
	    if ((*reported)++ < 20)
		printf("  %s, opcode %03x: %s differs (offset %u)\n", what, op_index, field_name(i), (unsigned) i);
	    ok = false;
	    break;
	}
    if (memcmp(ref.ram, ram, RAM_SIZE))
    {
	if ((*reported)++ < 20)
	    printf("  %s, opcode %03x: RAM differs\n", what, op_index);
	ok = false;
    }
    if (memcmp(ref.regs, regs, sizeof regs))
    {
	if ((*reported)++ < 20)
	    printf("  %s, opcode %03x: register space differs\n", what, op_index);
	ok = false;
    }
    return ok;
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 200;
    rng_state = argc > 2 ? strtoul(argv[2], NULL, 0) : 12345;
    if (!rng_state)
	rng_state = 1;
    const char *which = argc > 3 ? argv[3] : "c";
#ifdef ARM_ASM_FX
    if (!strcmp(which, "asm"))
    {
	fx_arm_setup_tables(fx_arm_tables_counted, fx_arm_handlers_counted);
	run_new = fx_run_arm_counted;
	asm_counted = true;
    }
#else
    if (strcmp(which, "c"))
    {
	printf("FAIL: only the C interpreter on this machine\n");
	return 1;
    }
#endif

    rom = (uint8 *) malloc(ROM_SIZE);
    ram = (uint8 *) malloc(RAM_SIZE);
    for (int i = 0; i < ROM_SIZE; i++) rom[i] = rnd();
    for (int i = 0; i < RAM_SIZE; i++) ram[i] = rnd();

    struct FxInit_s init;
    memset(&init, 0, sizeof init);
    init.pvRegisters = regs;
    init.nRamBanks = 2;
    init.pvRam = ram;
    init.nRomBanks = 0x20;
    init.pvRom = rom;
    FxReset(&init);
    /* A 4-colour 128-line screen, as FxReset leaves it, plus random register space */
    for (int i = 0; i < 0x40; i++) regs[i] = rnd();

    Snapshot start, ref;
    start.ram = (uint8 *) malloc(RAM_SIZE);
    ref.ram = (uint8 *) malloc(RAM_SIZE);

    int failed = 0, reported = 0;
    long runs = 0;

    /* Part 1: every opcode in every ALT mode, one instruction at a time */
    for (int round = 0; round < n; round++)
	for (uint32 op_index = 0; op_index < 1024; op_index++)
	{
	    random_state(op_index, true);
	    /* bytes after the opcode (immediates, the next pipe byte) */
	    write_program(2);
	    take(start);
	    fx_run_reference(1);
	    take(ref);
	    put_back(start);
	    plot_entries();
	    run_new(1);
	    if (!same(ref, "single", op_index, &reported))
		failed++;
	    runs++;
	}

    /* Part 2: random programs */
    for (int round = 0; round < n * 50; round++)
    {
	int length = 1 + rnd() % 200;
	random_state(0, false);
	write_program(length);
	take(start);
	fx_run_reference(length);
	take(ref);
	put_back(start);
	plot_entries();
	run_new(length);
	if (!same(ref, "program", length, &reported))
	    failed++;
	runs++;
    }

    printf("%s: %d of %ld runs differ (%s interpreter; %d rounds of all 1024 opcodes, %d programs)\n",
	   failed ? "FAIL" : "PASS", failed, runs, which, n, n * 50);
    return failed ? 1 : 0;
}
