/* Snes9x 1.43's 16-bit tile drawers (its tile.cpp: WRITE_4PIXELS16 with the
 * RENDER_TILE and RENDER_CLIPPED_TILE macros of tile.h), the reference the
 * tile drawing tests compare with: tests/unit_tests.cpp (the PC build's C
 * drawers) and tests/asm/tile_test.cpp (the calculator build's, C and ARM
 * assembler). Include after snes9x.h, memmap.h, ppu.h, gfx.h and tile.h.
 * The tests keep all their tiles in the cache (BG.Buffered), so
 * ConvertTile isn't called. */
static uint8 ConvertTile (uint8 *, uint32)
{
    return TRUE;   /* never called: the test's tiles are all cached */
}

static inline void REF_WRITE_4PIXELS16 (uint32 Offset, uint8 *Pixels, uint16 *ScreenColors)
{
    uint8  Pixel;
    uint16 *Screen = (uint16 *) GFX.S + (int32) Offset;
    uint8  *Depth = GFX.DB + (int32) Offset;

    for (uint8 N = 0; N < 4; N++)
    {
        if (GFX.Z1 > Depth [N] && (Pixel = Pixels[N]))
        {
            Screen [N] = ScreenColors [Pixel];
            Depth [N] = GFX.Z2;
        }
    }
}

static inline void REF_WRITE_4PIXELS16_FLIPPED (uint32 Offset, uint8 *Pixels, uint16 *ScreenColors)
{
    uint8  Pixel;
    uint16 *Screen = (uint16 *) GFX.S + (int32) Offset;
    uint8  *Depth = GFX.DB + (int32) Offset;

    for (uint8 N = 0; N < 4; N++)
    {
        if (GFX.Z1 > Depth [N] && (Pixel = Pixels[3 - N]))
        {
            Screen [N] = ScreenColors [Pixel];
            Depth [N] = GFX.Z2;
        }
    }
}

static void RefDrawTile16 (uint32 Tile, uint32 Offset, uint32 StartLine, uint32 LineCount)
{
    TILE_PREAMBLE
    register uint8 *bp;

    RENDER_TILE(REF_WRITE_4PIXELS16, REF_WRITE_4PIXELS16_FLIPPED, 4)
}

static void RefDrawClippedTile16 (uint32 Tile, uint32 Offset, uint32 StartPixel, uint32 Width,
                                  uint32 StartLine, uint32 LineCount)
{
    TILE_PREAMBLE
    register uint8 *bp;

    TILE_CLIP_PREAMBLE
    RENDER_CLIPPED_TILE(REF_WRITE_4PIXELS16, REF_WRITE_4PIXELS16_FLIPPED, 4)
}
