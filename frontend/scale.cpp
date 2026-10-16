#include "scale.h"

#define OUT_W 320
#define OUT_H 240
#define DST_W 240   /* pixels per portrait line (scale_turn) */

/* Two pixels at once (scale.h). */
static inline uint32_t average_pair(uint32_t p, uint32_t q)
{
    return (p & q) + (((p ^ q) & 0xF7DEF7DEu) >> 1);
}

static inline uint16_t average(uint16_t p, uint16_t q)
{
    return (uint16_t) ((p & q) + (((p ^ q) & 0xF7DEu) >> 1));
}

static int clamp_lines(int lines)
{
    return lines < SCALE_MIN_LINES ? SCALE_MIN_LINES : lines > OUT_H ? OUT_H : lines;
}

/* The source line output line y shows. */
static inline int source_line(int y, int lines)
{
    return (2 * y + 1) * lines / (2 * OUT_H);
}

/* A repeated line, when smooth: the average of the line and the next. */
static inline int blends(int y, int lines, int smooth)
{
    int s = source_line(y, lines);
    return smooth && y > 0 && s == source_line(y - 1, lines) && s + 1 < lines;
}

/* The averaged lines for smooth scaling: at most one per output line beyond
 * 'lines' (scale.h: no two repeated lines are next to each other). */
static uint16_t blended[OUT_H - SCALE_MIN_LINES][SCALE_SRC_W] __attribute__ ((aligned (8)));

/* plan[y]: what output line y shows before the columns are stretched, a
 * source line or an averaged one (made in 'blended'). */
static void plan_lines(const uint16_t *src, int src_pitch, int lines, int smooth, const uint16_t **plan)
{
    int n = 0;
    for (int y = 0; y < OUT_H; y++)
    {
        const uint16_t *line = src + source_line(y, lines) * src_pitch;
        if (blends(y, lines, smooth))
        {
            const uint32_t *a = (const uint32_t *) line, *b = (const uint32_t *) (line + src_pitch);
            uint32_t *d = (uint32_t *) blended[n];
            for (int i = 0; i < SCALE_SRC_W / 2; i++)
                d[i] = average_pair(a[i], b[i]);
            line = blended[n++];
        }
        plan[y] = line;
    }
}

#ifdef SCALE_ARM
/* The loops below in ARM assembler (scale_arm.S), for the calculator. */
extern "C" void scale_turn_strips(const uint16_t *const *plan, uint16_t *dst, int smooth);
#else
/* Strips of 4 output lines, each 2 words in the portrait buffer's lines;
 * per 4 source pixels across, 5 portrait lines. Output line y + 3 is the
 * first of the 4 in a portrait line (pixel 236 - y). */
static void scale_turn_strips(const uint16_t *const *plan, uint16_t *dst, int smooth)
{
    for (int y = 0; y < OUT_H; y += 4)
    {
        const uint16_t *l0 = plan[y + 3], *l1 = plan[y + 2], *l2 = plan[y + 1], *l3 = plan[y];
        uint32_t *d = (uint32_t *) (dst + DST_W - 4 - y);
        for (int x = 0; x < SCALE_SRC_W; x += 4, l0 += 4, l1 += 4, l2 += 4, l3 += 4)
        {
            uint32_t a0 = l0[0] | (uint32_t) l1[0] << 16, a1 = l2[0] | (uint32_t) l3[0] << 16;
            uint32_t b0 = l0[1] | (uint32_t) l1[1] << 16, b1 = l2[1] | (uint32_t) l3[1] << 16;
            uint32_t c0 = l0[2] | (uint32_t) l1[2] << 16, c1 = l2[2] | (uint32_t) l3[2] << 16;
            uint32_t e0 = l0[3] | (uint32_t) l1[3] << 16, e1 = l2[3] | (uint32_t) l3[3] << 16;
            d[0] = a0, d[1] = a1, d += DST_W / 2;
            d[0] = b0, d[1] = b1, d += DST_W / 2;
            d[0] = c0, d[1] = c1, d += DST_W / 2;
            if (smooth)
                d[0] = average_pair(c0, e0), d[1] = average_pair(c1, e1);
            else
                d[0] = c0, d[1] = c1;
            d += DST_W / 2;
            d[0] = e0, d[1] = e1, d += DST_W / 2;
        }
    }
}
#endif

void scale_turn(const uint16_t *src, int src_pitch, int lines, int smooth, uint16_t *dst)
{
    const uint16_t *plan[OUT_H];
    plan_lines(src, src_pitch, clamp_lines(lines), smooth, plan);
    scale_turn_strips(plan, dst, smooth);
}

void scale_frame(const uint16_t *src, int src_pitch, int lines, int smooth, uint16_t *dst, int dst_pitch)
{
    const uint16_t *plan[OUT_H];
    plan_lines(src, src_pitch, clamp_lines(lines), smooth, plan);

    for (int y = 0; y < OUT_H; y++)
    {
        const uint16_t *s = plan[y];
        uint16_t *d = dst + y * dst_pitch;
        for (int x = 0; x < SCALE_SRC_W; x += 4, s += 4, d += 5)
        {
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = smooth ? average(s[2], s[3]) : s[2];
            d[4] = s[3];
        }
    }
}

/* Source pixel (x, y) after the lines are stretched: output line Y. */
static uint16_t stretched_line_pixel(const uint16_t *src, int src_pitch, int lines, int smooth, int x, int Y)
{
    int s = source_line(Y, lines);
    uint16_t p = src[s * src_pitch + x];
    return blends(Y, lines, smooth) ? average(p, src[(s + 1) * src_pitch + x]) : p;
}

void scale_reference(const uint16_t *src, int src_pitch, int lines, int smooth, uint16_t *dst, int dst_pitch)
{
    lines = clamp_lines(lines);
    for (int Y = 0; Y < OUT_H; Y++)
        for (int X = 0; X < OUT_W; X++)
        {
            int g = X / 5, k = X % 5;
            uint16_t c = stretched_line_pixel(src, src_pitch, lines, smooth, 4 * g + 2, Y);
            uint16_t d = stretched_line_pixel(src, src_pitch, lines, smooth, 4 * g + 3, Y);
            uint16_t p;
            if (k < 3)
                p = stretched_line_pixel(src, src_pitch, lines, smooth, 4 * g + k, Y);
            else if (k == 3)
                p = smooth ? average(c, d) : c;
            else
                p = d;
            dst[Y * dst_pitch + X] = p;
        }
}
