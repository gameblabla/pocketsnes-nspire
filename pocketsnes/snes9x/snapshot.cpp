/*******************************************************************************
  Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
 
  (c) Copyright 1996 - 2002 Gary Henderson (gary.henderson@ntlworld.com) and
                            Jerremy Koot (jkoot@snes9x.com)

  (c) Copyright 2001 - 2004 John Weidman (jweidman@slip.net)

  (c) Copyright 2002 - 2004 Brad Jorsch (anomie@users.sourceforge.net),
                            funkyass (funkyass@spam.shaw.ca),
                            Joel Yliluoma (http://iki.fi/bisqwit/)
                            Kris Bleakley (codeviolation@hotmail.com),
                            Matthew Kendora,
                            Nach (n-a-c-h@users.sourceforge.net),
                            Peter Bortas (peter@bortas.org) and
                            zones (kasumitokoduck@yahoo.com)

  C4 x86 assembler and some C emulation code
  (c) Copyright 2000 - 2003 zsKnight (zsknight@zsnes.com),
                            _Demo_ (_demo_@zsnes.com), and Nach

  C4 C++ code
  (c) Copyright 2003 Brad Jorsch

  DSP-1 emulator code
  (c) Copyright 1998 - 2004 Ivar (ivar@snes9x.com), _Demo_, Gary Henderson,
                            John Weidman, neviksti (neviksti@hotmail.com),
                            Kris Bleakley, Andreas Naive

  DSP-2 emulator code
  (c) Copyright 2003 Kris Bleakley, John Weidman, neviksti, Matthew Kendora, and
                     Lord Nightmare (lord_nightmare@users.sourceforge.net

  OBC1 emulator code
  (c) Copyright 2001 - 2004 zsKnight, pagefault (pagefault@zsnes.com) and
                            Kris Bleakley
  Ported from x86 assembler to C by sanmaiwashi

  SPC7110 and RTC C++ emulator code
  (c) Copyright 2002 Matthew Kendora with research by
                     zsKnight, John Weidman, and Dark Force

  S-DD1 C emulator code
  (c) Copyright 2003 Brad Jorsch with research by
                     Andreas Naive and John Weidman
 
  S-RTC C emulator code
  (c) Copyright 2001 John Weidman
  
  ST010 C++ emulator code
  (c) Copyright 2003 Feather, Kris Bleakley, John Weidman and Matthew Kendora

  Super FX x86 assembler emulator code 
  (c) Copyright 1998 - 2003 zsKnight, _Demo_, and pagefault 

  Super FX C emulator code 
  (c) Copyright 1997 - 1999 Ivar, Gary Henderson and John Weidman


  SH assembler code partly based on x86 assembler code
  (c) Copyright 2002 - 2004 Marcus Comstedt (marcus@mc.pp.se) 

 
  Specific ports contains the works of other authors. See headers in
  individual files.
 
  Snes9x homepage: http://www.snes9x.com
 
  Permission to use, copy, modify and distribute Snes9x in both binary and
  source form, for non-commercial purposes, is hereby granted without fee,
  providing that this license information and copyright notice appear with
  all copies and any derived work.
 
  This software is provided 'as-is', without any express or implied
  warranty. In no event shall the authors be held liable for any damages
  arising from the use of this software.
 
  Snes9x is freeware for PERSONAL USE only. Commercial users should
  seek permission of the copyright holders first. Commercial use includes
  charging money for Snes9x or software derived from Snes9x.
 
  The copyright holders request that bug fixes and improvements to the code
  should be forwarded to them so everyone can benefit from the modifications
  in future versions.
 
  Super NES and Super Nintendo Entertainment System are trademarks of
  Nintendo Co., Limited and its subsidiary companies.
*******************************************************************************/

/* Save states for PocketSNES-nspire.
 *
 * Follows the Snes9x 1.43 snapshot code (unused/snapshot.cpp) in what it
 * saves and how it fixes things up after loading, but writes to memory so the
 * frontend can compress it, and leaves out the APU/sound state that this
 * build doesn't emulate. Structs without pointers are stored whole. Every
 * block records its size, so a state written by an incompatible build is
 * rejected instead of being half applied. */

#include <string.h>
#include <stdlib.h>

#include "snes9x.h"
#include "memmap.h"
#include "cpuexec.h"
#include "ppu.h"
#include "dma.h"
#include "sa1.h"
#include "srtc.h"
#include "dsp1.h"
#include "snapshot.h"

/* Defined in sa1.cpp but not declared in sa1.h. */
void S9xSetSA1MemMap (uint32 which1, uint8 map);

extern int OBC1_Address;
extern int OBC1_BasePtr;
extern int OBC1_Shift;

#define SNAPSHOT_MAGIC     "PSNSNAP1"
#define SNAPSHOT_MAGIC_LEN 8
#define MAX_BLOCKS         32

/* The plain values of SCPUState; its pointers are rebuilt on load. */
struct SnapCPU
{
    uint32 Flags;
    uint8  BranchSkip;
    uint8  NMIActive;
    uint8  IRQActive;
    uint8  WaitingForInterrupt;
    uint8  WhichEvent;
    uint8  SRAMModified;
    uint8  BRKTriggered;
    uint8  pad;
    int32  Cycles;
    int32  NextEvent;
    int32  V_Counter;
    int32  MemSpeed;
    int32  MemSpeedx2;
    int32  FastROMSpeed;
    uint32 WaitCounter;
    uint32 AutoSaveTimer;
    uint32 NMITriggerPoint;
    uint32 NMICycleCount;
    uint32 IRQCycleCount;
};

/* The parts of InternalPPU that are machine state rather than render caches. */
struct SnapIPPU
{
    uint8  HDMA;
    uint8  HDMAStarted;
    uint8  MaxBrightness;
    uint8  LatchedBlanking;
    uint8  Interlace;
    uint8  pad;
#ifdef CORRECT_VRAM_READS
    uint16 VRAMReadBuffer;
#else
    uint16 FirstVRAMRead;
#endif
};

struct SnapSA1
{
    uint32 Flags;
    uint8  Executing;
    uint8  NMIActive;
    uint8  IRQActive;
    uint8  WaitingForInterrupt;
    uint8  Waiting;
    uint8  overflow;
    uint8  VirtualBitmapFormat;
    uint8  in_char_dma;
    uint8  variable_bit_pos;
    uint8  pad;
    int16  op1;
    int16  op2;
    int32  arithmetic_op;
    uint32 WaitCounter;
    int64  sum;
};

struct SnapOBC1
{
    int32 Address;
    int32 BasePtr;
    int32 Shift;
};

struct Writer
{
    uint8  *buf;
    uint32 len;
    uint32 cap;
    bool8  ok;
};

static void WriteBytes (Writer *w, const void *data, uint32 size)
{
    if (!w->ok)
	return;
    if (w->len + size > w->cap)
    {
	uint32 cap = w->cap ? w->cap : 0x60000;
	while (cap < w->len + size)
	    cap *= 2;
	uint8 *grown = (uint8 *) realloc (w->buf, cap);
	if (!grown)
	{
	    w->ok = FALSE;
	    return;
	}
	w->buf = grown;
	w->cap = cap;
    }
    if (size)
	memcpy (w->buf + w->len, data, size);
    w->len += size;
}

static void WriteBlock (Writer *w, const char *tag, const void *data, uint32 size)
{
    WriteBytes (w, tag, 4);
    WriteBytes (w, &size, 4);
    WriteBytes (w, data, size);
}

bool8 S9xFreezeToMemory (uint8 **data, uint32 *size)
{
    Writer w = { NULL, 0, 0, TRUE };
    struct SnapCPU cpu;
    struct SnapIPPU ippu;

    if (Settings.SRTC)
	S9xSRTCPreSaveState ();

    memset (&cpu, 0, sizeof (cpu));
    cpu.Flags = CPU.Flags;
    cpu.BranchSkip = CPU.BranchSkip;
    cpu.NMIActive = CPU.NMIActive;
    cpu.IRQActive = CPU.IRQActive;
    cpu.WaitingForInterrupt = CPU.WaitingForInterrupt;
    cpu.WhichEvent = CPU.WhichEvent;
    cpu.SRAMModified = CPU.SRAMModified;
    cpu.BRKTriggered = CPU.BRKTriggered;
    cpu.Cycles = CPU.Cycles;
    cpu.NextEvent = CPU.NextEvent;
    cpu.V_Counter = CPU.V_Counter;
    cpu.MemSpeed = CPU.MemSpeed;
    cpu.MemSpeedx2 = CPU.MemSpeedx2;
    cpu.FastROMSpeed = CPU.FastROMSpeed;
    cpu.WaitCounter = CPU.WaitCounter;
    cpu.AutoSaveTimer = CPU.AutoSaveTimer;
    cpu.NMITriggerPoint = CPU.NMITriggerPoint;
    cpu.NMICycleCount = CPU.NMICycleCount;
    cpu.IRQCycleCount = CPU.IRQCycleCount;

    memset (&ippu, 0, sizeof (ippu));
    ippu.HDMA = IPPU.HDMA;
    ippu.HDMAStarted = IPPU.HDMAStarted;
    ippu.MaxBrightness = IPPU.MaxBrightness;
    ippu.LatchedBlanking = IPPU.LatchedBlanking;
    ippu.Interlace = IPPU.Interlace;
#ifdef CORRECT_VRAM_READS
    ippu.VRAMReadBuffer = IPPU.VRAMReadBuffer;
#else
    ippu.FirstVRAMRead = IPPU.FirstVRAMRead;
#endif

    WriteBytes (&w, SNAPSHOT_MAGIC, SNAPSHOT_MAGIC_LEN);
    WriteBlock (&w, "CPU ", &cpu, sizeof (cpu));
    WriteBlock (&w, "REGS", &ICPU.Registers, sizeof (ICPU.Registers));
    WriteBlock (&w, "PPU ", &PPU, sizeof (PPU));
    WriteBlock (&w, "DMA ", DMA, sizeof (DMA));
    WriteBlock (&w, "IPPU", &ippu, sizeof (ippu));
    WriteBlock (&w, "VRAM", Memory.VRAM, 0x10000);
    WriteBlock (&w, "WRAM", Memory.RAM, 0x20000);
    WriteBlock (&w, "SRAM", Memory.SRAM, 0x20000);
    WriteBlock (&w, "FILL", Memory.FillRAM, 0x8000);

    if (Settings.C4)
	WriteBlock (&w, "C4RM", Memory.C4RAM, 0x2000);

    if (Settings.DSP1Master)
	WriteBlock (&w, "DSP1", &DSP1, sizeof (DSP1));

    if (Settings.SA1)
    {
	struct SnapSA1 sa1;

	SA1.Registers.PC = SA1.PC - SA1.PCBase;
	S9xSA1PackStatus ();

	memset (&sa1, 0, sizeof (sa1));
	sa1.Flags = SA1.Flags;
	sa1.Executing = SA1.Executing;
	sa1.NMIActive = SA1.NMIActive;
	sa1.IRQActive = SA1.IRQActive;
	sa1.WaitingForInterrupt = SA1.WaitingForInterrupt;
	sa1.Waiting = SA1.Waiting;
	sa1.overflow = SA1.overflow;
	sa1.VirtualBitmapFormat = SA1.VirtualBitmapFormat;
	sa1.in_char_dma = SA1.in_char_dma;
	sa1.variable_bit_pos = SA1.variable_bit_pos;
	sa1.op1 = SA1.op1;
	sa1.op2 = SA1.op2;
	sa1.arithmetic_op = SA1.arithmetic_op;
	sa1.WaitCounter = SA1.WaitCounter;
	sa1.sum = SA1.sum;

	WriteBlock (&w, "SA1 ", &sa1, sizeof (sa1));
	WriteBlock (&w, "SA1R", &SA1.Registers, sizeof (SA1.Registers));
    }

    if (Settings.OBC1)
    {
	struct SnapOBC1 obc1;
	obc1.Address = OBC1_Address;
	obc1.BasePtr = OBC1_BasePtr;
	obc1.Shift = OBC1_Shift;
	WriteBlock (&w, "OBC1", &obc1, sizeof (obc1));
    }

    WriteBlock (&w, "END ", NULL, 0);

    if (!w.ok)
    {
	free (w.buf);
	return FALSE;
    }

    *data = w.buf;
    *size = w.len;
    return TRUE;
}

struct Block
{
    char         tag [4];
    uint32       size;
    const uint8 *data;
};

/* Returns the block with this tag, or NULL if it is missing or has the wrong size. */
static const uint8 *FindBlock (const struct Block *blocks, int count,
			       const char *tag, uint32 size)
{
    for (int i = 0; i < count; i++)
    {
	if (memcmp (blocks [i].tag, tag, 4) == 0)
	    return blocks [i].size == size ? blocks [i].data : NULL;
    }
    return NULL;
}

bool8 S9xUnfreezeFromMemory (const uint8 *data, uint32 size)
{
    struct Block blocks [MAX_BLOCKS];
    int count = 0;
    uint32 pos = SNAPSHOT_MAGIC_LEN;
    bool8 ended = FALSE;

    if (size < SNAPSHOT_MAGIC_LEN || memcmp (data, SNAPSHOT_MAGIC, SNAPSHOT_MAGIC_LEN) != 0)
	return FALSE;

    while (!ended)
    {
	if (count == MAX_BLOCKS || size - pos < 8)
	    return FALSE;
	memcpy (blocks [count].tag, data + pos, 4);
	memcpy (&blocks [count].size, data + pos + 4, 4);
	pos += 8;
	if (blocks [count].size > size - pos)
	    return FALSE;
	blocks [count].data = data + pos;
	pos += blocks [count].size;
	ended = memcmp (blocks [count].tag, "END ", 4) == 0;
	count++;
    }

    const uint8 *cpu_data   = FindBlock (blocks, count, "CPU ", sizeof (struct SnapCPU));
    const uint8 *regs_data  = FindBlock (blocks, count, "REGS", sizeof (ICPU.Registers));
    const uint8 *ppu_data   = FindBlock (blocks, count, "PPU ", sizeof (PPU));
    const uint8 *dma_data   = FindBlock (blocks, count, "DMA ", sizeof (DMA));
    const uint8 *ippu_data  = FindBlock (blocks, count, "IPPU", sizeof (struct SnapIPPU));
    const uint8 *vram_data  = FindBlock (blocks, count, "VRAM", 0x10000);
    const uint8 *wram_data  = FindBlock (blocks, count, "WRAM", 0x20000);
    const uint8 *sram_data  = FindBlock (blocks, count, "SRAM", 0x20000);
    const uint8 *fill_data  = FindBlock (blocks, count, "FILL", 0x8000);
    const uint8 *c4_data    = FindBlock (blocks, count, "C4RM", 0x2000);
    const uint8 *dsp1_data  = FindBlock (blocks, count, "DSP1", sizeof (DSP1));
    const uint8 *sa1_data   = FindBlock (blocks, count, "SA1 ", sizeof (struct SnapSA1));
    const uint8 *sa1r_data  = FindBlock (blocks, count, "SA1R", sizeof (SA1.Registers));
    const uint8 *obc1_data  = FindBlock (blocks, count, "OBC1", sizeof (struct SnapOBC1));

    if (!cpu_data || !regs_data || !ppu_data || !dma_data || !ippu_data ||
	!vram_data || !wram_data || !sram_data || !fill_data)
	return FALSE;
    if ((Settings.C4 && !c4_data) || (Settings.SA1 && (!sa1_data || !sa1r_data)) ||
	(Settings.OBC1 && !obc1_data))
	return FALSE;

    /* Everything is there; from here on the running game is replaced. */
    S9xReset ();

    struct SnapCPU cpu;
    memcpy (&cpu, cpu_data, sizeof (cpu));
    CPU.Flags = cpu.Flags;
    CPU.BranchSkip = cpu.BranchSkip;
    CPU.NMIActive = cpu.NMIActive;
    CPU.IRQActive = cpu.IRQActive;
    CPU.WaitingForInterrupt = cpu.WaitingForInterrupt;
    CPU.WhichEvent = cpu.WhichEvent;
    CPU.SRAMModified = cpu.SRAMModified;
    CPU.BRKTriggered = cpu.BRKTriggered;
    CPU.Cycles = cpu.Cycles;
    CPU.NextEvent = cpu.NextEvent;
    CPU.V_Counter = cpu.V_Counter;
    CPU.MemSpeed = cpu.MemSpeed;
    CPU.MemSpeedx2 = cpu.MemSpeedx2;
    CPU.FastROMSpeed = cpu.FastROMSpeed;
    CPU.WaitCounter = cpu.WaitCounter;
    CPU.AutoSaveTimer = cpu.AutoSaveTimer;
    CPU.NMITriggerPoint = cpu.NMITriggerPoint;
    CPU.NMICycleCount = cpu.NMICycleCount;
    CPU.IRQCycleCount = cpu.IRQCycleCount;
    CPU.InDMA = FALSE;

    memcpy (&ICPU.Registers, regs_data, sizeof (ICPU.Registers));
    memcpy (&PPU, ppu_data, sizeof (PPU));
    memcpy (DMA, dma_data, sizeof (DMA));
    memcpy (Memory.VRAM, vram_data, 0x10000);
    memcpy (Memory.RAM, wram_data, 0x20000);
    memcpy (Memory.SRAM, sram_data, 0x20000);
    memcpy (Memory.FillRAM, fill_data, 0x8000);

    struct SnapIPPU ippu;
    memcpy (&ippu, ippu_data, sizeof (ippu));
    IPPU.HDMA = ippu.HDMA;
    IPPU.HDMAStarted = ippu.HDMAStarted;
    IPPU.MaxBrightness = ippu.MaxBrightness;
    IPPU.LatchedBlanking = ippu.LatchedBlanking;
    IPPU.Interlace = ippu.Interlace;
#ifdef CORRECT_VRAM_READS
    IPPU.VRAMReadBuffer = ippu.VRAMReadBuffer;
#else
    IPPU.FirstVRAMRead = ippu.FirstVRAMRead;
#endif

    if (Settings.C4)
	memcpy (Memory.C4RAM, c4_data, 0x2000);

    if (Settings.DSP1Master && dsp1_data)
	memcpy (&DSP1, dsp1_data, sizeof (DSP1));

    if (Settings.OBC1)
    {
	struct SnapOBC1 obc1;
	memcpy (&obc1, obc1_data, sizeof (obc1));
	OBC1_Address = obc1.Address;
	OBC1_BasePtr = obc1.BasePtr;
	OBC1_Shift = obc1.Shift;
    }

    Memory.FixROMSpeed ();

    /* Everything derived from VRAM, CGRAM and OAM has to be rebuilt. */
    IPPU.ColorsChanged = TRUE;
    IPPU.OBJChanged = TRUE;
    IPPU.DirectColourMapsNeedRebuild = TRUE;
    PPU.RecomputeClipWindows = TRUE;
    memset (IPPU.TileCached [TILE_2BIT], 0, MAX_2BIT_TILES);
    memset (IPPU.TileCached [TILE_4BIT], 0, MAX_4BIT_TILES);
    memset (IPPU.TileCached [TILE_8BIT], 0, MAX_8BIT_TILES);
    S9xFixColourBrightness ();

    if (Settings.SA1)
    {
	struct SnapSA1 sa1;
	memcpy (&sa1, sa1_data, sizeof (sa1));
	SA1.Flags = sa1.Flags;
	SA1.NMIActive = sa1.NMIActive;
	SA1.IRQActive = sa1.IRQActive;
	SA1.WaitingForInterrupt = sa1.WaitingForInterrupt;
	SA1.overflow = sa1.overflow;
	SA1.in_char_dma = sa1.in_char_dma;
	SA1.variable_bit_pos = sa1.variable_bit_pos;
	SA1.op1 = sa1.op1;
	SA1.op2 = sa1.op2;
	SA1.arithmetic_op = sa1.arithmetic_op;
	SA1.WaitCounter = sa1.WaitCounter;
	SA1.sum = sa1.sum;
	memcpy (&SA1.Registers, sa1r_data, sizeof (SA1.Registers));

	/* Rebuilds PC, BW-RAM and the Executing/Waiting flags from the
	 * registers in FillRAM. Snes9x 1.43 stopped there, which left the
	 * SA-1 ROM bank switching (Super MMC) as it was before the load. */
	S9xFixSA1AfterSnapshotLoad ();
	for (int i = 0; i < 4; i++)
	    S9xSetSA1MemMap (i, Memory.FillRAM [0x2220 + i]);
    }

    ICPU.ShiftedPB = ICPU.Registers.PB << 16;
    ICPU.ShiftedDB = ICPU.Registers.DB << 16;
    S9xSetPCBase (ICPU.ShiftedPB + ICPU.Registers.PC);
    S9xUnpackStatus ();
    S9xFixCycles ();

    if (Settings.SRTC)
	S9xSRTCPostLoadState ();

    return TRUE;
}
