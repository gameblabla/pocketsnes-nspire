/* Glue between the C core and the ARM assembler CPU core in pocketsnes/arm.
 *
 * The assembler core comes from psnes 7.2.0 (DrPocketSnes for the GP2X, Wiz
 * and Caanoo): Yoyofr's 65c816 core from OpenSnes9x, with changes by
 * Squidge, notaz and Bitrider. It runs the main CPU only; games with an SA-1
 * stay on the C core. Built when ARM_ASM_CPU is defined (calculator builds).
 *
 * The assembler core keeps the registers in CPU.AsmRegisters and in ARM
 * registers while it runs, and reaches the rest of CPU by offset. Ordinary
 * ROM and RAM it reads and writes itself; everything else (I/O registers,
 * SRAM, special chips, open bus) goes through the C functions below, which
 * are the C core's own, so both cores see the same memory map. */

#include "snes9x.h"
#include "memmap.h"
#include "ppu.h"
#include "cpuexec.h"
#include "apu.h"
#include "sa1.h"
#include "getset.h"

#ifdef ARM_ASM_CPU

#include <stddef.h>

/* The offsets os9x_65c816_common.s uses (the .equ list at its start). */
#define CHECK_OFFSET(field, offset) \
    static_assert (offsetof (struct SCPUState, field) == (offset), \
                   "SCPUState." #field " moved: update os9x_65c816_common.s")
CHECK_OFFSET (Flags, 0);
CHECK_OFFSET (BranchSkip, 4);
CHECK_OFFSET (NMIActive, 5);
CHECK_OFFSET (IRQActive, 6);
CHECK_OFFSET (WaitingForInterrupt, 7);
CHECK_OFFSET (AsmRegisters.PB, 8);
CHECK_OFFSET (AsmRegisters.DB, 9);
CHECK_OFFSET (AsmRegisters.P, 10);
CHECK_OFFSET (AsmRegisters.A, 12);
CHECK_OFFSET (AsmRegisters.D, 14);
CHECK_OFFSET (AsmRegisters.X, 16);
CHECK_OFFSET (AsmRegisters.S, 18);
CHECK_OFFSET (AsmRegisters.Y, 20);
CHECK_OFFSET (AsmRegisters.PC, 22);
CHECK_OFFSET (PC, 24);
CHECK_OFFSET (Cycles, 28);
CHECK_OFFSET (PCBase, 32);
CHECK_OFFSET (PCAtOpcodeStart, 36);
CHECK_OFFSET (WaitAddress, 40);
CHECK_OFFSET (WaitCounter, 44);
CHECK_OFFSET (NextEvent, 48);
CHECK_OFFSET (V_Counter, 52);
CHECK_OFFSET (MemSpeed, 56);
CHECK_OFFSET (MemSpeedx2, 60);
CHECK_OFFSET (FastROMSpeed, 64);
CHECK_OFFSET (NMICycleCount, 76);
CHECK_OFFSET (IRQCycleCount, 80);
CHECK_OFFSET (InDMA, 84);
CHECK_OFFSET (BRKTriggered, 87);
CHECK_OFFSET (AsmOpcodes, 88);
CHECK_OFFSET (AsmOpenBus, 93);
CHECK_OFFSET (AsmMap, 96);
CHECK_OFFSET (AsmWriteMap, 100);
CHECK_OFFSET (AsmMemorySpeed, 104);
CHECK_OFFSET (AsmBlockIsRAM, 108);
CHECK_OFFSET (AsmHBlank, 112);
CHECK_OFFSET (AsmStatus, 116);
static_assert (sizeof (long) == 4 && sizeof (void *) == 4, "the assembler core is 32-bit");
static_assert (CMemory::MAP_LAST == 18, "os9x_65c816_common.s: MAP_LAST");
static_assert (MEMMAP_SHIFT == 12, "os9x_65c816_common.s: MEMMAP_SHIFT");
static_assert (sizeof (Memory.MemorySpeed [0]) == 1, "the assembler core reads MemorySpeed as bytes");

extern "C"
{
void asmMainLoop (struct SCPUState *cpu);

/* Called by the assembler core for anything that isn't plain ROM or RAM,
 * and for word accesses that cross a 4 KB block. They add the cycles
 * themselves. */
uint8 asm_S9xGetByte (uint32 Address)
{
    return S9xGetByte (Address);
}

uint16 asm_S9xGetWord (uint32 Address)
{
    return S9xGetWord (Address);
}

void asm_S9xSetByte (uint32 Address, uint8 Byte)
{
    S9xSetByte (Byte, Address);
}

void asm_S9xSetWord (uint32 Address, uint16 Word)
{
    S9xSetWord (Word, Address);
}

void asm_S9xSetPCBase (uint32 Address)
{
    S9xSetPCBase (Address);
}

/* Stack words as cpuops.cpp's PullW and PushW move them, a byte at a time
 * in bank 0, for the cases the assembler core doesn't do itself. */
uint16 asm_S9xGetStackWord (uint32 Address)
{
    uint16 Word = S9xGetByte (Address & 0xffff);
    return Word | (S9xGetByte ((Address + 1) & 0xffff) << 8);
}

void asm_S9xSetStackWord (uint32 Address, uint16 Word)
{
    S9xSetByte (Word >> 8, (Address + 1) & 0xffff);
    S9xSetByte (Word & 0xff, Address & 0xffff);
}

/* Decimal mode ADC and SBC, which games seldom use: the C core's arithmetic
 * from cpumacro.h, digits above 9 included. A and W are the accumulator and
 * the operand; the result comes back with the carry and overflow flags in
 * the two bits above it. */
uint32 asm_DecimalADC8 (uint32 A, uint32 Work8, uint32 carry)
{
    uint8 A1 = A & 0xF;
    uint8 A2 = (A >> 4) & 0xF;
    uint8 W1 = Work8 & 0xF;
    uint8 W2 = (Work8 >> 4) & 0xF;

    A1 += W1 + carry;
    if (A1 > 9)
    {
	A1 -= 10;
	A1 &= 0xF;
	A2++;
    }
    A2 += W2;
    if (A2 > 9)
    {
	A2 -= 10;
	A2 &= 0xF;
	carry = 1;
    }
    else
	carry = 0;

    uint8 Ans8 = (A2 << 4) | A1;
    uint32 overflow = (~(A ^ Work8) & (Work8 ^ Ans8) & 0x80) != 0;
    return Ans8 | (carry << 8) | (overflow << 9);
}

uint32 asm_DecimalADC16 (uint32 A, uint32 Work16, uint32 carry)
{
    uint8 A1 = A & 0xF;
    uint8 A2 = (A >> 4) & 0xF;
    uint8 A3 = (A >> 8) & 0xF;
    uint8 A4 = (A >> 12) & 0xF;
    uint8 W1 = Work16 & 0xF;
    uint8 W2 = (Work16 >> 4) & 0xF;
    uint8 W3 = (Work16 >> 8) & 0xF;
    uint8 W4 = (Work16 >> 12) & 0xF;

    A1 += W1 + carry;
    if (A1 > 9)
    {
	A1 -= 10;
	A1 &= 0xF;
	A2++;
    }
    A2 += W2;
    if (A2 > 9)
    {
	A2 -= 10;
	A2 &= 0xF;
	A3++;
    }
    A3 += W3;
    if (A3 > 9)
    {
	A3 -= 10;
	A3 &= 0xF;
	A4++;
    }
    A4 += W4;
    if (A4 > 9)
    {
	A4 -= 10;
	A4 &= 0xF;
	carry = 1;
    }
    else
	carry = 0;

    uint16 Ans16 = (A4 << 12) | (A3 << 8) | (A2 << 4) | (A1);
    uint32 overflow = (~(A ^ Work16) & (Work16 ^ Ans16) & 0x8000) != 0;
    return Ans16 | (carry << 16) | (overflow << 17);
}

uint32 asm_DecimalSBC8 (uint32 A, uint32 Work8, uint32 carry)
{
    uint8 A1 = A & 0xF;
    uint8 A2 = (A >> 4) & 0xF;
    uint8 W1 = Work8 & 0xF;
    uint8 W2 = (Work8 >> 4) & 0xF;

    A1 -= W1 + !carry;
    A2 -= W2;
    if (A1 > 9)
    {
	A1 += 10;
	A2--;
    }
    if (A2 > 9)
    {
	A2 += 10;
	carry = 0;
    }
    else
	carry = 1;

    uint8 Ans8 = (A2 << 4) | A1;
    uint32 overflow = ((A ^ Work8) & (A ^ Ans8) & 0x80) != 0;
    return Ans8 | (carry << 8) | (overflow << 9);
}

uint32 asm_DecimalSBC16 (uint32 A, uint32 Work16, uint32 carry)
{
    uint8 A1 = A & 0xF;
    uint8 A2 = (A >> 4) & 0xF;
    uint8 A3 = (A >> 8) & 0xF;
    uint8 A4 = (A >> 12) & 0xF;
    uint8 W1 = Work16 & 0xF;
    uint8 W2 = (Work16 >> 4) & 0xF;
    uint8 W3 = (Work16 >> 8) & 0xF;
    uint8 W4 = (Work16 >> 12) & 0xF;

    A1 -= W1 + !carry;
    A2 -= W2;
    A3 -= W3;
    A4 -= W4;
    if (A1 > 9)
    {
	A1 += 10;
	A2--;
    }
    if (A2 > 9)
    {
	A2 += 10;
	A3--;
    }
    if (A3 > 9)
    {
	A3 += 10;
	A4--;
    }
    if (A4 > 9)
    {
	A4 += 10;
	carry = 0;
    }
    else
	carry = 1;

    uint16 Ans16 = (A4 << 12) | (A3 << 8) | (A2 << 4) | (A1);
    uint32 overflow = ((A ^ Work16) & (A ^ Ans16) & 0x8000) != 0;
    return Ans16 | (carry << 16) | (overflow << 17);
}
}

static void copy_registers_in (void)
{
    struct SAsmRegisters *r = &CPU.AsmRegisters;
    r->PB = ICPU.Registers.PB;
    r->DB = ICPU.Registers.DB;
    r->P = ICPU.Registers.P.W;
    r->A = ICPU.Registers.A.W;
    r->D = ICPU.Registers.D.W;
    r->X = ICPU.Registers.X.W;
    r->S = ICPU.Registers.S.W;
    r->Y = ICPU.Registers.Y.W;
    r->PC = ICPU.Registers.PC;
}

/* Copies the C core's registers to where the assembler core keeps them. */
void S9xAsmRegistersIn (void)
{
    copy_registers_in ();

    CPU.AsmMap = Memory.Map;
    CPU.AsmWriteMap = Memory.WriteMap;
    CPU.AsmMemorySpeed = Memory.MemorySpeed;
    CPU.AsmBlockIsRAM = Memory.BlockIsRAM;
    CPU.AsmHBlank = Settings.SuperFX ? S9xDoHBlankProcessing_SFX : S9xDoHBlankProcessing_NoSFX;
}

/* And back, leaving the C core's flag copies and opcode table matching. */
void S9xAsmRegistersOut (void)
{
    const struct SAsmRegisters *r = &CPU.AsmRegisters;
    ICPU.Registers.PB = r->PB;
    ICPU.Registers.DB = r->DB;
    ICPU.Registers.P.W = r->P;
    ICPU.Registers.A.W = r->A;
    ICPU.Registers.D.W = r->D;
    ICPU.Registers.X.W = r->X;
    ICPU.Registers.S.W = r->S;
    ICPU.Registers.Y.W = r->Y;
    ICPU.Registers.PC = CPU.PC - CPU.PCBase;

    ICPU.ShiftedPB = ICPU.Registers.PB << 16;
    ICPU.ShiftedDB = ICPU.Registers.DB << 16;
    S9xUnpackStatus ();
    S9xFixCycles ();
}

/* Runs one opcode, already fetched, on the C core: the assembler core
 * leaves the emulation mode stack opcodes to it (COPCODE in
 * os9x_65c816_common.s). Its registers are in CPU.AsmRegisters, and are put
 * back there. */
extern "C" void asm_S9xRunCOpcode (uint32 Opcode)
{
    S9xAsmRegistersOut ();
    (*ICPU.S9xOpcodes [Opcode].S9xOpcode) ();
    S9xPackStatus ();
    ICPU.Registers.PC = CPU.PC - CPU.PCBase;
    copy_registers_in ();
}

/* One frame, like the C main loops in cpuexec.cpp. */
void S9xMainLoop_Asm (void)
{
    S9xAsmRegistersIn ();
    asmMainLoop (&CPU);
    S9xAsmRegistersOut ();

    IAPU.Registers.PC = IAPU.PC - IAPU.RAM;
    S9xAPUPackStatus ();
    if (CPU.Flags & SCAN_KEYS_FLAG)
    {
	S9xSyncSpeed ();
	CPU.Flags &= ~SCAN_KEYS_FLAG;
    }

#ifdef DETECT_NASTY_FX_INTERLEAVE
    if (CPU.BRKTriggered && Settings.SuperFX && !CPU.TriedInterleavedMode2)
    {
	CPU.TriedInterleavedMode2 = TRUE;
	CPU.BRKTriggered = FALSE;
	S9xDeinterleaveMode2 ();
    }
#endif
}

#endif /* ARM_ASM_CPU */
