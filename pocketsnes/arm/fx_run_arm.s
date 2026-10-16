@ The Super FX (GSU) interpreter in ARM assembler, for the calculator.
@
@ fx_run (pocketsnes/snes9x/fxinst.cpp) calls fx_run_arm to run the GSU until
@ it stops. Every instruction does exactly what the C interpreter does:
@ tests/asm/run.sh runs tests/fx_test.cpp on ARM, which compares this (built
@ with FX_COUNTED, to stop after a given number of instructions) with the
@ original C loop on random states and programs, the whole GSU state and RAM.
@
@ While it runs, the state every instruction touches is in ARM registers:
@   r4  &GSU                r5  status register (SFR: ALT1, ALT2, B, G...)
@   r6  R15                 r7  the pipe byte
@   r8  program bank        r9  RAM bank
@   r10 the dispatch table  r11 source register number (0-15)
@   r12 destination register number (0-15)
@ and at a handler's start r0 = ALT mode << 8 | opcode. R15 lives in r6, so
@ GSU.avReg[15] in memory is out of date until it's written back.
@
@ Three dispatch tables, one after the other (fx_arm_setup_tables in
@ fxinst.cpp fills them from fx_arm_handlers):
@  - fast: the destination is R0-R13 and the source isn't R15;
@  - careful: the source or the destination is R15. Everything but stop goes
@    to fx_arm_slow, which writes the state back, calls the instruction's C
@    function from fx_apfOpcodeTable and reads the state again;
@  - R14: the destination is R14 (after "to r14" or "with r14"). The same
@    handlers' _d14 versions, which reload the ROM buffer after writing the
@    destination (the C code's TESTR14) and go back to the fast table when
@    the instruction resets the destination (CLRFLAGS).
@ Rare instructions, and any that would read R15 or write R15 by register
@ number, go to fx_arm_slow from every table. So the code here can assume
@ sreg != 15 and dreg != 15, and dreg == 14 only in the _d14 versions.

	.syntax unified
	.arm

.ifdef FX_COUNTED
	.set	TABLES, fx_arm_tables_counted
.else
	.set	TABLES, fx_arm_tables
.endif

	.text

@ struct FxRegs_s offsets (fxinst.cpp checks them with static_assert)
	.equ R1OFF,		4
	.equ R2OFF,		8
	.equ R7OFF,		28
	.equ R8OFF,		32
	.equ R11OFF,		44
	.equ R12OFF,		48
	.equ R13OFF,		52
	.equ R14OFF,		56
	.equ R15OFF,		60
	.equ VCOLORREG,		64
	.equ VPLOTOPTIONREG,	68
	.equ VSTATUSREG,	72
	.equ VLASTRAMADR,	96
	.equ PVDREG,		100
	.equ PVSREG,		104
	.equ VROMBUFFER,	108
	.equ VPIPE,		109
	.equ VSIGN,		116
	.equ VZERO,		120
	.equ VCARRY,		124
	.equ VOVERFLOW,		128
	.equ PVREGISTERS,	140
	.equ APVSCREEN,		172
	.equ XTAB,		300
	.equ VSCREENHEIGHT,	428
	.equ PVRAMBANK,		444
	.equ PVROMBANK,		448
	.equ PVPRGBANK,		452

	.equ FLG_G,		0x20
	.equ FLG_ALT1,		0x100
	.equ FLG_ALT2,		0x200
	.equ FLG_B,		0x1000
	.equ FLG_IRQ,		0x8000
	.equ GSU_CFGR,		0x37

	.equ CAREFUL,		4096	@ the careful table, after the fast one
	.equ D14,		8192	@ the R14 table, after the careful one

@ Fetch the next byte into the pipe and run the instruction that was in it
.macro DISPATCH
.ifdef FX_COUNTED
	subs	lr, lr, #1
	beq	fx_exit
.endif
	and	r0, r5, #0x300
	orr	r0, r0, r7
	mov	r1, r6, lsl #16
	ldrb	r7, [r8, r1, lsr #16]
	ldr	pc, [r10, r0, lsl #2]
.endm

@ The end of most instructions: ALT1, ALT2 and B off, source and destination
@ R0 (and, from the R14 table, back to the fast one)
.macro CLRFLAGS d14=0
	bic	r5, r5, #(FLG_ALT1 | FLG_ALT2 | FLG_B)
	mov	r11, #0
	mov	r12, #0
.if \d14
	sub	r10, r10, #D14
.endif
.endm

@ The ROM buffer: GSU.vRomBuffer = ROM bank[R14 & 0xffff] (uses r1, r2)
.macro READR14
	ldr	r1, [r4, #PVROMBANK]
	ldr	r2, [r4, #R14OFF]
	mov	r2, r2, lsl #16
	ldrb	r2, [r1, r2, lsr #16]
	strb	r2, [r4, #VROMBUFFER]
.endm

@ The C code's TESTR14, after writing the destination: only R14 reloads
.macro TESTR14 d14=0
.if \d14
	READR14
.endif
.endm

@ pipe = program bank[R15 & 0xffff] (uses r1)
.macro FETCHPIPE
	mov	r1, r6, lsl #16
	ldrb	r7, [r8, r1, lsr #16]
.endm

@ Picks the table for the current source and destination
.macro SELECT_TABLE
	ldr	r10, =TABLES
	cmp	r12, #14
	addeq	r10, r10, #D14		@ dreg 14: the R14 table
	addhi	r10, r10, #CAREFUL	@ dreg 15: careful
	cmp	r11, #15
	ldreq	r10, =TABLES+CAREFUL	@ sreg 15: careful
.endm

@ vSign = vZero = \reg
.macro SIGN_ZERO reg
	str	\reg, [r4, #VSIGN]
	str	\reg, [r4, #VZERO]
.endm

@ A handler's label: fx_arm_NAME, or fx_arm_NAME_d14 for the R14 table
.macro HLABEL name, d14
.if \d14
fx_arm_\name\()_d14:
.else
fx_arm_\name:
.endif
.endm

@ ---------------------------------------------------------------------------

.ifdef FX_COUNTED
	.global	fx_run_arm_counted
fx_run_arm_counted:		@ (uint32 nInstructions); 0 runs nothing
.else
	.global	fx_run_arm
fx_run_arm:
.endif
	push	{r4-r11, lr}
.ifdef FX_COUNTED
	mov	lr, r0
	add	lr, lr, #1		@ (DISPATCH counts down before each instruction)
.endif
	ldr	r4, =GSU
	READR14
	ldr	r5, [r4, #VSTATUSREG]
	tst	r5, #FLG_G		@ only stop clears G, and stop ends the run
	popeq	{r4-r11, pc}
.ifdef FX_COUNTED
	cmp	lr, #1
	popeq	{r4-r11, pc}
.endif
	ldr	r6, [r4, #R15OFF]
	ldrb	r7, [r4, #VPIPE]
	ldr	r8, [r4, #PVPRGBANK]
	ldr	r9, [r4, #PVRAMBANK]
	ldr	r1, [r4, #PVSREG]
	sub	r1, r1, r4
	mov	r11, r1, lsr #2
	ldr	r1, [r4, #PVDREG]
	sub	r1, r1, r4
	mov	r12, r1, lsr #2
	SELECT_TABLE
	DISPATCH

@ Writes the state back and returns
fx_exit:
	str	r5, [r4, #VSTATUSREG]
	str	r6, [r4, #R15OFF]
	strb	r7, [r4, #VPIPE]
	add	r1, r4, r11, lsl #2
	str	r1, [r4, #PVSREG]
	add	r1, r4, r12, lsl #2
	str	r1, [r4, #PVDREG]
	pop	{r4-r11, pc}

@ Any instruction, through its C function
fx_arm_slow:
	str	r5, [r4, #VSTATUSREG]
	str	r6, [r4, #R15OFF]
	strb	r7, [r4, #VPIPE]
	add	r1, r4, r11, lsl #2
	str	r1, [r4, #PVSREG]
	add	r1, r4, r12, lsl #2
	str	r1, [r4, #PVDREG]
	ldr	r1, =fx_apfOpcodeTable
	ldr	r1, [r1, r0, lsl #2]
.ifdef FX_COUNTED
	str	lr, [sp, #-8]!		@ (the count; 8 keeps the stack aligned)
.endif
	blx	r1
.ifdef FX_COUNTED
	ldr	lr, [sp], #8
.endif
	ldr	r5, [r4, #VSTATUSREG]
	ldr	r6, [r4, #R15OFF]
	ldrb	r7, [r4, #VPIPE]
	ldr	r8, [r4, #PVPRGBANK]
	ldr	r9, [r4, #PVRAMBANK]
	ldr	r1, [r4, #PVSREG]
	sub	r1, r1, r4
	mov	r11, r1, lsr #2
	ldr	r1, [r4, #PVDREG]
	sub	r1, r1, r4
	mov	r12, r1, lsr #2
	SELECT_TABLE
	DISPATCH

	.ltorg				@ (the constants above)

@ ---------------------------------------------------------------------------
@ Instructions that don't depend on the destination (the same in every table)

@ 00 stop
fx_arm_stop:
	bic	r5, r5, #FLG_G
	ldr	r1, [r4, #PVREGISTERS]
	ldrb	r1, [r1, #GSU_CFGR]
	tst	r1, #0x80
	orreq	r5, r5, #FLG_IRQ
	mov	r1, #0
	str	r1, [r4, #VPLOTOPTIONREG]
	mov	r7, #1
	CLRFLAGS
	add	r6, r6, #1
	b	fx_exit

@ 05 bra
fx_arm_bra:
	mov	r3, r7, lsl #24
	add	r6, r6, #1
	FETCHPIPE
	add	r6, r6, r3, asr #24
	DISPATCH

@ Conditional branches: the offset is the pipe byte; TEST_name sets the ARM
@ flags, \yes is the condition that branches.
.macro BRANCH name, yes, no
fx_arm_\name:
	mov	r3, r7, lsl #24
	add	r6, r6, #1
	FETCHPIPE
	TEST_\name
	add\yes	r6, r6, r3, asr #24
	add\no	r6, r6, #1
	DISPATCH
.endm

@ TEST_S: vSign & 0x8000; TEST_Z: (uint16) vZero == 0; TEST_CY: vCarry & 1;
@ TEST_OV: vOverflow >= 0x8000 || vOverflow < -0x8000, that is
@ (uint32) (vOverflow + 0x8000) >= 0x10000.
.macro TEST_OV_BIT reg		@ \reg = TEST_OV != 0 (uses r2)
	ldr	r2, [r4, #VOVERFLOW]
	add	r2, r2, #0x8000
	cmp	r2, #0x10000
	movlo	\reg, #0
	movhs	\reg, #1
.endm
.macro TEST_bge			@ (fx_bge, opcode 06) S == OV: eq
	TEST_OV_BIT r1
	ldr	r2, [r4, #VSIGN]
	and	r2, r2, #0x8000
	cmp	r1, r2, lsr #15
.endm
.macro TEST_blt			@ (fx_blt, opcode 07) S != OV: ne
	TEST_bge
.endm
.macro TEST_bne
	ldr	r2, [r4, #VZERO]
	movs	r2, r2, lsl #16
.endm
.macro TEST_beq
	TEST_bne
.endm
.macro TEST_bpl
	ldr	r2, [r4, #VSIGN]
	tst	r2, #0x8000
.endm
.macro TEST_bmi
	TEST_bpl
.endm
.macro TEST_bcc
	ldr	r2, [r4, #VCARRY]
	tst	r2, #1
.endm
.macro TEST_bcs
	TEST_bcc
.endm
.macro TEST_bvc
	ldr	r2, [r4, #VOVERFLOW]
	add	r2, r2, #0x8000
	cmp	r2, #0x10000
.endm
.macro TEST_bvs
	TEST_bvc
.endm

	BRANCH	bge, eq, ne
	BRANCH	blt, ne, eq
	BRANCH	bne, ne, eq
	BRANCH	beq, eq, ne
	BRANCH	bpl, eq, ne
	BRANCH	bmi, ne, eq
	BRANCH	bcc, eq, ne
	BRANCH	bcs, ne, eq
	BRANCH	bvc, lo, hs
	BRANCH	bvs, hs, lo

@ 3d-3f alt1, alt2, alt3
fx_arm_alt1:
	orr	r5, r5, #FLG_ALT1
	bic	r5, r5, #FLG_B
	add	r6, r6, #1
	DISPATCH
fx_arm_alt2:
	orr	r5, r5, #FLG_ALT2
	bic	r5, r5, #FLG_B
	add	r6, r6, #1
	DISPATCH
fx_arm_alt3:
	orr	r5, r5, #(FLG_ALT1 | FLG_ALT2)
	bic	r5, r5, #FLG_B
	add	r6, r6, #1
	DISPATCH

	.ltorg

@ ---------------------------------------------------------------------------
@ The rest, each in a fast version and an R14 one (d14 = 1)

@ 01 nop
.macro H_nop d14
	HLABEL nop, \d14
	CLRFLAGS \d14
	add	r6, r6, #1
	DISPATCH
.endm

@ 03 lsr
.macro H_lsr d14
	HLABEL lsr, \d14
	ldr	r1, [r4, r11, lsl #2]
	and	r2, r1, #1
	str	r2, [r4, #VCARRY]
	mov	r1, r1, lsl #16
	mov	r1, r1, lsr #17
	add	r6, r6, #1
	str	r1, [r4, r12, lsl #2]
	SIGN_ZERO r1
	TESTR14 \d14
	CLRFLAGS \d14
	DISPATCH
.endm

@ 04 rol
.macro H_rol d14
	HLABEL rol, \d14
	ldr	r1, [r4, r11, lsl #2]
	ldr	r2, [r4, #VCARRY]
	add	r3, r2, r1, lsl #1
	mov	r3, r3, lsl #16
	mov	r3, r3, lsr #16
	mov	r2, r1, lsr #15
	and	r2, r2, #1
	str	r2, [r4, #VCARRY]
	add	r6, r6, #1
	str	r3, [r4, r12, lsl #2]
	SIGN_ZERO r3
	TESTR14 \d14
	CLRFLAGS \d14
	DISPATCH
.endm

@ 10-1d to rn; move rn (B)
.macro H_to d14
	HLABEL to, \d14
	and	r3, r0, #15
	tst	r5, #FLG_B
	beq	1f
	ldr	r1, [r4, r11, lsl #2]
	str	r1, [r4, r3, lsl #2]
	CLRFLAGS \d14
	add	r6, r6, #1
	DISPATCH
1:	mov	r12, r3
.if \d14
	sub	r10, r10, #D14		@ (the destination isn't R14 any more)
.endif
	add	r6, r6, #1
	DISPATCH
.endm

@ 1e to r14; move r14 (B)
.macro H_to_r14 d14
	HLABEL to_r14, \d14
	tst	r5, #FLG_B
	beq	1f
	ldr	r1, [r4, r11, lsl #2]
	str	r1, [r4, #R14OFF]
	CLRFLAGS \d14
	READR14
	add	r6, r6, #1
	DISPATCH
1:	mov	r12, #14
.if !\d14
	add	r10, r10, #D14		@ (to the R14 table)
.endif
	add	r6, r6, #1
	DISPATCH
.endm

@ 20-2d with rn
.macro H_with d14
	HLABEL with, \d14
	orr	r5, r5, #FLG_B
	and	r11, r0, #15
	mov	r12, r11
.if \d14
	sub	r10, r10, #D14
.endif
	add	r6, r6, #1
	DISPATCH
.endm

@ 2e with r14
.macro H_with_r14 d14
	HLABEL with_r14, \d14
	orr	r5, r5, #FLG_B
	mov	r11, #14
	mov	r12, #14
.if !\d14
	add	r10, r10, #D14
.endif
	add	r6, r6, #1
	DISPATCH
.endm

@ 30-3b stw (rn)
.macro H_stw d14
	HLABEL stw, \d14
	and	r3, r0, #15
	ldr	r3, [r4, r3, lsl #2]
	str	r3, [r4, #VLASTRAMADR]
	ldr	r1, [r4, r11, lsl #2]
	mov	r2, r3, lsl #16
	strb	r1, [r9, r2, lsr #16]
	eor	r3, r3, #1
	mov	r3, r3, lsl #16
	mov	r1, r1, lsr #8
	strb	r1, [r9, r3, lsr #16]
	CLRFLAGS \d14
	add	r6, r6, #1
	DISPATCH
.endm

@ 30-3b(ALT1) stb (rn)
.macro H_stb d14
	HLABEL stb, \d14
	and	r3, r0, #15
	ldr	r3, [r4, r3, lsl #2]
	str	r3, [r4, #VLASTRAMADR]
	ldr	r1, [r4, r11, lsl #2]
	mov	r2, r3, lsl #16
	strb	r1, [r9, r2, lsr #16]
	CLRFLAGS \d14
	add	r6, r6, #1
	DISPATCH
.endm

@ 3c loop
.macro H_loop d14
	HLABEL loop, \d14
	ldr	r1, [r4, #R12OFF]
	sub	r1, r1, #1
	str	r1, [r4, #R12OFF]
	SIGN_ZERO r1
	movs	r2, r1, lsl #16
	ldrne	r6, [r4, #R13OFF]
	addeq	r6, r6, #1
	CLRFLAGS \d14
	DISPATCH
.endm

@ 40-4b ldw (rn)
.macro H_ldw d14
	HLABEL ldw, \d14
	and	r3, r0, #15
	ldr	r3, [r4, r3, lsl #2]
	str	r3, [r4, #VLASTRAMADR]
	mov	r2, r3, lsl #16
	ldrb	r1, [r9, r2, lsr #16]
	eor	r3, r3, #1
	mov	r3, r3, lsl #16
	ldrb	r2, [r9, r3, lsr #16]
	orr	r1, r1, r2, lsl #8
	add	r6, r6, #1
	str	r1, [r4, r12, lsl #2]
	TESTR14 \d14
	CLRFLAGS \d14
	DISPATCH
.endm

@ 40-4b(ALT1) ldb (rn)
.macro H_ldb d14
	HLABEL ldb, \d14
	and	r3, r0, #15
	ldr	r3, [r4, r3, lsl #2]
	str	r3, [r4, #VLASTRAMADR]
	mov	r2, r3, lsl #16
	ldrb	r1, [r9, r2, lsr #16]
	add	r6, r6, #1
	str	r1, [r4, r12, lsl #2]
	TESTR14 \d14
	CLRFLAGS \d14
	DISPATCH
.endm

@ 4c plot, 4-bit colour (fx_plot_4bit): a pixel at R1, R2 in the colour
@ register; R1 moves right. fxinst.cpp's fx_arm_plot_entries points the
@ tables here when fx_apfOpcodeTable has fx_plot_4bit (screen modes 1, 2).
.macro H_plot4 d14
	HLABEL plot4, \d14
	ldr	r1, [r4, #R1OFF]
	ldr	r2, [r4, #R2OFF]
	add	r6, r6, #1
	CLRFLAGS \d14			@ (r11 and r12 are scratch until 9:)
	add	r3, r1, #1
	str	r3, [r4, #R1OFF]
	and	r1, r1, #0xff		@ x
	and	r2, r2, #0xff		@ y
	ldr	r3, [r4, #VSCREENHEIGHT]
	cmp	r2, r3
	bhs	9f			@ below the screen: nothing
	ldr	r0, [r4, #VPLOTOPTIONREG]
	ldr	r3, [r4, #VCOLORREG]
	tst	r0, #2			@ dithering: the high nibble where x ^ y is odd
	beq	1f
	eor	r11, r1, r2
	tst	r11, #1
	movne	r3, r3, lsr #4
1:	and	r3, r3, #0xff		@ c
	tst	r0, #1			@ colour 0 is transparent unless POR bit 0
	bne	2f
	tst	r3, #0x0f
	beq	9f
2:	add	r11, r4, #APVSCREEN
	mov	r0, r2, lsr #3
	ldr	r11, [r11, r0, lsl #2]	@ apvScreen[y >> 3]
	add	r12, r4, #XTAB
	mov	r0, r1, lsr #3
	ldr	r12, [r12, r0, lsl #2]	@ x[x >> 3]
	add	r11, r11, r12
	and	r0, r2, #7
	add	r11, r11, r0, lsl #1	@ the pixel's byte
	and	r0, r1, #7
	mov	r12, #0x80
	mov	r12, r12, lsr r0	@ its bit
	ldrb	r0, [r11]
	tst	r3, #1
	orrne	r0, r0, r12
	biceq	r0, r0, r12
	strb	r0, [r11]
	ldrb	r0, [r11, #1]
	tst	r3, #2
	orrne	r0, r0, r12
	biceq	r0, r0, r12
	strb	r0, [r11, #1]
	ldrb	r0, [r11, #0x10]
	tst	r3, #4
	orrne	r0, r0, r12
	biceq	r0, r0, r12
	strb	r0, [r11, #0x10]
	ldrb	r0, [r11, #0x11]
	tst	r3, #8
	orrne	r0, r0, r12
	biceq	r0, r0, r12
	strb	r0, [r11, #0x11]
9:	mov	r11, #0
	mov	r12, #0
	DISPATCH
.endm

@ 4d swap
.macro H_swap d14
	HLABEL swap, \d14
	ldr	r1, [r4, r11, lsl #2]
	and	r2, r1, #0xff
	mov	r1, r1, lsr #8
	and	r1, r1, #0xff
	orr	r1, r1, r2, lsl #8
	add	r6, r6, #1
	str	r1, [r4, r12, lsl #2]
	SIGN_ZERO r1
	TESTR14 \d14
	CLRFLAGS \d14
	DISPATCH
.endm

@ 4e color: the colour register from the source (POR bit 2: the high
@ nibble into the low one too; bit 3: only the low nibble changes)
.macro H_color d14
	HLABEL color, \d14
	ldr	r1, [r4, r11, lsl #2]
	and	r1, r1, #0xff
	ldr	r0, [r4, #VPLOTOPTIONREG]
	tst	r0, #4
	andne	r2, r1, #0xf0
	orrne	r1, r2, r1, lsr #4
	tst	r0, #8
	ldrne	r2, [r4, #VCOLORREG]
	andne	r2, r2, #0xf0
	andne	r1, r1, #0x0f
	orrne	r1, r2, r1
	str	r1, [r4, #VCOLORREG]
	CLRFLAGS \d14
	add	r6, r6, #1
	DISPATCH
.endm

@ 4f not
.macro H_not d14
	HLABEL not, \d14
	ldr	r1, [r4, r11, lsl #2]
	mvn	r1, r1
	add	r6, r6, #1
	str	r1, [r4, r12, lsl #2]
	SIGN_ZERO r1
	TESTR14 \d14
	CLRFLAGS \d14
	DISPATCH
.endm

@ Stores an addition's flags and result: r1 = s, r2 = SREG, r3 = the other
@ operand. vCarry = s >= 0x10000 (already in the ARM flags as \cs/\cc),
@ vOverflow = ~(SREG ^ b) & (b ^ s) & 0x8000.
.macro ADD_RESULT cs, cc, d14
	mov\cc	r0, #0
	mov\cs	r0, #1
	str	r0, [r4, #VCARRY]
	eor	r0, r2, r3
	eor	r2, r3, r1
	bic	r2, r2, r0
	and	r2, r2, #0x8000
	str	r2, [r4, #VOVERFLOW]
	SIGN_ZERO r1
	add	r6, r6, #1
	str	r1, [r4, r12, lsl #2]
	TESTR14 \d14
	CLRFLAGS \d14
	DISPATCH
.endm

@ 50-5e add rn: s = (SREG & 0xffff) + (rn & 0xffff)
.macro H_add d14
	HLABEL add, \d14
	and	r3, r0, #15
	ldr	r3, [r4, r3, lsl #2]
	ldr	r2, [r4, r11, lsl #2]
	mov	r1, r2, lsl #16
	adds	r1, r1, r3, lsl #16	@ C: the carry out of 16 bits
	mov	r1, r1, lsr #16
	orrcs	r1, r1, #0x10000
	ADD_RESULT cs, cc, \d14
.endm

@ 50-5e(ALT1) adc rn: s = (SREG & 0xffff) + (rn & 0xffff) + SEX16(vCarry)
.macro H_adc d14
	HLABEL adc, \d14
	and	r3, r0, #15
	ldr	r3, [r4, r3, lsl #2]
	ldr	r2, [r4, r11, lsl #2]
	mov	r1, r2, lsl #16
	mov	r1, r1, lsr #16
	mov	r0, r3, lsl #16
	add	r1, r1, r0, lsr #16
	ldr	r0, [r4, #VCARRY]
	mov	r0, r0, lsl #16
	add	r1, r1, r0, asr #16
	cmp	r1, #0x10000
	ADD_RESULT ge, lt, \d14
.endm

@ 50-5f(ALT2) add #n
.macro H_add_i d14
	HLABEL add_i, \d14
	and	r3, r0, #15
	ldr	r2, [r4, r11, lsl #2]
	mov	r1, r2, lsl #16
	adds	r1, r1, r3, lsl #16
	mov	r1, r1, lsr #16
	orrcs	r1, r1, #0x10000
	ADD_RESULT cs, cc, \d14
.endm

@ 50-5f(ALT3) adc #n: s = (SREG & 0xffff) + n + (vCarry & 0xffff)
.macro H_adc_i d14
	HLABEL adc_i, \d14
	and	r3, r0, #15
	ldr	r2, [r4, r11, lsl #2]
	ldr	r0, [r4, #VCARRY]
	mov	r1, r2, lsl #16
	mov	r1, r1, lsr #16
	add	r1, r1, r3
	mov	r0, r0, lsl #16
	add	r1, r1, r0, lsr #16
	cmp	r1, #0x10000
	ADD_RESULT ge, lt, \d14
.endm

@ Stores a subtraction's flags and result: r1 = s, r2 = SREG, r3 = the
@ other operand. vCarry = s >= 0 (\cs/\cc), vOverflow = (SREG ^ b) &
@ (SREG ^ s) & 0x8000.
.macro SUB_RESULT cs, cc, write, d14
	mov\cc	r0, #0
	mov\cs	r0, #1
	str	r0, [r4, #VCARRY]
	eor	r0, r2, r3
	eor	r2, r2, r1
	and	r0, r0, r2
	and	r0, r0, #0x8000
	str	r0, [r4, #VOVERFLOW]
	SIGN_ZERO r1
	add	r6, r6, #1
.if \write
	str	r1, [r4, r12, lsl #2]
	TESTR14 \d14
.endif
	CLRFLAGS \d14
	DISPATCH
.endm

@ 60-6e sub rn: s = (SREG & 0xffff) - (rn & 0xffff)
.macro H_sub d14
	HLABEL sub, \d14
	and	r3, r0, #15
	ldr	r3, [r4, r3, lsl #2]
	ldr	r2, [r4, r11, lsl #2]
	mov	r1, r2, lsl #16
	mov	r1, r1, lsr #16
	mov	r0, r3, lsl #16
	subs	r1, r1, r0, lsr #16
	SUB_RESULT pl, mi, 1, \d14
.endm

@ 60-6e(ALT1) sbc rn: s = (SREG & 0xffff) - (rn & 0xffff) - ((vCarry ^ 1) & 0xffff)
.macro H_sbc d14
	HLABEL sbc, \d14
	and	r3, r0, #15
	ldr	r3, [r4, r3, lsl #2]
	ldr	r2, [r4, r11, lsl #2]
	mov	r1, r2, lsl #16
	mov	r1, r1, lsr #16
	mov	r0, r3, lsl #16
	sub	r1, r1, r0, lsr #16
	ldr	r0, [r4, #VCARRY]
	eor	r0, r0, #1
	mov	r0, r0, lsl #16
	subs	r1, r1, r0, lsr #16
	SUB_RESULT pl, mi, 1, \d14
.endm

@ 60-6f(ALT2) sub #n
.macro H_sub_i d14
	HLABEL sub_i, \d14
	and	r3, r0, #15
	ldr	r2, [r4, r11, lsl #2]
	mov	r1, r2, lsl #16
	mov	r1, r1, lsr #16
	subs	r1, r1, r3
	SUB_RESULT pl, mi, 1, \d14
.endm

@ 60-6e(ALT3) cmp rn: as sub, but the result isn't stored
.macro H_cmp d14
	HLABEL cmp, \d14
	and	r3, r0, #15
	ldr	r3, [r4, r3, lsl #2]
	ldr	r2, [r4, r11, lsl #2]
	mov	r1, r2, lsl #16
	mov	r1, r1, lsr #16
	mov	r0, r3, lsl #16
	subs	r1, r1, r0, lsr #16
	SUB_RESULT pl, mi, 0, \d14
.endm

@ 70 merge: R7's high byte over R8's
.macro H_merge d14
	HLABEL merge, \d14
	ldr	r1, [r4, #R7OFF]
	ldr	r2, [r4, #R8OFF]
	and	r1, r1, #0xff00
	and	r2, r2, #0xff00
	orr	r1, r1, r2, lsr #8	@ v
	add	r6, r6, #1
	str	r1, [r4, r12, lsl #2]
	mov	r3, #0xc0		@ vOverflow = (v & 0xc0c0) << 16
	orr	r3, r3, #0xc000
	and	r2, r1, r3
	mov	r2, r2, lsl #16
	str	r2, [r4, #VOVERFLOW]
	mov	r3, #0xf0		@ vZero = !(v & 0xf0f0)
	orr	r3, r3, #0xf000
	tst	r1, r3
	moveq	r2, #1
	movne	r2, #0
	str	r2, [r4, #VZERO]
	orr	r2, r1, r1, lsl #8	@ vSign = (v | v << 8) & 0x8000
	and	r2, r2, #0x8000
	str	r2, [r4, #VSIGN]
	mov	r3, #0xe0		@ vCarry = (v & 0xe0e0) != 0
	orr	r3, r3, #0xe000
	tst	r1, r3
	movne	r2, #1
	moveq	r2, #0
	str	r2, [r4, #VCARRY]
	TESTR14 \d14
	CLRFLAGS \d14
	DISPATCH
.endm

@ Logic and multiply results: r1 = v; DREG = vSign = vZero = v
.macro LOGIC_RESULT d14
	add	r6, r6, #1
	str	r1, [r4, r12, lsl #2]
	SIGN_ZERO r1
	TESTR14 \d14
	CLRFLAGS \d14
	DISPATCH
.endm

@ 71-7e and rn, bic rn (ALT1), and #n (ALT2), bic #n (ALT3);
@ c1-ce or rn, xor rn (ALT1), or #n (ALT2), xor #n (ALT3)
.macro H_logic_r name, op, d14
	HLABEL \name, \d14
	and	r3, r0, #15
	ldr	r3, [r4, r3, lsl #2]
	ldr	r1, [r4, r11, lsl #2]
	\op	r1, r1, r3
	LOGIC_RESULT \d14
.endm
.macro H_logic_i name, op, d14
	HLABEL \name, \d14
	and	r3, r0, #15
	ldr	r1, [r4, r11, lsl #2]
	\op	r1, r1, r3
	LOGIC_RESULT \d14
.endm

@ 80-8e mult rn: SEX8(SREG) * SEX8(rn); umult rn (ALT1): USEX8 * USEX8;
@ mult #n (ALT2): SEX8(SREG) * n; umult #n (ALT3): USEX8(SREG) * n
.macro H_mult d14
	HLABEL mult, \d14
	and	r3, r0, #15
	ldr	r3, [r4, r3, lsl #2]
	ldr	r2, [r4, r11, lsl #2]
	mov	r3, r3, lsl #24
	mov	r3, r3, asr #24
	mov	r2, r2, lsl #24
	mov	r2, r2, asr #24
	mul	r1, r2, r3
	LOGIC_RESULT \d14
.endm
.macro H_umult d14
	HLABEL umult, \d14
	and	r3, r0, #15
	ldr	r3, [r4, r3, lsl #2]
	ldr	r2, [r4, r11, lsl #2]
	and	r3, r3, #0xff
	and	r2, r2, #0xff
	mul	r1, r2, r3
	LOGIC_RESULT \d14
.endm
.macro H_mult_i d14
	HLABEL mult_i, \d14
	and	r3, r0, #15
	ldr	r2, [r4, r11, lsl #2]
	mov	r2, r2, lsl #24
	mov	r2, r2, asr #24
	mul	r1, r2, r3
	LOGIC_RESULT \d14
.endm
.macro H_umult_i d14
	HLABEL umult_i, \d14
	and	r3, r0, #15
	ldr	r2, [r4, r11, lsl #2]
	and	r2, r2, #0xff
	mul	r1, r2, r3
	LOGIC_RESULT \d14
.endm

@ 91-94 link #n: R11 = R15 + n
.macro H_link d14
	HLABEL link, \d14
	and	r3, r0, #15
	add	r1, r6, r3
	str	r1, [r4, #R11OFF]
	CLRFLAGS \d14
	add	r6, r6, #1
	DISPATCH
.endm

@ 95 sex
.macro H_sex d14
	HLABEL sex, \d14
	ldr	r1, [r4, r11, lsl #2]
	mov	r1, r1, lsl #24
	mov	r1, r1, asr #24
	LOGIC_RESULT \d14
.endm

@ 96 asr: vCarry = SREG & 1; v = SEX16(SREG) >> 1
.macro H_asr d14
	HLABEL asr, \d14
	ldr	r1, [r4, r11, lsl #2]
	and	r2, r1, #1
	str	r2, [r4, #VCARRY]
	mov	r1, r1, lsl #16
	mov	r1, r1, asr #17
	LOGIC_RESULT \d14
.endm

@ 97 ror: v = ((SREG & 0xffff) >> 1) | (vCarry << 15); vCarry = SREG & 1
.macro H_ror d14
	HLABEL ror, \d14
	ldr	r2, [r4, r11, lsl #2]
	ldr	r3, [r4, #VCARRY]
	mov	r1, r2, lsl #16
	mov	r1, r1, lsr #17
	orr	r1, r1, r3, lsl #15
	and	r2, r2, #1
	str	r2, [r4, #VCARRY]
	LOGIC_RESULT \d14
.endm

@ 98-9d jmp rn
.macro H_jmp d14
	HLABEL jmp, \d14
	and	r3, r0, #15
	ldr	r6, [r4, r3, lsl #2]
	CLRFLAGS \d14
	DISPATCH
.endm

@ 9e lob: v = SREG & 0xff; vSign = vZero = v << 8 (c0 hib: (SREG >> 8) & 0xff)
.macro H_lobhib name, shift, d14
	HLABEL \name, \d14
	ldr	r1, [r4, r11, lsl #2]
.if \shift
	mov	r1, r1, lsr #8
.endif
	and	r1, r1, #0xff
	add	r6, r6, #1
	str	r1, [r4, r12, lsl #2]
	mov	r2, r1, lsl #8
	SIGN_ZERO r2
	TESTR14 \d14
	CLRFLAGS \d14
	DISPATCH
.endm

@ a0-ad ibt rn,#pp; ae ibt r14 (reloads the ROM buffer); af ibt r15
.macro H_ibt name, r14, r15, d14
	HLABEL \name, \d14
	mov	r3, r7, lsl #24
	add	r6, r6, #1
	FETCHPIPE
	add	r6, r6, #1
.if \r15
	mov	r6, r3, asr #24
.else
	and	r1, r0, #15
	mov	r3, r3, asr #24
	str	r3, [r4, r1, lsl #2]
.endif
	CLRFLAGS \d14
.if \r14
	READR14
.endif
	DISPATCH
.endm

@ a0-ae(ALT1, ALT3) lms rn,(yy): rn = RAM[2 * pipe] | RAM[2 * pipe + 1] << 8
.macro H_lms name, r14, d14
	HLABEL \name, \d14
	mov	r3, r7, lsl #1
	str	r3, [r4, #VLASTRAMADR]
	add	r6, r6, #1
	FETCHPIPE
	add	r6, r6, #1
	ldrb	r1, [r9, r3]
	add	r3, r3, #1
	ldrb	r2, [r9, r3]
	orr	r1, r1, r2, lsl #8
	and	r2, r0, #15
	str	r1, [r4, r2, lsl #2]
	CLRFLAGS \d14
.if \r14
	READR14
.endif
	DISPATCH
.endm

@ a0-ae(ALT2) sms (yy),rn
.macro H_sms d14
	HLABEL sms, \d14
	and	r2, r0, #15
	ldr	r3, [r4, r2, lsl #2]
	mov	r2, r7, lsl #1
	str	r2, [r4, #VLASTRAMADR]
	add	r6, r6, #1
	FETCHPIPE
	strb	r3, [r9, r2]
	add	r2, r2, #1
	mov	r3, r3, lsr #8
	strb	r3, [r9, r2]
	CLRFLAGS \d14
	add	r6, r6, #1
	DISPATCH
.endm

@ b0-be from rn; moves rn (B)
.macro H_from d14
	HLABEL from, \d14
	and	r3, r0, #15
	tst	r5, #FLG_B
	beq	1f
	ldr	r1, [r4, r3, lsl #2]
	add	r6, r6, #1
	str	r1, [r4, r12, lsl #2]
	and	r2, r1, #0x80
	mov	r2, r2, lsl #16
	str	r2, [r4, #VOVERFLOW]
	SIGN_ZERO r1
	TESTR14 \d14
	CLRFLAGS \d14
	DISPATCH
1:	mov	r11, r3
	add	r6, r6, #1
	DISPATCH
.endm

@ d0-dd inc rn, de inc r14; e0-ed dec rn, ee dec r14
.macro H_incdec name, op, r14, d14
	HLABEL \name, \d14
	and	r3, r0, #15
	ldr	r1, [r4, r3, lsl #2]
	\op	r1, r1, #1
	str	r1, [r4, r3, lsl #2]
	SIGN_ZERO r1
	CLRFLAGS \d14
	add	r6, r6, #1
.if \r14
	READR14
.endif
	DISPATCH
.endm

@ ef getb: DREG = the ROM buffer
.macro H_getb d14
	HLABEL getb, \d14
	ldrb	r1, [r4, #VROMBUFFER]
	add	r6, r6, #1
	str	r1, [r4, r12, lsl #2]
	TESTR14 \d14
	CLRFLAGS \d14
	DISPATCH
.endm

@ f0-fd iwt rn,#xx; fe iwt r14; ff iwt r15
.macro H_iwt name, r14, r15, d14
	HLABEL \name, \d14
	mov	r3, r7
	add	r6, r6, #1
	FETCHPIPE
	add	r6, r6, #1
	orr	r3, r3, r7, lsl #8
	FETCHPIPE
	add	r6, r6, #1
.if \r15
	mov	r6, r3
.else
	and	r1, r0, #15
	str	r3, [r4, r1, lsl #2]
.endif
	CLRFLAGS \d14
.if \r14
	READR14
.endif
	DISPATCH
.endm

@ f0-fe(ALT1, ALT3) lm rn,(xx): rn = RAM[xx] | RAM[xx ^ 1] << 8
.macro H_lm name, r14, d14
	HLABEL \name, \d14
	mov	r3, r7
	add	r6, r6, #1
	FETCHPIPE
	add	r6, r6, #1
	orr	r3, r3, r7, lsl #8
	str	r3, [r4, #VLASTRAMADR]
	FETCHPIPE
	add	r6, r6, #1
	ldrb	r1, [r9, r3]
	eor	r3, r3, #1
	ldrb	r2, [r9, r3]
	orr	r1, r1, r2, lsl #8
	and	r2, r0, #15
	str	r1, [r4, r2, lsl #2]
	CLRFLAGS \d14
.if \r14
	READR14
.endif
	DISPATCH
.endm

@ f0-fe(ALT2) sm (xx),rn: RAM[xx] = rn, RAM[xx ^ 1] = rn >> 8
.macro H_sm d14
	HLABEL sm, \d14
	and	r2, r0, #15
	ldr	r0, [r4, r2, lsl #2]	@ the value
	mov	r3, r7
	add	r6, r6, #1
	FETCHPIPE
	add	r6, r6, #1
	orr	r3, r3, r7, lsl #8
	str	r3, [r4, #VLASTRAMADR]
	FETCHPIPE
	strb	r0, [r9, r3]
	eor	r3, r3, #1
	mov	r0, r0, lsr #8
	strb	r0, [r9, r3]
	CLRFLAGS \d14
	add	r6, r6, #1
	DISPATCH
.endm

@ Every handler, once for the fast table and once for the R14 table
.macro ALL_HANDLERS d14
	H_nop		\d14
	H_lsr		\d14
	H_rol		\d14
	H_to		\d14
	H_to_r14	\d14
	H_with		\d14
	H_with_r14	\d14
	H_stw		\d14
	H_stb		\d14
	H_loop		\d14
	H_ldw		\d14
	H_ldb		\d14
	.ltorg
	H_plot4		\d14
	H_swap		\d14
	H_color		\d14
	H_not		\d14
	H_add		\d14
	H_adc		\d14
	H_add_i		\d14
	H_adc_i		\d14
	H_sub		\d14
	H_sbc		\d14
	H_sub_i		\d14
	H_cmp		\d14
	H_merge		\d14
	.ltorg
	H_logic_r	and, and, \d14
	H_logic_r	bic, bic, \d14
	H_logic_i	and_i, and, \d14
	H_logic_i	bic_i, bic, \d14
	H_logic_r	or, orr, \d14
	H_logic_r	xor, eor, \d14
	H_logic_i	or_i, orr, \d14
	H_logic_i	xor_i, eor, \d14
	H_mult		\d14
	H_umult		\d14
	H_mult_i	\d14
	H_umult_i	\d14
	H_link		\d14
	H_sex		\d14
	H_asr		\d14
	H_ror		\d14
	H_jmp		\d14
	H_lobhib	lob, 0, \d14
	H_lobhib	hib, 1, \d14
	.ltorg
	H_ibt		ibt, 0, 0, \d14
	H_ibt		ibt_r14, 1, 0, \d14
	H_ibt		ibt_r15, 0, 1, \d14
	H_lms		lms, 0, \d14
	H_lms		lms_r14, 1, \d14
	H_sms		\d14
	H_from		\d14
	H_incdec	inc, add, 0, \d14
	H_incdec	inc_r14, add, 1, \d14
	H_incdec	dec, sub, 0, \d14
	H_incdec	dec_r14, sub, 1, \d14
	H_getb		\d14
	H_iwt		iwt, 0, 0, \d14
	H_iwt		iwt_r14, 1, 0, \d14
	H_iwt		iwt_r15, 0, 1, \d14
	H_lm		lm, 0, \d14
	H_lm		lm_r14, 1, \d14
	H_sm		\d14
	.ltorg
.endm

	ALL_HANDLERS 0
	ALL_HANDLERS 1

@ The handlers, in the order of enum FxArmHandler in fxinst.cpp: the fast
@ versions, then the R14 table's (the same where the instruction doesn't
@ depend on the destination)
.macro HANDLER_LIST suffix
	.word	fx_arm_slow, fx_arm_stop, fx_arm_nop\suffix, fx_arm_lsr\suffix, fx_arm_rol\suffix, fx_arm_bra
	.word	fx_arm_bge, fx_arm_blt, fx_arm_bne, fx_arm_beq, fx_arm_bpl, fx_arm_bmi
	.word	fx_arm_bcc, fx_arm_bcs, fx_arm_bvc, fx_arm_bvs
	.word	fx_arm_to\suffix, fx_arm_with\suffix, fx_arm_stw\suffix, fx_arm_stb\suffix, fx_arm_loop\suffix
	.word	fx_arm_alt1, fx_arm_alt2, fx_arm_alt3, fx_arm_ldw\suffix, fx_arm_ldb\suffix
	.word	fx_arm_swap\suffix, fx_arm_not\suffix
	.word	fx_arm_add\suffix, fx_arm_adc\suffix, fx_arm_add_i\suffix, fx_arm_adc_i\suffix
	.word	fx_arm_sub\suffix, fx_arm_sbc\suffix, fx_arm_sub_i\suffix, fx_arm_cmp\suffix
	.word	fx_arm_and\suffix, fx_arm_bic\suffix, fx_arm_and_i\suffix, fx_arm_bic_i\suffix
	.word	fx_arm_or\suffix, fx_arm_xor\suffix, fx_arm_or_i\suffix, fx_arm_xor_i\suffix
	.word	fx_arm_mult\suffix, fx_arm_umult\suffix, fx_arm_mult_i\suffix, fx_arm_umult_i\suffix
	.word	fx_arm_link\suffix, fx_arm_sex\suffix, fx_arm_asr\suffix, fx_arm_ror\suffix
	.word	fx_arm_jmp\suffix, fx_arm_lob\suffix, fx_arm_hib\suffix
	.word	fx_arm_ibt\suffix, fx_arm_ibt_r14\suffix, fx_arm_ibt_r15\suffix
	.word	fx_arm_lms\suffix, fx_arm_lms_r14\suffix, fx_arm_sms\suffix
	.word	fx_arm_from\suffix, fx_arm_inc\suffix, fx_arm_inc_r14\suffix, fx_arm_dec\suffix, fx_arm_dec_r14\suffix
	.word	fx_arm_getb\suffix, fx_arm_iwt\suffix, fx_arm_iwt_r14\suffix, fx_arm_iwt_r15\suffix
	.word	fx_arm_lm\suffix, fx_arm_lm_r14\suffix, fx_arm_sm\suffix
	.word	fx_arm_plot4\suffix, fx_arm_merge\suffix, fx_arm_color\suffix
	.word	fx_arm_to_r14\suffix, fx_arm_with_r14\suffix
.endm

	.align	2
.ifdef FX_COUNTED
	.global	fx_arm_handlers_counted
fx_arm_handlers_counted:
.else
	.global	fx_arm_handlers
fx_arm_handlers:
.endif
	HANDLER_LIST
	HANDLER_LIST _d14

@ The three tables (1024 handler addresses each, ALT mode << 8 | opcode):
@ fast, careful, R14
	.bss
	.align	2
.ifdef FX_COUNTED
	.global	fx_arm_tables_counted
fx_arm_tables_counted:
.else
	.global	fx_arm_tables
fx_arm_tables:
.endif
	.space	12288
