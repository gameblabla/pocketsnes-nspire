@ scale_turn's pixel work in ARM assembler, for the calculator (scale.h,
@ scale.cpp; tests/asm/scale_test.cpp checks it against the C version).
@
@ void scale_turn_strips (const uint16_t *const *plan, uint16_t *dst, int smooth)
@ plan: 240 pointers, the line each output line shows (256 pixels, 4-byte
@ aligned); dst: the 240x320 portrait buffer, 8-byte aligned. Output lines
@ go in strips of 4: lines y .. y + 3 are pixels 239 - y .. 236 - y of each
@ portrait line, two words (line y + 3 first). Each 4 source pixels across
@ (a b c d) make 5 portrait lines: a b c X d, X = c, or with smooth the
@ average of c and d, two pixels at a time:
@ (c & d) + (((c ^ d) & 0xF7DEF7DE) >> 1).
@
@ The words of a portrait line go out with one STRD, which also moves on to
@ the next portrait line (480 bytes on). The line pointers point at a
@ group's last pixel, whose load moves them on by 4 pixels.

	.syntax unified
	.arm
	.text

	.equ DST_LINE,	480		@ bytes in a portrait line
	.equ GROUPS,	64		@ 4 source pixels each

@ Registers in a strip: r0 r1 r12 lr the lines y+3, y+2, y+1, y; r2 r3 a
@ portrait line's words (a, b, c, then X); r8 r9 d's; r4 the portrait line;
@ r5 DST_LINE; r6 groups left; r7 a halfword; r10 the average's mask;
@ r11 the plan.

@ The words of source pixel \k (0-3) of the group into \w0, \w1; \next: the
@ last pixel of the group, which moves the line pointers on.
.macro COLUMN k, w0, w1, next=0
.if \next
	ldrh	\w0, [r0], #8
	ldrh	r7, [r1], #8
	ldrh	\w1, [r12], #8
	orr	\w0, \w0, r7, lsl #16
	ldrh	r7, [lr], #8
.else
	ldrh	\w0, [r0, #(2 * \k - 6)]
	ldrh	r7, [r1, #(2 * \k - 6)]
	ldrh	\w1, [r12, #(2 * \k - 6)]
	orr	\w0, \w0, r7, lsl #16
	ldrh	r7, [lr, #(2 * \k - 6)]
.endif
	orr	\w1, \w1, r7, lsl #16
.endm

@ \w = average of \w and \v, two pixels at once (uses r7)
.macro AVERAGE w, v
	eor	r7, \w, \v
	and	\w, \w, \v
	and	r7, r7, r10
	add	\w, \w, r7, lsr #1
.endm

.macro GROUP smooth
	COLUMN	0, r2, r3
	strd	r2, r3, [r4], r5
	COLUMN	1, r2, r3
	strd	r2, r3, [r4], r5
	COLUMN	2, r2, r3
	strd	r2, r3, [r4], r5
	COLUMN	3, r8, r9, 1
.if \smooth
	AVERAGE	r2, r8
	AVERAGE	r3, r9
.endif
	strd	r2, r3, [r4], r5
	strd	r8, r9, [r4], r5
.endm

	.global	scale_turn_strips
	.type	scale_turn_strips, %function
scale_turn_strips:
	push	{r4-r11, lr}
	mov	r11, r0
	add	r3, r0, #(4 * 240)	@ the plan's end
	add	r4, r1, #(2 * 236)	@ the first strip's pixels in portrait line 0
	push	{r2, r3, r4}		@ smooth, the plan's end, the strip's start
	mov	r5, #DST_LINE
	ldr	r10, =0xF7DEF7DE
1:	ldr	lr, [r11], #4		@ line y
	ldr	r12, [r11], #4		@ y + 1
	ldr	r1, [r11], #4		@ y + 2
	ldr	r0, [r11], #4		@ y + 3
	add	lr, lr, #6		@ (pixel 3)
	add	r12, r12, #6
	add	r1, r1, #6
	add	r0, r0, #6
	mov	r6, #GROUPS
	ldr	r2, [sp]
	cmp	r2, #0
	bne	3f
2:	GROUP	0
	subs	r6, r6, #1
	bne	2b
	b	4f
3:	GROUP	1
	subs	r6, r6, #1
	bne	3b
4:	ldr	r4, [sp, #8]		@ the next strip: 4 pixels back
	sub	r4, r4, #8
	str	r4, [sp, #8]
	ldr	r2, [sp, #4]
	cmp	r11, r2
	bne	1b
	add	sp, sp, #12
	pop	{r4-r11, pc}
	.size	scale_turn_strips, . - scale_turn_strips
	.ltorg
