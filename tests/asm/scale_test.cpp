/* The full screen picture as the calculator build makes it: scale_turn
 * with its loops in ARM assembler (frontend/scale_arm.S), against the
 * definition (scale_reference, turned with rotate_frame_reference), on
 * random pictures, every height from 120 to 240 lines, sharp and smooth.
 *   scale_test [ROUNDS] [SEED]      prints the first differences and PASS or FAIL
 *   scale_test time [CALLS]         times the turned frame output for
 *                                   tests/calc_speed.sh-style counting: the
 *                                   original size (rotate_block), then
 *                                   scale_turn sharp and smooth, CALLS each,
 *                                   with timing_frame_mark between calls
 * Runs on ARM (under qemu-arm on a PC, tests/asm/run.sh). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "rotate.h"
#include "scale.h"

static uint32_t rng = 1;
static uint32_t rnd (uint32_t n)
{
    rng = rng * 1103515245u + 12345u;
    return (rng >> 8) % n;
}

extern "C" void __attribute__ ((noinline)) timing_frame_mark (int frame)
{
    __asm__ volatile ("" : : "r" (frame) : "memory");
}

/* A game screen (320-pixel lines, the picture at x 32) with spare lines. */
static uint16_t screen [320 * 260] __attribute__ ((aligned (32)));
static uint16_t expected [320 * 240], turned [240 * 320] __attribute__ ((aligned (32)));
static uint16_t portrait [240 * 320] __attribute__ ((aligned (32)));

static int time_outputs (int calls)
{
    for (int i = 0; i < 320 * 260; i++)
        screen [i] = (uint16_t) rnd (0x10000);
    int frame = 0;
    for (int which = 0; which < 3; which++)
        for (int i = 0; i < calls; i++)
        {
            timing_frame_mark (frame++);
            if (which == 0)
                rotate_block (screen + 8 * 320 + 32, 320, 32, 8, 256, 224, portrait);
            else
                scale_turn (screen + 8 * 320 + 32, 320, 224, which == 2, portrait);
        }
    timing_frame_mark (frame);
    printf ("timed %d calls each: original (rotate_block), sharp, smooth\n", calls);
    return 0;
}

int main (int argc, char **argv)
{
    if (argc > 1 && strcmp (argv [1], "time") == 0)
        return time_outputs (argc > 2 ? atoi (argv [2]) : 20);

    int rounds = argc > 1 ? atoi (argv [1]) : 3;
    rng = argc > 2 ? (uint32_t) atoi (argv [2]) : 1;
    int failures = 0, cases = 0;

    for (int round = 0; round < rounds; round++)
    {
        for (int i = 0; i < 320 * 260; i++)
            screen [i] = (uint16_t) rnd (0x10000);
        const uint16_t *src = screen + (int) rnd (20) * 320 + 32;
        for (int lines = SCALE_MIN_LINES; lines <= 240; lines++)
            for (int smooth = 0; smooth < 2; smooth++)
            {
                scale_reference (src, 320, lines, smooth, expected, 320);
                rotate_frame_reference (expected, turned, ROTATE_FLIP_COLUMNS);
                memset (portrait, 0x55, sizeof (portrait));
                scale_turn (src, 320, lines, smooth, portrait);
                cases++;
                if (memcmp (portrait, turned, sizeof (portrait)) != 0)
                {
                    if (failures++ < 5)
                        for (int p = 0; p < 240 * 320; p++)
                            if (portrait [p] != turned [p])
                            {
                                printf ("lines %d smooth %d: portrait line %d pixel %d is %04x, not %04x\n",
                                        lines, smooth, p / 240, p % 240, portrait [p], turned [p]);
                                break;
                            }
                }
            }
    }
    printf ("%d cases, %d differ\n", cases, failures);
    printf (failures ? "FAIL\n" : "PASS\n");
    return failures != 0;
}
