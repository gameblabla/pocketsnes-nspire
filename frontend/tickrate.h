/* The speed of the timer behind platform_ticks(), checked against the
 * real-time clock.
 *
 * The timer is set up to count 32768 times a second, and on the CX II it
 * does (its speed test reports it against the real-time clock). On an older
 * CX it may not: one report (gameblabla/pocketsnes-nspire#13) had games at a
 * quarter of their speed with the FPS counter at 60/60, which is what a timer
 * counting about 8192 times a second does, as frames are timed by it. So
 * while the program runs, the timer is timed against the real-time clock's
 * seconds, and if it is more than 5% off, its count is turned into ticks at
 * the speed it was meant to have from then on. */
#ifndef TICKRATE_H
#define TICKRATE_H

#include <stddef.h>
#include <stdint.h>

/* Starts over, for a timer meant to count 'hz' times a second. */
void tick_rate_start(uint32_t hz);

/* A timer reading 'raw' (counting up) taken when the real-time clock read
 * 'seconds', as ticks at the meant speed. Every reading of the timer should
 * come here: the ones just before and after the clock's second changes time
 * it, over two stretches of 2 seconds or more that have to agree. Until the
 * timer is found to be off, the reading comes back as it is; when it is, the
 * ticks carry on from where they were, without a jump. */
uint32_t tick_rate_ticks(uint32_t raw, uint32_t seconds);

/* Done checking (no more need for the real-time clock). */
int tick_rate_checked(void);

/* A line about it for the speed test results. */
void tick_rate_report(char *text, size_t size);

#endif
