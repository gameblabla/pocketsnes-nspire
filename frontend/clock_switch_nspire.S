/* The TI-Nspire CX II's CPU clock switch, done the way the OS does it (see
 * "CPU clock" in platform_nspire.cpp and FINDINGS.md). The code from
 * clock_switch_code to clock_switch_code_end is copied into the on-chip
 * SRAM and called there with IRQs and FIQs off, because normal memory sleeps
 * (self-refresh) during the switch. So between going to sleep and waking it,
 * this only touches registers and peripherals: no stack, no literal pool (a
 * read of cacheable memory could make the data cache write a line back to
 * the sleeping memory).
 *
 *   uint32_t clock_switch(uint32_t target, uint32_t step_ticks)
 *
 * target:     a clock register value; only its multiplier (bits 24-29) is
 *             used. The multiplier is stepped there one at a time.
 * step_ticks: how long one step may take, in ticks of the 32768 Hz timer.
 * Returns the steps taken in bits 0-7, and
 *   0x100  the memory didn't go to sleep (nothing was switched)
 *   0x200  a step didn't finish in time (the switch stopped there)
 *   0x400  the memory didn't wake up
 *   0x800  the power controller's interrupt was on before a step started
 *
 * The OS's switch (its code in the SRAM at 0xA4001640-0xA40019E8, OS 6.4):
 *   memory controller (0x90120000), register 4: writing 4 puts the memory to
 *     sleep (bit 2 clears when it has), bit 10 says it sleeps, writing 8
 *     wakes it (bit 3 clears when it has);
 *   each step: the clock register (power controller 0x90140030), only the
 *     power controller's interrupt (15) enabled at the interrupt controller,
 *     0x10000100 into power controller register 0x20 starts the switch, the
 *     CPU waits for the interrupt, power controller flags (0x24) cleared;
 *   then memory timing registers 0x74 and 0x1C written again (0x1C is
 *     always 0x502), 550 us, memory woken.
 * Here a step is done when the power controller's interrupt or its flag 0x100
 * (in 0x24) is on, and the second timer (0x900D0000, interrupt 19) can wake
 * the CPU too, so a switch that never finishes stops instead of hanging the
 * calculator. Seen on a CX II (OS 6.4): a step takes about 0.15 ms. */
    .syntax unified
    .arm
    .section .text.clock_switch, "ax", %progbits
    .balign 32
    .global clock_switch_code
    .global clock_switch_code_end
    .type clock_switch_code, %function

clock_switch_code:
    push    {r4-r11, lr}
    mov     r4, #0x90000000
    orr     r4, r4, #0x00140000     @ r4: power controller
    mov     r5, #0xDC000000         @ r5: interrupt controller
    mov     r6, #0x90000000
    orr     r6, r6, #0x00120000     @ r6: memory controller
    mov     r7, #0x90000000
    orr     r7, r7, #0x000D0000     @ r7: second timer
    mov     r11, #0                 @ r11: the result
    and     r0, r0, #0x3F000000     @ r0: the target multiplier, in place
    ldr     r9, [r6, #0x74]         @ r9, r10: memory timings
    ldr     r10, [r6, #0x1C]
    mov     lr, #0x10000000
    orr     lr, lr, #0x100          @ lr: what starts a switch

    @ Only the power controller and the timer can wake the CPU.
    ldr     r8, [r5, #0x10]         @ r8: the interrupts enabled before
    mvn     r3, #0
    str     r3, [r5, #0x14]
    mov     r3, #0x00008000
    orr     r3, r3, #0x00080000
    str     r3, [r5, #0x10]

    @ Normal memory to sleep, the write buffer emptied first.
    mov     r3, #0
    mcr     p15, 0, r3, c7, c10, 4
    ldr     r3, [r6, #4]
    tst     r3, #0x400
    bne     .Lstep
    mov     r3, #4
    str     r3, [r6, #4]
    mov     r12, #0x400000
1:  ldr     r3, [r6, #4]
    tst     r3, #4
    beq     .Lstep
    subs    r12, r12, #1
    bne     1b
    orr     r11, r11, #0x100
    b       .Lwake

.Lstep:
    ldr     r2, [r4, #0x30]
    and     r3, r2, #0x3F000000
    cmp     r3, r0
    beq     .Ldone
    addlo   r2, r2, #0x01000000
    subhi   r2, r2, #0x01000000
    str     r2, [r4, #0x30]
    @ Its flags cleared, the power controller's interrupt must be off.
    ldr     r3, [r4, #0x24]
    str     r3, [r4, #0x24]
    mov     r12, #64
7:  ldr     r3, [r5, #0x08]
    tst     r3, #0x8000
    beq     8f
    subs    r12, r12, #1
    bne     7b
    orr     r11, r11, #0x800
    b       .Ldone
8:
    @ The timer counts step_ticks down once.
    mov     r3, #0
    str     r3, [r7, #0x08]
    mov     r3, #1
    str     r3, [r7, #0x0C]
    str     r1, [r7, #0x00]
    mov     r3, #0xE3
    str     r3, [r7, #0x08]
    @ Start the switch and wait for the power controller's interrupt, or
    @ its "done" flag (bit 8). The timer's interrupt line can already be on
    @ from the OS's tick (seen), so only the timer's own flag means time's up.
    str     lr, [r4, #0x20]
2:  mov     r3, #0
    mcr     p15, 0, r3, c7, c0, 4   @ wait for interrupt
    ldr     r3, [r5, #0x08]
    tst     r3, #0x8000
    bne     3f
    ldr     r3, [r4, #0x24]
    tst     r3, #0x100
    bne     3f
    ldr     r3, [r7, #0x10]
    tst     r3, #1
    beq     2b
    orr     r11, r11, #0x200        @ the timer ran out first
    b       .Ldone
3:  ldr     r3, [r4, #0x24]
    str     r3, [r4, #0x24]
    add     r11, r11, #1
    b       .Lstep

.Ldone:
    @ The memory timings again, as the OS writes them after a switch, then
    @ about 600 us before the memory wakes (the OS waits 550).
    str     r9, [r6, #0x74]
    mov     r3, #0x500
    orr     r3, r3, #2
    cmp     r10, r3
    streq   r10, [r6, #0x1C]
    mov     r3, #0
    str     r3, [r7, #0x08]
    mov     r3, #1
    str     r3, [r7, #0x0C]
    mov     r3, #20
    str     r3, [r7, #0x00]
    mov     r3, #0xE3
    str     r3, [r7, #0x08]
4:  ldr     r3, [r7, #0x10]
    tst     r3, #1
    beq     4b

.Lwake:
    mov     r3, #0
    str     r3, [r7, #0x08]
    mov     r3, #1
    str     r3, [r7, #0x0C]
    ldr     r3, [r6, #4]
    tst     r3, #0x400
    beq     6f
    mov     r3, #8
    str     r3, [r6, #4]
    mov     r12, #0x400000
5:  ldr     r3, [r6, #4]
    tst     r3, #8
    beq     6f
    subs    r12, r12, #1
    bne     5b
    orr     r11, r11, #0x400
6:  nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop

    @ The interrupt controller as it was.
    mvn     r3, #0
    str     r3, [r5, #0x14]
    str     r8, [r5, #0x10]
    mov     r0, r11
    pop     {r4-r11, pc}
    .balign 4
clock_switch_code_end:
    .size clock_switch_code, . - clock_switch_code
