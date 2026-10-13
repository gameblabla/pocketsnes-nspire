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
@ The common instructions are done here. The rare ones, and any that would
@ read R15 by register number, write R14 by number (which reloads the ROM
@ buffer) or set the source to R15 or the destination to R14 or R15, go to
@ fx_slow: it writes the state back, calls the instruction's C function from
@ fx_apfOpcodeTable, and reads the state again. While the source is R15 or
@ the destination R14 or R15 (after with/to/from/move) the careful table is
@ used, which sends every instruction to fx_slow, until those clear (every
@ non-prefix instruction resets them to R0). So the code here can assume
@ sreg != 15 and dreg < 14: no R15 aliasing and no ROM buffer reload.
@ fx_arm_setup_tables (fxinst.cpp) fills both tables from fx_arm_handlers,
@ the handlers' addresses in the order of enum FxArmHandler there.

	.syntax unified
	.arm

.ifdef FX_COUNTED
	.set	TABLES, fx_arm_tables_counted
.else
	.set	TABLES, fx_arm_tables
.endif

	.text

@ struct FxRegs_s offsets (fxinst.cpp checks them with static_assert)
	.equ R11OFF,		44
	.equ R12OFF,		48
	.equ R13OFF,		52
	.equ R14OFF,		56
	.equ R15OFF,		60
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
	.equ PVRAMBANK,		444
	.equ PVROMBANK,		448
	.equ PVPRGBANK,		452

	.equ FLG_G,		0x20
	.equ FLG_ALT1,		0x100
	.equ FLG_ALT2,		0x200
	.equ FLG_B,		0x1000
	.equ FLG_IRQ,		0x8000
	.equ GSU_CFGR,		0x37

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

@ The end of most instructions: ALT1, ALT2 and B off, source and destination R0
.macro CLRFLAGS
	bic	r5, r5, #(FLG_ALT1 | FLG_ALT2 | FLG_B)
	mov	r11, #0
	mov	r12, #0
.endm

@ The ROM buffer: GSU.vRomBuffer = ROM bank[R14 & 0xffff] (uses r1, r2)
.macro READR14
	ldr	r1, [r4, #PVROMBANK]
	ldr	r2, [r4, #R14OFF]
	mov	r2, r2, lsl #16
	ldrb	r2, [r1, r2, lsr #16]
	strb	r2, [r4, #VROMBUFFER]
.endm

@ pipe = program bank[R15 & 0xffff] (uses r1)
.macro FETCHPIPE
	mov	r1, r6, lsl #16
	ldrb	r7, [r8, r1, lsr #16]
.endm

@ Picks the table for the current source and destination: the careful one
@ (which follows the fast one) if dreg >= 14 or sreg == 15
.macro SELECT_TABLE
	ldr	r10, =TABLES
	cmp	r12, #14
	cmplo	r11, #15		@ (C is set by either: dreg >= 14 or sreg == 15)
	addhs	r10, r10, #4096
.endm

@ vSign = vZero = \reg
.macro SIGN_ZERO reg
	str	\reg, [r4, #VSIGN]
	str	\reg, [r4, #VZERO]
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

@ 01 nop
fx_arm_nop:
	CLRFLAGS
	add	r6, r6, #1
	DISPATCH

@ 03 lsr
fx_arm_lsr:
	ldr	r1, [r4, r11, lsl #2]
	and	r2, r1, #1
	str	r2, [r4, #VCARRY]
	mov	r1, r1, lsl #16
	mov	r1, r1, lsr #17
	add	r6, r6, #1
	str	r1, [r4, r12, lsl #2]
	SIGN_ZERO r1
	CLRFLAGS
	DISPATCH

@ 04 rol
fx_arm_rol:
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
	CLRFLAGS
	DISPATCH

@ 05 bra
fx_arm_bra:
	mov	r3, r7, lsl #24
	add	r6, r6, #1
	FETCHPIPE
	add	r6, r6, r3, asr #24
	DISPATCH

@ Conditional branches: the offset is the pipe byte; \test sets the ARM
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

@ 10-1d to rn; move rn (B)
fx_arm_to:
	and	r3, r0, #15
	tst	r5, #FLG_B
	beq	1f
	ldr	r1, [r4, r11, lsl #2]
	str	r1, [r4, r3, lsl #2]
	CLRFLAGS
	add	r6, r6, #1
	DISPATCH
1:	mov	r12, r3
	add	r6, r6, #1
	DISPATCH

@ 20-2d with rn
fx_arm_with:
	orr	r5, r5, #FLG_B
	and	r11, r0, #15
	mov	r12, r11
	add	r6, r6, #1
	DISPATCH

@ 30-3b stw (rn)
fx_arm_stw:
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
	CLRFLAGS
	add	r6, r6, #1
	DISPATCH

@ 30-3b(ALT1) stb (rn)
fx_arm_stb:
	and	r3, r0, #15
	ldr	r3, [r4, r3, lsl #2]
	str	r3, [r4, #VLASTRAMADR]
	ldr	r1, [r4, r11, lsl #2]
	mov	r2, r3, lsl #16
	strb	r1, [r9, r2, lsr #16]
	CLRFLAGS
	add	r6, r6, #1
	DISPATCH

@ 3c loop
fx_arm_loop:
	ldr	r1, [r4, #R12OFF]
	sub	r1, r1, #1
	str	r1, [r4, #R12OFF]
	SIGN_ZERO r1
	movs	r2, r1, lsl #16
	ldrne	r6, [r4, #R13OFF]
	addeq	r6, r6, #1
	CLRFLAGS
	DISPATCH

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

@ 40-4b ldw (rn)
fx_arm_ldw:
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
	CLRFLAGS
	DISPATCH

@ 40-4b(ALT1) ldb (rn)
fx_arm_ldb:
	and	r3, r0, #15
	ldr	r3, [r4, r3, lsl #2]
	str	r3, [r4, #VLASTRAMADR]
	mov	r2, r3, lsl #16
	ldrb	r1, [r9, r2, lsr #16]
	add	r6, r6, #1
	str	r1, [r4, r12, lsl #2]
	CLRFLAGS
	DISPATCH

@ 4d swap
fx_arm_swap:
	ldr	r1, [r4, r11, lsl #2]
	and	r2, r1, #0xff
	mov	r1, r1, lsr #8
	and	r1, r1, #0xff
	orr	r1, r1, r2, lsl #8
	add	r6, r6, #1
	str	r1, [r4, r12, lsl #2]
	SIGN_ZERO r1
	CLRFLAGS
	DISPATCH

@ 4f not
fx_arm_not:
	ldr	r1, [r4, r11, lsl #2]
	mvn	r1, r1
	add	r6, r6, #1
	str	r1, [r4, r12, lsl #2]
	SIGN_ZERO r1
	CLRFLAGS
	DISPATCH

@ Stores an addition's flags and result: r1 = s, r2 = SREG, r3 = the other
@ operand. vCarry = s >= 0x10000 (already in the ARM flags as \cs/\cc),
@ vOverflow = ~(SREG ^ b) & (b ^ s) & 0x8000.
.macro ADD_RESULT cs, cc, write=1
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
.if \write
	str	r1, [r4, r12, lsl #2]
.endif
	CLRFLAGS
	DISPATCH
.endm

@ 50-5e add rn: s = (SREG & 0xffff) + (rn & 0xffff)
fx_arm_add:
	and	r3, r0, #15
	ldr	r3, [r4, r3, lsl #2]
	ldr	r2, [r4, r11, lsl #2]
	mov	r1, r2, lsl #16
	adds	r1, r1, r3, lsl #16	@ C: the carry out of 16 bits
	mov	r1, r1, lsr #16
	orrcs	r1, r1, #0x10000
	ADD_RESULT cs, cc

@ 50-5e(ALT1) adc rn: s = (SREG & 0xffff) + (rn & 0xffff) + SEX16(vCarry)
fx_arm_adc:
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
	ADD_RESULT ge, lt

@ 50-5f(ALT2) add #n
fx_arm_add_i:
	and	r3, r0, #15
	ldr	r2, [r4, r11, lsl #2]
	mov	r1, r2, lsl #16
	adds	r1, r1, r3, lsl #16
	mov	r1, r1, lsr #16
	orrcs	r1, r1, #0x10000
	ADD_RESULT cs, cc

@ 50-5f(ALT3) adc #n: s = (SREG & 0xffff) + n + (vCarry & 0xffff)
fx_arm_adc_i:
	and	r3, r0, #15
	ldr	r2, [r4, r11, lsl #2]
	ldr	r0, [r4, #VCARRY]
	mov	r1, r2, lsl #16
	mov	r1, r1, lsr #16
	add	r1, r1, r3
	mov	r0, r0, lsl #16
	add	r1, r1, r0, lsr #16
	cmp	r1, #0x10000
	ADD_RESULT ge, lt

@ Stores a subtraction's flags and result: r1 = s, r2 = SREG, r3 = the
@ other operand. vCarry = s >= 0 (\cs/\cc), vOverflow = (SREG ^ b) &
@ (SREG ^ s) & 0x8000.
.macro SUB_RESULT cs, cc, write=1
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
.endif
	CLRFLAGS
	DISPATCH
.endm

@ 60-6e sub rn: s = (SREG & 0xffff) - (rn & 0xffff)
fx_arm_sub:
	and	r3, r0, #15
	ldr	r3, [r4, r3, lsl #2]
	ldr	r2, [r4, r11, lsl #2]
	mov	r1, r2, lsl #16
	mov	r1, r1, lsr #16
	mov	r0, r3, lsl #16
	subs	r1, r1, r0, lsr #16
	SUB_RESULT pl, mi

@ 60-6e(ALT1) sbc rn: s = (SREG & 0xffff) - (rn & 0xffff) - ((vCarry ^ 1) & 0xffff)
fx_arm_sbc:
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
	SUB_RESULT pl, mi

@ 60-6f(ALT2) sub #n
fx_arm_sub_i:
	and	r3, r0, #15
	ldr	r2, [r4, r11, lsl #2]
	mov	r1, r2, lsl #16
	mov	r1, r1, lsr #16
	subs	r1, r1, r3
	SUB_RESULT pl, mi

@ 60-6e(ALT3) cmp rn: as sub, but the result isn't stored
fx_arm_cmp:
	and	r3, r0, #15
	ldr	r3, [r4, r3, lsl #2]
	ldr	r2, [r4, r11, lsl #2]
	mov	r1, r2, lsl #16
	mov	r1, r1, lsr #16
	mov	r0, r3, lsl #16
	subs	r1, r1, r0, lsr #16
	SUB_RESULT pl, mi, 0

@ Logic and multiply results: r1 = v; DREG = vSign = vZero = v
.macro LOGIC_RESULT
	add	r6, r6, #1
	str	r1, [r4, r12, lsl #2]
	SIGN_ZERO r1
	CLRFLAGS
	DISPATCH
.endm

@ 71-7e and rn, bic rn (ALT1), and #n (ALT2), bic #n (ALT3)
fx_arm_and:
	and	r3, r0, #15
	ldr	r3, [r4, r3, lsl #2]
	ldr	r1, [r4, r11, lsl #2]
	and	r1, r1, r3
	LOGIC_RESULT
fx_arm_bic:
	and	r3, r0, #15
	ldr	r3, [r4, r3, lsl #2]
	ldr	r1, [r4, r11, lsl #2]
	bic	r1, r1, r3
	LOGIC_RESULT
fx_arm_and_i:
	and	r3, r0, #15
	ldr	r1, [r4, r11, lsl #2]
	and	r1, r1, r3
	LOGIC_RESULT
fx_arm_bic_i:
	and	r3, r0, #15
	ldr	r1, [r4, r11, lsl #2]
	bic	r1, r1, r3
	LOGIC_RESULT

@ c1-ce or rn, xor rn (ALT1), or #n (ALT2), xor #n (ALT3)
fx_arm_or:
	and	r3, r0, #15
	ldr	r3, [r4, r3, lsl #2]
	ldr	r1, [r4, r11, lsl #2]
	orr	r1, r1, r3
	LOGIC_RESULT
fx_arm_xor:
	and	r3, r0, #15
	ldr	r3, [r4, r3, lsl #2]
	ldr	r1, [r4, r11, lsl #2]
	eor	r1, r1, r3
	LOGIC_RESULT
fx_arm_or_i:
	and	r3, r0, #15
	ldr	r1, [r4, r11, lsl #2]
	orr	r1, r1, r3
	LOGIC_RESULT
fx_arm_xor_i:
	and	r3, r0, #15
	ldr	r1, [r4, r11, lsl #2]
	eor	r1, r1, r3
	LOGIC_RESULT

@ 80-8e mult rn: SEX8(SREG) * SEX8(rn); umult rn (ALT1): USEX8 * USEX8;
@ mult #n (ALT2): SEX8(SREG) * n; umult #n (ALT3): USEX8(SREG) * n
fx_arm_mult:
	and	r3, r0, #15
	ldr	r3, [r4, r3, lsl #2]
	ldr	r2, [r4, r11, lsl #2]
	mov	r3, r3, lsl #24
	mov	r3, r3, asr #24
	mov	r2, r2, lsl #24
	mov	r2, r2, asr #24
	mul	r1, r2, r3
	LOGIC_RESULT
fx_arm_umult:
	and	r3, r0, #15
	ldr	r3, [r4, r3, lsl #2]
	ldr	r2, [r4, r11, lsl #2]
	and	r3, r3, #0xff
	and	r2, r2, #0xff
	mul	r1, r2, r3
	LOGIC_RESULT
fx_arm_mult_i:
	and	r3, r0, #15
	ldr	r2, [r4, r11, lsl #2]
	mov	r2, r2, lsl #24
	mov	r2, r2, asr #24
	mul	r1, r2, r3
	LOGIC_RESULT
fx_arm_umult_i:
	and	r3, r0, #15
	ldr	r2, [r4, r11, lsl #2]
	and	r2, r2, #0xff
	mul	r1, r2, r3
	LOGIC_RESULT

@ 91-94 link #n: R11 = R15 + n
fx_arm_link:
	and	r3, r0, #15
	add	r1, r6, r3
	str	r1, [r4, #R11OFF]
	CLRFLAGS
	add	r6, r6, #1
	DISPATCH

@ 95 sex
fx_arm_sex:
	ldr	r1, [r4, r11, lsl #2]
	mov	r1, r1, lsl #24
	mov	r1, r1, asr #24
	LOGIC_RESULT

@ 96 asr: vCarry = SREG & 1; v = SEX16(SREG) >> 1
fx_arm_asr:
	ldr	r1, [r4, r11, lsl #2]
	and	r2, r1, #1
	str	r2, [r4, #VCARRY]
	mov	r1, r1, lsl #16
	mov	r1, r1, asr #17
	LOGIC_RESULT

@ 97 ror: v = ((SREG & 0xffff) >> 1) | (vCarry << 15); vCarry = SREG & 1
fx_arm_ror:
	ldr	r2, [r4, r11, lsl #2]
	ldr	r3, [r4, #VCARRY]
	mov	r1, r2, lsl #16
	mov	r1, r1, lsr #17
	orr	r1, r1, r3, lsl #15
	and	r2, r2, #1
	str	r2, [r4, #VCARRY]
	LOGIC_RESULT

@ 98-9d jmp rn
fx_arm_jmp:
	and	r3, r0, #15
	ldr	r6, [r4, r3, lsl #2]
	CLRFLAGS
	DISPATCH

@ 9e lob: v = SREG & 0xff; vSign = vZero = v << 8
fx_arm_lob:
	ldr	r1, [r4, r11, lsl #2]
	and	r1, r1, #0xff
	add	r6, r6, #1
	str	r1, [r4, r12, lsl #2]
	mov	r2, r1, lsl #8
	SIGN_ZERO r2
	CLRFLAGS
	DISPATCH

@ c0 hib: v = (SREG >> 8) & 0xff; vSign = vZero = v << 8
fx_arm_hib:
	ldr	r1, [r4, r11, lsl #2]
	mov	r1, r1, lsr #8
	and	r1, r1, #0xff
	add	r6, r6, #1
	str	r1, [r4, r12, lsl #2]
	mov	r2, r1, lsl #8
	SIGN_ZERO r2
	CLRFLAGS
	DISPATCH

@ a0-ad ibt rn,#pp; ae ibt r14 (reloads the ROM buffer); af ibt r15
.macro IBT name, r14=0, r15=0
fx_arm_\name:
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
	CLRFLAGS
.if \r14
	READR14
.endif
	DISPATCH
.endm
	IBT	ibt
	IBT	ibt_r14, 1
	IBT	ibt_r15, 0, 1

@ a0-ae(ALT1, ALT3) lms rn,(yy): rn = RAM[2 * pipe] | RAM[2 * pipe + 1] << 8
.macro LMS name, r14=0
fx_arm_\name:
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
	CLRFLAGS
.if \r14
	READR14
.endif
	DISPATCH
.endm
	LMS	lms
	LMS	lms_r14, 1

@ a0-ae(ALT2) sms (yy),rn
fx_arm_sms:
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
	CLRFLAGS
	add	r6, r6, #1
	DISPATCH

@ b0-be from rn; moves rn (B)
fx_arm_from:
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
	CLRFLAGS
	DISPATCH
1:	mov	r11, r3
	add	r6, r6, #1
	DISPATCH

@ d0-dd inc rn, de inc r14; e0-ed dec rn, ee dec r14
.macro INCDEC name, op, r14=0
fx_arm_\name:
	and	r3, r0, #15
	ldr	r1, [r4, r3, lsl #2]
	\op	r1, r1, #1
	str	r1, [r4, r3, lsl #2]
	SIGN_ZERO r1
	CLRFLAGS
	add	r6, r6, #1
.if \r14
	READR14
.endif
	DISPATCH
.endm
	INCDEC	inc, add
	INCDEC	inc_r14, add, 1
	INCDEC	dec, sub
	INCDEC	dec_r14, sub, 1

@ ef getb: DREG = the ROM buffer
fx_arm_getb:
	ldrb	r1, [r4, #VROMBUFFER]
	add	r6, r6, #1
	str	r1, [r4, r12, lsl #2]
	CLRFLAGS
	DISPATCH

@ f0-fd iwt rn,#xx; fe iwt r14; ff iwt r15
.macro IWT name, r14=0, r15=0
fx_arm_\name:
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
	CLRFLAGS
.if \r14
	READR14
.endif
	DISPATCH
.endm
	IWT	iwt
	IWT	iwt_r14, 1
	IWT	iwt_r15, 0, 1

@ f0-fe(ALT1, ALT3) lm rn,(xx): rn = RAM[xx] | RAM[xx ^ 1] << 8
.macro LM name, r14=0
fx_arm_\name:
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
	CLRFLAGS
.if \r14
	READR14
.endif
	DISPATCH
.endm
	LM	lm
	LM	lm_r14, 1

@ f0-fe(ALT2) sm (xx),rn: RAM[xx] = rn, RAM[xx ^ 1] = rn >> 8
fx_arm_sm:
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
	CLRFLAGS
	add	r6, r6, #1
	DISPATCH

	.ltorg

@ The handlers, in the order of enum FxArmHandler in fxinst.cpp
	.align	2
.ifdef FX_COUNTED
	.global	fx_arm_handlers_counted
fx_arm_handlers_counted:
.else
	.global	fx_arm_handlers
fx_arm_handlers:
.endif
	.word	fx_arm_slow, fx_arm_stop, fx_arm_nop, fx_arm_lsr, fx_arm_rol, fx_arm_bra
	.word	fx_arm_bge, fx_arm_blt, fx_arm_bne, fx_arm_beq, fx_arm_bpl, fx_arm_bmi
	.word	fx_arm_bcc, fx_arm_bcs, fx_arm_bvc, fx_arm_bvs
	.word	fx_arm_to, fx_arm_with, fx_arm_stw, fx_arm_stb, fx_arm_loop
	.word	fx_arm_alt1, fx_arm_alt2, fx_arm_alt3, fx_arm_ldw, fx_arm_ldb, fx_arm_swap, fx_arm_not
	.word	fx_arm_add, fx_arm_adc, fx_arm_add_i, fx_arm_adc_i
	.word	fx_arm_sub, fx_arm_sbc, fx_arm_sub_i, fx_arm_cmp
	.word	fx_arm_and, fx_arm_bic, fx_arm_and_i, fx_arm_bic_i
	.word	fx_arm_or, fx_arm_xor, fx_arm_or_i, fx_arm_xor_i
	.word	fx_arm_mult, fx_arm_umult, fx_arm_mult_i, fx_arm_umult_i
	.word	fx_arm_link, fx_arm_sex, fx_arm_asr, fx_arm_ror, fx_arm_jmp, fx_arm_lob, fx_arm_hib
	.word	fx_arm_ibt, fx_arm_ibt_r14, fx_arm_ibt_r15, fx_arm_lms, fx_arm_lms_r14, fx_arm_sms
	.word	fx_arm_from, fx_arm_inc, fx_arm_inc_r14, fx_arm_dec, fx_arm_dec_r14
	.word	fx_arm_getb, fx_arm_iwt, fx_arm_iwt_r14, fx_arm_iwt_r15
	.word	fx_arm_lm, fx_arm_lm_r14, fx_arm_sm
	.word	0

@ The fast table (1024 handler addresses, ALT mode << 8 | opcode), then the
@ careful one
	.bss
	.align	2
.ifdef FX_COUNTED
	.global	fx_arm_tables_counted
fx_arm_tables_counted:
.else
	.global	fx_arm_tables
fx_arm_tables:
.endif
	.space	8192
