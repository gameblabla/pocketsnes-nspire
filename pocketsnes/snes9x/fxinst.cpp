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
#define FX_DO_ROMBUFFER

#ifdef ARM_ASM_FX
#include "snes9x.h"	/* Settings.AsmCPU */
#endif
#include "fxemu.h"
#include "fxinst.h"
#include <stddef.h>
#include <string.h>
#include <stdio.h>

extern struct FxRegs_s GSU;
int gsu_bank [512] = {0};

/* Set this define if you wish the plot instruction to check for y-pos limits */
/* (I don't think it's nessecary) */
#define CHECK_LIMITS

/* Codes used:
 *
 * rn   = a GSU register (r0-r15)
 * #n   = 4 bit immediate value
 * #pp  = 8 bit immediate value
 * (yy) = 8 bit word address (0x0000 - 0x01fe)
 * #xx  = 16 bit immediate value
 * (xx) = 16 bit address (0x0000 - 0xffff)
 *
 */

/* 00 - stop - stop GSU execution (and maybe generate an IRQ) */
static void fx_stop()
{
    CF(G);

    /* Check if we need to generate an IRQ */
    if(!(GSU.pvRegisters[GSU_CFGR] & 0x80))
	SF(IRQ);

    GSU.vPlotOptionReg = 0;
    GSU.vPipe = 1;
    CLRFLAGS;
    R15++;
}

/* 01 - nop - no operation */
static void fx_nop() { CLRFLAGS; R15++; }

extern void fx_flushCache();

/* 02 - cache - reintialize GSU cache */
static void fx_cache()
{
    uint32 c = R15 & 0xfff0;
    if(GSU.vCacheBaseReg != c || !GSU.bCacheActive)
    {
	fx_flushCache();
	GSU.vCacheBaseReg = c;
	GSU.bCacheActive = TRUE;	
    }
    R15++;
    CLRFLAGS;
}

/* 03 - lsr - logic shift right */
static void fx_lsr()
{
    uint32 v;
    GSU.vCarry = SREG & 1;
    v = USEX16(SREG) >> 1;
    R15++; DREG = v;
    GSU.vSign = v;
    GSU.vZero = v;
    TESTR14;
    CLRFLAGS;
}

/* 04 - rol - rotate left */
static void fx_rol()
{
    uint32 v = USEX16((SREG << 1) + GSU.vCarry);
    GSU.vCarry = (SREG >> 15) & 1;
    R15++; DREG = v;
    GSU.vSign = v;
    GSU.vZero = v;
    TESTR14;
    CLRFLAGS;
}

/* 05 - bra - branch always */
static void fx_bra() { uint8 v = PIPE; R15++; FETCHPIPE; R15 += SEX8(v); }

/* Branch on condition */
#define BRA_COND(cond) uint8 v = PIPE; R15++; FETCHPIPE; if(cond) R15 += SEX8(v); else R15++;

#define TEST_S (GSU.vSign & 0x8000)
#define TEST_Z (USEX16(GSU.vZero) == 0)
#define TEST_OV (GSU.vOverflow >= 0x8000 || GSU.vOverflow < -0x8000)
#define TEST_CY (GSU.vCarry & 1)

/* 06 - blt - branch on less than */
static void fx_blt() { BRA_COND( (TEST_S!=0) != (TEST_OV!=0) ); }

/* 07 - bge - branch on greater or equals */
static void fx_bge() { BRA_COND( (TEST_S!=0) == (TEST_OV!=0)); }

/* 08 - bne - branch on not equal */
static void fx_bne() { BRA_COND( !TEST_Z ); }

/* 09 - beq - branch on equal */
static void fx_beq() { BRA_COND( TEST_Z ); }

/* 0a - bpl - branch on plus */
static void fx_bpl() { BRA_COND( !TEST_S ); }

/* 0b - bmi - branch on minus */
static void fx_bmi() { BRA_COND( TEST_S ); }

/* 0c - bcc - branch on carry clear */
static void fx_bcc() { BRA_COND( !TEST_CY ); }

/* 0d - bcs - branch on carry set */
static void fx_bcs() { BRA_COND( TEST_CY ); }

/* 0e - bvc - branch on overflow clear */
static void fx_bvc() { BRA_COND( !TEST_OV ); }

/* 0f - bvs - branch on overflow set */
static void fx_bvs() { BRA_COND( TEST_OV ); }

/* 10-1f - to rn - set register n as destination register */
/* 10-1f(B) - move rn - move one register to another (if B flag is set) */
#define FX_TO(reg) \
if(TF(B)) { GSU.avReg[(reg)] = SREG; CLRFLAGS; } \
else { GSU.pvDreg = &GSU.avReg[reg]; } R15++;
#define FX_TO_R14(reg) \
if(TF(B)) { GSU.avReg[(reg)] = SREG; CLRFLAGS; READR14; } \
else { GSU.pvDreg = &GSU.avReg[reg]; } R15++;
#define FX_TO_R15(reg) \
if(TF(B)) { GSU.avReg[(reg)] = SREG; CLRFLAGS; } \
else { GSU.pvDreg = &GSU.avReg[reg]; R15++; }
static void fx_to_r0() { FX_TO(0); }
static void fx_to_r1() { FX_TO(1); }
static void fx_to_r2() { FX_TO(2); }
static void fx_to_r3() { FX_TO(3); }
static void fx_to_r4() { FX_TO(4); }
static void fx_to_r5() { FX_TO(5); }
static void fx_to_r6() { FX_TO(6); }
static void fx_to_r7() { FX_TO(7); }
static void fx_to_r8() { FX_TO(8); }
static void fx_to_r9() { FX_TO(9); }
static void fx_to_r10() { FX_TO(10); }
static void fx_to_r11() { FX_TO(11); }
static void fx_to_r12() { FX_TO(12); }
static void fx_to_r13() { FX_TO(13); }
static void fx_to_r14() { FX_TO_R14(14); }
static void fx_to_r15() { FX_TO_R15(15); }

/* 20-2f - to rn - set register n as source and destination register */
#define FX_WITH(reg) SF(B); GSU.pvSreg = GSU.pvDreg = &GSU.avReg[reg]; R15++;
static void fx_with_r0() { FX_WITH(0); }
static void fx_with_r1() { FX_WITH(1); }
static void fx_with_r2() { FX_WITH(2); }
static void fx_with_r3() { FX_WITH(3); }
static void fx_with_r4() { FX_WITH(4); }
static void fx_with_r5() { FX_WITH(5); }
static void fx_with_r6() { FX_WITH(6); }
static void fx_with_r7() { FX_WITH(7); }
static void fx_with_r8() { FX_WITH(8); }
static void fx_with_r9() { FX_WITH(9); }
static void fx_with_r10() { FX_WITH(10); }
static void fx_with_r11() { FX_WITH(11); }
static void fx_with_r12() { FX_WITH(12); }
static void fx_with_r13() { FX_WITH(13); }
static void fx_with_r14() { FX_WITH(14); }
static void fx_with_r15() { FX_WITH(15); }

/* 30-3b - stw (rn) - store word */
#define FX_STW(reg) \
GSU.vLastRamAdr = GSU.avReg[reg]; \
RAM(GSU.avReg[reg]) = (uint8)SREG; \
RAM(GSU.avReg[reg]^1) = (uint8)(SREG>>8); \
CLRFLAGS; R15++
static void fx_stw_r0() { FX_STW(0); }
static void fx_stw_r1() { FX_STW(1); }
static void fx_stw_r2() { FX_STW(2); }
static void fx_stw_r3() { FX_STW(3); }
static void fx_stw_r4() { FX_STW(4); }
static void fx_stw_r5() { FX_STW(5); }
static void fx_stw_r6() { FX_STW(6); }
static void fx_stw_r7() { FX_STW(7); }
static void fx_stw_r8() { FX_STW(8); }
static void fx_stw_r9() { FX_STW(9); }
static void fx_stw_r10() { FX_STW(10); }
static void fx_stw_r11() { FX_STW(11); }

/* 30-3b(ALT1) - stb (rn) - store byte */
#define FX_STB(reg) \
GSU.vLastRamAdr = GSU.avReg[reg]; \
RAM(GSU.avReg[reg]) = (uint8)SREG; \
CLRFLAGS; R15++
static void fx_stb_r0() { FX_STB(0); }
static void fx_stb_r1() { FX_STB(1); }
static void fx_stb_r2() { FX_STB(2); }
static void fx_stb_r3() { FX_STB(3); }
static void fx_stb_r4() { FX_STB(4); }
static void fx_stb_r5() { FX_STB(5); }
static void fx_stb_r6() { FX_STB(6); }
static void fx_stb_r7() { FX_STB(7); }
static void fx_stb_r8() { FX_STB(8); }
static void fx_stb_r9() { FX_STB(9); }
static void fx_stb_r10() { FX_STB(10); }
static void fx_stb_r11() { FX_STB(11); }

/* 3c - loop - decrement loop counter, and branch on not zero */
static void fx_loop()
{
    GSU.vSign = GSU.vZero = --R12;
    if( (uint16) R12 != 0 )
	R15 = R13;
    else
	R15++;

    CLRFLAGS;
}

/* 3d - alt1 - set alt1 mode */
static void fx_alt1() { SF(ALT1); CF(B); R15++; }

/* 3e - alt2 - set alt2 mode */
static void fx_alt2() { SF(ALT2); CF(B); R15++; }

/* 3f - alt3 - set alt3 mode */
static void fx_alt3() { SF(ALT1); SF(ALT2); CF(B); R15++; }
    
/* 40-4b - ldw (rn) - load word from RAM */
#define FX_LDW(reg) uint32 v; \
GSU.vLastRamAdr = GSU.avReg[reg]; \
v = (uint32)RAM(GSU.avReg[reg]); \
v |= ((uint32)RAM(GSU.avReg[reg]^1))<<8; \
R15++; DREG = v; \
TESTR14; \
CLRFLAGS
static void fx_ldw_r0() { FX_LDW(0); }
static void fx_ldw_r1() { FX_LDW(1); }
static void fx_ldw_r2() { FX_LDW(2); }
static void fx_ldw_r3() { FX_LDW(3); }
static void fx_ldw_r4() { FX_LDW(4); }
static void fx_ldw_r5() { FX_LDW(5); }
static void fx_ldw_r6() { FX_LDW(6); }
static void fx_ldw_r7() { FX_LDW(7); }
static void fx_ldw_r8() { FX_LDW(8); }
static void fx_ldw_r9() { FX_LDW(9); }
static void fx_ldw_r10() { FX_LDW(10); }
static void fx_ldw_r11() { FX_LDW(11); }

/* 40-4b(ALT1) - ldb (rn) - load byte */
#define FX_LDB(reg) uint32 v; \
GSU.vLastRamAdr = GSU.avReg[reg]; \
v = (uint32)RAM(GSU.avReg[reg]); \
R15++; DREG = v; \
TESTR14; \
CLRFLAGS
static void fx_ldb_r0() { FX_LDB(0); }
static void fx_ldb_r1() { FX_LDB(1); }
static void fx_ldb_r2() { FX_LDB(2); }
static void fx_ldb_r3() { FX_LDB(3); }
static void fx_ldb_r4() { FX_LDB(4); }
static void fx_ldb_r5() { FX_LDB(5); }
static void fx_ldb_r6() { FX_LDB(6); }
static void fx_ldb_r7() { FX_LDB(7); }
static void fx_ldb_r8() { FX_LDB(8); }
static void fx_ldb_r9() { FX_LDB(9); }
static void fx_ldb_r10() { FX_LDB(10); }
static void fx_ldb_r11() { FX_LDB(11); }

/* 4c - plot - plot pixel with R1,R2 as x,y and the color register as the color */
static void fx_plot_2bit()
{
    uint32 x = USEX8(R1);
    uint32 y = USEX8(R2);
    uint8 *a;
    uint8 v,c;

    R15++;
    CLRFLAGS;
    R1++;

#ifdef CHECK_LIMITS
    if(y >= GSU.vScreenHeight) return;
#endif
    if(GSU.vPlotOptionReg & 0x02)
	c = (x^y)&1 ? (uint8)(GSU.vColorReg>>4) : (uint8)GSU.vColorReg;
    else
	c = (uint8)GSU.vColorReg;
    
    if( !(GSU.vPlotOptionReg & 0x01) && !(c & 0xf)) return;
    a = GSU.apvScreen[y >> 3] + GSU.x[x >> 3] + ((y & 7) << 1);
    v = 128 >> (x&7);

    if(c & 0x01) a[0] |= v;
    else a[0] &= ~v;
    if(c & 0x02) a[1] |= v;
    else a[1] &= ~v;
}

/* 2c(ALT1) - rpix - read color of the pixel with R1,R2 as x,y */
static void fx_rpix_2bit()
{
    uint32 x = USEX8(R1);
    uint32 y = USEX8(R2);
    uint8 *a;
    uint8 v;

    R15++;
    CLRFLAGS;
#ifdef CHECK_LIMITS
    if(y >= GSU.vScreenHeight) return;
#endif

    a = GSU.apvScreen[y >> 3] + GSU.x[x >> 3] + ((y & 7) << 1);
    v = 128 >> (x&7);

    DREG = 0;
    DREG |= ((uint32)((a[0] & v) != 0)) << 0;
    DREG |= ((uint32)((a[1] & v) != 0)) << 1;
    TESTR14;
}

/* 4c - plot - plot pixel with R1,R2 as x,y and the color register as the color */
static void fx_plot_4bit()
{
    uint32 x = USEX8(R1);
    uint32 y = USEX8(R2);
    uint8 *a;
    uint8 v,c;

    R15++;
    CLRFLAGS;
    R1++;
    
#ifdef CHECK_LIMITS
    if(y >= GSU.vScreenHeight) return;
#endif
    if(GSU.vPlotOptionReg & 0x02)
	c = (x^y)&1 ? (uint8)(GSU.vColorReg>>4) : (uint8)GSU.vColorReg;
    else
	c = (uint8)GSU.vColorReg;

    if( !(GSU.vPlotOptionReg & 0x01) && !(c & 0xf)) return;

    a = GSU.apvScreen[y >> 3] + GSU.x[x >> 3] + ((y & 7) << 1);
    v = 128 >> (x&7);

    if(c & 0x01) a[0x00] |= v;
    else a[0x00] &= ~v;
    if(c & 0x02) a[0x01] |= v;
    else a[0x01] &= ~v;
    if(c & 0x04) a[0x10] |= v;
    else a[0x10] &= ~v;
    if(c & 0x08) a[0x11] |= v;
    else a[0x11] &= ~v;
}

/* 4c(ALT1) - rpix - read color of the pixel with R1,R2 as x,y */
static void fx_rpix_4bit()
{
    uint32 x = USEX8(R1);
    uint32 y = USEX8(R2);
    uint8 *a;
    uint8 v;

    R15++;
    CLRFLAGS;

#ifdef CHECK_LIMITS
    if(y >= GSU.vScreenHeight) return;
#endif

    a = GSU.apvScreen[y >> 3] + GSU.x[x >> 3] + ((y & 7) << 1);
    v = 128 >> (x&7);

    DREG = 0;
    DREG |= ((uint32)((a[0x00] & v) != 0)) << 0;
    DREG |= ((uint32)((a[0x01] & v) != 0)) << 1;
    DREG |= ((uint32)((a[0x10] & v) != 0)) << 2;
    DREG |= ((uint32)((a[0x11] & v) != 0)) << 3;
    TESTR14;
}

/* 8c - plot - plot pixel with R1,R2 as x,y and the color register as the color */
static void fx_plot_8bit()
{
    uint32 x = USEX8(R1);
    uint32 y = USEX8(R2);
    uint8 *a;
    uint8 v,c;

    R15++;
    CLRFLAGS;
    R1++;
    
#ifdef CHECK_LIMITS
    if(y >= GSU.vScreenHeight) return;
#endif
    c = (uint8)GSU.vColorReg;
    if( !(GSU.vPlotOptionReg & 0x10) )
    {
	if( !(GSU.vPlotOptionReg & 0x01) && !(c&0xf)) return;
    }
    else
	if( !(GSU.vPlotOptionReg & 0x01) && !c) return;

    a = GSU.apvScreen[y >> 3] + GSU.x[x >> 3] + ((y & 7) << 1);
    v = 128 >> (x&7);

    if(c & 0x01) a[0x00] |= v;
    else a[0x00] &= ~v;
    if(c & 0x02) a[0x01] |= v;
    else a[0x01] &= ~v;
    if(c & 0x04) a[0x10] |= v;
    else a[0x10] &= ~v;
    if(c & 0x08) a[0x11] |= v;
    else a[0x11] &= ~v;
    if(c & 0x10) a[0x20] |= v;
    else a[0x20] &= ~v;
    if(c & 0x20) a[0x21] |= v;
    else a[0x21] &= ~v;
    if(c & 0x40) a[0x30] |= v;
    else a[0x30] &= ~v;
    if(c & 0x80) a[0x31] |= v;
    else a[0x31] &= ~v;
}

/* 4c(ALT1) - rpix - read color of the pixel with R1,R2 as x,y */
static void fx_rpix_8bit()
{
    uint32 x = USEX8(R1);
    uint32 y = USEX8(R2);
    uint8 *a;
    uint8 v;

    R15++;
    CLRFLAGS;

#ifdef CHECK_LIMITS
    if(y >= GSU.vScreenHeight) return;
#endif
    a = GSU.apvScreen[y >> 3] + GSU.x[x >> 3] + ((y & 7) << 1);
    v = 128 >> (x&7);

    DREG = 0;
    DREG |= ((uint32)((a[0x00] & v) != 0)) << 0;
    DREG |= ((uint32)((a[0x01] & v) != 0)) << 1;
    DREG |= ((uint32)((a[0x10] & v) != 0)) << 2;
    DREG |= ((uint32)((a[0x11] & v) != 0)) << 3;
    DREG |= ((uint32)((a[0x20] & v) != 0)) << 4;
    DREG |= ((uint32)((a[0x21] & v) != 0)) << 5;
    DREG |= ((uint32)((a[0x30] & v) != 0)) << 6;
    DREG |= ((uint32)((a[0x31] & v) != 0)) << 7;
    GSU.vZero = DREG;
    TESTR14;
}

/* 4o - plot - plot pixel with R1,R2 as x,y and the color register as the color */
static void fx_plot_obj()
{
    printf ("ERROR fx_plot_obj called\n");
}

/* 4c(ALT1) - rpix - read color of the pixel with R1,R2 as x,y */
static void fx_rpix_obj()
{
    printf ("ERROR fx_rpix_obj called\n");
}

/* 4d - swap - swap upper and lower byte of a register */
static void fx_swap()
{
    uint8 c = (uint8)SREG;
    uint8 d = (uint8)(SREG>>8);
    uint32 v = (((uint32)c)<<8)|((uint32)d);
    R15++; DREG = v;
    GSU.vSign = v;
    GSU.vZero = v;
    TESTR14;
    CLRFLAGS;
}

/* 4e - color - copy source register to color register */
static void fx_color()
{
    uint8 c = (uint8)SREG;
    if(GSU.vPlotOptionReg & 0x04)
	c = (c&0xf0) | (c>>4);
    if(GSU.vPlotOptionReg & 0x08)
    {
	GSU.vColorReg &= 0xf0;
	GSU.vColorReg |= c & 0x0f;
    }
    else
	GSU.vColorReg = USEX8(c);
    CLRFLAGS;
    R15++;
}

/* 4e(ALT1) - cmode - set plot option register */
static void fx_cmode()
{
    GSU.vPlotOptionReg = SREG;

    if(GSU.vPlotOptionReg & 0x10)
    {
	/* OBJ Mode (for drawing into sprites) */
	GSU.vScreenHeight = 256;
    }
    else
	GSU.vScreenHeight = GSU.vScreenRealHeight;

    fx_computeScreenPointers ();
    CLRFLAGS;
    R15++;
}

/* 4f - not - perform exclusive exor with 1 on all bits */
static void fx_not()
{
    uint32 v = ~SREG;
    R15++; DREG = v;
    GSU.vSign = v;
    GSU.vZero = v;
    TESTR14;
    CLRFLAGS;
}

/* 50-5f - add rn - add, register + register */
#define FX_ADD(reg) \
int32 s = SUSEX16(SREG) + SUSEX16(GSU.avReg[reg]); \
GSU.vCarry = s >= 0x10000; \
GSU.vOverflow = ~(SREG ^ GSU.avReg[reg]) & (GSU.avReg[reg] ^ s) & 0x8000; \
GSU.vSign = s; \
GSU.vZero = s; \
R15++; DREG = s; \
TESTR14; \
CLRFLAGS
static void fx_add_r0() { FX_ADD(0); }
static void fx_add_r1() { FX_ADD(1); }
static void fx_add_r2() { FX_ADD(2); }
static void fx_add_r3() { FX_ADD(3); }
static void fx_add_r4() { FX_ADD(4); }
static void fx_add_r5() { FX_ADD(5); }
static void fx_add_r6() { FX_ADD(6); }
static void fx_add_r7() { FX_ADD(7); }
static void fx_add_r8() { FX_ADD(8); }
static void fx_add_r9() { FX_ADD(9); }
static void fx_add_r10() { FX_ADD(10); }
static void fx_add_r11() { FX_ADD(11); }
static void fx_add_r12() { FX_ADD(12); }
static void fx_add_r13() { FX_ADD(13); }
static void fx_add_r14() { FX_ADD(14); }
static void fx_add_r15() { FX_ADD(15); }

/* 50-5f(ALT1) - adc rn - add with carry, register + register */
#define FX_ADC(reg) \
int32 s = SUSEX16(SREG) + SUSEX16(GSU.avReg[reg]) + SEX16(GSU.vCarry); \
GSU.vCarry = s >= 0x10000; \
GSU.vOverflow = ~(SREG ^ GSU.avReg[reg]) & (GSU.avReg[reg] ^ s) & 0x8000; \
GSU.vSign = s; \
GSU.vZero = s; \
R15++; DREG = s; \
TESTR14; \
CLRFLAGS
static void fx_adc_r0() { FX_ADC(0); }
static void fx_adc_r1() { FX_ADC(1); }
static void fx_adc_r2() { FX_ADC(2); }
static void fx_adc_r3() { FX_ADC(3); }
static void fx_adc_r4() { FX_ADC(4); }
static void fx_adc_r5() { FX_ADC(5); }
static void fx_adc_r6() { FX_ADC(6); }
static void fx_adc_r7() { FX_ADC(7); }
static void fx_adc_r8() { FX_ADC(8); }
static void fx_adc_r9() { FX_ADC(9); }
static void fx_adc_r10() { FX_ADC(10); }
static void fx_adc_r11() { FX_ADC(11); }
static void fx_adc_r12() { FX_ADC(12); }
static void fx_adc_r13() { FX_ADC(13); }
static void fx_adc_r14() { FX_ADC(14); }
static void fx_adc_r15() { FX_ADC(15); }

/* 50-5f(ALT2) - add #n - add, register + immediate */
#define FX_ADD_I(imm) \
int32 s = SUSEX16(SREG) + imm; \
GSU.vCarry = s >= 0x10000; \
GSU.vOverflow = ~(SREG ^ imm) & (imm ^ s) & 0x8000; \
GSU.vSign = s; \
GSU.vZero = s; \
R15++; DREG = s; \
TESTR14; \
CLRFLAGS
static void fx_add_i0() { FX_ADD_I(0); }
static void fx_add_i1() { FX_ADD_I(1); }
static void fx_add_i2() { FX_ADD_I(2); }
static void fx_add_i3() { FX_ADD_I(3); }
static void fx_add_i4() { FX_ADD_I(4); }
static void fx_add_i5() { FX_ADD_I(5); }
static void fx_add_i6() { FX_ADD_I(6); }
static void fx_add_i7() { FX_ADD_I(7); }
static void fx_add_i8() { FX_ADD_I(8); }
static void fx_add_i9() { FX_ADD_I(9); }
static void fx_add_i10() { FX_ADD_I(10); }
static void fx_add_i11() { FX_ADD_I(11); }
static void fx_add_i12() { FX_ADD_I(12); }
static void fx_add_i13() { FX_ADD_I(13); }
static void fx_add_i14() { FX_ADD_I(14); }
static void fx_add_i15() { FX_ADD_I(15); }

/* 50-5f(ALT3) - adc #n - add with carry, register + immediate */
#define FX_ADC_I(imm) \
int32 s = SUSEX16(SREG) + imm + SUSEX16(GSU.vCarry); \
GSU.vCarry = s >= 0x10000; \
GSU.vOverflow = ~(SREG ^ imm) & (imm ^ s) & 0x8000; \
GSU.vSign = s; \
GSU.vZero = s; \
R15++; DREG = s; \
TESTR14; \
CLRFLAGS
static void fx_adc_i0() { FX_ADC_I(0); }
static void fx_adc_i1() { FX_ADC_I(1); }
static void fx_adc_i2() { FX_ADC_I(2); }
static void fx_adc_i3() { FX_ADC_I(3); }
static void fx_adc_i4() { FX_ADC_I(4); }
static void fx_adc_i5() { FX_ADC_I(5); }
static void fx_adc_i6() { FX_ADC_I(6); }
static void fx_adc_i7() { FX_ADC_I(7); }
static void fx_adc_i8() { FX_ADC_I(8); }
static void fx_adc_i9() { FX_ADC_I(9); }
static void fx_adc_i10() { FX_ADC_I(10); }
static void fx_adc_i11() { FX_ADC_I(11); }
static void fx_adc_i12() { FX_ADC_I(12); }
static void fx_adc_i13() { FX_ADC_I(13); }
static void fx_adc_i14() { FX_ADC_I(14); }
static void fx_adc_i15() { FX_ADC_I(15); }

/* 60-6f - sub rn - subtract, register - register */
#define FX_SUB(reg) \
int32 s = SUSEX16(SREG) - SUSEX16(GSU.avReg[reg]); \
GSU.vCarry = s >= 0; \
GSU.vOverflow = (SREG ^ GSU.avReg[reg]) & (SREG ^ s) & 0x8000; \
GSU.vSign = s; \
GSU.vZero = s; \
R15++; DREG = s; \
TESTR14; \
CLRFLAGS
static void fx_sub_r0() { FX_SUB(0); }
static void fx_sub_r1() { FX_SUB(1); }
static void fx_sub_r2() { FX_SUB(2); }
static void fx_sub_r3() { FX_SUB(3); }
static void fx_sub_r4() { FX_SUB(4); }
static void fx_sub_r5() { FX_SUB(5); }
static void fx_sub_r6() { FX_SUB(6); }
static void fx_sub_r7() { FX_SUB(7); }
static void fx_sub_r8() { FX_SUB(8); }
static void fx_sub_r9() { FX_SUB(9); }
static void fx_sub_r10() { FX_SUB(10); }
static void fx_sub_r11() { FX_SUB(11); }
static void fx_sub_r12() { FX_SUB(12); }
static void fx_sub_r13() { FX_SUB(13); }
static void fx_sub_r14() { FX_SUB(14); }
static void fx_sub_r15() { FX_SUB(15); }

/* 60-6f(ALT1) - sbc rn - subtract with carry, register - register */
#define FX_SBC(reg) \
int32 s = SUSEX16(SREG) - SUSEX16(GSU.avReg[reg]) - (SUSEX16(GSU.vCarry^1)); \
GSU.vCarry = s >= 0; \
GSU.vOverflow = (SREG ^ GSU.avReg[reg]) & (SREG ^ s) & 0x8000; \
GSU.vSign = s; \
GSU.vZero = s; \
R15++; DREG = s; \
TESTR14; \
CLRFLAGS
static void fx_sbc_r0() { FX_SBC(0); }
static void fx_sbc_r1() { FX_SBC(1); }
static void fx_sbc_r2() { FX_SBC(2); }
static void fx_sbc_r3() { FX_SBC(3); }
static void fx_sbc_r4() { FX_SBC(4); }
static void fx_sbc_r5() { FX_SBC(5); }
static void fx_sbc_r6() { FX_SBC(6); }
static void fx_sbc_r7() { FX_SBC(7); }
static void fx_sbc_r8() { FX_SBC(8); }
static void fx_sbc_r9() { FX_SBC(9); }
static void fx_sbc_r10() { FX_SBC(10); }
static void fx_sbc_r11() { FX_SBC(11); }
static void fx_sbc_r12() { FX_SBC(12); }
static void fx_sbc_r13() { FX_SBC(13); }
static void fx_sbc_r14() { FX_SBC(14); }
static void fx_sbc_r15() { FX_SBC(15); }

/* 60-6f(ALT2) - sub #n - subtract, register - immediate */
#define FX_SUB_I(imm) \
int32 s = SUSEX16(SREG) - imm; \
GSU.vCarry = s >= 0; \
GSU.vOverflow = (SREG ^ imm) & (SREG ^ s) & 0x8000; \
GSU.vSign = s; \
GSU.vZero = s; \
R15++; DREG = s; \
TESTR14; \
CLRFLAGS
static void fx_sub_i0() { FX_SUB_I(0); }
static void fx_sub_i1() { FX_SUB_I(1); }
static void fx_sub_i2() { FX_SUB_I(2); }
static void fx_sub_i3() { FX_SUB_I(3); }
static void fx_sub_i4() { FX_SUB_I(4); }
static void fx_sub_i5() { FX_SUB_I(5); }
static void fx_sub_i6() { FX_SUB_I(6); }
static void fx_sub_i7() { FX_SUB_I(7); }
static void fx_sub_i8() { FX_SUB_I(8); }
static void fx_sub_i9() { FX_SUB_I(9); }
static void fx_sub_i10() { FX_SUB_I(10); }
static void fx_sub_i11() { FX_SUB_I(11); }
static void fx_sub_i12() { FX_SUB_I(12); }
static void fx_sub_i13() { FX_SUB_I(13); }
static void fx_sub_i14() { FX_SUB_I(14); }
static void fx_sub_i15() { FX_SUB_I(15); }

/* 60-6f(ALT3) - cmp rn - compare, register, register */
#define FX_CMP(reg) \
int32 s = SUSEX16(SREG) - SUSEX16(GSU.avReg[reg]); \
GSU.vCarry = s >= 0; \
GSU.vOverflow = (SREG ^ GSU.avReg[reg]) & (SREG ^ s) & 0x8000; \
GSU.vSign = s; \
GSU.vZero = s; \
R15++; \
CLRFLAGS;
static void fx_cmp_r0() { FX_CMP(0); }
static void fx_cmp_r1() { FX_CMP(1); }
static void fx_cmp_r2() { FX_CMP(2); }
static void fx_cmp_r3() { FX_CMP(3); }
static void fx_cmp_r4() { FX_CMP(4); }
static void fx_cmp_r5() { FX_CMP(5); }
static void fx_cmp_r6() { FX_CMP(6); }
static void fx_cmp_r7() { FX_CMP(7); }
static void fx_cmp_r8() { FX_CMP(8); }
static void fx_cmp_r9() { FX_CMP(9); }
static void fx_cmp_r10() { FX_CMP(10); }
static void fx_cmp_r11() { FX_CMP(11); }
static void fx_cmp_r12() { FX_CMP(12); }
static void fx_cmp_r13() { FX_CMP(13); }
static void fx_cmp_r14() { FX_CMP(14); }
static void fx_cmp_r15() { FX_CMP(15); }

/* 70 - merge - R7 as upper byte, R8 as lower byte (used for texture-mapping) */
static void fx_merge()
{
    uint32 v = (R7&0xff00) | ((R8&0xff00)>>8);
    R15++; DREG = v;
    GSU.vOverflow = (v & 0xc0c0) << 16;
    GSU.vZero = !(v & 0xf0f0);
    GSU.vSign = ((v | (v<<8)) & 0x8000);
    GSU.vCarry = (v & 0xe0e0) != 0;
    TESTR14;
    CLRFLAGS;
}

/* 71-7f - and rn - reister & register */
#define FX_AND(reg) \
uint32 v = SREG & GSU.avReg[reg]; \
R15++; DREG = v; \
GSU.vSign = v; \
GSU.vZero = v; \
TESTR14; \
CLRFLAGS;
static void fx_and_r1() { FX_AND(1); }
static void fx_and_r2() { FX_AND(2); }
static void fx_and_r3() { FX_AND(3); }
static void fx_and_r4() { FX_AND(4); }
static void fx_and_r5() { FX_AND(5); }
static void fx_and_r6() { FX_AND(6); }
static void fx_and_r7() { FX_AND(7); }
static void fx_and_r8() { FX_AND(8); }
static void fx_and_r9() { FX_AND(9); }
static void fx_and_r10() { FX_AND(10); }
static void fx_and_r11() { FX_AND(11); }
static void fx_and_r12() { FX_AND(12); }
static void fx_and_r13() { FX_AND(13); }
static void fx_and_r14() { FX_AND(14); }
static void fx_and_r15() { FX_AND(15); }

/* 71-7f(ALT1) - bic rn - reister & ~register */
#define FX_BIC(reg) \
uint32 v = SREG & ~GSU.avReg[reg];	\
R15++; DREG = v; \
GSU.vSign = v; \
GSU.vZero = v; \
TESTR14; \
CLRFLAGS;
static void fx_bic_r1() { FX_BIC(1); }
static void fx_bic_r2() { FX_BIC(2); }
static void fx_bic_r3() { FX_BIC(3); }
static void fx_bic_r4() { FX_BIC(4); }
static void fx_bic_r5() { FX_BIC(5); }
static void fx_bic_r6() { FX_BIC(6); }
static void fx_bic_r7() { FX_BIC(7); }
static void fx_bic_r8() { FX_BIC(8); }
static void fx_bic_r9() { FX_BIC(9); }
static void fx_bic_r10() { FX_BIC(10); }
static void fx_bic_r11() { FX_BIC(11); }
static void fx_bic_r12() { FX_BIC(12); }
static void fx_bic_r13() { FX_BIC(13); }
static void fx_bic_r14() { FX_BIC(14); }
static void fx_bic_r15() { FX_BIC(15); }

/* 71-7f(ALT2) - and #n - reister & immediate */
#define FX_AND_I(imm) \
uint32 v = SREG & imm; \
R15++; DREG = v; \
GSU.vSign = v; \
GSU.vZero = v; \
TESTR14; \
CLRFLAGS;
static void fx_and_i1() { FX_AND_I(1); }
static void fx_and_i2() { FX_AND_I(2); }
static void fx_and_i3() { FX_AND_I(3); }
static void fx_and_i4() { FX_AND_I(4); }
static void fx_and_i5() { FX_AND_I(5); }
static void fx_and_i6() { FX_AND_I(6); }
static void fx_and_i7() { FX_AND_I(7); }
static void fx_and_i8() { FX_AND_I(8); }
static void fx_and_i9() { FX_AND_I(9); }
static void fx_and_i10() { FX_AND_I(10); }
static void fx_and_i11() { FX_AND_I(11); }
static void fx_and_i12() { FX_AND_I(12); }
static void fx_and_i13() { FX_AND_I(13); }
static void fx_and_i14() { FX_AND_I(14); }
static void fx_and_i15() { FX_AND_I(15); }

/* 71-7f(ALT3) - bic #n - reister & ~immediate */
#define FX_BIC_I(imm) \
uint32 v = SREG & ~imm; \
R15++; DREG = v; \
GSU.vSign = v; \
GSU.vZero = v; \
TESTR14; \
CLRFLAGS;
static void fx_bic_i1() { FX_BIC_I(1); }
static void fx_bic_i2() { FX_BIC_I(2); }
static void fx_bic_i3() { FX_BIC_I(3); }
static void fx_bic_i4() { FX_BIC_I(4); }
static void fx_bic_i5() { FX_BIC_I(5); }
static void fx_bic_i6() { FX_BIC_I(6); }
static void fx_bic_i7() { FX_BIC_I(7); }
static void fx_bic_i8() { FX_BIC_I(8); }
static void fx_bic_i9() { FX_BIC_I(9); }
static void fx_bic_i10() { FX_BIC_I(10); }
static void fx_bic_i11() { FX_BIC_I(11); }
static void fx_bic_i12() { FX_BIC_I(12); }
static void fx_bic_i13() { FX_BIC_I(13); }
static void fx_bic_i14() { FX_BIC_I(14); }
static void fx_bic_i15() { FX_BIC_I(15); }

/* 80-8f - mult rn - 8 bit to 16 bit signed multiply, register * register */
#define FX_MULT(reg) \
uint32 v = (uint32)(SEX8(SREG) * SEX8(GSU.avReg[reg])); \
R15++; DREG = v; \
GSU.vSign = v; \
GSU.vZero = v; \
TESTR14; \
CLRFLAGS;
static void fx_mult_r0() { FX_MULT(0); }
static void fx_mult_r1() { FX_MULT(1); }
static void fx_mult_r2() { FX_MULT(2); }
static void fx_mult_r3() { FX_MULT(3); }
static void fx_mult_r4() { FX_MULT(4); }
static void fx_mult_r5() { FX_MULT(5); }
static void fx_mult_r6() { FX_MULT(6); }
static void fx_mult_r7() { FX_MULT(7); }
static void fx_mult_r8() { FX_MULT(8); }
static void fx_mult_r9() { FX_MULT(9); }
static void fx_mult_r10() { FX_MULT(10); }
static void fx_mult_r11() { FX_MULT(11); }
static void fx_mult_r12() { FX_MULT(12); }
static void fx_mult_r13() { FX_MULT(13); }
static void fx_mult_r14() { FX_MULT(14); }
static void fx_mult_r15() { FX_MULT(15); }

/* 80-8f(ALT1) - umult rn - 8 bit to 16 bit unsigned multiply, register * register */
#define FX_UMULT(reg) \
uint32 v = USEX8(SREG) * USEX8(GSU.avReg[reg]); \
R15++; DREG = v; \
GSU.vSign = v; \
GSU.vZero = v; \
TESTR14; \
CLRFLAGS;
static void fx_umult_r0() { FX_UMULT(0); }
static void fx_umult_r1() { FX_UMULT(1); }
static void fx_umult_r2() { FX_UMULT(2); }
static void fx_umult_r3() { FX_UMULT(3); }
static void fx_umult_r4() { FX_UMULT(4); }
static void fx_umult_r5() { FX_UMULT(5); }
static void fx_umult_r6() { FX_UMULT(6); }
static void fx_umult_r7() { FX_UMULT(7); }
static void fx_umult_r8() { FX_UMULT(8); }
static void fx_umult_r9() { FX_UMULT(9); }
static void fx_umult_r10() { FX_UMULT(10); }
static void fx_umult_r11() { FX_UMULT(11); }
static void fx_umult_r12() { FX_UMULT(12); }
static void fx_umult_r13() { FX_UMULT(13); }
static void fx_umult_r14() { FX_UMULT(14); }
static void fx_umult_r15() { FX_UMULT(15); }
  
/* 80-8f(ALT2) - mult #n - 8 bit to 16 bit signed multiply, register * immediate */
#define FX_MULT_I(imm) \
uint32 v = (uint32) (SEX8(SREG) * ((int32)imm)); \
R15++; DREG = v; \
GSU.vSign = v; \
GSU.vZero = v; \
TESTR14; \
CLRFLAGS;
static void fx_mult_i0() { FX_MULT_I(0); }
static void fx_mult_i1() { FX_MULT_I(1); }
static void fx_mult_i2() { FX_MULT_I(2); }
static void fx_mult_i3() { FX_MULT_I(3); }
static void fx_mult_i4() { FX_MULT_I(4); }
static void fx_mult_i5() { FX_MULT_I(5); }
static void fx_mult_i6() { FX_MULT_I(6); }
static void fx_mult_i7() { FX_MULT_I(7); }
static void fx_mult_i8() { FX_MULT_I(8); }
static void fx_mult_i9() { FX_MULT_I(9); }
static void fx_mult_i10() { FX_MULT_I(10); }
static void fx_mult_i11() { FX_MULT_I(11); }
static void fx_mult_i12() { FX_MULT_I(12); }
static void fx_mult_i13() { FX_MULT_I(13); }
static void fx_mult_i14() { FX_MULT_I(14); }
static void fx_mult_i15() { FX_MULT_I(15); }
  
/* 80-8f(ALT3) - umult #n - 8 bit to 16 bit unsigned multiply, register * immediate */
#define FX_UMULT_I(imm) \
uint32 v = USEX8(SREG) * ((uint32)imm); \
R15++; DREG = v; \
GSU.vSign = v; \
GSU.vZero = v; \
TESTR14; \
CLRFLAGS;
static void fx_umult_i0() { FX_UMULT_I(0); }
static void fx_umult_i1() { FX_UMULT_I(1); }
static void fx_umult_i2() { FX_UMULT_I(2); }
static void fx_umult_i3() { FX_UMULT_I(3); }
static void fx_umult_i4() { FX_UMULT_I(4); }
static void fx_umult_i5() { FX_UMULT_I(5); }
static void fx_umult_i6() { FX_UMULT_I(6); }
static void fx_umult_i7() { FX_UMULT_I(7); }
static void fx_umult_i8() { FX_UMULT_I(8); }
static void fx_umult_i9() { FX_UMULT_I(9); }
static void fx_umult_i10() { FX_UMULT_I(10); }
static void fx_umult_i11() { FX_UMULT_I(11); }
static void fx_umult_i12() { FX_UMULT_I(12); }
static void fx_umult_i13() { FX_UMULT_I(13); }
static void fx_umult_i14() { FX_UMULT_I(14); }
static void fx_umult_i15() { FX_UMULT_I(15); }
  
/* 90 - sbk - store word to last accessed RAM address */
static void fx_sbk()
{
    RAM(GSU.vLastRamAdr) = (uint8)SREG;
    RAM(GSU.vLastRamAdr^1) = (uint8)(SREG>>8);
    CLRFLAGS;
    R15++;
}

/* 91-94 - link #n - R11 = R15 + immediate */
#define FX_LINK_I(lkn) R11 = R15 + lkn; CLRFLAGS; R15++
static void fx_link_i1() { FX_LINK_I(1); }
static void fx_link_i2() { FX_LINK_I(2); }
static void fx_link_i3() { FX_LINK_I(3); }
static void fx_link_i4() { FX_LINK_I(4); }

/* 95 - sex - sign extend 8 bit to 16 bit */
static void fx_sex()
{
    uint32 v = (uint32)SEX8(SREG);
    R15++; DREG = v;
    GSU.vSign = v;
    GSU.vZero = v;
    TESTR14;
    CLRFLAGS;
}

/* 96 - asr - aritmetric shift right by one */
static void fx_asr()
{
    uint32 v;
    GSU.vCarry = SREG & 1;
    v = (uint32)(SEX16(SREG)>>1);
    R15++; DREG = v;
    GSU.vSign = v;
    GSU.vZero = v;
    TESTR14;
    CLRFLAGS;
}

/* 96(ALT1) - div2 - aritmetric shift right by one */
static void fx_div2()
{
    uint32 v;
    int32 s = SEX16(SREG);
    GSU.vCarry = s & 1;
    if(s == -1)
	v = 0;
    else
	v = (uint32)(s>>1);
    R15++; DREG = v;
    GSU.vSign = v;
    GSU.vZero = v;
    TESTR14;
    CLRFLAGS;
}

/* 97 - ror - rotate right by one */
static void fx_ror()
{
    uint32 v = (USEX16(SREG)>>1) | (GSU.vCarry<<15);
    GSU.vCarry = SREG & 1;
    R15++; DREG = v;
    GSU.vSign = v;
    GSU.vZero = v;
    TESTR14;
    CLRFLAGS;
}

/* 98-9d - jmp rn - jump to address of register */
#define FX_JMP(reg) \
R15 = GSU.avReg[reg]; \
CLRFLAGS;
static void fx_jmp_r8() { FX_JMP(8); }
static void fx_jmp_r9() { FX_JMP(9); }
static void fx_jmp_r10() { FX_JMP(10); }
static void fx_jmp_r11() { FX_JMP(11); }
static void fx_jmp_r12() { FX_JMP(12); }
static void fx_jmp_r13() { FX_JMP(13); }

/* 98-9d(ALT1) - ljmp rn - set program bank to source register and jump to address of register */
#define FX_LJMP(reg) \
GSU.vPrgBankReg = GSU.avReg[reg] & 0x7f; \
GSU.pvPrgBank = GSU.apvRomBank[GSU.vPrgBankReg]; \
R15 = SREG; \
GSU.bCacheActive = FALSE; fx_cache(); R15--;
static void fx_ljmp_r8() { FX_LJMP(8); }
static void fx_ljmp_r9() { FX_LJMP(9); }
static void fx_ljmp_r10() { FX_LJMP(10); }
static void fx_ljmp_r11() { FX_LJMP(11); }
static void fx_ljmp_r12() { FX_LJMP(12); }
static void fx_ljmp_r13() { FX_LJMP(13); }

/* 9e - lob - set upper byte to zero (keep low byte) */
static void fx_lob()
{
    uint32 v = USEX8(SREG);
    R15++; DREG = v;
    GSU.vSign = v<<8;
    GSU.vZero = v<<8;
    TESTR14;
    CLRFLAGS;
}

/* 9f - fmult - 16 bit to 32 bit signed multiplication, upper 16 bits only */
static void fx_fmult()
{
    uint32 v;
    uint32 c = (uint32) (SEX16(SREG) * SEX16(R6));
    v = c >> 16;
    R15++; DREG = v;
    GSU.vSign = v;
    GSU.vZero = v;
    GSU.vCarry = (c >> 15) & 1;
    TESTR14;
    CLRFLAGS;
}

/* 9f(ALT1) - lmult - 16 bit to 32 bit signed multiplication */
static void fx_lmult()
{
    uint32 v;
    uint32 c = (uint32) (SEX16(SREG) * SEX16(R6));
    R4 = c;
    v = c >> 16;
    R15++; DREG = v;
    GSU.vSign = v;
    GSU.vZero = v;
    /* XXX R6 or R4? */
    GSU.vCarry = (R4 >> 15) & 1;	/* should it be bit 15 of R4 instead? */
    TESTR14;
    CLRFLAGS;
}

/* a0-af - ibt rn,#pp - immediate byte transfer */
#define FX_IBT(reg) \
uint8 v = PIPE; R15++; \
FETCHPIPE; R15++; \
GSU.avReg[reg] = SEX8(v); \
CLRFLAGS;
static void fx_ibt_r0() { FX_IBT(0); }
static void fx_ibt_r1() { FX_IBT(1); }
static void fx_ibt_r2() { FX_IBT(2); }
static void fx_ibt_r3() { FX_IBT(3); }
static void fx_ibt_r4() { FX_IBT(4); }
static void fx_ibt_r5() { FX_IBT(5); }
static void fx_ibt_r6() { FX_IBT(6); }
static void fx_ibt_r7() { FX_IBT(7); }
static void fx_ibt_r8() { FX_IBT(8); }
static void fx_ibt_r9() { FX_IBT(9); }
static void fx_ibt_r10() { FX_IBT(10); }
static void fx_ibt_r11() { FX_IBT(11); }
static void fx_ibt_r12() { FX_IBT(12); }
static void fx_ibt_r13() { FX_IBT(13); }
static void fx_ibt_r14() { FX_IBT(14); READR14; }
static void fx_ibt_r15() { FX_IBT(15); }

/* a0-af(ALT1) - lms rn,(yy) - load word from RAM (short address) */
#define FX_LMS(reg) \
GSU.vLastRamAdr = ((uint32)PIPE) << 1; \
R15++; FETCHPIPE; R15++; \
GSU.avReg[reg] = (uint32)RAM(GSU.vLastRamAdr); \
GSU.avReg[reg] |= ((uint32)RAM(GSU.vLastRamAdr+1))<<8; \
CLRFLAGS;
static void fx_lms_r0() { FX_LMS(0); }
static void fx_lms_r1() { FX_LMS(1); }
static void fx_lms_r2() { FX_LMS(2); }
static void fx_lms_r3() { FX_LMS(3); }
static void fx_lms_r4() { FX_LMS(4); }
static void fx_lms_r5() { FX_LMS(5); }
static void fx_lms_r6() { FX_LMS(6); }
static void fx_lms_r7() { FX_LMS(7); }
static void fx_lms_r8() { FX_LMS(8); }
static void fx_lms_r9() { FX_LMS(9); }
static void fx_lms_r10() { FX_LMS(10); }
static void fx_lms_r11() { FX_LMS(11); }
static void fx_lms_r12() { FX_LMS(12); }
static void fx_lms_r13() { FX_LMS(13); }
static void fx_lms_r14() { FX_LMS(14); READR14; }
static void fx_lms_r15() { FX_LMS(15); }

/* a0-af(ALT2) - sms (yy),rn - store word in RAM (short address) */
/* If rn == r15, is the value of r15 before or after the extra byte is read? */
#define FX_SMS(reg) \
uint32 v = GSU.avReg[reg]; \
GSU.vLastRamAdr = ((uint32)PIPE) << 1; \
R15++; FETCHPIPE; \
RAM(GSU.vLastRamAdr) = (uint8)v; \
RAM(GSU.vLastRamAdr+1) = (uint8)(v>>8); \
CLRFLAGS; R15++;
static void fx_sms_r0() { FX_SMS(0); }
static void fx_sms_r1() { FX_SMS(1); }
static void fx_sms_r2() { FX_SMS(2); }
static void fx_sms_r3() { FX_SMS(3); }
static void fx_sms_r4() { FX_SMS(4); }
static void fx_sms_r5() { FX_SMS(5); }
static void fx_sms_r6() { FX_SMS(6); }
static void fx_sms_r7() { FX_SMS(7); }
static void fx_sms_r8() { FX_SMS(8); }
static void fx_sms_r9() { FX_SMS(9); }
static void fx_sms_r10() { FX_SMS(10); }
static void fx_sms_r11() { FX_SMS(11); }
static void fx_sms_r12() { FX_SMS(12); }
static void fx_sms_r13() { FX_SMS(13); }
static void fx_sms_r14() { FX_SMS(14); }
static void fx_sms_r15() { FX_SMS(15); }

/* b0-bf - from rn - set source register */
/* b0-bf(B) - moves rn - move register to register, and set flags, (if B flag is set) */
#define FX_FROM(reg) \
if(TF(B)) { uint32 v = GSU.avReg[reg]; R15++; DREG = v; \
GSU.vOverflow = (v&0x80) << 16; GSU.vSign = v; GSU.vZero = v; TESTR14; CLRFLAGS; } \
else { GSU.pvSreg = &GSU.avReg[reg]; R15++; }
static void fx_from_r0() { FX_FROM(0); }
static void fx_from_r1() { FX_FROM(1); }
static void fx_from_r2() { FX_FROM(2); }
static void fx_from_r3() { FX_FROM(3); }
static void fx_from_r4() { FX_FROM(4); }
static void fx_from_r5() { FX_FROM(5); }
static void fx_from_r6() { FX_FROM(6); }
static void fx_from_r7() { FX_FROM(7); }
static void fx_from_r8() { FX_FROM(8); }
static void fx_from_r9() { FX_FROM(9); }
static void fx_from_r10() { FX_FROM(10); }
static void fx_from_r11() { FX_FROM(11); }
static void fx_from_r12() { FX_FROM(12); }
static void fx_from_r13() { FX_FROM(13); }
static void fx_from_r14() { FX_FROM(14); }
static void fx_from_r15() { FX_FROM(15); }

/* c0 - hib - move high-byte to low-byte */
static void fx_hib()
{
    uint32 v = USEX8(SREG>>8);
    R15++; DREG = v;
    GSU.vSign = v<<8;
    GSU.vZero = v<<8;
    TESTR14;
    CLRFLAGS;
}

/* c1-cf - or rn */
#define FX_OR(reg) \
uint32 v = SREG | GSU.avReg[reg]; R15++; DREG = v; \
GSU.vSign = v; \
GSU.vZero = v; \
TESTR14; \
CLRFLAGS;
static void fx_or_r1() { FX_OR(1); }
static void fx_or_r2() { FX_OR(2); }
static void fx_or_r3() { FX_OR(3); }
static void fx_or_r4() { FX_OR(4); }
static void fx_or_r5() { FX_OR(5); }
static void fx_or_r6() { FX_OR(6); }
static void fx_or_r7() { FX_OR(7); }
static void fx_or_r8() { FX_OR(8); }
static void fx_or_r9() { FX_OR(9); }
static void fx_or_r10() { FX_OR(10); }
static void fx_or_r11() { FX_OR(11); }
static void fx_or_r12() { FX_OR(12); }
static void fx_or_r13() { FX_OR(13); }
static void fx_or_r14() { FX_OR(14); }
static void fx_or_r15() { FX_OR(15); }

/* c1-cf(ALT1) - xor rn */
#define FX_XOR(reg) \
uint32 v = SREG ^ GSU.avReg[reg]; R15++; DREG = v; \
GSU.vSign = v; \
GSU.vZero = v; \
TESTR14; \
CLRFLAGS;
static void fx_xor_r1() { FX_XOR(1); }
static void fx_xor_r2() { FX_XOR(2); }
static void fx_xor_r3() { FX_XOR(3); }
static void fx_xor_r4() { FX_XOR(4); }
static void fx_xor_r5() { FX_XOR(5); }
static void fx_xor_r6() { FX_XOR(6); }
static void fx_xor_r7() { FX_XOR(7); }
static void fx_xor_r8() { FX_XOR(8); }
static void fx_xor_r9() { FX_XOR(9); }
static void fx_xor_r10() { FX_XOR(10); }
static void fx_xor_r11() { FX_XOR(11); }
static void fx_xor_r12() { FX_XOR(12); }
static void fx_xor_r13() { FX_XOR(13); }
static void fx_xor_r14() { FX_XOR(14); }
static void fx_xor_r15() { FX_XOR(15); }

/* c1-cf(ALT2) - or #n */
#define FX_OR_I(imm) \
uint32 v = SREG | imm; R15++; DREG = v; \
GSU.vSign = v; \
GSU.vZero = v; \
TESTR14; \
CLRFLAGS;
static void fx_or_i1() { FX_OR_I(1); }
static void fx_or_i2() { FX_OR_I(2); }
static void fx_or_i3() { FX_OR_I(3); }
static void fx_or_i4() { FX_OR_I(4); }
static void fx_or_i5() { FX_OR_I(5); }
static void fx_or_i6() { FX_OR_I(6); }
static void fx_or_i7() { FX_OR_I(7); }
static void fx_or_i8() { FX_OR_I(8); }
static void fx_or_i9() { FX_OR_I(9); }
static void fx_or_i10() { FX_OR_I(10); }
static void fx_or_i11() { FX_OR_I(11); }
static void fx_or_i12() { FX_OR_I(12); }
static void fx_or_i13() { FX_OR_I(13); }
static void fx_or_i14() { FX_OR_I(14); }
static void fx_or_i15() { FX_OR_I(15); }

/* c1-cf(ALT3) - xor #n */
#define FX_XOR_I(imm) \
uint32 v = SREG ^ imm; R15++; DREG = v; \
GSU.vSign = v; \
GSU.vZero = v; \
TESTR14; \
CLRFLAGS;
static void fx_xor_i1() { FX_XOR_I(1); }
static void fx_xor_i2() { FX_XOR_I(2); }
static void fx_xor_i3() { FX_XOR_I(3); }
static void fx_xor_i4() { FX_XOR_I(4); }
static void fx_xor_i5() { FX_XOR_I(5); }
static void fx_xor_i6() { FX_XOR_I(6); }
static void fx_xor_i7() { FX_XOR_I(7); }
static void fx_xor_i8() { FX_XOR_I(8); }
static void fx_xor_i9() { FX_XOR_I(9); }
static void fx_xor_i10() { FX_XOR_I(10); }
static void fx_xor_i11() { FX_XOR_I(11); }
static void fx_xor_i12() { FX_XOR_I(12); }
static void fx_xor_i13() { FX_XOR_I(13); }
static void fx_xor_i14() { FX_XOR_I(14); }
static void fx_xor_i15() { FX_XOR_I(15); }

/* d0-de - inc rn - increase by one */
#define FX_INC(reg) \
GSU.avReg[reg] += 1; \
GSU.vSign = GSU.avReg[reg]; \
GSU.vZero = GSU.avReg[reg]; \
CLRFLAGS; R15++;
static void fx_inc_r0() { FX_INC(0); }
static void fx_inc_r1() { FX_INC(1); }
static void fx_inc_r2() { FX_INC(2); }
static void fx_inc_r3() { FX_INC(3); }
static void fx_inc_r4() { FX_INC(4); }
static void fx_inc_r5() { FX_INC(5); }
static void fx_inc_r6() { FX_INC(6); }
static void fx_inc_r7() { FX_INC(7); }
static void fx_inc_r8() { FX_INC(8); }
static void fx_inc_r9() { FX_INC(9); }
static void fx_inc_r10() { FX_INC(10); }
static void fx_inc_r11() { FX_INC(11); }
static void fx_inc_r12() { FX_INC(12); }
static void fx_inc_r13() { FX_INC(13); }
static void fx_inc_r14() { FX_INC(14); READR14; }

/* df - getc - transfer ROM buffer to color register */
static void fx_getc()
{
#ifndef FX_DO_ROMBUFFER
    uint8 c;
    c = ROM(R14);
#else
    uint8 c = GSU.vRomBuffer;
#endif
    if(GSU.vPlotOptionReg & 0x04)
	c = (c&0xf0) | (c>>4);
    if(GSU.vPlotOptionReg & 0x08)
    {
	GSU.vColorReg &= 0xf0;
	GSU.vColorReg |= c & 0x0f;
    }
    else
	GSU.vColorReg = USEX8(c);
    CLRFLAGS;
    R15++;
}

/* df(ALT2) - ramb - set current RAM bank */
static void fx_ramb()
{
    GSU.vRamBankReg = SREG & (FX_RAM_BANKS-1);
    GSU.pvRamBank = GSU.apvRamBank[GSU.vRamBankReg & 0x3];
    CLRFLAGS;
    R15++;
}

/* df(ALT3) - romb - set current ROM bank */
static void fx_romb()
{
    GSU.vRomBankReg = USEX8(SREG) & 0x7f;
    GSU.pvRomBank = GSU.apvRomBank[GSU.vRomBankReg];
    CLRFLAGS;
    R15++;
}

/* e0-ee - dec rn - decrement by one */
#define FX_DEC(reg) \
GSU.avReg[reg] -= 1; \
GSU.vSign = GSU.avReg[reg]; \
GSU.vZero = GSU.avReg[reg]; \
CLRFLAGS; R15++;
static void fx_dec_r0() { FX_DEC(0); }
static void fx_dec_r1() { FX_DEC(1); }
static void fx_dec_r2() { FX_DEC(2); }
static void fx_dec_r3() { FX_DEC(3); }
static void fx_dec_r4() { FX_DEC(4); }
static void fx_dec_r5() { FX_DEC(5); }
static void fx_dec_r6() { FX_DEC(6); }
static void fx_dec_r7() { FX_DEC(7); }
static void fx_dec_r8() { FX_DEC(8); }
static void fx_dec_r9() { FX_DEC(9); }
static void fx_dec_r10() { FX_DEC(10); }
static void fx_dec_r11() { FX_DEC(11); }
static void fx_dec_r12() { FX_DEC(12); }
static void fx_dec_r13() { FX_DEC(13); }
static void fx_dec_r14() { FX_DEC(14); READR14; }

/* ef - getb - get byte from ROM at address R14 */
static void fx_getb()
{
    uint32 v;
#ifndef FX_DO_ROMBUFFER
    v = (uint32)ROM(R14);
#else
    v = (uint32)GSU.vRomBuffer;
#endif
    R15++; DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* ef(ALT1) - getbh - get high-byte from ROM at address R14 */
static void fx_getbh()
{
    uint32 v;
#ifndef FX_DO_ROMBUFFER
    uint32 c;
    c = (uint32)ROM(R14);
#else
    uint32 c = USEX8(GSU.vRomBuffer);
#endif
    v = USEX8(SREG) | (c<<8);
    R15++; DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* ef(ALT2) - getbl - get low-byte from ROM at address R14 */
static void fx_getbl()
{
    uint32 v;
#ifndef FX_DO_ROMBUFFER
    uint32 c;
    c = (uint32)ROM(R14);
#else
    uint32 c = USEX8(GSU.vRomBuffer);
#endif
    v = (SREG & 0xff00) | c;
    R15++; DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* ef(ALT3) - getbs - get sign extended byte from ROM at address R14 */
static void fx_getbs()
{
    uint32 v;
#ifndef FX_DO_ROMBUFFER
    int8 c;
    c = ROM(R14);
    v = SEX8(c);
#else
    v = SEX8(GSU.vRomBuffer);
#endif
    R15++; DREG = v;
    TESTR14;
    CLRFLAGS;
}

/* f0-ff - iwt rn,#xx - immediate word transfer to register */
#define FX_IWT(reg) \
uint32 v = PIPE; R15++; FETCHPIPE; R15++; \
v |= USEX8(PIPE) << 8; FETCHPIPE; R15++; \
GSU.avReg[reg] = v; \
CLRFLAGS;
static void fx_iwt_r0() { FX_IWT(0); }
static void fx_iwt_r1() { FX_IWT(1); }
static void fx_iwt_r2() { FX_IWT(2); }
static void fx_iwt_r3() { FX_IWT(3); }
static void fx_iwt_r4() { FX_IWT(4); }
static void fx_iwt_r5() { FX_IWT(5); }
static void fx_iwt_r6() { FX_IWT(6); }
static void fx_iwt_r7() { FX_IWT(7); }
static void fx_iwt_r8() { FX_IWT(8); }
static void fx_iwt_r9() { FX_IWT(9); }
static void fx_iwt_r10() { FX_IWT(10); }
static void fx_iwt_r11() { FX_IWT(11); }
static void fx_iwt_r12() { FX_IWT(12); }
static void fx_iwt_r13() { FX_IWT(13); }
static void fx_iwt_r14() { FX_IWT(14); READR14; }
static void fx_iwt_r15() { FX_IWT(15); }

/* f0-ff(ALT1) - lm rn,(xx) - load word from RAM */
#define FX_LM(reg) \
GSU.vLastRamAdr = PIPE; R15++; FETCHPIPE; R15++; \
GSU.vLastRamAdr |= USEX8(PIPE) << 8; FETCHPIPE; R15++; \
GSU.avReg[reg] = RAM(GSU.vLastRamAdr); \
GSU.avReg[reg] |= USEX8(RAM(GSU.vLastRamAdr^1)) << 8; \
CLRFLAGS;
static void fx_lm_r0() { FX_LM(0); }
static void fx_lm_r1() { FX_LM(1); }
static void fx_lm_r2() { FX_LM(2); }
static void fx_lm_r3() { FX_LM(3); }
static void fx_lm_r4() { FX_LM(4); }
static void fx_lm_r5() { FX_LM(5); }
static void fx_lm_r6() { FX_LM(6); }
static void fx_lm_r7() { FX_LM(7); }
static void fx_lm_r8() { FX_LM(8); }
static void fx_lm_r9() { FX_LM(9); }
static void fx_lm_r10() { FX_LM(10); }
static void fx_lm_r11() { FX_LM(11); }
static void fx_lm_r12() { FX_LM(12); }
static void fx_lm_r13() { FX_LM(13); }
static void fx_lm_r14() { FX_LM(14); READR14; }
static void fx_lm_r15() { FX_LM(15); }

/* f0-ff(ALT2) - sm (xx),rn - store word in RAM */
/* If rn == r15, is the value of r15 before or after the extra bytes are read? */
#define FX_SM(reg) \
uint32 v = GSU.avReg[reg]; \
GSU.vLastRamAdr = PIPE; R15++; FETCHPIPE; R15++; \
GSU.vLastRamAdr |= USEX8(PIPE) << 8; FETCHPIPE; \
RAM(GSU.vLastRamAdr) = (uint8)v; \
RAM(GSU.vLastRamAdr^1) = (uint8)(v>>8); \
CLRFLAGS; R15++;
static void fx_sm_r0() { FX_SM(0); }
static void fx_sm_r1() { FX_SM(1); }
static void fx_sm_r2() { FX_SM(2); }
static void fx_sm_r3() { FX_SM(3); }
static void fx_sm_r4() { FX_SM(4); }
static void fx_sm_r5() { FX_SM(5); }
static void fx_sm_r6() { FX_SM(6); }
static void fx_sm_r7() { FX_SM(7); }
static void fx_sm_r8() { FX_SM(8); }
static void fx_sm_r9() { FX_SM(9); }
static void fx_sm_r10() { FX_SM(10); }
static void fx_sm_r11() { FX_SM(11); }
static void fx_sm_r12() { FX_SM(12); }
static void fx_sm_r13() { FX_SM(13); }
static void fx_sm_r14() { FX_SM(14); }
static void fx_sm_r15() { FX_SM(15); }

/*** The ARM assembler interpreter (pocketsnes/arm/fx_run_arm.s) ***/

#ifdef ARM_ASM_FX
/* The offsets fx_run_arm.s uses (its .equ list) */
#define FX_CHECK_OFFSET(field, offset) \
    static_assert (offsetof (struct FxRegs_s, field) == (offset), \
		   "FxRegs_s." #field " moved: update pocketsnes/arm/fx_run_arm.s")
FX_CHECK_OFFSET (avReg, 0);
FX_CHECK_OFFSET (vColorReg, 64);
FX_CHECK_OFFSET (vPlotOptionReg, 68);
FX_CHECK_OFFSET (vStatusReg, 72);
FX_CHECK_OFFSET (vLastRamAdr, 96);
FX_CHECK_OFFSET (pvDreg, 100);
FX_CHECK_OFFSET (pvSreg, 104);
FX_CHECK_OFFSET (vRomBuffer, 108);
FX_CHECK_OFFSET (vPipe, 109);
FX_CHECK_OFFSET (vSign, 116);
FX_CHECK_OFFSET (vZero, 120);
FX_CHECK_OFFSET (vCarry, 124);
FX_CHECK_OFFSET (vOverflow, 128);
FX_CHECK_OFFSET (pvRegisters, 140);
FX_CHECK_OFFSET (apvScreen, 172);
FX_CHECK_OFFSET (x, 300);
FX_CHECK_OFFSET (vScreenHeight, 428);
FX_CHECK_OFFSET (pvRamBank, 444);
FX_CHECK_OFFSET (pvRomBank, 448);
FX_CHECK_OFFSET (pvPrgBank, 452);

extern "C"
{
    void fx_run_arm (void);
    extern uint32 fx_arm_handlers [];
    extern uint32 fx_arm_tables [3072];
}
#endif

/* fx_run_arm.s's handlers, in the order of its fx_arm_handlers list */
enum FxArmHandler
{
    H_SLOW, H_STOP, H_NOP, H_LSR, H_ROL, H_BRA, H_BGE, H_BLT, H_BNE, H_BEQ, H_BPL, H_BMI,
    H_BCC, H_BCS, H_BVC, H_BVS, H_TO, H_WITH, H_STW, H_STB, H_LOOP,
    H_ALT1, H_ALT2, H_ALT3, H_LDW, H_LDB, H_SWAP, H_NOT,
    H_ADD, H_ADC, H_ADD_I, H_ADC_I, H_SUB, H_SBC, H_SUB_I, H_CMP,
    H_AND, H_BIC, H_AND_I, H_BIC_I, H_OR, H_XOR, H_OR_I, H_XOR_I,
    H_MULT, H_UMULT, H_MULT_I, H_UMULT_I, H_LINK, H_SEX, H_ASR, H_ROR, H_JMP, H_LOB, H_HIB,
    H_IBT, H_IBT_R14, H_IBT_R15, H_LMS, H_LMS_R14, H_SMS,
    H_FROM, H_INC, H_INC_R14, H_DEC, H_DEC_R14,
    H_GETB, H_IWT, H_IWT_R14, H_IWT_R15, H_LM, H_LM_R14, H_SM,
    H_PLOT4, H_MERGE, H_COLOR, H_TO_R14, H_WITH_R14,
    H_COUNT	/* (then the same list again, the R14 table's versions) */
};

/* Fills an interpreter's three tables (1024 handler addresses each, ALT mode
 * << 8 | opcode): the fast one; the careful one, for a source or
 * destination of R15, which runs everything but stop through
 * fx_apfOpcodeTable; and the R14 one, for a destination of R14, with the
 * handlers' versions that reload the ROM buffer (handlers [H_COUNT + h]).
 * Instructions that would read or write R15 by register number run through
 * the C table (H_SLOW) from every table, as do rpix, cache, cmode, sbk, div2,
 * ljmp, fmult, lmult, getc, ramb, romb, getbh, getbl, getbs, and plot unless
 * fx_arm_plot_entries says otherwise. */
extern "C" void fx_arm_setup_tables (uint32 *tables, const uint32 *handlers)
{
    static const struct { uint8 first, last, alts, handler; } map[] =
    {
	/* alts: bit n = ALT mode n */
	{ 0x00, 0x00, 15, H_STOP }, { 0x01, 0x01, 15, H_NOP }, { 0x03, 0x03, 15, H_LSR },
	{ 0x04, 0x04, 15, H_ROL }, { 0x05, 0x05, 15, H_BRA }, { 0x06, 0x06, 15, H_BGE },
	{ 0x07, 0x07, 15, H_BLT }, { 0x08, 0x08, 15, H_BNE }, { 0x09, 0x09, 15, H_BEQ },
	{ 0x0a, 0x0a, 15, H_BPL }, { 0x0b, 0x0b, 15, H_BMI }, { 0x0c, 0x0c, 15, H_BCC },
	{ 0x0d, 0x0d, 15, H_BCS }, { 0x0e, 0x0e, 15, H_BVC }, { 0x0f, 0x0f, 15, H_BVS },
	{ 0x10, 0x1d, 15, H_TO }, { 0x20, 0x2d, 15, H_WITH },
	{ 0x30, 0x3b, 5, H_STW }, { 0x30, 0x3b, 10, H_STB },
	{ 0x3c, 0x3c, 15, H_LOOP }, { 0x3d, 0x3d, 15, H_ALT1 }, { 0x3e, 0x3e, 15, H_ALT2 },
	{ 0x3f, 0x3f, 15, H_ALT3 },
	{ 0x40, 0x4b, 5, H_LDW }, { 0x40, 0x4b, 10, H_LDB },
	{ 0x4d, 0x4d, 15, H_SWAP }, { 0x4f, 0x4f, 15, H_NOT },
	{ 0x50, 0x5e, 1, H_ADD }, { 0x50, 0x5e, 2, H_ADC }, { 0x50, 0x5f, 4, H_ADD_I },
	{ 0x50, 0x5f, 8, H_ADC_I },
	{ 0x60, 0x6e, 1, H_SUB }, { 0x60, 0x6e, 2, H_SBC }, { 0x60, 0x6f, 4, H_SUB_I },
	{ 0x60, 0x6e, 8, H_CMP },
	{ 0x71, 0x7e, 1, H_AND }, { 0x71, 0x7e, 2, H_BIC }, { 0x71, 0x7f, 4, H_AND_I },
	{ 0x71, 0x7f, 8, H_BIC_I },
	{ 0x80, 0x8e, 1, H_MULT }, { 0x80, 0x8e, 2, H_UMULT }, { 0x80, 0x8f, 4, H_MULT_I },
	{ 0x80, 0x8f, 8, H_UMULT_I },
	{ 0x91, 0x94, 15, H_LINK }, { 0x95, 0x95, 15, H_SEX }, { 0x96, 0x96, 5, H_ASR },
	{ 0x97, 0x97, 15, H_ROR }, { 0x98, 0x9d, 5, H_JMP }, { 0x9e, 0x9e, 15, H_LOB },
	{ 0xa0, 0xad, 1, H_IBT }, { 0xae, 0xae, 1, H_IBT_R14 }, { 0xaf, 0xaf, 1, H_IBT_R15 },
	{ 0xa0, 0xad, 10, H_LMS }, { 0xae, 0xae, 10, H_LMS_R14 }, { 0xa0, 0xae, 4, H_SMS },
	{ 0xb0, 0xbe, 15, H_FROM }, { 0xc0, 0xc0, 15, H_HIB },
	{ 0xc1, 0xce, 1, H_OR }, { 0xc1, 0xce, 2, H_XOR }, { 0xc1, 0xcf, 4, H_OR_I },
	{ 0xc1, 0xcf, 8, H_XOR_I },
	{ 0xd0, 0xdd, 15, H_INC }, { 0xde, 0xde, 15, H_INC_R14 },
	{ 0xe0, 0xed, 15, H_DEC }, { 0xee, 0xee, 15, H_DEC_R14 }, { 0xef, 0xef, 1, H_GETB },
	{ 0xf0, 0xfd, 1, H_IWT }, { 0xfe, 0xfe, 1, H_IWT_R14 }, { 0xff, 0xff, 1, H_IWT_R15 },
	{ 0xf0, 0xfd, 10, H_LM }, { 0xfe, 0xfe, 10, H_LM_R14 }, { 0xf0, 0xfe, 4, H_SM },
	{ 0x70, 0x70, 15, H_MERGE }, { 0x4e, 0x4e, 5, H_COLOR },
	{ 0x1e, 0x1e, 15, H_TO_R14 }, { 0x2e, 0x2e, 15, H_WITH_R14 },
    };
    uint8 handler [1024];
    memset (handler, H_SLOW, sizeof handler);
    for (unsigned m = 0; m < sizeof map / sizeof map [0]; m++)
	for (int alt = 0; alt < 4; alt++)
	    if (map [m].alts & (1 << alt))
		for (int o = map [m].first; o <= map [m].last; o++)
		    handler [(alt << 8) | o] = map [m].handler;
    uint32 *fast = tables, *careful = tables + 1024, *r14 = tables + 2048;
    for (int i = 0; i < 1024; i++)
    {
	fast [i] = handlers [handler [i]];
	careful [i] = handlers [(i & 0xff) == 0 ? H_STOP : H_SLOW];
	r14 [i] = handlers [H_COUNT + handler [i]];
    }
}

/* plot (4c, ALT0 and ALT2) in assembler for the 4-bit colour modes, which
 * fx_readRegisterSpace has set fx_apfOpcodeTable to before each run. */
extern "C" void fx_arm_plot_entries (uint32 *tables, const uint32 *handlers)
{
    int h = fx_apfOpcodeTable [0x04c] == &fx_plot_4bit ? H_PLOT4 : H_SLOW;
    tables [0x04c] = tables [0x24c] = handlers [h];
    tables [2048 + 0x04c] = tables [2048 + 0x24c] = handlers [H_COUNT + h];
}

/*** GSU executions functions ***/

/* The interpreter. The GSU's state lives in the GSU struct, but what every
 * instruction touches is kept in local variables while it runs: the status
 * register (for the ALT1, ALT2 and B prefix bits), the source and destination
 * register pointers, the pipe byte and the program and RAM bank pointers. The
 * common instructions are done right here, for any register (op & 15), each
 * ending with a jump straight to the next one's code; the rare ones (plot,
 * rpix, cmode, cache, ljmp, getc, romb, ramb, merge and a few more) call their
 * function from fx_apfOpcodeTable, with the locals written back first and
 * read again after. Every instruction does exactly what its function does:
 * tests/fx_test.cpp runs random instructions and programs through this and
 * through fx_run_reference (the original loop, a call through the table for
 * every instruction) and compares the whole GSU state and RAM. In Yoshi's
 * Island this takes ?? ARM instructions per GSU instruction, against 38.8. */

#define FXR_CLRFLAGS { sfr &= ~(FLG_ALT1|FLG_ALT2|FLG_B); sidx = didx = 0; }
#define FXR_FETCHPIPE { pipe = prg[USEX16(R15)]; }
#define FXR_TESTR14 { if (didx == 14) READR14; }
#define FXR_RAM(adr) ram[USEX16(adr)]
#define FXR_SAVE { GSU.vStatusReg = sfr; GSU.pvSreg = &r[sidx]; GSU.pvDreg = &r[didx]; GSU.vPipe = pipe; }
#define FXR_LOAD { sfr = GSU.vStatusReg; sidx = GSU.pvSreg - r; didx = GSU.pvDreg - r; pipe = GSU.vPipe; \
		   prg = GSU.pvPrgBank; ram = GSU.pvRamBank; }
/* Ends an instruction: fetch the next one and jump to its code */
#define FXR_NEXT { if (--vCounter == 0) goto out; op = pipe; FXR_FETCHPIPE; \
		   goto *table[(sfr & 0x300) | op]; }
#define FXR_BRA(cond) { uint8 v = pipe; R15++; FXR_FETCHPIPE; if(cond) R15 += SEX8(v); else R15++; } FXR_NEXT

/* The interpreter. A run starts by reading the ROM buffer (READR14); a step
 * that goes on with a run doesn't (fx_step), as the ROM buffer keeps what it
 * read when R14 was last written, even after a romb. */
static void fx_run_c(uint32 nInstructions, bool start);

void fx_run(uint32 nInstructions)
{
#ifdef ARM_ASM_FX
    /* Until the GSU stops (FxEmulate's usual ~0), with the ARM CPU core set:
     * translated into ARM code (fxjit.cpp) */
    if (nInstructions == ~0u && Settings.AsmCPU)
    {
	fx_jit_run ();
	return;
    }
#endif
    fx_run_c (nInstructions, true);
}

void fx_step(uint32 nInstructions)
{
    fx_run_c (nInstructions, false);
}

#ifdef ARM_ASM_FX
/* The assembler interpreter (fx_run_arm.s), until the GSU stops. */
void fx_run_arm_interpreter(void)
{
    if (!fx_arm_tables [0])
	fx_arm_setup_tables (fx_arm_tables, fx_arm_handlers);
    fx_arm_plot_entries (fx_arm_tables, fx_arm_handlers);
    fx_run_arm ();
}
#endif

static void fx_run_c(uint32 nInstructions, bool start)
{
    static void *table[1024];
    uint32 *const r = GSU.avReg;
    uint32 sfr = GSU.vStatusReg;
    uint32 sidx = GSU.pvSreg - r, didx = GSU.pvDreg - r;	/* source and destination register */
    uint8 *prg = GSU.pvPrgBank, *ram = GSU.pvRamBank;
    uint32 pipe = GSU.vPipe;
    uint32 vCounter = nInstructions;
    uint32 op;

    if (!table[0])
    {
	/* Which code runs each opcode in each ALT mode (ALT mode << 8 | opcode) */
	static const struct { uint16 first, last; uint8 alts; int8 code; } map[] =
	{
	    /* alts: bit n = ALT mode n (0x0f = all four) */
	    { 0x00, 0x00, 0x0f,  1 }, { 0x01, 0x01, 0x0f,  2 }, { 0x03, 0x03, 0x0f,  3 }, { 0x04, 0x04, 0x0f,  4 },
	    { 0x05, 0x05, 0x0f,  5 }, { 0x06, 0x06, 0x0f,  6 }, { 0x07, 0x07, 0x0f,  7 }, { 0x08, 0x08, 0x0f,  8 },
	    { 0x09, 0x09, 0x0f,  9 }, { 0x0a, 0x0a, 0x0f, 10 }, { 0x0b, 0x0b, 0x0f, 11 }, { 0x0c, 0x0c, 0x0f, 12 },
	    { 0x0d, 0x0d, 0x0f, 13 }, { 0x0e, 0x0e, 0x0f, 14 }, { 0x0f, 0x0f, 0x0f, 15 }, { 0x10, 0x1f, 0x0f, 16 },
	    { 0x20, 0x2f, 0x0f, 17 }, { 0x30, 0x3b, 0x05, 18 }, { 0x30, 0x3b, 0x0a, 19 }, { 0x3c, 0x3c, 0x0f, 20 },
	    { 0x3d, 0x3d, 0x0f, 21 }, { 0x3e, 0x3e, 0x0f, 22 }, { 0x3f, 0x3f, 0x0f, 23 }, { 0x40, 0x4b, 0x05, 24 },
	    { 0x40, 0x4b, 0x0a, 25 }, { 0x50, 0x5f, 0x01, 26 }, { 0x50, 0x5f, 0x02, 27 }, { 0x50, 0x5f, 0x04, 28 },
	    { 0x50, 0x5f, 0x08, 29 }, { 0x60, 0x6f, 0x01, 30 }, { 0x60, 0x6f, 0x02, 31 }, { 0x60, 0x6f, 0x04, 32 },
	    { 0x60, 0x6f, 0x08, 33 }, { 0x71, 0x7f, 0x01, 34 }, { 0x71, 0x7f, 0x02, 35 }, { 0x71, 0x7f, 0x04, 36 },
	    { 0x71, 0x7f, 0x08, 37 }, { 0x80, 0x8f, 0x01, 38 }, { 0x80, 0x8f, 0x02, 39 }, { 0x80, 0x8f, 0x04, 40 },
	    { 0x80, 0x8f, 0x08, 41 }, { 0x91, 0x94, 0x0f, 42 }, { 0x98, 0x9d, 0x05, 43 }, { 0xa0, 0xaf, 0x01, 44 },
	    { 0xa0, 0xaf, 0x0a, 45 }, { 0xa0, 0xaf, 0x04, 46 }, { 0xb0, 0xbf, 0x0f, 47 }, { 0xc1, 0xcf, 0x01, 48 },
	    { 0xc1, 0xcf, 0x02, 49 }, { 0xc1, 0xcf, 0x04, 50 }, { 0xc1, 0xcf, 0x08, 51 }, { 0xd0, 0xde, 0x0f, 52 },
	    { 0xe0, 0xee, 0x0f, 53 }, { 0xef, 0xef, 0x01, 54 }, { 0xf0, 0xff, 0x01, 55 }, { 0xf0, 0xff, 0x0a, 56 },
	    { 0xf0, 0xff, 0x04, 57 },
	};
	void *const code[] =
	{
	    &&slow, &&stop, &&nop, &&lsr, &&rol, &&bra, &&bge, &&blt, &&bne, &&beq, &&bpl, &&bmi, &&bcc, &&bcs,
	    &&bvc, &&bvs, &&to, &&with, &&stw, &&stb, &&loop, &&alt1, &&alt2, &&alt3, &&ldw, &&ldb, &&add,
	    &&adc, &&add_i, &&adc_i, &&sub, &&sbc, &&sub_i, &&cmp, &&and_r, &&bic, &&and_i, &&bic_i, &&mult,
	    &&umult, &&mult_i, &&umult_i, &&link, &&jmp, &&ibt, &&lms, &&sms, &&from, &&or_r, &&xor_r, &&or_i,
	    &&xor_i, &&inc, &&dec, &&getb, &&iwt, &&lm, &&sm,
	};
	for (int i = 1; i < 1024; i++)
	    table[i] = &&slow;
	for (unsigned m = 0; m < sizeof map / sizeof map[0]; m++)
	    for (int alt = 0; alt < 4; alt++)
		if (map[m].alts & (1 << alt))
		    for (int o = map[m].first; o <= map[m].last; o++)
			table[(alt << 8) | o] = code[map[m].code];
	table[0] = &&stop;	/* (set last: it marks the table as ready) */
    }

    if (start)
	READR14;
    /* Only stop clears G, and stop ends the run. */
    if (!(sfr & FLG_G) || vCounter == 0)
	return;
    /* Execute the instruction in the pipe, and fetch the next byte into it */
    op = pipe;
    FXR_FETCHPIPE;
    goto *table[(sfr & 0x300) | op];

  stop:
    sfr &= ~FLG_G;
    if(!(GSU.pvRegisters[GSU_CFGR] & 0x80))
	sfr |= FLG_IRQ;
    GSU.vPlotOptionReg = 0;
    pipe = 1;
    FXR_CLRFLAGS;
    R15++;
    goto out;
  nop:
    FXR_CLRFLAGS; R15++;
    FXR_NEXT
  lsr:
    {
	uint32 v;
	GSU.vCarry = r[sidx] & 1;
	v = USEX16(r[sidx]) >> 1;
	R15++; r[didx] = v;
	GSU.vSign = v;
	GSU.vZero = v;
	FXR_TESTR14; FXR_CLRFLAGS;
    }
    FXR_NEXT
  rol:
    {
	uint32 v = USEX16((r[sidx] << 1) + GSU.vCarry);
	GSU.vCarry = (r[sidx] >> 15) & 1;
	R15++; r[didx] = v;
	GSU.vSign = v;
	GSU.vZero = v;
	FXR_TESTR14; FXR_CLRFLAGS;
    }
    FXR_NEXT
  bra:
    {
	uint8 v = pipe; R15++; FXR_FETCHPIPE; R15 += SEX8(v);
    }
    FXR_NEXT
    /* The branches, as fx_apfOpcodeTable has them (06 is fx_bge, 07 fx_blt) */
  bge: FXR_BRA( (TEST_S!=0) == (TEST_OV!=0) )
  blt: FXR_BRA( (TEST_S!=0) != (TEST_OV!=0) )
  bne: FXR_BRA( !TEST_Z )
  beq: FXR_BRA( TEST_Z )
  bpl: FXR_BRA( !TEST_S )
  bmi: FXR_BRA( TEST_S )
  bcc: FXR_BRA( !TEST_CY )
  bcs: FXR_BRA( TEST_CY )
  bvc: FXR_BRA( !TEST_OV )
  bvs: FXR_BRA( TEST_OV )
  to:	/* to rn; move rn (B) */
    {
	uint32 n = op & 15;
	if (sfr & FLG_B)
	{
	    r[n] = r[sidx];
	    FXR_CLRFLAGS;
	    if (n == 14) READR14;
	    if (n != 15) R15++;
	}
	else
	{
	    didx = n;
	    R15++;
	}
    }
    FXR_NEXT
  with:	/* with rn */
    sfr |= FLG_B;
    sidx = didx = op & 15;
    R15++;
    FXR_NEXT
  stw:	/* stw (rn) */
    {
	uint32 a = r[op & 15];
	GSU.vLastRamAdr = a;
	FXR_RAM(a) = (uint8)r[sidx];
	FXR_RAM(a^1) = (uint8)(r[sidx]>>8);
	FXR_CLRFLAGS; R15++;
    }
    FXR_NEXT
  stb:	/* stb (rn) */
    {
	uint32 a = r[op & 15];
	GSU.vLastRamAdr = a;
	FXR_RAM(a) = (uint8)r[sidx];
	FXR_CLRFLAGS; R15++;
    }
    FXR_NEXT
  loop:
    GSU.vSign = GSU.vZero = --R12;
    if( (uint16) R12 != 0 )
	R15 = R13;
    else
	R15++;
    FXR_CLRFLAGS;
    FXR_NEXT
  alt1:
    sfr |= FLG_ALT1; sfr &= ~FLG_B; R15++;
    FXR_NEXT
  alt2:
    sfr |= FLG_ALT2; sfr &= ~FLG_B; R15++;
    FXR_NEXT
  alt3:
    sfr |= FLG_ALT1 | FLG_ALT2; sfr &= ~FLG_B; R15++;
    FXR_NEXT
  ldw:	/* ldw (rn) */
    {
	uint32 a = r[op & 15], v;
	GSU.vLastRamAdr = a;
	v = (uint32)FXR_RAM(a);
	v |= ((uint32)FXR_RAM(a^1))<<8;
	R15++; r[didx] = v;
	FXR_TESTR14; FXR_CLRFLAGS;
    }
    FXR_NEXT
  ldb:	/* ldb (rn) */
    {
	uint32 a = r[op & 15], v;
	GSU.vLastRamAdr = a;
	v = (uint32)FXR_RAM(a);
	R15++; r[didx] = v;
	FXR_TESTR14; FXR_CLRFLAGS;
    }
    FXR_NEXT
  add:	/* add rn */
    {
	uint32 n = op & 15;
	int32 s = SUSEX16(r[sidx]) + SUSEX16(r[n]);
	GSU.vCarry = s >= 0x10000;
	GSU.vOverflow = ~(r[sidx] ^ r[n]) & (r[n] ^ s) & 0x8000;
	GSU.vSign = s;
	GSU.vZero = s;
	R15++; r[didx] = s;
	FXR_TESTR14; FXR_CLRFLAGS;
    }
    FXR_NEXT
  adc:	/* adc rn */
    {
	uint32 n = op & 15;
	int32 s = SUSEX16(r[sidx]) + SUSEX16(r[n]) + SEX16(GSU.vCarry);
	GSU.vCarry = s >= 0x10000;
	GSU.vOverflow = ~(r[sidx] ^ r[n]) & (r[n] ^ s) & 0x8000;
	GSU.vSign = s;
	GSU.vZero = s;
	R15++; r[didx] = s;
	FXR_TESTR14; FXR_CLRFLAGS;
    }
    FXR_NEXT
  add_i:	/* add #n */
    {
	int32 imm = op & 15;
	int32 s = SUSEX16(r[sidx]) + imm;
	GSU.vCarry = s >= 0x10000;
	GSU.vOverflow = ~(r[sidx] ^ imm) & (imm ^ s) & 0x8000;
	GSU.vSign = s;
	GSU.vZero = s;
	R15++; r[didx] = s;
	FXR_TESTR14; FXR_CLRFLAGS;
    }
    FXR_NEXT
  adc_i:	/* adc #n */
    {
	int32 imm = op & 15;
	int32 s = SUSEX16(r[sidx]) + imm + SUSEX16(GSU.vCarry);
	GSU.vCarry = s >= 0x10000;
	GSU.vOverflow = ~(r[sidx] ^ imm) & (imm ^ s) & 0x8000;
	GSU.vSign = s;
	GSU.vZero = s;
	R15++; r[didx] = s;
	FXR_TESTR14; FXR_CLRFLAGS;
    }
    FXR_NEXT
  sub:	/* sub rn */
    {
	uint32 n = op & 15;
	int32 s = SUSEX16(r[sidx]) - SUSEX16(r[n]);
	GSU.vCarry = s >= 0;
	GSU.vOverflow = (r[sidx] ^ r[n]) & (r[sidx] ^ s) & 0x8000;
	GSU.vSign = s;
	GSU.vZero = s;
	R15++; r[didx] = s;
	FXR_TESTR14; FXR_CLRFLAGS;
    }
    FXR_NEXT
  sbc:	/* sbc rn */
    {
	uint32 n = op & 15;
	int32 s = SUSEX16(r[sidx]) - SUSEX16(r[n]) - (SUSEX16(GSU.vCarry^1));
	GSU.vCarry = s >= 0;
	GSU.vOverflow = (r[sidx] ^ r[n]) & (r[sidx] ^ s) & 0x8000;
	GSU.vSign = s;
	GSU.vZero = s;
	R15++; r[didx] = s;
	FXR_TESTR14; FXR_CLRFLAGS;
    }
    FXR_NEXT
  sub_i:	/* sub #n */
    {
	int32 imm = op & 15;
	int32 s = SUSEX16(r[sidx]) - imm;
	GSU.vCarry = s >= 0;
	GSU.vOverflow = (r[sidx] ^ imm) & (r[sidx] ^ s) & 0x8000;
	GSU.vSign = s;
	GSU.vZero = s;
	R15++; r[didx] = s;
	FXR_TESTR14; FXR_CLRFLAGS;
    }
    FXR_NEXT
  cmp:	/* cmp rn */
    {
	uint32 n = op & 15;
	int32 s = SUSEX16(r[sidx]) - SUSEX16(r[n]);
	GSU.vCarry = s >= 0;
	GSU.vOverflow = (r[sidx] ^ r[n]) & (r[sidx] ^ s) & 0x8000;
	GSU.vSign = s;
	GSU.vZero = s;
	R15++;
	FXR_CLRFLAGS;
    }
    FXR_NEXT
  and_r:	/* and rn */
    {
	uint32 v = r[sidx] & r[op & 15];
	R15++; r[didx] = v;
	GSU.vSign = v;
	GSU.vZero = v;
	FXR_TESTR14; FXR_CLRFLAGS;
    }
    FXR_NEXT
  bic:	/* bic rn */
    {
	uint32 v = r[sidx] & ~r[op & 15];
	R15++; r[didx] = v;
	GSU.vSign = v;
	GSU.vZero = v;
	FXR_TESTR14; FXR_CLRFLAGS;
    }
    FXR_NEXT
  and_i:	/* and #n */
    {
	int32 imm = op & 15;
	uint32 v = r[sidx] & imm;
	R15++; r[didx] = v;
	GSU.vSign = v;
	GSU.vZero = v;
	FXR_TESTR14; FXR_CLRFLAGS;
    }
    FXR_NEXT
  bic_i:	/* bic #n */
    {
	int32 imm = op & 15;
	uint32 v = r[sidx] & ~imm;
	R15++; r[didx] = v;
	GSU.vSign = v;
	GSU.vZero = v;
	FXR_TESTR14; FXR_CLRFLAGS;
    }
    FXR_NEXT
  mult:	/* mult rn */
    {
	uint32 v = (uint32)(SEX8(r[sidx]) * SEX8(r[op & 15]));
	R15++; r[didx] = v;
	GSU.vSign = v;
	GSU.vZero = v;
	FXR_TESTR14; FXR_CLRFLAGS;
    }
    FXR_NEXT
  umult:	/* umult rn */
    {
	uint32 v = USEX8(r[sidx]) * USEX8(r[op & 15]);
	R15++; r[didx] = v;
	GSU.vSign = v;
	GSU.vZero = v;
	FXR_TESTR14; FXR_CLRFLAGS;
    }
    FXR_NEXT
  mult_i:	/* mult #n */
    {
	int32 imm = op & 15;
	uint32 v = (uint32) (SEX8(r[sidx]) * ((int32)imm));
	R15++; r[didx] = v;
	GSU.vSign = v;
	GSU.vZero = v;
	FXR_TESTR14; FXR_CLRFLAGS;
    }
    FXR_NEXT
  umult_i:	/* umult #n */
    {
	int32 imm = op & 15;
	uint32 v = USEX8(r[sidx]) * ((uint32)imm);
	R15++; r[didx] = v;
	GSU.vSign = v;
	GSU.vZero = v;
	FXR_TESTR14; FXR_CLRFLAGS;
    }
    FXR_NEXT
  link:	/* link #n */
    R11 = R15 + (op & 15); FXR_CLRFLAGS; R15++;
    FXR_NEXT
  jmp:	/* jmp rn */
    R15 = r[op & 15];
    FXR_CLRFLAGS;
    FXR_NEXT
  ibt:	/* ibt rn,#pp */
    {
	uint32 n = op & 15;
	uint8 v = pipe; R15++;
	FXR_FETCHPIPE; R15++;
	r[n] = SEX8(v);
	FXR_CLRFLAGS;
	if (n == 14) READR14;
    }
    FXR_NEXT
  lms:	/* lms rn,(yy) */
    {
	uint32 n = op & 15;
	GSU.vLastRamAdr = ((uint32)pipe) << 1;
	R15++; FXR_FETCHPIPE; R15++;
	r[n] = (uint32)FXR_RAM(GSU.vLastRamAdr);
	r[n] |= ((uint32)FXR_RAM(GSU.vLastRamAdr+1))<<8;
	FXR_CLRFLAGS;
	if (n == 14) READR14;
    }
    FXR_NEXT
  sms:	/* sms (yy),rn */
    {
	uint32 v = r[op & 15];
	GSU.vLastRamAdr = ((uint32)pipe) << 1;
	R15++; FXR_FETCHPIPE;
	FXR_RAM(GSU.vLastRamAdr) = (uint8)v;
	FXR_RAM(GSU.vLastRamAdr+1) = (uint8)(v>>8);
	FXR_CLRFLAGS; R15++;
    }
    FXR_NEXT
  from:	/* from rn; moves rn (B) */
    {
	uint32 n = op & 15;
	if (sfr & FLG_B)
	{
	    uint32 v = r[n]; R15++; r[didx] = v;
	    GSU.vOverflow = (v&0x80) << 16; GSU.vSign = v; GSU.vZero = v;
	    FXR_TESTR14; FXR_CLRFLAGS;
	}
	else
	{
	    sidx = n;
	    R15++;
	}
    }
    FXR_NEXT
  or_r:	/* or rn */
    {
	uint32 v = r[sidx] | r[op & 15]; R15++; r[didx] = v;
	GSU.vSign = v;
	GSU.vZero = v;
	FXR_TESTR14; FXR_CLRFLAGS;
    }
    FXR_NEXT
  xor_r:	/* xor rn */
    {
	uint32 v = r[sidx] ^ r[op & 15]; R15++; r[didx] = v;
	GSU.vSign = v;
	GSU.vZero = v;
	FXR_TESTR14; FXR_CLRFLAGS;
    }
    FXR_NEXT
  or_i:	/* or #n */
    {
	int32 imm = op & 15;
	uint32 v = r[sidx] | imm; R15++; r[didx] = v;
	GSU.vSign = v;
	GSU.vZero = v;
	FXR_TESTR14; FXR_CLRFLAGS;
    }
    FXR_NEXT
  xor_i:	/* xor #n */
    {
	int32 imm = op & 15;
	uint32 v = r[sidx] ^ imm; R15++; r[didx] = v;
	GSU.vSign = v;
	GSU.vZero = v;
	FXR_TESTR14; FXR_CLRFLAGS;
    }
    FXR_NEXT
  inc:	/* inc rn */
    {
	uint32 n = op & 15;
	r[n] += 1;
	GSU.vSign = r[n];
	GSU.vZero = r[n];
	FXR_CLRFLAGS; R15++;
	if (n == 14) READR14;
    }
    FXR_NEXT
  dec:	/* dec rn */
    {
	uint32 n = op & 15;
	r[n] -= 1;
	GSU.vSign = r[n];
	GSU.vZero = r[n];
	FXR_CLRFLAGS; R15++;
	if (n == 14) READR14;
    }
    FXR_NEXT
  getb:	/* getb */
    {
	uint32 v = (uint32)GSU.vRomBuffer;
	R15++; r[didx] = v;
	FXR_TESTR14; FXR_CLRFLAGS;
    }
    FXR_NEXT
  iwt:	/* iwt rn,#xx */
    {
	uint32 n = op & 15;
	uint32 v = pipe; R15++; FXR_FETCHPIPE; R15++;
	v |= USEX8(pipe) << 8; FXR_FETCHPIPE; R15++;
	r[n] = v;
	FXR_CLRFLAGS;
	if (n == 14) READR14;
    }
    FXR_NEXT
  lm:	/* lm rn,(xx) */
    {
	uint32 n = op & 15;
	GSU.vLastRamAdr = pipe; R15++; FXR_FETCHPIPE; R15++;
	GSU.vLastRamAdr |= USEX8(pipe) << 8; FXR_FETCHPIPE; R15++;
	r[n] = FXR_RAM(GSU.vLastRamAdr);
	r[n] |= USEX8(FXR_RAM(GSU.vLastRamAdr^1)) << 8;
	FXR_CLRFLAGS;
	if (n == 14) READR14;
    }
    FXR_NEXT
  sm:	/* sm (xx),rn */
    {
	uint32 v = r[op & 15];
	GSU.vLastRamAdr = pipe; R15++; FXR_FETCHPIPE; R15++;
	GSU.vLastRamAdr |= USEX8(pipe) << 8; FXR_FETCHPIPE;
	FXR_RAM(GSU.vLastRamAdr) = (uint8)v;
	FXR_RAM(GSU.vLastRamAdr^1) = (uint8)(v>>8);
	FXR_CLRFLAGS; R15++;
    }
    FXR_NEXT
  slow:	/* the rest: through the table */
    FXR_SAVE;
    (*fx_apfOpcodeTable[(sfr & 0x300) | op])();
    FXR_LOAD;
    FXR_NEXT

  out:
    FXR_SAVE;
}

#ifdef FX_REFERENCE_RUN
/* The original loop, for tests/fx_test.cpp */
void fx_run_reference(uint32 nInstructions)
{
    uint32 vCounter = nInstructions;
    READR14;
    while( TF(G) && (vCounter-- > 0) )
    {
	/* Execute instruction from the pipe, and fetch next byte to the pipe */
	uint32 vOpcode = (uint32) PIPE;
	FETCHPIPE;
	(*fx_apfOpcodeTable[ (GSU.vStatusReg & 0x300) | vOpcode ])();
	if (vOpcode == 0 /* fx_stop opcode, all alternatives */)
		return;
    }
}
#endif

/*** Special table for the different plot configurations ***/

#ifdef FX_PLOT_TABLE
void (*FX_PLOT_TABLE[])() =
#else
void (*fx_apfPlotTable[])() =
#endif
{
    &fx_plot_2bit,    &fx_plot_4bit,	&fx_plot_4bit,	&fx_plot_8bit,	&fx_plot_obj,
    &fx_rpix_2bit,    &fx_rpix_4bit,    &fx_rpix_4bit,	&fx_rpix_8bit,	&fx_rpix_obj,
};

/*** Opcode table ***/

#ifdef FX_OPCODE_TABLE
void (*FX_OPCODE_TABLE[])() =
#else
void (*fx_apfOpcodeTable[])() =
#endif
{
    /*
     * ALT0 Table
     */
    /* 00 - 0f */
    &fx_stop,    &fx_nop,     &fx_cache,    &fx_lsr,      &fx_rol,      &fx_bra,      &fx_bge,      &fx_blt,
    &fx_bne,     &fx_beq,     &fx_bpl,      &fx_bmi,      &fx_bcc,      &fx_bcs,      &fx_bvc,      &fx_bvs,
    /* 10 - 1f */
    &fx_to_r0,   &fx_to_r1,   &fx_to_r2,    &fx_to_r3,    &fx_to_r4,    &fx_to_r5,    &fx_to_r6,    &fx_to_r7,
    &fx_to_r8,   &fx_to_r9,   &fx_to_r10,   &fx_to_r11,   &fx_to_r12,   &fx_to_r13,   &fx_to_r14,   &fx_to_r15,
    /* 20 - 2f */
    &fx_with_r0, &fx_with_r1, &fx_with_r2,  &fx_with_r3,  &fx_with_r4,  &fx_with_r5,  &fx_with_r6,  &fx_with_r7, 
    &fx_with_r8, &fx_with_r9, &fx_with_r10, &fx_with_r11, &fx_with_r12, &fx_with_r13, &fx_with_r14, &fx_with_r15,
    /* 30 - 3f */
    &fx_stw_r0,  &fx_stw_r1,  &fx_stw_r2,   &fx_stw_r3,   &fx_stw_r4,   &fx_stw_r5,   &fx_stw_r6,   &fx_stw_r7,
    &fx_stw_r8,  &fx_stw_r9,  &fx_stw_r10,  &fx_stw_r11,  &fx_loop,     &fx_alt1,     &fx_alt2,     &fx_alt3,
    /* 40 - 4f */
    &fx_ldw_r0,  &fx_ldw_r1,  &fx_ldw_r2,   &fx_ldw_r3,   &fx_ldw_r4,   &fx_ldw_r5,   &fx_ldw_r6,   &fx_ldw_r7,
    &fx_ldw_r8,  &fx_ldw_r9,  &fx_ldw_r10,  &fx_ldw_r11,  &fx_plot_2bit,&fx_swap,     &fx_color,    &fx_not,
    /* 50 - 5f */
    &fx_add_r0,  &fx_add_r1,  &fx_add_r2,   &fx_add_r3,   &fx_add_r4,   &fx_add_r5,   &fx_add_r6,   &fx_add_r7,
    &fx_add_r8,  &fx_add_r9,  &fx_add_r10,  &fx_add_r11,  &fx_add_r12,  &fx_add_r13,  &fx_add_r14,  &fx_add_r15,
    /* 60 - 6f */
    &fx_sub_r0,  &fx_sub_r1,  &fx_sub_r2,   &fx_sub_r3,   &fx_sub_r4,   &fx_sub_r5,   &fx_sub_r6,   &fx_sub_r7,
    &fx_sub_r8,  &fx_sub_r9,  &fx_sub_r10,  &fx_sub_r11,  &fx_sub_r12,  &fx_sub_r13,  &fx_sub_r14,  &fx_sub_r15,
    /* 70 - 7f */
    &fx_merge,   &fx_and_r1,  &fx_and_r2,   &fx_and_r3,   &fx_and_r4,   &fx_and_r5,   &fx_and_r6,   &fx_and_r7,
    &fx_and_r8,  &fx_and_r9,  &fx_and_r10,  &fx_and_r11,  &fx_and_r12,  &fx_and_r13,  &fx_and_r14,  &fx_and_r15,
    /* 80 - 8f */
    &fx_mult_r0, &fx_mult_r1, &fx_mult_r2,  &fx_mult_r3,  &fx_mult_r4,  &fx_mult_r5,  &fx_mult_r6,  &fx_mult_r7,
    &fx_mult_r8, &fx_mult_r9, &fx_mult_r10, &fx_mult_r11, &fx_mult_r12, &fx_mult_r13, &fx_mult_r14, &fx_mult_r15,
    /* 90 - 9f */
    &fx_sbk,     &fx_link_i1, &fx_link_i2,  &fx_link_i3,  &fx_link_i4,  &fx_sex,      &fx_asr,      &fx_ror,
    &fx_jmp_r8,  &fx_jmp_r9,  &fx_jmp_r10,  &fx_jmp_r11,  &fx_jmp_r12,  &fx_jmp_r13,  &fx_lob,      &fx_fmult,
    /* a0 - af */
    &fx_ibt_r0,  &fx_ibt_r1,  &fx_ibt_r2,   &fx_ibt_r3,   &fx_ibt_r4,   &fx_ibt_r5,   &fx_ibt_r6,   &fx_ibt_r7,
    &fx_ibt_r8,  &fx_ibt_r9,  &fx_ibt_r10,  &fx_ibt_r11,  &fx_ibt_r12,  &fx_ibt_r13,  &fx_ibt_r14,  &fx_ibt_r15,
    /* b0 - bf */
    &fx_from_r0, &fx_from_r1, &fx_from_r2,  &fx_from_r3,  &fx_from_r4,  &fx_from_r5,  &fx_from_r6,  &fx_from_r7,
    &fx_from_r8, &fx_from_r9, &fx_from_r10, &fx_from_r11, &fx_from_r12, &fx_from_r13, &fx_from_r14, &fx_from_r15,
    /* c0 - cf */
    &fx_hib,     &fx_or_r1,   &fx_or_r2,    &fx_or_r3,    &fx_or_r4,    &fx_or_r5,    &fx_or_r6,    &fx_or_r7,
    &fx_or_r8,   &fx_or_r9,   &fx_or_r10,   &fx_or_r11,   &fx_or_r12,   &fx_or_r13,   &fx_or_r14,   &fx_or_r15,
    /* d0 - df */
    &fx_inc_r0,  &fx_inc_r1,  &fx_inc_r2,   &fx_inc_r3,   &fx_inc_r4,   &fx_inc_r5,   &fx_inc_r6,   &fx_inc_r7,
    &fx_inc_r8,  &fx_inc_r9,  &fx_inc_r10,  &fx_inc_r11,  &fx_inc_r12,  &fx_inc_r13,  &fx_inc_r14,  &fx_getc,
    /* e0 - ef */
    &fx_dec_r0,  &fx_dec_r1,  &fx_dec_r2,   &fx_dec_r3,   &fx_dec_r4,   &fx_dec_r5,   &fx_dec_r6,   &fx_dec_r7,
    &fx_dec_r8,  &fx_dec_r9,  &fx_dec_r10,  &fx_dec_r11,  &fx_dec_r12,  &fx_dec_r13,  &fx_dec_r14,  &fx_getb,
    /* f0 - ff */
    &fx_iwt_r0,  &fx_iwt_r1,  &fx_iwt_r2,   &fx_iwt_r3,   &fx_iwt_r4,   &fx_iwt_r5,   &fx_iwt_r6,   &fx_iwt_r7,
    &fx_iwt_r8,  &fx_iwt_r9,  &fx_iwt_r10,  &fx_iwt_r11,  &fx_iwt_r12,  &fx_iwt_r13,  &fx_iwt_r14,  &fx_iwt_r15,

    /*
     * ALT1 Table
     */

    /* 00 - 0f */
    &fx_stop,    &fx_nop,     &fx_cache,    &fx_lsr,      &fx_rol,      &fx_bra,      &fx_bge,      &fx_blt,
    &fx_bne,     &fx_beq,     &fx_bpl,      &fx_bmi,      &fx_bcc,      &fx_bcs,      &fx_bvc,      &fx_bvs,
    /* 10 - 1f */
    &fx_to_r0,   &fx_to_r1,   &fx_to_r2,    &fx_to_r3,    &fx_to_r4,    &fx_to_r5,    &fx_to_r6,    &fx_to_r7,
    &fx_to_r8,   &fx_to_r9,   &fx_to_r10,   &fx_to_r11,   &fx_to_r12,   &fx_to_r13,   &fx_to_r14,   &fx_to_r15,
    /* 20 - 2f */
    &fx_with_r0, &fx_with_r1, &fx_with_r2,  &fx_with_r3,  &fx_with_r4,  &fx_with_r5,  &fx_with_r6,  &fx_with_r7, 
    &fx_with_r8, &fx_with_r9, &fx_with_r10, &fx_with_r11, &fx_with_r12, &fx_with_r13, &fx_with_r14, &fx_with_r15,
    /* 30 - 3f */
    &fx_stb_r0,  &fx_stb_r1,  &fx_stb_r2,   &fx_stb_r3,   &fx_stb_r4,   &fx_stb_r5,   &fx_stb_r6,   &fx_stb_r7,
    &fx_stb_r8,  &fx_stb_r9,  &fx_stb_r10,  &fx_stb_r11,  &fx_loop,     &fx_alt1,     &fx_alt2,     &fx_alt3,
    /* 40 - 4f */
    &fx_ldb_r0,  &fx_ldb_r1,  &fx_ldb_r2,   &fx_ldb_r3,   &fx_ldb_r4,   &fx_ldb_r5,   &fx_ldb_r6,   &fx_ldb_r7,
    &fx_ldb_r8,  &fx_ldb_r9,  &fx_ldb_r10,  &fx_ldb_r11,  &fx_rpix_2bit,&fx_swap,     &fx_cmode,    &fx_not,
    /* 50 - 5f */
    &fx_adc_r0,  &fx_adc_r1,  &fx_adc_r2,   &fx_adc_r3,   &fx_adc_r4,   &fx_adc_r5,   &fx_adc_r6,   &fx_adc_r7,
    &fx_adc_r8,  &fx_adc_r9,  &fx_adc_r10,  &fx_adc_r11,  &fx_adc_r12,  &fx_adc_r13,  &fx_adc_r14,  &fx_adc_r15,
    /* 60 - 6f */
    &fx_sbc_r0,  &fx_sbc_r1,  &fx_sbc_r2,   &fx_sbc_r3,   &fx_sbc_r4,   &fx_sbc_r5,   &fx_sbc_r6,   &fx_sbc_r7,
    &fx_sbc_r8,  &fx_sbc_r9,  &fx_sbc_r10,  &fx_sbc_r11,  &fx_sbc_r12,  &fx_sbc_r13,  &fx_sbc_r14,  &fx_sbc_r15,
    /* 70 - 7f */
    &fx_merge,   &fx_bic_r1,  &fx_bic_r2,   &fx_bic_r3,   &fx_bic_r4,   &fx_bic_r5,   &fx_bic_r6,   &fx_bic_r7,
    &fx_bic_r8,  &fx_bic_r9,  &fx_bic_r10,  &fx_bic_r11,  &fx_bic_r12,  &fx_bic_r13,  &fx_bic_r14,  &fx_bic_r15,
    /* 80 - 8f */
    &fx_umult_r0,&fx_umult_r1,&fx_umult_r2, &fx_umult_r3, &fx_umult_r4, &fx_umult_r5, &fx_umult_r6, &fx_umult_r7,
    &fx_umult_r8,&fx_umult_r9,&fx_umult_r10,&fx_umult_r11,&fx_umult_r12,&fx_umult_r13,&fx_umult_r14,&fx_umult_r15,
    /* 90 - 9f */
    &fx_sbk,     &fx_link_i1, &fx_link_i2,  &fx_link_i3,  &fx_link_i4,  &fx_sex,      &fx_div2,     &fx_ror,
    &fx_ljmp_r8, &fx_ljmp_r9, &fx_ljmp_r10, &fx_ljmp_r11, &fx_ljmp_r12, &fx_ljmp_r13, &fx_lob,      &fx_lmult,
    /* a0 - af */
    &fx_lms_r0,  &fx_lms_r1,  &fx_lms_r2,   &fx_lms_r3,   &fx_lms_r4,   &fx_lms_r5,   &fx_lms_r6,   &fx_lms_r7,
    &fx_lms_r8,  &fx_lms_r9,  &fx_lms_r10,  &fx_lms_r11,  &fx_lms_r12,  &fx_lms_r13,  &fx_lms_r14,  &fx_lms_r15,
    /* b0 - bf */
    &fx_from_r0, &fx_from_r1, &fx_from_r2,  &fx_from_r3,  &fx_from_r4,  &fx_from_r5,  &fx_from_r6,  &fx_from_r7,
    &fx_from_r8, &fx_from_r9, &fx_from_r10, &fx_from_r11, &fx_from_r12, &fx_from_r13, &fx_from_r14, &fx_from_r15,
    /* c0 - cf */
    &fx_hib,     &fx_xor_r1,  &fx_xor_r2,   &fx_xor_r3,   &fx_xor_r4,   &fx_xor_r5,   &fx_xor_r6,   &fx_xor_r7,
    &fx_xor_r8,  &fx_xor_r9,  &fx_xor_r10,  &fx_xor_r11,  &fx_xor_r12,  &fx_xor_r13,  &fx_xor_r14,  &fx_xor_r15,
    /* d0 - df */
    &fx_inc_r0,  &fx_inc_r1,  &fx_inc_r2,   &fx_inc_r3,   &fx_inc_r4,   &fx_inc_r5,   &fx_inc_r6,   &fx_inc_r7,
    &fx_inc_r8,  &fx_inc_r9,  &fx_inc_r10,  &fx_inc_r11,  &fx_inc_r12,  &fx_inc_r13,  &fx_inc_r14,  &fx_getc,
    /* e0 - ef */
    &fx_dec_r0,  &fx_dec_r1,  &fx_dec_r2,   &fx_dec_r3,   &fx_dec_r4,   &fx_dec_r5,   &fx_dec_r6,   &fx_dec_r7,
    &fx_dec_r8,  &fx_dec_r9,  &fx_dec_r10,  &fx_dec_r11,  &fx_dec_r12,  &fx_dec_r13,  &fx_dec_r14,  &fx_getbh,
    /* f0 - ff */
    &fx_lm_r0,   &fx_lm_r1,   &fx_lm_r2,    &fx_lm_r3,    &fx_lm_r4,    &fx_lm_r5,    &fx_lm_r6,    &fx_lm_r7,
    &fx_lm_r8,   &fx_lm_r9,   &fx_lm_r10,   &fx_lm_r11,   &fx_lm_r12,   &fx_lm_r13,   &fx_lm_r14,   &fx_lm_r15,

    /*
     * ALT2 Table
     */

    /* 00 - 0f */
    &fx_stop,    &fx_nop,     &fx_cache,    &fx_lsr,      &fx_rol,      &fx_bra,      &fx_bge,      &fx_blt,
    &fx_bne,     &fx_beq,     &fx_bpl,      &fx_bmi,      &fx_bcc,      &fx_bcs,      &fx_bvc,      &fx_bvs,
    /* 10 - 1f */
    &fx_to_r0,   &fx_to_r1,   &fx_to_r2,    &fx_to_r3,    &fx_to_r4,    &fx_to_r5,    &fx_to_r6,    &fx_to_r7,
    &fx_to_r8,   &fx_to_r9,   &fx_to_r10,   &fx_to_r11,   &fx_to_r12,   &fx_to_r13,   &fx_to_r14,   &fx_to_r15,
    /* 20 - 2f */
    &fx_with_r0, &fx_with_r1, &fx_with_r2,  &fx_with_r3,  &fx_with_r4,  &fx_with_r5,  &fx_with_r6,  &fx_with_r7, 
    &fx_with_r8, &fx_with_r9, &fx_with_r10, &fx_with_r11, &fx_with_r12, &fx_with_r13, &fx_with_r14, &fx_with_r15,
    /* 30 - 3f */
    &fx_stw_r0,  &fx_stw_r1,  &fx_stw_r2,   &fx_stw_r3,   &fx_stw_r4,   &fx_stw_r5,   &fx_stw_r6,   &fx_stw_r7,
    &fx_stw_r8,  &fx_stw_r9,  &fx_stw_r10,  &fx_stw_r11,  &fx_loop,     &fx_alt1,     &fx_alt2,     &fx_alt3,
    /* 40 - 4f */
    &fx_ldw_r0,  &fx_ldw_r1,  &fx_ldw_r2,   &fx_ldw_r3,   &fx_ldw_r4,   &fx_ldw_r5,   &fx_ldw_r6,   &fx_ldw_r7,
    &fx_ldw_r8,  &fx_ldw_r9,  &fx_ldw_r10,  &fx_ldw_r11,  &fx_plot_2bit,&fx_swap,     &fx_color,    &fx_not,
    /* 50 - 5f */
    &fx_add_i0,  &fx_add_i1,  &fx_add_i2,   &fx_add_i3,   &fx_add_i4,   &fx_add_i5,   &fx_add_i6,   &fx_add_i7,
    &fx_add_i8,  &fx_add_i9,  &fx_add_i10,  &fx_add_i11,  &fx_add_i12,  &fx_add_i13,  &fx_add_i14,  &fx_add_i15,
    /* 60 - 6f */
    &fx_sub_i0,  &fx_sub_i1,  &fx_sub_i2,   &fx_sub_i3,   &fx_sub_i4,   &fx_sub_i5,   &fx_sub_i6,   &fx_sub_i7,
    &fx_sub_i8,  &fx_sub_i9,  &fx_sub_i10,  &fx_sub_i11,  &fx_sub_i12,  &fx_sub_i13,  &fx_sub_i14,  &fx_sub_i15,
    /* 70 - 7f */
    &fx_merge,   &fx_and_i1,  &fx_and_i2,   &fx_and_i3,   &fx_and_i4,   &fx_and_i5,   &fx_and_i6,   &fx_and_i7,
    &fx_and_i8,  &fx_and_i9,  &fx_and_i10,  &fx_and_i11,  &fx_and_i12,  &fx_and_i13,  &fx_and_i14,  &fx_and_i15,
    /* 80 - 8f */
    &fx_mult_i0, &fx_mult_i1, &fx_mult_i2,  &fx_mult_i3,  &fx_mult_i4,  &fx_mult_i5,  &fx_mult_i6,  &fx_mult_i7,
    &fx_mult_i8, &fx_mult_i9, &fx_mult_i10, &fx_mult_i11, &fx_mult_i12, &fx_mult_i13, &fx_mult_i14, &fx_mult_i15,
    /* 90 - 9f */
    &fx_sbk,     &fx_link_i1, &fx_link_i2,  &fx_link_i3,  &fx_link_i4,  &fx_sex,      &fx_asr,      &fx_ror,
    &fx_jmp_r8,  &fx_jmp_r9,  &fx_jmp_r10,  &fx_jmp_r11,  &fx_jmp_r12,  &fx_jmp_r13,  &fx_lob,      &fx_fmult,
    /* a0 - af */
    &fx_sms_r0,  &fx_sms_r1,  &fx_sms_r2,   &fx_sms_r3,   &fx_sms_r4,   &fx_sms_r5,   &fx_sms_r6,   &fx_sms_r7,
    &fx_sms_r8,  &fx_sms_r9,  &fx_sms_r10,  &fx_sms_r11,  &fx_sms_r12,  &fx_sms_r13,  &fx_sms_r14,  &fx_sms_r15,
    /* b0 - bf */
    &fx_from_r0, &fx_from_r1, &fx_from_r2,  &fx_from_r3,  &fx_from_r4,  &fx_from_r5,  &fx_from_r6,  &fx_from_r7,
    &fx_from_r8, &fx_from_r9, &fx_from_r10, &fx_from_r11, &fx_from_r12, &fx_from_r13, &fx_from_r14, &fx_from_r15,
    /* c0 - cf */
    &fx_hib,     &fx_or_i1,   &fx_or_i2,    &fx_or_i3,    &fx_or_i4,    &fx_or_i5,    &fx_or_i6,    &fx_or_i7,
    &fx_or_i8,   &fx_or_i9,   &fx_or_i10,   &fx_or_i11,   &fx_or_i12,   &fx_or_i13,   &fx_or_i14,   &fx_or_i15,
    /* d0 - df */
    &fx_inc_r0,  &fx_inc_r1,  &fx_inc_r2,   &fx_inc_r3,   &fx_inc_r4,   &fx_inc_r5,   &fx_inc_r6,   &fx_inc_r7,
    &fx_inc_r8,  &fx_inc_r9,  &fx_inc_r10,  &fx_inc_r11,  &fx_inc_r12,  &fx_inc_r13,  &fx_inc_r14,  &fx_ramb,
    /* e0 - ef */
    &fx_dec_r0,  &fx_dec_r1,  &fx_dec_r2,   &fx_dec_r3,   &fx_dec_r4,   &fx_dec_r5,   &fx_dec_r6,   &fx_dec_r7,
    &fx_dec_r8,  &fx_dec_r9,  &fx_dec_r10,  &fx_dec_r11,  &fx_dec_r12,  &fx_dec_r13,  &fx_dec_r14,  &fx_getbl,
    /* f0 - ff */
    &fx_sm_r0,   &fx_sm_r1,   &fx_sm_r2,    &fx_sm_r3,    &fx_sm_r4,    &fx_sm_r5,    &fx_sm_r6,    &fx_sm_r7,
    &fx_sm_r8,   &fx_sm_r9,   &fx_sm_r10,   &fx_sm_r11,   &fx_sm_r12,   &fx_sm_r13,   &fx_sm_r14,   &fx_sm_r15,

    /*
     * ALT3 Table
     */

    /* 00 - 0f */
    &fx_stop,    &fx_nop,     &fx_cache,    &fx_lsr,      &fx_rol,      &fx_bra,      &fx_bge,      &fx_blt,
    &fx_bne,     &fx_beq,     &fx_bpl,      &fx_bmi,      &fx_bcc,      &fx_bcs,      &fx_bvc,      &fx_bvs,
    /* 10 - 1f */
    &fx_to_r0,   &fx_to_r1,   &fx_to_r2,    &fx_to_r3,    &fx_to_r4,    &fx_to_r5,    &fx_to_r6,    &fx_to_r7,
    &fx_to_r8,   &fx_to_r9,   &fx_to_r10,   &fx_to_r11,   &fx_to_r12,   &fx_to_r13,   &fx_to_r14,   &fx_to_r15,
    /* 20 - 2f */
    &fx_with_r0, &fx_with_r1, &fx_with_r2,  &fx_with_r3,  &fx_with_r4,  &fx_with_r5,  &fx_with_r6,  &fx_with_r7, 
    &fx_with_r8, &fx_with_r9, &fx_with_r10, &fx_with_r11, &fx_with_r12, &fx_with_r13, &fx_with_r14, &fx_with_r15,
    /* 30 - 3f */
    &fx_stb_r0,  &fx_stb_r1,  &fx_stb_r2,   &fx_stb_r3,   &fx_stb_r4,   &fx_stb_r5,   &fx_stb_r6,   &fx_stb_r7,
    &fx_stb_r8,  &fx_stb_r9,  &fx_stb_r10,  &fx_stb_r11,  &fx_loop,     &fx_alt1,     &fx_alt2,     &fx_alt3,
    /* 40 - 4f */
    &fx_ldb_r0,  &fx_ldb_r1,  &fx_ldb_r2,   &fx_ldb_r3,   &fx_ldb_r4,   &fx_ldb_r5,   &fx_ldb_r6,   &fx_ldb_r7,
    &fx_ldb_r8,  &fx_ldb_r9,  &fx_ldb_r10,  &fx_ldb_r11,  &fx_rpix_2bit,&fx_swap,     &fx_cmode,    &fx_not,
    /* 50 - 5f */
    &fx_adc_i0,  &fx_adc_i1,  &fx_adc_i2,   &fx_adc_i3,   &fx_adc_i4,   &fx_adc_i5,   &fx_adc_i6,   &fx_adc_i7,
    &fx_adc_i8,  &fx_adc_i9,  &fx_adc_i10,  &fx_adc_i11,  &fx_adc_i12,  &fx_adc_i13,  &fx_adc_i14,  &fx_adc_i15,
    /* 60 - 6f */
    &fx_cmp_r0,  &fx_cmp_r1,  &fx_cmp_r2,   &fx_cmp_r3,   &fx_cmp_r4,   &fx_cmp_r5,   &fx_cmp_r6,   &fx_cmp_r7,
    &fx_cmp_r8,  &fx_cmp_r9,  &fx_cmp_r10,  &fx_cmp_r11,  &fx_cmp_r12,  &fx_cmp_r13,  &fx_cmp_r14,  &fx_cmp_r15,
    /* 70 - 7f */
    &fx_merge,   &fx_bic_i1,  &fx_bic_i2,   &fx_bic_i3,   &fx_bic_i4,   &fx_bic_i5,   &fx_bic_i6,   &fx_bic_i7,
    &fx_bic_i8,  &fx_bic_i9,  &fx_bic_i10,  &fx_bic_i11,  &fx_bic_i12,  &fx_bic_i13,  &fx_bic_i14,  &fx_bic_i15,
    /* 80 - 8f */
    &fx_umult_i0,&fx_umult_i1,&fx_umult_i2, &fx_umult_i3, &fx_umult_i4, &fx_umult_i5, &fx_umult_i6, &fx_umult_i7,
    &fx_umult_i8,&fx_umult_i9,&fx_umult_i10,&fx_umult_i11,&fx_umult_i12,&fx_umult_i13,&fx_umult_i14,&fx_umult_i15,
    /* 90 - 9f */
    &fx_sbk,     &fx_link_i1, &fx_link_i2,  &fx_link_i3,  &fx_link_i4,  &fx_sex,      &fx_div2,     &fx_ror,
    &fx_ljmp_r8, &fx_ljmp_r9, &fx_ljmp_r10, &fx_ljmp_r11, &fx_ljmp_r12, &fx_ljmp_r13, &fx_lob,      &fx_lmult,
    /* a0 - af */
    &fx_lms_r0,  &fx_lms_r1,  &fx_lms_r2,   &fx_lms_r3,   &fx_lms_r4,   &fx_lms_r5,   &fx_lms_r6,   &fx_lms_r7,
    &fx_lms_r8,  &fx_lms_r9,  &fx_lms_r10,  &fx_lms_r11,  &fx_lms_r12,  &fx_lms_r13,  &fx_lms_r14,  &fx_lms_r15,
    /* b0 - bf */
    &fx_from_r0, &fx_from_r1, &fx_from_r2,  &fx_from_r3,  &fx_from_r4,  &fx_from_r5,  &fx_from_r6,  &fx_from_r7,
    &fx_from_r8, &fx_from_r9, &fx_from_r10, &fx_from_r11, &fx_from_r12, &fx_from_r13, &fx_from_r14, &fx_from_r15,
    /* c0 - cf */
    &fx_hib,     &fx_xor_i1,  &fx_xor_i2,   &fx_xor_i3,   &fx_xor_i4,   &fx_xor_i5,   &fx_xor_i6,   &fx_xor_i7,
    &fx_xor_i8,  &fx_xor_i9,  &fx_xor_i10,  &fx_xor_i11,  &fx_xor_i12,  &fx_xor_i13,  &fx_xor_i14,  &fx_xor_i15,
    /* d0 - df */
    &fx_inc_r0,  &fx_inc_r1,  &fx_inc_r2,   &fx_inc_r3,   &fx_inc_r4,   &fx_inc_r5,   &fx_inc_r6,   &fx_inc_r7,
    &fx_inc_r8,  &fx_inc_r9,  &fx_inc_r10,  &fx_inc_r11,  &fx_inc_r12,  &fx_inc_r13,  &fx_inc_r14,  &fx_romb,
    /* e0 - ef */
    &fx_dec_r0,  &fx_dec_r1,  &fx_dec_r2,   &fx_dec_r3,   &fx_dec_r4,   &fx_dec_r5,   &fx_dec_r6,   &fx_dec_r7,
    &fx_dec_r8,  &fx_dec_r9,  &fx_dec_r10,  &fx_dec_r11,  &fx_dec_r12,  &fx_dec_r13,  &fx_dec_r14,  &fx_getbs,
    /* f0 - ff */
    &fx_lm_r0,   &fx_lm_r1,   &fx_lm_r2,    &fx_lm_r3,    &fx_lm_r4,    &fx_lm_r5,    &fx_lm_r6,    &fx_lm_r7,
    &fx_lm_r8,   &fx_lm_r9,   &fx_lm_r10,   &fx_lm_r11,   &fx_lm_r12,   &fx_lm_r13,   &fx_lm_r14,   &fx_lm_r15,
};

