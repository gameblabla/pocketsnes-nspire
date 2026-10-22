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
	@ its own section, for frontend/hot_sections.txt
	.section .text.gfx_arm,"ax",%progbits

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

@ The same without the depth test, for tiles nothing drawn on their lines
@ can hide (DrawBGTiles16 knows: their depth is above every depth already
@ on those lines): a pixel that isn't 0 is drawn and gets depth Z2. Same
@ arguments (MaskA and MaskB unused), same pixels as the tested drawers
@ in that case; no depth is read.
.macro PIXEL_UNTESTED n, w, k
.if \k == 0
	ands	r6, r11, \w, lsl #1
.else
	ands	r6, r11, \w, lsr #(8 * \k - 1)
.endif
	ldrhne	r8, [r3, r6]
	strbne	r10, [r1, #\n]
	strhne	r8, [r0, #(2 * \n)]
.endm

.macro TILE_LINES_UNTESTED name, hflip, vflip
	.global	\name
	.type	\name, %function
\name:
	push	{r4-r11, lr}
	ldr	lr, [sp, #ARG_COUNT]
	ldr	r12, =GFX
	mov	r11, #0xff
	lsl	r11, r11, #1		@ 0x1fe
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
	orrs	r6, r4, r5
	beq	2f			@ nothing to draw on this line
.if \hflip
	PIXEL_UNTESTED	0, r5, 3
	PIXEL_UNTESTED	1, r5, 2
	PIXEL_UNTESTED	2, r5, 1
	PIXEL_UNTESTED	3, r5, 0
	PIXEL_UNTESTED	4, r4, 3
	PIXEL_UNTESTED	5, r4, 2
	PIXEL_UNTESTED	6, r4, 1
	PIXEL_UNTESTED	7, r4, 0
.else
	PIXEL_UNTESTED	0, r4, 0
	PIXEL_UNTESTED	1, r4, 1
	PIXEL_UNTESTED	2, r4, 2
	PIXEL_UNTESTED	3, r4, 3
	PIXEL_UNTESTED	4, r5, 0
	PIXEL_UNTESTED	5, r5, 1
	PIXEL_UNTESTED	6, r5, 2
	PIXEL_UNTESTED	7, r5, 3
.endif
2:	add	r1, r1, r12
	add	r0, r0, r12, lsl #1
	subs	lr, lr, #1
	bne	1b
9:	pop	{r4-r11, pc}
	.size	\name, . - \name
.endm

	TILE_LINES_UNTESTED DrawTileLines16_un, 0, 0
	TILE_LINES_UNTESTED DrawTileLines16_uh, 1, 0
	TILE_LINES_UNTESTED DrawTileLines16_uv, 0, 1
	TILE_LINES_UNTESTED DrawTileLines16_uhv, 1, 1
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

@ ---------------------------------------------------------------------------
@ DrawBackground's unclipped tiles of a row with the ARM CPU core setting,
@ in two passes (tile.cpp's DrawBGTiles16 has the details): ListBGTiles16
@ makes a command for each tile to draw, DrawBGCommands16 draws them.

@ struct BGTileCommand *ListBGTiles16 (struct BGRowList *L)
@ For each of L->Count columns from L->Quot, the tile in the map row: a
@ command {bp, Colors, How} in L->Commands unless it's blank, with bp its
@ first line to draw (not flipped) and How column << 16 | L->How [bits
@ 13-15 of the map entry]. t (L->t) moves on and goes to the other half of
@ the map row (b1, b2) as in DrawBackground. Returns the commands' end, or
@ 0 when a 16x16 tile's number plus its quarter may carry into the palette
@ (tile.cpp then lists the row in C). Tiles not in the cache yet are
@ converted (tile.cpp's ConvertBGTile). Out: L->t, L->Last (the last map
@ entry), L->Drawn (the commands' hows ORed).

@ struct BGRowList (tile.cpp checks the offsets)
	.equ LIST_HOW,		0	@ [8], by the map entry's bits 13-15
	.equ LIST_COLORS,	32	@ [8], by bits 10-12
	.equ LIST_T,		64
	.equ LIST_B1,		68
	.equ LIST_B2,		72
	.equ LIST_QUOT,		76
	.equ LIST_COUNT,	80
	.equ LIST_BUFFERED,	84	@ BG.Buffered
	.equ LIST_BUFFER,	88	@ BG.Buffer + the first line to draw
	.equ LIST_FIRST,	92	@ the first tile (BG.TileAddress)
	.equ LIST_MASK,		96	@ the tile numbers' mask
	.equ LIST_T1,		100	@ 16x16 tiles: the half, 0 (top) or 16; 8x8: -1
	.equ LIST_COMMANDS,	104
	.equ LIST_LAST,		108
	.equ LIST_DRAWN,	112
@ the stack
	.equ LSTK_HOW,		0	@ (the tables copied)
	.equ LSTK_COLORS,	32
	.equ LSTK_LIST,		64
	.equ LSTK_T1,		68
	.equ LSTK_NEXT,		72	@ where t goes at the end of the run
	.equ LSTK_SIZE,		76	@ (with the 9 registers pushed, 8-byte aligned)
	.equ BLANK_TILE,	2

@ Registers in the runs: r0 t, r1 the run's end (8x8 tiles) or columns left
@ (16x16), r2 the next command, r3 the column << 16, r4 BG.Buffered, r5 the
@ cache + the first line, r6 the first tile, r7 the tile numbers' mask, r8
@ the map entry, r9 the tile number, lr the hows ORed.

@ The tile r9 (map entry r8): a command unless it's blank; if it isn't in
@ the cache yet, on at \slow (TILE_SLOW), back at \draw
.macro TILE_COMMAND slow, draw
	ldrb	r10, [r4, r9]		@ cached: 1 (TRUE)
	cmp	r10, #1
	bne	\slow
\draw:
	mov	r12, r8, lsr #13
	ldr	r12, [sp, r12, lsl #2]	@ How (LSTK_HOW 0)
	and	r11, r8, #0x1c00
	add	r10, r5, r9, lsl #6	@ bp
	add	r11, sp, r11, lsr #8
	ldr	r11, [r11, #LSTK_COLORS]
	orr	r12, r12, r3		@ the column
	orr	lr, lr, r12
	stmia	r2!, {r10, r11, r12}
.endm

@ A blank tile: on at \next; anything else but 0 (not converted yet) is
@ drawn, as by DrawTile16
.macro TILE_SLOW slow, draw, next
\slow:
	cmp	r10, #BLANK_TILE
	beq	\next
	cmp	r10, #0
	bne	\draw
	push	{r0-r3, r12, lr}
	mov	r0, r9
	bl	ConvertBGTile
	cmp	r0, #BLANK_TILE
	pop	{r0-r3, r12, lr}
	bne	\draw
	b	\next
.endm

@ r9 = the tile number of 8x8 quarter \q (0 left, 1 right) of the 16x16
@ tile in r8, as DrawBackground adds it up
.macro QUARTER q
	mov	r9, r8, lsl #22		@ the map entry's number << 22
	cmp	r9, #0xfb000000		@ (1004 << 22: from there on the sum may
	bhs	95f			@ carry into the palette)
	ldr	r10, [sp, #LSTK_T1]	@ the top or bottom half,
	and	r11, r8, #0x8000
	eor	r10, r10, r11, lsr #11	@ swapped when flipped vertically
	add	r9, r10, r9, lsr #22
	and	r11, r8, #0x4000	@ left or right, swapped when flipped
.if \q					@ horizontally
	add	r9, r9, #1
	sub	r9, r9, r11, lsr #14
.else
	add	r9, r9, r11, lsr #14
.endif
	add	r9, r9, r6		@ from the first tile
	and	r9, r9, r7		@ (wrapping past the last)
.endm

	.global	ListBGTiles16
	.type	ListBGTiles16, %function
ListBGTiles16:
	push	{r4-r11, lr}
	sub	sp, sp, #LSTK_SIZE
	str	r0, [sp, #LSTK_LIST]
	ldmia	r0!, {r1-r8}		@ How
	stmia	sp, {r1-r8}
	add	r12, sp, #LSTK_COLORS
	ldmia	r0!, {r1-r8}		@ Colors
	stmia	r12, {r1-r8}
	ldr	r12, [sp, #LSTK_LIST]
	ldr	r0, [r12, #LIST_T1]
	str	r0, [sp, #LSTK_T1]
	ldr	r0, [r12, #LIST_T]
	ldr	r2, [r12, #LIST_COMMANDS]
	ldr	r4, [r12, #LIST_BUFFERED]
	ldr	r5, [r12, #LIST_BUFFER]
	ldr	r6, [r12, #LIST_FIRST]
	ldr	r7, [r12, #LIST_MASK]
	mov	r3, #0
	mov	lr, #0

@ The next run of columns: to where t goes to the other half of the map row
@ (after Quot 31 and 63 with 8x8 tiles, 63 and 127 with 16x16), or the end
10:	ldr	r12, [sp, #LSTK_LIST]
	ldr	r1, [r12, #LIST_COUNT]
	cmp	r1, #0
	beq	90f			@ every column done
	ldr	r9, [r12, #LIST_QUOT]
	ldr	r10, [sp, #LSTK_T1]
	cmn	r10, #1
	moveq	r11, #32		@ columns in a half of the map row
	movne	r11, #64
	mov	r10, #0			@ where t goes at the end (0: on)
	cmp	r9, r11, lsl #1
	bhs	12f			@ (past the second half: on to the end)
	cmp	r9, r11
	ldrlo	r10, [r12, #LIST_B2]	@ in the first half: then the second
	ldrhs	r10, [r12, #LIST_B1]	@ in the second: then the first
	addhs	r11, r11, r11
	sub	r11, r11, r9		@ columns to the half's end
	cmp	r11, r1
	movhi	r10, #0			@ (the row ends first)
	movls	r1, r11
12:	str	r10, [sp, #LSTK_NEXT]
	add	r10, r9, r1
	str	r10, [r12, #LIST_QUOT]
	ldr	r10, [r12, #LIST_COUNT]
	sub	r10, r10, r1
	str	r10, [r12, #LIST_COUNT]
	ldr	r10, [sp, #LSTK_T1]
	cmn	r10, #1
	bne	40f			@ 16x16 tiles

	add	r1, r0, r1, lsl #1	@ 8x8 tiles: the run's end
20:	ldrh	r8, [r0], #2
	mov	r9, r8, lsl #22
	add	r9, r6, r9, lsr #22	@ from the first tile
	and	r9, r9, r7		@ (wrapping past the last)
	TILE_COMMAND .Lslow8, .Ldraw8
.Lnext8:
	add	r3, r3, #0x10000
	cmp	r0, r1
	bne	20b
	b	50f
	TILE_SLOW .Lslow8, .Ldraw8, .Lnext8

40:	tst	r9, #1			@ 16x16 tiles: Quot odd, a right half first
	bne	42f
41:	ldrh	r8, [r0]		@ a left half
	QUARTER 0
	TILE_COMMAND .Lslow16l, .Ldraw16l
.Lnext16l:
	add	r3, r3, #0x10000
	subs	r1, r1, #1
	beq	50f
42:	ldrh	r8, [r0], #2		@ a right half; then the next map entry
	QUARTER 1
	TILE_COMMAND .Lslow16r, .Ldraw16r
.Lnext16r:
	add	r3, r3, #0x10000
	subs	r1, r1, #1
	bne	41b
	b	50f
	TILE_SLOW .Lslow16l, .Ldraw16l, .Lnext16l
	TILE_SLOW .Lslow16r, .Ldraw16r, .Lnext16r

50:	ldr	r10, [sp, #LSTK_NEXT]	@ the run's end
	cmp	r10, #0
	movne	r0, r10
	b	10b

90:	ldr	r12, [sp, #LSTK_LIST]
	str	r0, [r12, #LIST_T]
	str	r8, [r12, #LIST_LAST]
	str	lr, [r12, #LIST_DRAWN]
	mov	r0, r2			@ the commands' end
	add	sp, sp, #LSTK_SIZE
	pop	{r4-r11, pc}

95:	mov	r0, #0			@ (C lists the row)
	add	sp, sp, #LSTK_SIZE
	pop	{r4-r11, pc}
	.size	ListBGTiles16, . - ListBGTiles16

@ void DrawBGCommands16 (const struct BGTileCommand *c, const struct BGTileCommand *end,
@			 const struct BGRowDraw *row)
@ For each command {bp, Colors, column << 16 | depth << 8 | how} from c to
@ end, the tile's lines at column * 8 pixels from the row's start, with the
@ depth test (how 0-3: the flips) or without it (how 4-7: its depth is
@ above every depth on its lines). bp is the first line to draw when not
@ flipped vertically; when it is, the row's VFlip on. A background tile's
@ two depths are the same (GFX.Z1 = GFX.Z2), here in r9. The lines are
@ drawn as by DrawTileLines16_X and DrawTileLines16_uX.

@ struct BGRowDraw (tile.cpp checks the offsets): the row's first pixel, its
@ first depth, the lines to draw, VFlip (+ 4), PPL; on the stack the first
@ four and the commands' end
	.equ STK_VFLIP,		12
	.equ STK_END,		16

@ Registers in the lines: r0 Screen, r1 Depth, r2 bp, r3 Colors, r4 A,
@ r5 B, r6 the pixel's colour offset, r7 its depth (tested), r8 its colour,
@ r9 the tile's depth, r10 the next command, r11 0x1fe, r12 PPL, lr lines
@ left.

@ A tested pixel: drawn where it isn't 0 and the depth there is below r9
.macro BG_PIXEL n, w, k, last=0
.if \k == 0
	ands	r6, r11, \w, lsl #1
.else
	ands	r6, r11, \w, lsr #(8 * \k - 1)
.endif
	cmpne	r9, r7
	ldrhhi	r8, [r3, r6]
.if !\last
	ldrb	r7, [r1, #(\n + 1)]
.endif
	strbhi	r9, [r1, #\n]
	strhhi	r8, [r0, #(2 * \n)]
.endm

@ An untested pixel: drawn where it isn't 0
.macro BG_PIXEL_U n, w, k
.if \k == 0
	ands	r6, r11, \w, lsl #1
.else
	ands	r6, r11, \w, lsr #(8 * \k - 1)
.endif
	ldrhne	r8, [r3, r6]
	strbne	r9, [r1, #\n]
	strhne	r8, [r0, #(2 * \n)]
.endm

.macro BG_LINES hflip, vflip, tested
.if \vflip
	ldr	r6, [sp, #STK_VFLIP]	@ the first line flipped (+ 4, for ldmda)
	add	r2, r2, r6
.endif
1:
.if \vflip
	ldmda	r2!, {r4, r5}		@ A and B; bp - 8
.else
	ldmia	r2!, {r4, r5}		@ A and B; bp + 8
.endif
	orrs	r6, r4, r5
	beq	2f			@ nothing to draw on this line
.if \tested
	ldrb	r7, [r1]
.if \hflip
	BG_PIXEL	0, r5, 3
	BG_PIXEL	1, r5, 2
	BG_PIXEL	2, r5, 1
	BG_PIXEL	3, r5, 0
	BG_PIXEL	4, r4, 3
	BG_PIXEL	5, r4, 2
	BG_PIXEL	6, r4, 1
	BG_PIXEL	7, r4, 0, 1
.else
	BG_PIXEL	0, r4, 0
	BG_PIXEL	1, r4, 1
	BG_PIXEL	2, r4, 2
	BG_PIXEL	3, r4, 3
	BG_PIXEL	4, r5, 0
	BG_PIXEL	5, r5, 1
	BG_PIXEL	6, r5, 2
	BG_PIXEL	7, r5, 3, 1
.endif
.else
.if \hflip
	BG_PIXEL_U	0, r5, 3
	BG_PIXEL_U	1, r5, 2
	BG_PIXEL_U	2, r5, 1
	BG_PIXEL_U	3, r5, 0
	BG_PIXEL_U	4, r4, 3
	BG_PIXEL_U	5, r4, 2
	BG_PIXEL_U	6, r4, 1
	BG_PIXEL_U	7, r4, 0
.else
	BG_PIXEL_U	0, r4, 0
	BG_PIXEL_U	1, r4, 1
	BG_PIXEL_U	2, r4, 2
	BG_PIXEL_U	3, r4, 3
	BG_PIXEL_U	4, r5, 0
	BG_PIXEL_U	5, r5, 1
	BG_PIXEL_U	6, r5, 2
	BG_PIXEL_U	7, r5, 3
.endif
.endif
2:	add	r1, r1, r12
	add	r0, r0, r12, lsl #1
	subs	lr, lr, #1
	bne	1b
	b	20b			@ the next command
.endm

	.global	DrawBGCommands16
	.type	DrawBGCommands16, %function
DrawBGCommands16:
	push	{r4-r11, lr}
	ldmia	r2, {r4, r5, r6, r7, r12} @ the row's first pixel and depth, lines, VFlip, PPL
	mov	r8, r1
	push	{r4-r8}
	mov	r10, r0
	mov	r11, #0xff
	lsl	r11, r11, #1		@ 0x1fe
20:	ldr	r0, [sp, #STK_END]
	cmp	r10, r0
	beq	99f
	ldmia	r10!, {r2, r3, r7}	@ bp, Colors, column << 16 | depth << 8 | how
	ldmia	sp, {r0, r1, lr}	@ the row's first pixel and depth, its lines
	mov	r6, r7, lsr #16
	add	r0, r0, r6, lsl #4	@ 8 pixels a column
	add	r1, r1, r6, lsl #3
	mov	r9, r7, lsr #8
	and	r9, r9, #0xff
	and	r6, r7, #7
	add	pc, pc, r6, lsl #2
	nop
	b	30f
	b	31f
	b	32f
	b	33f
	b	34f
	b	35f
	b	36f
	b	37f
30:	BG_LINES 0, 0, 1
31:	BG_LINES 1, 0, 1
32:	BG_LINES 0, 1, 1
33:	BG_LINES 1, 1, 1
34:	BG_LINES 0, 0, 0
35:	BG_LINES 1, 0, 0
36:	BG_LINES 0, 1, 0
37:	BG_LINES 1, 1, 0
99:	add	sp, sp, #20
	pop	{r4-r11, pc}
	.size	DrawBGCommands16, . - DrawBGCommands16
	.ltorg

@ ---------------------------------------------------------------------------
@ The colour maths tile drawers with the ARM CPU core setting (tile.cpp's
@ DrawTile16Add and the others):
@ void DrawTileLinesMath16_X (uint16 *Screen, uint8 *Depth, const uint8 *bp,
@                             const uint16 *Colors, const struct MathTile *m)
@ draws m->LineCount lines from bp (m->Step bytes a line: 8, or -8 flipped
@ vertically), each line's two words masked with m->MaskA and m->MaskB and
@ flipped horizontally in the _h versions, as DrawTileLines16_X does. A pixel
@ is drawn where it isn't 0 and Z1 (m->Z >> 8) is above the depth there, and
@ gets depth Z2 (m->Z & 0xff); its colour is mixed by the sub screen's depth
@ under it (at Depth + m->SubDepth): 0 the pixel's own colour, 1 its colour
@ in m->Fixed (the palette with the fixed colour already mixed in, made by
@ tile.cpp), more its colour mixed with the sub screen's pixel (at Screen +
@ m->Sub bytes) as X says: add (COLOR_ADD, with GFX.X2 in m->Table), add1_2
@ (COLOR_ADD1_2), sub (COLOR_SUB, GFX.ZERO_OR_X2), sub1_2 (COLOR_SUB1_2,
@ GFX.ZERO); fadd1_2 and fsub1_2 mix in the fixed colour only.
@
@ The sums (gfx.h's macros, with L the low bits 0x0821): COLOR_ADD's table
@ index is ((C1 & ~L) + (C2 & ~L)) / 2 + (C1 & C2 & L) = (C1 + C2 - ((C1 ^
@ C2) & L)) / 2, and that numerator is even, so it's the table entry's byte
@ offset; COLOR_ADD1_2 is that index itself. COLOR_SUB's index is ((C1 |
@ 0x10820) - (C2 & ~L)) / 2, and C2 & ~L = C2 - (C2 & L). The strh keeps a
@ sum's low 16 bits, as the C code's (uint16).

@ struct MathTile (tile.cpp checks the offsets)
	.equ MATH_LINES,	0
	.equ MATH_STEP,		4
	.equ MATH_MASKA,	8
	.equ MATH_MASKB,	12
	.equ MATH_PPL,		16
	.equ MATH_FIXED,	20
	.equ MATH_Z,		24
	.equ MATH_SUB,		28
	.equ MATH_SUBDEPTH,	32
	.equ MATH_TABLE,	36
@ the stack: the first six copied, then the line's second word
	.equ MSTK_LINES,	0
	.equ MSTK_STEP,		4
	.equ MSTK_MASKA,	8
	.equ MSTK_MASKB,	12
	.equ MSTK_PPL,		16
	.equ MSTK_FIXED,	20
	.equ MSTK_WORD,		24
	.equ MSTK_SIZE,		28	@ (with the 9 registers pushed, 64 bytes)

@ Registers: r0 Screen, r1 Depth, r2 bp, r3 Colors, r4 the line's word
@ being drawn, r5 0x0821, r6 r7 lr temporary, r8 the colour, r9 Z1 << 8 |
@ Z2, r10 the sub screen's line, r11 the table, r12 the sub screen's depths.

@ r8 = r8 mixed with r7 (r6, lr temporary)
.macro MIX_add
	eor	r6, r8, r7
	and	r6, r6, r5		@ (C1 ^ C2) & L
	add	lr, r8, r7
	sub	lr, lr, r6		@ the X2 entry's byte offset
	ldrh	lr, [r11, lr]
	orr	r8, lr, r6
.endm
.macro MIX_add1_2
	eor	r6, r8, r7
	and	r6, r6, r5
	add	lr, r8, r7
	sub	lr, lr, r6
	mov	r8, lr, lsr #1
.endm
.macro MIX_sub
	orr	r6, r8, #0x10000
	orr	r6, r6, #0x820		@ C1 | 0x10820
	and	lr, r7, r5		@ C2 & L
	sub	r6, r6, r7
	add	r6, r6, lr		@ - (C2 & ~L)
	bic	r6, r6, #1		@ the ZERO_OR_X2 entry's byte offset
	ldrh	r6, [r11, r6]
	sub	r6, r6, lr
	and	lr, r8, r5
	add	r8, r6, lr		@ + (C1 & L) - (C2 & L)
.endm
.macro MIX_sub1_2
	orr	r6, r8, #0x10000
	orr	r6, r6, #0x820
	and	lr, r7, r5
	sub	r6, r6, r7
	add	r6, r6, lr
	bic	r6, r6, #1
	ldrh	r8, [r11, r6]
.endm

@ Screen pixel \n from byte \k of r4
.macro MATH_PIXEL mix, fixed_only, n, k
	ands	r6, r4, #(0xff << (8 * \k))
	beq	.Lnext\@		@ 0: not drawn
	ldrb	r7, [r1, #\n]
.if \k == 0
	mov	r6, r6, lsl #1		@ the colour's offset
.else
	mov	r6, r6, lsr #(8 * \k - 1)
.endif
	ldrb	lr, [r12, #\n]		@ the sub screen's depth
	rsbs	r7, r7, r9, lsr #8
	bls	.Lnext\@		@ Z1 not above the depth
	cmp	lr, #1
.if \fixed_only
	ldreq	r7, [sp, #MSTK_FIXED]
	ldrhne	r8, [r3, r6]
	ldrheq	r8, [r7, r6]
.else
	ldrls	r7, [sp, #MSTK_FIXED]
	ldrhlo	r8, [r3, r6]		@ 0: the colour
	ldrheq	r8, [r7, r6]		@ 1: with the fixed colour
	bls	.Lstore\@
	ldrh	r8, [r3, r6]		@ more: with the sub screen's pixel
	ldrh	r7, [r10, #(2 * \n)]
	MIX_\mix
.endif
.Lstore\@:
	strb	r9, [r1, #\n]
	strh	r8, [r0, #(2 * \n)]
.Lnext\@:
.endm

.macro MATH_LINES name, mix, fixed_only, hflip
	.global	\name
	.type	\name, %function
\name:
	push	{r4-r11, lr}
	sub	sp, sp, #MSTK_SIZE
	ldr	lr, [sp, #(MSTK_SIZE + 36)]	@ m
	ldmia	lr, {r4-r9}		@ the lines, step, masks, PPL, Fixed
	stmia	sp, {r4-r9}
	ldr	r9, [lr, #MATH_Z]
	ldr	r10, [lr, #MATH_SUB]
	ldr	r12, [lr, #MATH_SUBDEPTH]
	ldr	r11, [lr, #MATH_TABLE]
	add	r10, r0, r10
	add	r12, r1, r12
	ldr	r5, =0x0821
	cmp	r4, #0
	beq	9f
1:	ldr	r4, [r2]		@ A, pixels 0-3
	ldr	r6, [r2, #4]		@ B, pixels 4-7
	ldr	r7, [sp, #MSTK_MASKA]
	ldr	r8, [sp, #MSTK_MASKB]
	and	r4, r4, r7
	and	r6, r6, r8
	orrs	r7, r4, r6
	beq	2f			@ nothing to draw on this line
.if \hflip
	str	r4, [sp, #MSTK_WORD]
	mov	r4, r6
	MATH_PIXEL \mix, \fixed_only, 0, 3
	MATH_PIXEL \mix, \fixed_only, 1, 2
	MATH_PIXEL \mix, \fixed_only, 2, 1
	MATH_PIXEL \mix, \fixed_only, 3, 0
	ldr	r4, [sp, #MSTK_WORD]
	MATH_PIXEL \mix, \fixed_only, 4, 3
	MATH_PIXEL \mix, \fixed_only, 5, 2
	MATH_PIXEL \mix, \fixed_only, 6, 1
	MATH_PIXEL \mix, \fixed_only, 7, 0
.else
	str	r6, [sp, #MSTK_WORD]
	MATH_PIXEL \mix, \fixed_only, 0, 0
	MATH_PIXEL \mix, \fixed_only, 1, 1
	MATH_PIXEL \mix, \fixed_only, 2, 2
	MATH_PIXEL \mix, \fixed_only, 3, 3
	ldr	r4, [sp, #MSTK_WORD]
	MATH_PIXEL \mix, \fixed_only, 4, 0
	MATH_PIXEL \mix, \fixed_only, 5, 1
	MATH_PIXEL \mix, \fixed_only, 6, 2
	MATH_PIXEL \mix, \fixed_only, 7, 3
.endif
2:	ldr	r7, [sp, #MSTK_STEP]
	ldr	r8, [sp, #MSTK_PPL]
	add	r2, r2, r7
	add	r1, r1, r8
	add	r12, r12, r8
	add	r0, r0, r8, lsl #1
	add	r10, r10, r8, lsl #1
	ldr	r7, [sp, #MSTK_LINES]
	subs	r7, r7, #1
	str	r7, [sp, #MSTK_LINES]
	bne	1b
9:	add	sp, sp, #MSTK_SIZE
	pop	{r4-r11, pc}
	.ltorg
	.size	\name, . - \name
.endm

	MATH_LINES DrawTileLinesMath16_add, add, 0, 0
	MATH_LINES DrawTileLinesMath16_add_h, add, 0, 1
	MATH_LINES DrawTileLinesMath16_add1_2, add1_2, 0, 0
	MATH_LINES DrawTileLinesMath16_add1_2_h, add1_2, 0, 1
	MATH_LINES DrawTileLinesMath16_sub, sub, 0, 0
	MATH_LINES DrawTileLinesMath16_sub_h, sub, 0, 1
	MATH_LINES DrawTileLinesMath16_sub1_2, sub1_2, 0, 0
	MATH_LINES DrawTileLinesMath16_sub1_2_h, sub1_2, 0, 1
	MATH_LINES DrawTileLinesMath16_fixed, add, 1, 0
	MATH_LINES DrawTileLinesMath16_fixed_h, add, 1, 1

@ ---------------------------------------------------------------------------
@ A sprite's whole tiles on one line, with the ARM CPU core setting
@ (tile.cpp's DrawOBJTiles16, for DrawOBJS):
@ void DrawOBJTiles16_X (const struct OBJTile *c, const struct OBJTile *end,
@                        const struct OBJLine *l)
@ For each {bp, X} from c to end: the tile line at bp (8 bytes) at pixel X
@ of l->Screen and l->Depth, drawn as by DrawTileLines16_n (_h: flipped
@ horizontally) with Z1 l->Z1 and Z2 l->Z2, in l->Colors.

@ struct OBJLine (tile.cpp checks the offsets)
	.equ OBJ_SCREEN,	0
	.equ OBJ_DEPTH,		4
	.equ OBJ_COLORS,	8
	.equ OBJ_Z1,		12
	.equ OBJ_Z2,		16
@ the stack: the line's screen and depth, the commands' end
	.equ OSTK_SCREEN,	0
	.equ OSTK_DEPTH,	4
	.equ OSTK_END,		8

@ Registers: r0 Screen, r1 Depth, r2 bp, r3 Colors, r4 A, r5 B, r6 the
@ pixel's colour offset, r7 its depth, r8 its colour, r9 Z1, r10 Z2,
@ r11 0x1fe, r12 the next command. (The last PIXEL's add to r1 doesn't
@ matter: r1 is set again for each tile.)

.macro OBJ_TILES name, hflip
	.global	\name
	.type	\name, %function
\name:
	push	{r4-r11, lr}
	ldmia	r2, {r4, r5, r6, r9, r10}	@ Screen, Depth, Colors, Z1, Z2
	mov	r8, r1
	push	{r4, r5, r8}		@ the line's screen and depth, the end
	mov	r3, r6
	mov	r12, r0
	mov	r11, #0xff
	lsl	r11, r11, #1		@ 0x1fe
1:	ldr	r0, [sp, #OSTK_END]
	cmp	r12, r0
	beq	9f
	ldmia	r12!, {r2, r6}		@ bp, X
	ldmia	r2, {r4, r5}		@ A and B
	orrs	r7, r4, r5
	beq	1b			@ nothing to draw on this line
	ldmia	sp, {r0, r1}		@ the line's screen and depth
	add	r0, r0, r6, lsl #1
	add	r1, r1, r6
	ldrb	r7, [r1]
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
	b	1b
9:	add	sp, sp, #12
	pop	{r4-r11, pc}
	.size	\name, . - \name
.endm

	OBJ_TILES DrawOBJTiles16_n, 0
	OBJ_TILES DrawOBJTiles16_h, 1
