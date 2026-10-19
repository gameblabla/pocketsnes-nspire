#include "rotate.h"

#define SRC_W 320
#define SRC_H 240
#define DST_W 240
#define TILE  16

/* Works in 16x16 tiles: the tile's 16 source lines stay in the data cache
 * while each of its 16 columns becomes 8 consecutive 32-bit stores into one
 * output row, which the write buffer can send out in bursts. */
void rotate_frame(const uint16_t *src, uint16_t *dst, int flags)
{
    const int flip_columns = flags & ROTATE_FLIP_COLUMNS;
    const int flip_rows = flags & ROTATE_FLIP_ROWS;

    for (int yb = 0; yb < SRC_H; yb += TILE)
    {
        /* Output columns this tile covers start here. */
        int column = flip_columns ? DST_W - TILE - yb : yb;

        for (int x = 0; x < SRC_W; x++)
        {
            const uint16_t *s = src + yb * SRC_W + x;
            int row = flip_rows ? SRC_W - 1 - x : x;
            uint32_t *d = (uint32_t *) (dst + row * DST_W + column);

            if (!flip_columns)
            {
                d[0] = s[0 * SRC_W] | ((uint32_t) s[1 * SRC_W] << 16);
                d[1] = s[2 * SRC_W] | ((uint32_t) s[3 * SRC_W] << 16);
                d[2] = s[4 * SRC_W] | ((uint32_t) s[5 * SRC_W] << 16);
                d[3] = s[6 * SRC_W] | ((uint32_t) s[7 * SRC_W] << 16);
                d[4] = s[8 * SRC_W] | ((uint32_t) s[9 * SRC_W] << 16);
                d[5] = s[10 * SRC_W] | ((uint32_t) s[11 * SRC_W] << 16);
                d[6] = s[12 * SRC_W] | ((uint32_t) s[13 * SRC_W] << 16);
                d[7] = s[14 * SRC_W] | ((uint32_t) s[15 * SRC_W] << 16);
            }
            else
            {
                d[0] = s[15 * SRC_W] | ((uint32_t) s[14 * SRC_W] << 16);
                d[1] = s[13 * SRC_W] | ((uint32_t) s[12 * SRC_W] << 16);
                d[2] = s[11 * SRC_W] | ((uint32_t) s[10 * SRC_W] << 16);
                d[3] = s[9 * SRC_W] | ((uint32_t) s[8 * SRC_W] << 16);
                d[4] = s[7 * SRC_W] | ((uint32_t) s[6 * SRC_W] << 16);
                d[5] = s[5 * SRC_W] | ((uint32_t) s[4 * SRC_W] << 16);
                d[6] = s[3 * SRC_W] | ((uint32_t) s[2 * SRC_W] << 16);
                d[7] = s[1 * SRC_W] | ((uint32_t) s[0 * SRC_W] << 16);
            }
        }
    }
}

/* Writes 4 words to d (4-byte aligned). On the ARM it is one STM: GCC
 * writes them as 4 separate stores, and the ARM926's write buffer sends a
 * multiple store to memory as one burst but single stores one by one (the
 * frame buffers aren't in the data cache, which only fills on reads). STM
 * stores registers in register order, so the values are put in r4-r7. */
static inline void store_4_words(uint32_t *d, uint32_t a, uint32_t b, uint32_t c, uint32_t e)
{
#if defined(__arm__)
    register uint32_t r4 __asm__("r4") = a;
    register uint32_t r5 __asm__("r5") = b;
    register uint32_t r6 __asm__("r6") = c;
    register uint32_t r7 __asm__("r7") = e;
    __asm__ volatile ("stmia %0, {%1, %2, %3, %4}"
                      : : "r" (d), "r" (r4), "r" (r5), "r" (r6), "r" (r7) : "memory");
#else
    d[0] = a;
    d[1] = b;
    d[2] = c;
    d[3] = e;
#endif
}

/* One strip of up to 8 lines: each column becomes one run of h pixels in
 * output row x, written two at a time where the run is 4-byte aligned. A
 * full strip at an even line is 16 bytes a column, 4-byte aligned (16-byte
 * aligned at multiples of 8), and gets a loop of its own. */
static void rotate_strip(const uint16_t *src, int src_pitch, int x, int y, int w, int h, uint16_t *dst)
{
    const int first_column = DST_W - y - h;   /* where line y + h - 1 goes */

    /* The usual piece: a band of 8 lines whose runs are 4-byte aligned. */
    if (h == 8 && !(first_column & 1))
    {
        const uint16_t *s = src;   /* line y */
        uint32_t *d = (uint32_t *) (dst + x * DST_W + first_column);
        const int p = src_pitch;
        for (int i = 0; i < w; i++, s++, d += DST_W / 2)
            store_4_words(d,
                          s[7 * p] | ((uint32_t) s[6 * p] << 16),
                          s[5 * p] | ((uint32_t) s[4 * p] << 16),
                          s[3 * p] | ((uint32_t) s[2 * p] << 16),
                          s[1 * p] | ((uint32_t) s[0] << 16));
        return;
    }

    for (int i = 0; i < w; i++)
    {
        uint16_t *row = dst + (x + i) * DST_W;
        const uint16_t *s = src + (h - 1) * src_pitch + i;   /* line y + h - 1 */
        int column = first_column, end = DST_W - y;

        if (column & 1)
        {
            row[column++] = *s;
            s -= src_pitch;
        }
        for (; column + 1 < end; column += 2, s -= 2 * src_pitch)
            *(uint32_t *) (row + column) = s[0] | ((uint32_t) s[-src_pitch] << 16);
        if (column < end)
            row[column] = *s;
    }
}

/* In strips of 8 lines: a strip's source lines (4 KB for the 256-pixel
 * picture) stay in the data cache while it is turned column by column. */
void rotate_block(const uint16_t *src, int src_pitch, int x, int y, int w, int h, uint16_t *dst)
{
    while (h > 0)
    {
        int lines = h < 8 ? h : 8;
        rotate_strip(src, src_pitch, x, y, w, lines, dst);
        src += lines * src_pitch;
        y += lines;
        h -= lines;
    }
}

void rotate_frame_reference(const uint16_t *src, uint16_t *dst, int flags)
{
    for (int y = 0; y < SRC_H; y++)
        for (int x = 0; x < SRC_W; x++)
        {
            int column = (flags & ROTATE_FLIP_COLUMNS) ? DST_W - 1 - y : y;
            int row = (flags & ROTATE_FLIP_ROWS) ? SRC_W - 1 - x : x;
            dst[row * DST_W + column] = src[y * SRC_W + x];
        }
}
