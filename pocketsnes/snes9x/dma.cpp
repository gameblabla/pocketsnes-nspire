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
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "snes9x.h"

#include "memmap.h"
#include "ppu.h"
#include "cpuexec.h"
#include "missing.h"
#include "dma.h"
#include "apu.h"
#include "gfx.h"
#include "sa1.h"
#include "spc7110.h"

#ifdef SDD1_DECOMP
#include "sdd1emu.h"
#endif

#ifdef SDD1_DECOMP
uint8 buffer[0x10000];
#endif

extern int HDMA_ModeByteCounts [8];
extern uint8 *HDMAMemPointers [8];
extern uint8 *HDMABasePointers [8];

// #define SETA010_HDMA_FROM_CART

#ifdef SETA010_HDMA_FROM_CART
uint32 HDMARawPointers[8];	// Cart address space pointer
#endif

#if defined(__linux__) || defined(__WIN32__)
static int S9xCompareSDD1IndexEntries (const void *p1, const void *p2)
{
    return (*(uint32 *) p1 - *(uint32 *) p2);
}
#endif

/**********************************************************************************************/
/* S9xDoDMA()                                                                                   */
/* This function preforms the general dma transfer                                            */
/**********************************************************************************************/

void S9xDoDMA (uint8 Channel)
{
    uint8 Work;
	
    if (Channel > 7 || CPU.InDMA)
		return;
	
    CPU.InDMA = TRUE;
    bool8 in_sa1_dma = FALSE;
    uint8 *in_sdd1_dma = NULL;
	uint8 *spc7110_dma=NULL;
	bool s7_wrap=false;
    SDMA *d = &DMA[Channel];
	

    int count = d->TransferBytes;
	
    if (count == 0)
		count = 0x10000;
	
    int inc = d->AAddressFixed ? 0 : (!d->AAddressDecrement ? 1 : -1);

	if((d->ABank==0x7E||d->ABank==0x7F)&&d->BAddress==0x80)
	{
		d->AAddress+= d->TransferBytes;
		//does an invalid DMA actually take time?
		// I'd say yes, since 'invalid' is probably just the WRAM chip
		// not being able to read and write itself at the same time
		CPU.Cycles+=(d->TransferBytes+1)*SLOW_ONE_CYCLE;
		goto update_address;
	}
    switch (d->BAddress)
    {
    case 0x18:
    case 0x19:
		if (IPPU.RenderThisFrame)
			FLUSH_REDRAW ();
		break;
    }
    // S-DD1
#ifdef SDD1_DECOMP
    if (Settings.SDD1)
    {
		if (d->AAddressFixed && Memory.FillRAM [0x4801] > 0)
		{
			// XXX: Should probably verify that we're DMAing from ROM?
			// And somewhere we should make sure we're not running across a mapping boundary too.
			inc = !d->AAddressDecrement ? 1 : -1;

			uint8	*in_ptr = GetBasePointer(((d->ABank << 16) | d->AAddress));
			if (in_ptr)
			{
				in_ptr += d->AAddress;
				SDD1_decompress(buffer, in_ptr, d->TransferBytes);
			}
			in_sdd1_dma = buffer;
		}

		Memory.FillRAM [0x4801] = 0;
    }
#endif

	if(Settings.SPC7110&&(d->AAddress==0x4800||d->ABank==0x50))
	{
		uint32 i,j;
		i%=DECOMP_BUFFER_SIZE;
		j=0;
		
		spc7110_dma=new uint8[d->TransferBytes];
		j=DECOMP_BUFFER_SIZE-i;
		s7_wrap=true;
		
		/*int icount=s7r.reg4809|(s7r.reg480A<<8);
		icount-=d->TransferBytes;
		s7r.reg4809=0x00ff&icount;
		s7r.reg480A=(0xff00&icount)>>8;*/

		/*s7r.bank50Internal+=d->TransferBytes;
		s7r.bank50Internal%=DECOMP_BUFFER_SIZE;*/
		inc=1;
		d->AAddress-=count;
	}
    if (d->BAddress == 0x18 && SA1.in_char_dma && (d->ABank & 0xf0) == 0x40)
    {
		// Perform packed bitmap to PPU character format conversion on the
		// data before transmitting it to V-RAM via-DMA.
		int num_chars = 1 << ((Memory.FillRAM [0x2231] >> 2) & 7);
		int depth = (Memory.FillRAM [0x2231] & 3) == 0 ? 8 :
		(Memory.FillRAM [0x2231] & 3) == 1 ? 4 : 2;
		
		int bytes_per_char = 8 * depth;
		int bytes_per_line = depth * num_chars;
		int char_line_bytes = bytes_per_char * num_chars;
		uint32 addr = (d->AAddress / char_line_bytes) * char_line_bytes;
		uint8 *base = GetBasePointer ((d->ABank << 16) + addr) + addr;
		uint8 *buffer = &Memory.ROM [CMemory::MAX_ROM_SIZE - 0x10000];
		uint8 *p = buffer;
		uint32 inc = char_line_bytes - (d->AAddress % char_line_bytes);
		uint32 char_count = inc / bytes_per_char;
		
		in_sa1_dma = TRUE;
		
		//printf ("%08x,", base); fflush (stdout);
		//printf ("depth = %d, count = %d, bytes_per_char = %d, bytes_per_line = %d, num_chars = %d, char_line_bytes = %d\n",
		//depth, count, bytes_per_char, bytes_per_line, num_chars, char_line_bytes);
		int i;
		
		switch (depth)
		{
		case 2:
			for (i = 0; i < count; i += inc, base += char_line_bytes, 
				inc = char_line_bytes, char_count = num_chars)
			{
				uint8 *line = base + (num_chars - char_count) * 2;
				for (uint32 j = 0; j < char_count && p - buffer < count; 
				j++, line += 2)
				{
					uint8 *q = line;
					for (int l = 0; l < 8; l++, q += bytes_per_line)
					{
						for (int b = 0; b < 2; b++)
						{
							uint8 r = *(q + b);
							*(p + 0) = (*(p + 0) << 1) | ((r >> 0) & 1);
							*(p + 1) = (*(p + 1) << 1) | ((r >> 1) & 1);
							*(p + 0) = (*(p + 0) << 1) | ((r >> 2) & 1);
							*(p + 1) = (*(p + 1) << 1) | ((r >> 3) & 1);
							*(p + 0) = (*(p + 0) << 1) | ((r >> 4) & 1);
							*(p + 1) = (*(p + 1) << 1) | ((r >> 5) & 1);
							*(p + 0) = (*(p + 0) << 1) | ((r >> 6) & 1);
							*(p + 1) = (*(p + 1) << 1) | ((r >> 7) & 1);
						}
						p += 2;
					}
				}
			}
			break;
		case 4:
			for (i = 0; i < count; i += inc, base += char_line_bytes, 
				inc = char_line_bytes, char_count = num_chars)
			{
				uint8 *line = base + (num_chars - char_count) * 4;
				for (uint32 j = 0; j < char_count && p - buffer < count; 
				j++, line += 4)
				{
					uint8 *q = line;
					for (int l = 0; l < 8; l++, q += bytes_per_line)
					{
						for (int b = 0; b < 4; b++)
						{
							uint8 r = *(q + b);
							*(p +  0) = (*(p +  0) << 1) | ((r >> 0) & 1);
							*(p +  1) = (*(p +  1) << 1) | ((r >> 1) & 1);
							*(p + 16) = (*(p + 16) << 1) | ((r >> 2) & 1);
							*(p + 17) = (*(p + 17) << 1) | ((r >> 3) & 1);
							*(p +  0) = (*(p +  0) << 1) | ((r >> 4) & 1);
							*(p +  1) = (*(p +  1) << 1) | ((r >> 5) & 1);
							*(p + 16) = (*(p + 16) << 1) | ((r >> 6) & 1);
							*(p + 17) = (*(p + 17) << 1) | ((r >> 7) & 1);
						}
						p += 2;
					}
					p += 32 - 16;
				}
			}
			break;
		case 8:
			for (i = 0; i < count; i += inc, base += char_line_bytes, 
				inc = char_line_bytes, char_count = num_chars)
			{
				uint8 *line = base + (num_chars - char_count) * 8;
				for (uint32 j = 0; j < char_count && p - buffer < count; 
				j++, line += 8)
				{
					uint8 *q = line;
					for (int l = 0; l < 8; l++, q += bytes_per_line)
					{
						for (int b = 0; b < 8; b++)
						{
							uint8 r = *(q + b);
							*(p +  0) = (*(p +  0) << 1) | ((r >> 0) & 1);
							*(p +  1) = (*(p +  1) << 1) | ((r >> 1) & 1);
							*(p + 16) = (*(p + 16) << 1) | ((r >> 2) & 1);
							*(p + 17) = (*(p + 17) << 1) | ((r >> 3) & 1);
							*(p + 32) = (*(p + 32) << 1) | ((r >> 4) & 1);
							*(p + 33) = (*(p + 33) << 1) | ((r >> 5) & 1);
							*(p + 48) = (*(p + 48) << 1) | ((r >> 6) & 1);
							*(p + 49) = (*(p + 49) << 1) | ((r >> 7) & 1);
						}
						p += 2;
					}
					p += 64 - 16;
				}
			}
			break;
		}
    }
	
#ifdef DEBUGGER
    if (Settings.TraceDMA)
    {
		sprintf (String_text, "DMA[%d]: %s Mode: %d 0x%02X%04X->0x21%02X Bytes: %d (%s) V-Line:%ld",
			Channel, d->TransferDirection ? "read" : "write",
			d->TransferMode, d->ABank, d->AAddress,
			d->BAddress, d->TransferBytes,
			d->AAddressFixed ? "fixed" :
		(d->AAddressDecrement ? "dec" : "inc"),
			CPU.V_Counter);
		if (d->BAddress == 0x18 || d->BAddress == 0x19 || d->BAddress == 0x39 || d->BAddress == 0x3a)
			sprintf (String_text, "%s VRAM: %04X (%d,%d) %s", String_text,
				PPU.VMA.Address,
				PPU.VMA.Increment, PPU.VMA.FullGraphicCount,
				PPU.VMA.High ? "word" : "byte");

		else
			if (d->BAddress == 0x22 || d->BAddress == 0x3b)
			
				sprintf (String_text, "%s CGRAM: %02X (%x)", String_text, PPU.CGADD,
					PPU.CGFLIP);			
			else
				if (d->BAddress == 0x04 || d->BAddress == 0x38)
					sprintf (String_text, "%s OBJADDR: %04X", String_text, PPU.OAMAddr);
				S9xMessage (S9X_TRACE, S9X_DMA_TRACE, String_text);
    }
#endif
	
    if (!d->TransferDirection)
    {
		/* XXX: DMA is potentially broken here for cases where we DMA across
		 * XXX: memmap boundries. A possible solution would be to re-call
		 * XXX: GetBasePointer whenever we cross a boundry, and when
		 * XXX: GetBasePointer returns (0) to take the 'slow path' and use
		 * XXX: S9xGetByte instead of *base. GetBasePointer() would want to
		 * XXX: return (0) for MAP_PPU and whatever else is a register range
		 * XXX: rather than a RAM/ROM block, and we'd want to detect MAP_PPU
		 * XXX: (or specifically, Address Bus B addresses $2100-$21FF in
		 * XXX: banks $00-$3F) specially and treat it as MAP_NONE (since
		 * XXX: PPU->PPU transfers don't work).
		 */

		//reflects extra cycle used by DMA
		CPU.Cycles += SLOW_ONE_CYCLE * (count+1);

		uint8 *base = GetBasePointer ((d->ABank << 16) + d->AAddress);
		uint16 p = d->AAddress;
		
		if (!base)
			base = Memory.ROM;
		
		if (in_sa1_dma)
		{
			base = &Memory.ROM [CMemory::MAX_ROM_SIZE - 0x10000];
			p = 0;
		}
		
		if (in_sdd1_dma)
		{
			base = in_sdd1_dma;
			p = 0;
		}
		if(spc7110_dma)
		{
			base=spc7110_dma;
			p = 0;
		}
		if (inc > 0)
			d->AAddress += count;
		else
			if (inc < 0)
				d->AAddress -= count;
			
			if (d->TransferMode == 0 || d->TransferMode == 2 || d->TransferMode == 6)
			{
				switch (d->BAddress)
				{
				case 0x04:
					do
					{
						Work = *(base + p);
						REGISTER_2104(Work);
						p += inc;
						CHECK_SOUND();
					} while (--count > 0);
					break;
				case 0x18:
#ifndef CORRECT_VRAM_READS
					IPPU.FirstVRAMRead = TRUE;
#endif
					if (!PPU.VMA.FullGraphicCount)
					{
						do
						{
							Work = *(base + p);
							REGISTER_2118_linear(Work);
							p += inc;
							CHECK_SOUND();
						} while (--count > 0);
					}
					else
					{
						do
						{
							Work = *(base + p);
							REGISTER_2118_tile(Work);
							p += inc;
							CHECK_SOUND();
						} while (--count > 0);
					}
					break;
				case 0x19:
#ifndef CORRECT_VRAM_READS
					IPPU.FirstVRAMRead = TRUE;
#endif
					if (!PPU.VMA.FullGraphicCount)
					{
						do
						{
							Work = *(base + p);
							REGISTER_2119_linear(Work);
							p += inc;
							CHECK_SOUND();
						} while (--count > 0);
					}
					else
					{
						do
						{
							Work = *(base + p);
							REGISTER_2119_tile(Work);
							p += inc;
							CHECK_SOUND();
						} while (--count > 0);
					}
					break;
				case 0x22:
					do
					{
						Work = *(base + p);
						REGISTER_2122(Work);
						p += inc;
						CHECK_SOUND();
					} while (--count > 0);
					break;
				case 0x80:
					do
					{
						Work = *(base + p);
						REGISTER_2180(Work);
						p += inc;
						CHECK_SOUND();
					} while (--count > 0);
					break;
				default:
					do
					{
						Work = *(base + p);
						S9xSetPPU (Work, 0x2100 + d->BAddress);
						p += inc;
						CHECK_SOUND();
					} while (--count > 0);
					break;
				}
			}
			else
				if (d->TransferMode == 1 || d->TransferMode == 5)
				{
					if (d->BAddress == 0x18)
					{
						// Write to V-RAM
#ifndef CORRECT_VRAM_READS
						IPPU.FirstVRAMRead = TRUE;
#endif
						if (!PPU.VMA.FullGraphicCount)
						{
							while (count > 1)
							{
								Work = *(base + p);
								REGISTER_2118_linear(Work);
								p += inc;
								
								Work = *(base + p);
								REGISTER_2119_linear(Work);
								p += inc;
								CHECK_SOUND();
								count -= 2;
							}
							if (count == 1)
							{
								Work = *(base + p);
								REGISTER_2118_linear(Work);
								p += inc;
							}
						}
						else
						{
							while (count > 1)
							{
								Work = *(base + p);
								REGISTER_2118_tile(Work);
								p += inc;
								
								Work = *(base + p);
								REGISTER_2119_tile(Work);
								p += inc;
								CHECK_SOUND();
								count -= 2;
							}
							if (count == 1)
							{
								Work = *(base + p);
								REGISTER_2118_tile(Work);
								p += inc;
							}
						}
					}
					else
					{
						// DMA mode 1 general case
						while (count > 1)
						{
							Work = *(base + p);
							S9xSetPPU (Work, 0x2100 + d->BAddress);
							p += inc;
							
							Work = *(base + p);
							S9xSetPPU (Work, 0x2101 + d->BAddress);
							p += inc;
							CHECK_SOUND();
							count -= 2;
						}
						if (count == 1)
						{
							Work = *(base + p);
							S9xSetPPU (Work, 0x2100 + d->BAddress);
							p += inc;
						}
					}
				}
				else
					if (d->TransferMode == 3 || d->TransferMode == 7)
					{
						do
						{
							Work = *(base + p);
							S9xSetPPU (Work, 0x2100 + d->BAddress);
							p += inc;
							if (count <= 1)
								break;
							
							Work = *(base + p);
							S9xSetPPU (Work, 0x2100 + d->BAddress);
							p += inc;
							if (count <= 2)
								break;
							
							Work = *(base + p);
							S9xSetPPU (Work, 0x2101 + d->BAddress);
							p += inc;
							if (count <= 3)
								break;
							
							Work = *(base + p);
							S9xSetPPU (Work, 0x2101 + d->BAddress);
							p += inc;
							CHECK_SOUND();
							count -= 4;
						} while (count > 0);
					}
					else
						if (d->TransferMode == 4)
						{
							do
							{
								Work = *(base + p);
								S9xSetPPU (Work, 0x2100 + d->BAddress);
								p += inc;
								if (count <= 1)
									break;
								
								Work = *(base + p);
								S9xSetPPU (Work, 0x2101 + d->BAddress);
								p += inc;
								if (count <= 2)
									break;
								
								Work = *(base + p);
								S9xSetPPU (Work, 0x2102 + d->BAddress);
								p += inc;
								if (count <= 3)
									break;
								
								Work = *(base + p);
								S9xSetPPU (Work, 0x2103 + d->BAddress);
								p += inc;
								CHECK_SOUND();
								count -= 4;
							} while (count > 0);
						}
						else
						{
#ifdef DEBUGGER
							//	    if (Settings.TraceDMA)
							{
								sprintf (String_text, "Unknown DMA transfer mode: %d on channel %d\n",
									d->TransferMode, Channel);
								S9xMessage (S9X_TRACE, S9X_DMA_TRACE, String_text);
							}
#endif
						}
    }
    else
    {
		/* XXX: DMA is potentially broken here for cases where the dest is
		 * XXX: in the Address Bus B range. Note that this bad dest may not
		 * XXX: cover the whole range of the DMA though, if we transfer
		 * XXX: 65536 bytes only 256 of them may be Address Bus B.
		 */
		do
		{
			switch (d->TransferMode)
			{
			case 0:
			case 2:
			case 6:
				Work = S9xGetPPU (0x2100 + d->BAddress);
				S9xSetByte (Work, (d->ABank << 16) + d->AAddress);
				d->AAddress += inc;
				--count;
				break;
				
			case 1:
			case 5:
				Work = S9xGetPPU (0x2100 + d->BAddress);
				S9xSetByte (Work, (d->ABank << 16) + d->AAddress);
				d->AAddress += inc;
				if (!--count)
					break;
				
				Work = S9xGetPPU (0x2101 + d->BAddress);
				S9xSetByte (Work, (d->ABank << 16) + d->AAddress);
				d->AAddress += inc;
				count--;
				break;
				
			case 3:
			case 7:
				Work = S9xGetPPU (0x2100 + d->BAddress);
				S9xSetByte (Work, (d->ABank << 16) + d->AAddress);
				d->AAddress += inc;
				if (!--count)
					break;
				
				Work = S9xGetPPU (0x2100 + d->BAddress);
				S9xSetByte (Work, (d->ABank << 16) + d->AAddress);
				d->AAddress += inc;
				if (!--count)
					break;
				
				Work = S9xGetPPU (0x2101 + d->BAddress);
				S9xSetByte (Work, (d->ABank << 16) + d->AAddress);
				d->AAddress += inc;
				if (!--count)
					break;
				
				Work = S9xGetPPU (0x2101 + d->BAddress);
				S9xSetByte (Work, (d->ABank << 16) + d->AAddress);
				d->AAddress += inc;
				count--;
				break;
				
			case 4:
				Work = S9xGetPPU (0x2100 + d->BAddress);
				S9xSetByte (Work, (d->ABank << 16) + d->AAddress);
				d->AAddress += inc;
				if (!--count)
					break;
				
				Work = S9xGetPPU (0x2101 + d->BAddress);
				S9xSetByte (Work, (d->ABank << 16) + d->AAddress);
				d->AAddress += inc;
				if (!--count)
					break;
				
				Work = S9xGetPPU (0x2102 + d->BAddress);
				S9xSetByte (Work, (d->ABank << 16) + d->AAddress);
				d->AAddress += inc;
				if (!--count)
					break;
				
				Work = S9xGetPPU (0x2103 + d->BAddress);
				S9xSetByte (Work, (d->ABank << 16) + d->AAddress);
				d->AAddress += inc;
				count--;
				break;
				
			default:
#ifdef DEBUGGER
				if (1) //Settings.TraceDMA)
				{
					sprintf (String_text, "Unknown DMA transfer mode: %d on channel %d\n",
						d->TransferMode, Channel);
					S9xMessage (S9X_TRACE, S9X_DMA_TRACE, String_text);
				}
#endif
				count = 0;
				break;
			}
			CHECK_SOUND();
		} while (count);
    }
    
#ifdef SPC700_C
    IAPU.APUExecuting = Settings.APUEnabled;
    APU_EXECUTE ();
#endif
	if (Settings.SuperFX)
		while (CPU.Cycles > CPU.NextEvent)
			S9xDoHBlankProcessing_SFX ();
	else /* if (!Settings.SuperFX) */
		while (CPU.Cycles > CPU.NextEvent)
			S9xDoHBlankProcessing_NoSFX ();

	if(Settings.SPC7110&&spc7110_dma)
	{
		if(spc7110_dma&&s7_wrap)
			delete [] spc7110_dma;
	}

update_address:
    // Super Punch-Out requires that the A-BUS address be updated after the
    // DMA transfer.
    Memory.FillRAM[0x4302 + (Channel << 4)] = (uint8) d->AAddress;
    Memory.FillRAM[0x4303 + (Channel << 4)] = d->AAddress >> 8;
	
    // Secret of the Mana requires that the DMA bytes transfer count be set to
    // zero when DMA has completed.
    Memory.FillRAM [0x4305 + (Channel << 4)] = 0;
    Memory.FillRAM [0x4306 + (Channel << 4)] = 0;
	
    DMA[Channel].IndirectAddress = 0;
    d->TransferBytes = 0;
    
    CPU.InDMA = FALSE;


}

void S9xStartHDMA ()
{
    if (Settings.DisableHDMA)
		IPPU.HDMA = 0;
    else
		missing.hdma_this_frame = IPPU.HDMA = Memory.FillRAM [0x420c];
	
	//per anomie timing post
	if(IPPU.HDMA!=0)
		CPU.Cycles+=ONE_CYCLE*3;
    
	IPPU.HDMAStarted = TRUE;

    for (uint8 i = 0; i < 8; i++)
    {
		if (IPPU.HDMA & (1 << i))
		{
			CPU.Cycles+=SLOW_ONE_CYCLE ;
			DMA [i].LineCount = 0;
			DMA [i].FirstLine = TRUE;
			DMA [i].Address = DMA [i].AAddress;
			if(DMA[i].HDMAIndirectAddressing)
				CPU.Cycles+=(SLOW_ONE_CYCLE <<2);
		}
		HDMAMemPointers [i] = NULL;
#ifdef SETA010_HDMA_FROM_CART
		HDMARawPointers [i] = 0;
#endif
    }
}

#ifdef DEBUGGER
void S9xTraceSoundDSP (const char *s, int i1 = 0, int i2 = 0, int i3 = 0,
					   int i4 = 0, int i5 = 0, int i6 = 0, int i7 = 0);
#endif


uint8 S9xDoHDMA (uint8 byte)
{
    struct SDMA *p = &DMA [0];
    
    int d = 0;

	CPU.InDMA = TRUE;
	CPU.Cycles+=ONE_CYCLE*3;
    for (uint8 mask = 1; mask; mask <<= 1, p++, d++)
	{
		if (byte & mask)
		{
			if (!p->LineCount)
			{
				//remember, InDMA is set.
				//Get/Set incur no charges!
				CPU.Cycles+=SLOW_ONE_CYCLE;
				uint8 line = S9xGetByte ((p->ABank << 16) + p->Address);
				if (line == 0x80)
				{
					p->Repeat = TRUE;
					p->LineCount = 128;
				}
				else
				{
					p->Repeat = !(line & 0x80);
					p->LineCount = line & 0x7f;
				}

				// Disable H-DMA'ing into V-RAM (register 2118) for Hook
				/* XXX: instead of p->BAddress == 0x18, make S9xSetPPU fail
				 * XXX: writes to $2118/9 when appropriate
				 */
#ifdef SETA010_HDMA_FROM_CART
				if (!p->LineCount)
#else
				if (!p->LineCount || p->BAddress == 0x18)
#endif
				{
					byte &= ~mask;
					p->IndirectAddress += HDMAMemPointers [d] - HDMABasePointers [d];
					Memory.FillRAM [0x4305 + (d << 4)] = (uint8) p->IndirectAddress;
					Memory.FillRAM [0x4306 + (d << 4)] = p->IndirectAddress >> 8;
					continue;
				}

				p->Address++;
				p->FirstLine = 1;
				if (p->HDMAIndirectAddressing)
				{
					p->IndirectBank = Memory.FillRAM [0x4307 + (d << 4)];
					//again, no cycle charges while InDMA is set!
					CPU.Cycles+=SLOW_ONE_CYCLE<<2;
					p->IndirectAddress = S9xGetWord ((p->ABank << 16) + p->Address);
					p->Address += 2;
				}
				else
				{
					p->IndirectBank = p->ABank;
					p->IndirectAddress = p->Address;
				}
				HDMABasePointers [d] = HDMAMemPointers [d] = 
					S9xGetMemPointer ((p->IndirectBank << 16) + p->IndirectAddress);
#ifdef SETA010_HDMA_FROM_CART
				HDMARawPointers [d] = (p->IndirectBank << 16) + p->IndirectAddress;
#endif
			}
			else
			{
				CPU.Cycles += SLOW_ONE_CYCLE;
			}

			if (!HDMAMemPointers [d])
			{
				if (!p->HDMAIndirectAddressing)
				{
					p->IndirectBank = p->ABank;
					p->IndirectAddress = p->Address;
				}
#ifdef SETA010_HDMA_FROM_CART
				HDMARawPointers [d] = (p->IndirectBank << 16) + p->IndirectAddress;
#endif
				if (!(HDMABasePointers [d] = HDMAMemPointers [d] = 
					  S9xGetMemPointer ((p->IndirectBank << 16) + p->IndirectAddress)))
				{
					/* XXX: Instead of this, goto a slow path that first
					 * XXX: verifies src!=Address Bus B, then uses
					 * XXX: S9xGetByte(). Or make S9xGetByte return OpenBus
					 * XXX: (probably?) for Address Bus B while inDMA.
					 */
					byte &= ~mask;
					continue;
				}
				// Uncommenting the following line breaks Punchout - it starts
				// H-DMA during the frame.
				//p->FirstLine = TRUE;
			}
			if (p->Repeat && !p->FirstLine)
			{
				p->LineCount--;
				continue;
			}

			if (p->BAddress == 0x04){
				if(SNESGameFixes.Uniracers){
					PPU.OAMAddr = 0x10c;
					PPU.OAMFlip=0;
				}
			}

#ifdef DEBUGGER
			if (Settings.TraceSoundDSP && p->FirstLine && 
					p->BAddress >= 0x40 && p->BAddress <= 0x43)
				S9xTraceSoundDSP ("Spooling data!!!\n");
			if (Settings.TraceHDMA && p->FirstLine)
			{
				sprintf (String_text, "H-DMA[%d] (%d) 0x%02X%04X->0x21%02X %s, Count: %3d, Rep: %s, V-LINE: %3ld %02X%04X",
						p-DMA, p->TransferMode, p->IndirectBank,
						p->IndirectAddress,
						p->BAddress,
						p->HDMAIndirectAddressing ? "ind" : "abs",
						p->LineCount,
						p->Repeat ? "yes" : "no ", CPU.V_Counter,
						p->ABank, p->Address);
				S9xMessage (S9X_TRACE, S9X_HDMA_TRACE, String_text);
			}
#endif

			switch (p->TransferMode)
			{
				case 0:
					CPU.Cycles += SLOW_ONE_CYCLE;
#ifdef SETA010_HDMA_FROM_CART
					S9xSetPPU (S9xGetByte (HDMARawPointers [d]++), 0x2100 + p->BAddress);
					HDMAMemPointers [d]++;
#else
					S9xSetPPU (*HDMAMemPointers [d]++, 0x2100 + p->BAddress);
#endif
					break;
				case 5:
					CPU.Cycles += 2*SLOW_ONE_CYCLE;
#ifdef SETA010_HDMA_FROM_CART
					S9xSetPPU (S9xGetByte (HDMARawPointers [d]), 0x2100 + p->BAddress);
					S9xSetPPU (S9xGetByte (HDMARawPointers [d] + 1), 0x2101 + p->BAddress);
					HDMARawPointers [d] += 2;
#else
					S9xSetPPU (*(HDMAMemPointers [d] + 0), 0x2100 + p->BAddress);
					S9xSetPPU (*(HDMAMemPointers [d] + 1), 0x2101 + p->BAddress);
#endif
					HDMAMemPointers [d] += 2;
					/* fall through */
				case 1:
					CPU.Cycles += 2*SLOW_ONE_CYCLE;
#ifdef SETA010_HDMA_FROM_CART
					S9xSetPPU (S9xGetByte (HDMARawPointers [d]), 0x2100 + p->BAddress);
					S9xSetPPU (S9xGetByte (HDMARawPointers [d] + 1), 0x2101 + p->BAddress);
					HDMARawPointers [d] += 2;
#else
					S9xSetPPU (*(HDMAMemPointers [d] + 0), 0x2100 + p->BAddress);
					S9xSetPPU (*(HDMAMemPointers [d] + 1), 0x2101 + p->BAddress);
#endif
					HDMAMemPointers [d] += 2;
					break;
				case 2:
				case 6:
					CPU.Cycles += 2*SLOW_ONE_CYCLE;
#ifdef SETA010_HDMA_FROM_CART
					S9xSetPPU (S9xGetByte (HDMARawPointers [d]), 0x2100 + p->BAddress);
					S9xSetPPU (S9xGetByte (HDMARawPointers [d] + 1), 0x2100 + p->BAddress);
					HDMARawPointers [d] += 2;
#else
					S9xSetPPU (*(HDMAMemPointers [d] + 0), 0x2100 + p->BAddress);
					S9xSetPPU (*(HDMAMemPointers [d] + 1), 0x2100 + p->BAddress);
#endif
					HDMAMemPointers [d] += 2;
					break;
				case 3:
				case 7:
					CPU.Cycles += 4*SLOW_ONE_CYCLE;
#ifdef SETA010_HDMA_FROM_CART
					S9xSetPPU (S9xGetByte (HDMARawPointers [d]), 0x2100 + p->BAddress);
					S9xSetPPU (S9xGetByte (HDMARawPointers [d] + 1), 0x2100 + p->BAddress);
					S9xSetPPU (S9xGetByte (HDMARawPointers [d] + 2), 0x2101 + p->BAddress);
					S9xSetPPU (S9xGetByte (HDMARawPointers [d] + 3), 0x2101 + p->BAddress);
					HDMARawPointers [d] += 4;
#else
					S9xSetPPU (*(HDMAMemPointers [d] + 0), 0x2100 + p->BAddress);
					S9xSetPPU (*(HDMAMemPointers [d] + 1), 0x2100 + p->BAddress);
					S9xSetPPU (*(HDMAMemPointers [d] + 2), 0x2101 + p->BAddress);
					S9xSetPPU (*(HDMAMemPointers [d] + 3), 0x2101 + p->BAddress);
#endif
					HDMAMemPointers [d] += 4;
					break;
				case 4:
					CPU.Cycles += 4*SLOW_ONE_CYCLE;
#ifdef SETA010_HDMA_FROM_CART
					S9xSetPPU (S9xGetByte (HDMARawPointers [d]), 0x2100 + p->BAddress);
					S9xSetPPU (S9xGetByte (HDMARawPointers [d] + 1), 0x2101 + p->BAddress);
					S9xSetPPU (S9xGetByte (HDMARawPointers [d] + 2), 0x2102 + p->BAddress);
					S9xSetPPU (S9xGetByte (HDMARawPointers [d] + 3), 0x2103 + p->BAddress);
					HDMARawPointers [d] += 4;
#else
					S9xSetPPU (*(HDMAMemPointers [d] + 0), 0x2100 + p->BAddress);
					S9xSetPPU (*(HDMAMemPointers [d] + 1), 0x2101 + p->BAddress);
					S9xSetPPU (*(HDMAMemPointers [d] + 2), 0x2102 + p->BAddress);
					S9xSetPPU (*(HDMAMemPointers [d] + 3), 0x2103 + p->BAddress);
#endif
					HDMAMemPointers [d] += 4;
					break;
			}
			if (!p->HDMAIndirectAddressing)
				p->Address += HDMA_ModeByteCounts [p->TransferMode];
			p->IndirectAddress += HDMA_ModeByteCounts [p->TransferMode];
			/* XXX: Check for p->IndirectAddress crossing a mapping boundry,
			 * XXX: and invalidate HDMAMemPointers[d]
			 */
			p->FirstLine = FALSE;
			p->LineCount--;
		}
	}
	CPU.InDMA=FALSE;
    return (byte);
}

void S9xResetDMA ()
{
    int d;
    for (d = 0; d < 8; d++)
    {
		DMA [d].TransferDirection = FALSE;
		DMA [d].HDMAIndirectAddressing = FALSE;
		DMA [d].AAddressFixed = TRUE;
		DMA [d].AAddressDecrement = FALSE;
		DMA [d].TransferMode = 0xff;
		DMA [d].ABank = 0xff;
		DMA [d].AAddress = 0xffff;
		DMA [d].Address = 0xffff;
		DMA [d].BAddress = 0xff;
		DMA [d].TransferBytes = 0xffff;
    }
    for (int c = 0x4300; c < 0x4380; c += 0x10)
    {
		for (d = c; d < c + 12; d++)
			Memory.FillRAM [d] = 0xff;
		
		Memory.FillRAM [c + 0xf] = 0xff;
    }
}
