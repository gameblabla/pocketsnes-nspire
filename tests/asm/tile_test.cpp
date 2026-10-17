/* The calculator build's 16-bit tile drawers, DrawTile16 and
 * DrawClippedTile16 with the ARM CPU core setting (their inner loop in ARM
 * assembler, pocketsnes/arm/gfx_arm.s) and with the C one, against
 * Snes9x 1.43's drawers (tests/tile_ref.h): random tiles (blank, opaque,
 * with transparent pixels and lines), flips, palettes, places on the
 * screen, lines, clips, depths and line lengths (GFX.PPL). The whole
 * screen and depth buffers must come out the same. Then the drawers
 * without the depth test (DrawTileLines16_un and the others, for tiles
 * nothing on their lines can hide) against the tested ones, on depths all
 * below Z1, where they must do the same. Then DrawBackground's rows of
 * unclipped tiles (tile.cpp's DrawBGTiles16, listed in ARM assembler and in
 * C) against Snes9x 1.43's loop: random map rows (8x8 and 16x16 tiles, the
 * tile numbers now and then where 16x16 quarters carry into the palette),
 * runs across the halves of the map row, tile data anywhere in VRAM (tile
 * numbers wrapping), 2, 4 and 8 bits a pixel, tiles still to be converted,
 * depths with ceilings.
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
const uint16 *DrawBGTiles16 (const uint16 *t, const uint16 *b1, const uint16 *b2, uint32 s,
                             uint32 Quot, int Count, uint32 StartLine, uint32 LineCount,
                             uint32 t1, uint32 t2, const uint8 *depths, uint32 Ceiling, uint32 *Drawn);
extern bool8 BGListInC;

#include "../tile_ref.h"

typedef void (*LinesDrawer) (uint16 *, uint8 *, const uint8 *, const uint16 *, uint32, uint32, uint32);
extern "C"
{
    void DrawTileLines16_n (uint16 *, uint8 *, const uint8 *, const uint16 *, uint32, uint32, uint32);
    void DrawTileLines16_h (uint16 *, uint8 *, const uint8 *, const uint16 *, uint32, uint32, uint32);
    void DrawTileLines16_v (uint16 *, uint8 *, const uint8 *, const uint16 *, uint32, uint32, uint32);
    void DrawTileLines16_hv (uint16 *, uint8 *, const uint8 *, const uint16 *, uint32, uint32, uint32);
    void DrawTileLines16_un (uint16 *, uint8 *, const uint8 *, const uint16 *, uint32, uint32, uint32);
    void DrawTileLines16_uh (uint16 *, uint8 *, const uint8 *, const uint16 *, uint32, uint32, uint32);
    void DrawTileLines16_uv (uint16 *, uint8 *, const uint8 *, const uint16 *, uint32, uint32, uint32);
    void DrawTileLines16_uhv (uint16 *, uint8 *, const uint8 *, const uint16 *, uint32, uint32, uint32);
}

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
#define TILES     4096

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

/* DrawBackground's unclipped tiles of a row as Snes9x 1.43 draws them (its
 * gfx.cpp loop, with tests/tile_ref.h's drawer): returns t after the row,
 * *Drawn the highest depth of a tile drawn (0: none) */
static const uint16 *RefBGRow (const uint16 *t, const uint16 *b1, const uint16 *b2, uint32 s, uint32 Quot,
                               int Count, uint32 StartLine, uint32 LineCount, uint32 t1, uint32 t2,
                               const uint8 *depths, uint32 *Drawn)
{
    *Drawn = 0;
    for (int C = Count; C > 0; s += 8, Quot++, C--)
    {
        uint32 Tile = *t;
        GFX.Z1 = GFX.Z2 = depths [(Tile & 0x2000) >> 13];
        if (BG.TileSize != 8)
            Tile += (Tile & V_FLIP ? t2 : t1) + (Tile & H_FLIP ? 1 - (Quot & 1) : (Quot & 1));
        RefDrawTile16 (Tile, s, StartLine, LineCount);
        uint32 TileAddr = (BG.TileAddress + ((Tile & 0x3ff) << BG.TileShift)) & 0xffff;
        if (BG.Buffered [TileAddr >> BG.TileShift] != BLANK_TILE && GFX.Z2 > *Drawn)
            *Drawn = GFX.Z2;
        if (BG.TileSize == 8)
        {
            t++;
            if (Quot == 31)
                t = b2;
            else if (Quot == 63)
                t = b1;
        }
        else
        {
            t += Quot & 1;
            if (Quot == 63)
                t = b2;
            else if (Quot == 127)
                t = b1;
        }
    }
    return t;
}

#define ROW_PITCH 640
#define ROW_LINES 10

static uint16 row_screen [3] [ROW_PITCH * ROW_LINES];
static uint8 row_depth [3] [ROW_PITCH * ROW_LINES];
static uint16 map_rows [2] [128];
static uint8 vram [0x10000 + 64];

static uint16 map_entry (void)
{
    uint32 number = rnd (4) == 0 ? 1000 + rnd (24) : rnd (0x400);
    return (uint16) (number | rnd (8) << 10 | rnd (2) << 13 | rnd (4) << 14);
}

/* DrawBGTiles16 (1: its tiles listed in ARM assembler, 2: in C) against
 * RefBGRow: the screen, the depths, t after the row, GFX.Z1/Z2 and Drawn
 * must come out the same. Returns the cases that differ. */
static int test_bg_rows (int cases, int *failed)
{
    S9xGraphicsInit ();		/* (ConvertTile's tables) */
    Memory.VRAM = vram;
    for (uint32 i = 0; i < sizeof (vram); i++)
        vram [i] = rnd (3) ? (uint8) rnd (256) : 0;
    for (int i = 0; i < cases; i++)
    {
        uint32 depth_bits = rnd (3);		/* 2, 4 or 8 bits a pixel */
        BG.TileShift = 4 + depth_bits;
        BG.BitShift = 2 << depth_bits;
        BG.TileAddress = rnd (0x10000 >> BG.TileShift) << BG.TileShift;
        BG.NameSelect = 0;
        BG.TileSize = rnd (2) ? 8 : 16;
        BG.PaletteShift = depth_bits == 0 ? 2 : depth_bits == 1 ? 4 : 0;
        BG.PaletteMask = depth_bits == 2 ? 0 : 7;
        BG.StartPalette = depth_bits == 0 ? 32 * rnd (8) : 0;
        BG.DirectColourMode = FALSE;
        GFX.PPL = ROW_PITCH;
        for (int r = 0; r < 2; r++)
            for (int k = 0; k < 128; k++)
                map_rows [r] [k] = map_entry ();
        const uint16 *b1 = map_rows [0], *b2 = map_rows [1], *t;
        uint32 Quot;
        if (BG.TileSize == 8)
        {
            Quot = rnd (8) == 0 ? 64 : rnd (64);
            t = Quot == 64 ? b1 : Quot > 31 ? b2 + (Quot & 31) : b1 + Quot;
        }
        else
        {
            Quot = rnd (8) == 0 ? 128 : rnd (128);
            t = Quot == 128 ? b1 : Quot > 63 ? b2 + ((Quot >> 1) & 31) : b1 + (Quot >> 1);
        }
        int Count = 1 + (int) rnd (rnd (2) ? 64 : 34);
        uint32 line = rnd (8), lines = 1 + rnd (8 - line);
        uint32 t1 = 16 * rnd (2), t2 = 16 - t1;
        uint8 depths [2];
        depths [0] = (uint8) (1 + rnd (100));
        depths [1] = rnd (4) ? (uint8) (depths [0] + 1 + rnd (50)) : (uint8) (1 + rnd (100));
        /* every depth on the row's lines at most Ceiling */
        uint32 Ceiling = rnd (4) == 0 ? 0 : rnd (160);
        for (int j = 0; j < ROW_PITCH * ROW_LINES; j++)
        {
            row_screen [0] [j] = (uint16) rnd (0x10000);
            row_depth [0] [j] = (uint8) rnd (Ceiling + 1);
        }
        for (int k = 1; k < 3; k++)
        {
            memcpy (row_screen [k], row_screen [0], sizeof (row_screen [0]));
            memcpy (row_depth [k], row_depth [0], sizeof (row_depth [0]));
        }
        uint32 s = ROW_PITCH + rnd (ROW_PITCH - 8 * Count - 8);
        /* a few tiles to be converted (by each run but the reference, last) */
        uint32 unconverted [8];
        for (int k = 0; k < 8; k++)
            unconverted [k] = rnd (TILES);

        const uint16 *t_after [3];
        uint32 drawn [3], z [3];
        for (int k = 1; k < 3; k++)
        {
            for (int u = 0; u < 8; u++)
                tile_cached [unconverted [u]] = 0;
            GFX.S = (uint8 *) row_screen [k];
            GFX.DB = row_depth [k];
            GFX.Z1 = GFX.Z2 = 0;
            BGListInC = k == 2;
            t_after [k] = DrawBGTiles16 (t, b1, b2, s, Quot, Count, line * 8, lines, t1, t2, depths, Ceiling,
                                         &drawn [k]);
            z [k] = GFX.Z1 | GFX.Z2 << 8;
        }
        BGListInC = FALSE;
        GFX.S = (uint8 *) row_screen [0];
        GFX.DB = row_depth [0];
        GFX.Z1 = GFX.Z2 = 0;
        t_after [0] = RefBGRow (t, b1, b2, s, Quot, Count, line * 8, lines, t1, t2, depths, &drawn [0]);
        z [0] = GFX.Z1 | GFX.Z2 << 8;

        for (int k = 1; k < 3; k++)
        {
            if (memcmp (row_screen [k], row_screen [0], sizeof (row_screen [0])) == 0 &&
                memcmp (row_depth [k], row_depth [0], sizeof (row_depth [0])) == 0 &&
                t_after [k] == t_after [0] && drawn [k] == drawn [0] && z [k] == z [0])
                continue;
            if (failed [k - 1]++ < 5)
            {
                printf ("%s DrawBGTiles16: %ux%u tiles, %u bits, tiles at %04x, Quot %u, %d columns, line %u, "
                        "%u lines, t1 %u, depths %u %u, ceiling %u\n", k == 1 ? "asm" : "C",
                        BG.TileSize, BG.TileSize, BG.BitShift, BG.TileAddress, Quot, Count, line, lines,
                        t1, depths [0], depths [1], Ceiling);
                printf ("  t after: %+d, original %+d; drawn %u, original %u; Z1/Z2 %04x, original %04x\n",
                        (int) (t_after [k] - b1), (int) (t_after [0] - b1), drawn [k], drawn [0], z [k], z [0]);
                int shown = 0;
                for (int j = 0; j < ROW_PITCH * ROW_LINES && shown < 8; j++)
                    if (row_screen [k] [j] != row_screen [0] [j] || row_depth [k] [j] != row_depth [0] [j])
                    {
                        printf ("  at %d: screen %04x depth %u, original %04x depth %u\n", j,
                                row_screen [k] [j], row_depth [k] [j], row_screen [0] [j], row_depth [0] [j]);
                        shown++;
                    }
            }
        }
    }
    return failed [0] + failed [1];
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

    /* The untested drawers: every depth below Z1, so the tested ones draw
     * every pixel that isn't 0 too */
    static const LinesDrawer tested [4] = { DrawTileLines16_n, DrawTileLines16_h, DrawTileLines16_v,
                                            DrawTileLines16_hv };
    static const LinesDrawer untested [4] = { DrawTileLines16_un, DrawTileLines16_uh, DrawTileLines16_uv,
                                              DrawTileLines16_uhv };
    int untested_failed = 0;
    for (int i = 0; i < cases; i++)
    {
        GFX.PPL = rnd (3) == 0 ? 256 + 8 * rnd (33) : 320;
        GFX.Z1 = (uint8) (1 + rnd (255));
        GFX.Z2 = rnd (2) ? GFX.Z1 : (uint8) rnd (256);
        for (int j = 0; j < MAX_PITCH * LINES; j++)
        {
            screen [0] [j] = (uint16) rnd (0x10000);
            depth [0] [j] = (uint8) rnd (GFX.Z1);
        }
        memcpy (screen [1], screen [0], sizeof (screen [0]));
        memcpy (depth [1], depth [0], sizeof (depth [0]));
        uint32 t = rnd (TILES), flips = rnd (4), line = rnd (8), count = rnd (9 - line);
        const uint8 *bp = tile_cache + t * 64 + ((flips & 2) ? 56 - line * 8 : line * 8);
        const uint16 *colors = IPPU.ScreenColors + 16 * rnd (8);
        uint32 offset = (1 + rnd (LINES - 10)) * GFX.PPL + 8 + rnd (GFX.PPL - 24);
        tested [flips] (screen [0] + offset, depth [0] + offset, bp, colors, count, 0xffffffff, 0xffffffff);
        untested [flips] (screen [1] + offset, depth [1] + offset, bp, colors, count, 0xffffffff, 0xffffffff);
        if (memcmp (screen [1], screen [0], sizeof (screen [0])) || memcmp (depth [1], depth [0], sizeof (depth [0])))
        {
            if (untested_failed++ < 5)
                printf ("untested drawer %u: tile %u, offset %u (PPL %u), line %u, %u lines, Z1 %u, Z2 %u\n",
                        flips, t, offset, GFX.PPL, line, count, GFX.Z1, GFX.Z2);
        }
    }
    printf ("untested drawers: %d of %d cases differ from the tested ones\n", untested_failed, cases);
    int rows_failed [2] = {0, 0};
    int row_cases = cases / 4;
    test_bg_rows (row_cases, rows_failed);
    printf ("background rows: %d (assembler list) and %d (C) of %d differ from Snes9x 1.43's loop\n",
            rows_failed [0], rows_failed [1], row_cases);
    bool pass = !failed [0] && !failed [1] && !clipped_failed [0] && !clipped_failed [1] && !untested_failed &&
                !rows_failed [0] && !rows_failed [1];
    printf ("%s: %d cases\n", pass ? "PASS" : "FAIL", cases);
    return pass ? 0 : 1;
}
