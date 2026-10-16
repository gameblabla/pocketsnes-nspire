/* The calculator build's 16-bit tile drawers, DrawTile16 and
 * DrawClippedTile16 with the ARM CPU core setting (their inner loop in ARM
 * assembler, pocketsnes/arm/gfx_arm.s) and with the C one, against
 * Snes9x 1.43's drawers (tests/tile_ref.h): random tiles (blank, opaque,
 * with transparent pixels and lines), flips, palettes, places on the
 * screen, lines, clips, depths and line lengths (GFX.PPL). The whole
 * screen and depth buffers must come out the same.
 *   tile_test [CASES] [SEED]
 * Prints the first differences and PASS or FAIL. Runs on ARM (under
 * qemu-arm on a PC, tests/asm/run.sh). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snes9x.h"
#include "memmap.h"
#include "ppu.h"
#include "gfx.h"
#include "tile.h"

void DrawTile16 (uint32 Tile, uint32 Offset, uint32 StartLine, uint32 LineCount);
void DrawClippedTile16 (uint32 Tile, uint32 Offset, uint32 StartPixel, uint32 Width,
                        uint32 StartLine, uint32 LineCount);

#include "../tile_ref.h"

extern "C" bool8 S9xInitUpdate () { return TRUE; }
extern "C" uint32 S9xReadJoypad (int) { return 0; }

static uint32 rng = 1;
static uint32 rnd (uint32 n)
{
    rng = rng * 1103515245u + 12345u;
    return (rng >> 8) % n;
}

#define MAX_PITCH 512
#define LINES     24
#define TILES     2048

static uint8 tile_cache [TILES * 64] __attribute__ ((aligned (8)));
static uint8 tile_cached [TILES];
static uint16 screen [3] [MAX_PITCH * LINES];
static uint8 depth [3] [MAX_PITCH * LINES];

static void setup_tiles (void)
{
    BG.TileShift = 5;
    BG.TileAddress = 0;
    BG.NameSelect = 0x2000;
    BG.Buffer = tile_cache;
    BG.Buffered = tile_cached;
    BG.PaletteShift = 4;
    BG.PaletteMask = 7;
    BG.StartPalette = 0;
    BG.DirectColourMode = FALSE;

    for (int t = 0; t < TILES; t++)
    {
        /* Blank, all opaque, or a mix with transparent (0) pixels, now and
         * then a whole line of them; all 256 palette indexes turn up. */
        int kind = (int) rnd (10);
        tile_cached [t] = kind == 0 ? BLANK_TILE : TRUE;
        for (int i = 0; i < 64; i++)
        {
            uint8 pixel = (uint8) rnd (t & 1 ? 256 : 16);
            if (kind == 1)
                pixel |= 1;
            else if (kind >= 2 && (rnd (3) == 0 || (i >> 3) == kind))
                pixel = 0;
            tile_cache [t * 64 + i] = kind == 0 ? 0 : pixel;
        }
    }
    for (int i = 0; i < 256; i++)
        IPPU.ScreenColors [i] = (uint16) rnd (0x10000);
}

int main (int argc, char **argv)
{
    int cases = argc > 1 ? atoi (argv [1]) : 100000;
    rng = argc > 2 ? (uint32) atoi (argv [2]) : 1;
    setup_tiles ();

    int failed [2] = {0, 0}, clipped_failed [2] = {0, 0};
    for (int i = 0; i < cases; i++)
    {
        /* Depths and Z often near each other, sometimes anywhere in 0-255 */
        uint32 range = rnd (4) == 0 ? 256 : 48;
        GFX.PPL = rnd (3) == 0 ? 256 + 8 * rnd (33) : 320;
        for (int j = 0; j < MAX_PITCH * LINES; j++)
        {
            screen [0] [j] = (uint16) rnd (0x10000);
            depth [0] [j] = (uint8) rnd (range);
        }
        for (int k = 1; k < 3; k++)
        {
            memcpy (screen [k], screen [0], sizeof (screen [0]));
            memcpy (depth [k], depth [0], sizeof (depth [0]));
        }
        GFX.Z1 = (uint8) rnd (range);
        GFX.Z2 = rnd (2) ? GFX.Z1 : (uint8) rnd (range);
        uint32 tile = rnd (0x400) | (rnd (8) << 10) | (rnd (4) << 14);   /* number, palette, flips */
        uint32 line = rnd (8), count = rnd (9 - line);
        if (count == 0 && rnd (4))
            count = 1;
        uint32 offset = (1 + rnd (LINES - 10)) * GFX.PPL + 8 + rnd (GFX.PPL - 24);
        bool clipped = i & 1;
        uint32 start = rnd (8), width = 1 + rnd (8 - start);

        /* 0: the reference, 1: the C drawer, 2: the assembler one */
        for (int k = 0; k < 3; k++)
        {
            GFX.S = (uint8 *) screen [k];
            GFX.DB = depth [k];
            Settings.AsmCPU = k == 2;
            if (k == 0)
            {
                if (clipped)
                    RefDrawClippedTile16 (tile, offset, start, width, line * 8, count);
                else
                    RefDrawTile16 (tile, offset, line * 8, count);
            }
            else if (clipped)
                DrawClippedTile16 (tile, offset, start, width, line * 8, count);
            else
                DrawTile16 (tile, offset, line * 8, count);
        }
        for (int k = 1; k < 3; k++)
        {
            if (memcmp (screen [k], screen [0], sizeof (screen [0])) == 0 &&
                memcmp (depth [k], depth [0], sizeof (depth [0])) == 0)
                continue;
            int &n = clipped ? clipped_failed [k - 1] : failed [k - 1];
            if (n++ < 5)
            {
                printf ("%s %s: tile %04x, offset %u (PPL %u), line %u, %u lines",
                        k == 2 ? "asm" : "C", clipped ? "DrawClippedTile16" : "DrawTile16",
                        tile, offset, GFX.PPL, line, count);
                if (clipped)
                    printf (", pixels %u-%u", start, start + width - 1);
                printf (", Z1 %u, Z2 %u\n", GFX.Z1, GFX.Z2);
                for (int j = 0; j < MAX_PITCH * LINES; j++)
                    if (screen [k] [j] != screen [0] [j] || depth [k] [j] != depth [0] [j])
                        printf ("  at %d: screen %04x depth %u, original %04x depth %u\n", j,
                                screen [k] [j], depth [k] [j], screen [0] [j], depth [0] [j]);
            }
        }
    }
    printf ("asm: %d and %d of %d cases differ (DrawTile16, DrawClippedTile16); C: %d and %d\n",
            failed [1], clipped_failed [1], cases, failed [0], clipped_failed [0]);
    bool pass = !failed [0] && !failed [1] && !clipped_failed [0] && !clipped_failed [1];
    printf ("%s: %d cases\n", pass ? "PASS" : "FAIL", cases);
    return pass ? 0 : 1;
}
