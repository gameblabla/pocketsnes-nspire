@ The drawing code's busiest loops in ARM assembler, used with the ARM CPU
@ core setting (Settings.AsmCPU) in the calculator build: the 16-bit tile
@ drawers' inner loop (DrawTile16 and DrawClippedTile16, tile.cpp) and the
@ fills that clear the depth buffers and the screen for each line (gfx.cpp's
@ DrawLines). The C core setting keeps the C versions, so the game tests
@ (tests/asm/run.sh) compare the two; tests/asm/tile_test.cpp also checks
@ the tile drawers against Snes9x 1.43's on random tiles.
@
@ The tile drawers: the C loop takes 9 instructions a pixel and then waits
@ for each depth it loads; these take 6 and don't wait.
@
@ void DrawTileLines16_X (uint16 *Screen, uint8 *Depth, const uint8 *bp,
@                         const uint16 *Colors, uint32 LineCount,
@                         uint32 MaskA, uint32 MaskB)
@ draws LineCount lines of an 8x8 tile from the tile cache (8 bytes a line,
@ a palette index a pixel, 0 for transparent), the first at bp, like
@ tile.cpp's DrawTileLines16: for each line, A = its pixels 0-3 and B its
@ pixels 4-7, masked with MaskA and MaskB in the clipped versions; screen
@ pixel n shows tile pixel n (7 - n flipped horizontally) where that's not
@ 0 and GFX.Z1 is above the depth there: Screen [n] = Colors [pixel] and
@ Depth [n] = GFX.Z2. Then the next line: bp + 8 (bp - 8 flipped
@ vertically), Screen and Depth + GFX.PPL.
@ X: n, h (flipped horizontally), v (vertically) and hv, and nm, hm, vm and
@ hvm, the clipped ones (the others don't look at MaskA and MaskB).
@
@ A pixel: "ands" gets its palette index already doubled (its colour's byte
@ offset) and whether it's 0, then the depth test and the two stores are
@ conditional. The next pixel's depth is loaded in between, early enough
@ for its compare and late enough not to hold up this colour's store (on
@ the ARM926 a byte or halfword load's result comes 2 cycles late).

	.syntax unified
	.arm
	.text

@ struct SGFX (pocketsnes/include/gfx.h; tile.cpp checks the offsets)
	.equ GFX_PPL,	48
	.equ GFX_Z1,	72
	.equ GFX_Z2,	73

@ The arguments on the stack, after the push
	.equ ARG_COUNT,	36
	.equ ARG_MASKA,	40
	.equ ARG_MASKB,	44

@ Registers: r0 Screen, r1 Depth, r2 bp, r3 Colors, r4 A, r5 B, r6 the
@ pixel's colour offset, r7 its depth, r8 its colour, r9 Z1, r10 Z2,
@ r11 0x1fe, r12 GFX.PPL, lr the lines left

@ Screen pixel n, from byte k of word w; its depth is in r7. The last one
@ moves Depth on to the next line instead of loading a depth.
.macro PIXEL n, w, k, last=0
.if \k == 0
	ands	r6, r11, \w, lsl #1
.else
	ands	r6, r11, \w, lsr #(8 * \k - 1)
.endif
	cmpne	r9, r7			@ hi: not 0, and Z1 above the depth
	ldrhhi	r8, [r3, r6]
.if \last
	strbhi	r10, [r1, #\n]
	add	r1, r1, r12
.else
	ldrb	r7, [r1, #(\n + 1)]
	strbhi	r10, [r1, #\n]
.endif
	strhhi	r8, [r0, #(2 * \n)]
.endm

.macro TILE_LINES name, hflip, vflip, masked
	.global	\name
	.type	\name, %function
\name:
	push	{r4-r11, lr}
	ldr	lr, [sp, #ARG_COUNT]
	ldr	r12, =GFX
	mov	r11, #0xff
	lsl	r11, r11, #1		@ 0x1fe
	ldrb	r9, [r12, #GFX_Z1]
	ldrb	r10, [r12, #GFX_Z2]
	ldr	r12, [r12, #GFX_PPL]
.if \vflip
	add	r2, r2, #4		@ (for ldmda)
.endif
	cmp	lr, #0
	beq	9f
1:
.if \vflip
	ldmda	r2!, {r4, r5}		@ A and B; bp - 8
.else
	ldmia	r2!, {r4, r5}		@ A and B; bp + 8
.endif
	ldrb	r7, [r1]
.if \masked
	ldr	r6, [sp, #ARG_MASKA]
	ldr	r8, [sp, #ARG_MASKB]
	and	r4, r4, r6
	and	r5, r5, r8
.endif
	orrs	r6, r4, r5
	beq	2f			@ nothing to draw on this line
.if \hflip
	PIXEL	0, r5, 3
	PIXEL	1, r5, 2
	PIXEL	2, r5, 1
	PIXEL	3, r5, 0
	PIXEL	4, r4, 3
	PIXEL	5, r4, 2
	PIXEL	6, r4, 1
	PIXEL	7, r4, 0, 1
.else
	PIXEL	0, r4, 0
	PIXEL	1, r4, 1
	PIXEL	2, r4, 2
	PIXEL	3, r4, 3
	PIXEL	4, r5, 0
	PIXEL	5, r5, 1
	PIXEL	6, r5, 2
	PIXEL	7, r5, 3, 1
.endif
	add	r0, r0, r12, lsl #1
	subs	lr, lr, #1
	bne	1b
	pop	{r4-r11, pc}
2:	add	r1, r1, r12
	add	r0, r0, r12, lsl #1
	subs	lr, lr, #1
	bne	1b
9:	pop	{r4-r11, pc}
	.size	\name, . - \name
.endm

	TILE_LINES DrawTileLines16_n, 0, 0, 0
	TILE_LINES DrawTileLines16_h, 1, 0, 0
	TILE_LINES DrawTileLines16_v, 0, 1, 0
	TILE_LINES DrawTileLines16_hv, 1, 1, 0
	TILE_LINES DrawTileLines16_nm, 0, 0, 1
	TILE_LINES DrawTileLines16_hm, 1, 0, 1
	TILE_LINES DrawTileLines16_vm, 0, 1, 1
	TILE_LINES DrawTileLines16_hvm, 1, 1, 1
	.ltorg

@ void FillWords (void *p, uint32 pattern, uint32 bytes): bytes (a multiple
@ of 4) from p (word-aligned) set to pattern, 64 a loop in two 8-word
@ stores, which the write buffer sends to memory as bursts (memset stores
@ a word at a time).
	.global	FillWords
	.type	FillWords, %function
FillWords:
	push	{r4-r7, lr}
	mov	r3, r1
	mov	r4, r1
	mov	r5, r1
	mov	r6, r1
	mov	r7, r1
	mov	r12, r1
	mov	lr, r1
	subs	r2, r2, #64
	blo	2f
1:	stmia	r0!, {r1, r3-r7, r12, lr}
	stmia	r0!, {r1, r3-r7, r12, lr}
	subs	r2, r2, #64
	bhs	1b
2:	@ (r2 is what's left - 64, so its bits 5-2 are what's left)
	tst	r2, #32
	stmiane	r0!, {r1, r3-r7, r12, lr}
	tst	r2, #16
	stmiane	r0!, {r1, r3-r5}
	tst	r2, #8
	stmiane	r0!, {r1, r3}
	tst	r2, #4
	strne	r1, [r0]
	pop	{r4-r7, pc}
	.size	FillWords, . - FillWords
