/* The SNES picture stretched to the whole 320x240 screen (Graphics & speed >
 * Screen size). Across, every 4 pixels a b c d become 5: a b c X d; down,
 * 'lines' lines become 240, the repeated ones spread evenly (output line y
 * shows source line (2y + 1) * lines / 480, and a line that repeats the one
 * above it is a repeated line). X and the repeated lines are copies (c, and
 * the line above) when sharp; smooth makes them the average of their two
 * neighbours (c and d; the line and the next one), so no column or line is
 * wider than the others. The lines are done first, then the columns: a
 * repeated line's X is the average of its two averages.
 *
 * The average of two RGB565 pixels rounds each colour down:
 * (p & q) + (((p ^ q) & 0xF7DE) >> 1), which also works on two pixels in a
 * 32-bit word at once (mask 0xF7DEF7DE). */
#ifndef SCALE_H
#define SCALE_H

#include <stdint.h>

#define SCALE_SRC_W     256   /* pixels across in the picture */
#define SCALE_MIN_LINES 120   /* at most two output lines per source line */

/* For the CX II's LCD, turned like rotate.h's frames: picture pixel (x, y)
 * stretched to landscape (X, Y) goes to dst[X * 240 + 239 - Y]. src is the
 * picture's first pixel, src_pitch pixels a line; dst is the whole 240x320
 * portrait buffer, 8-byte aligned. lines is clamped to 120..240. */
void scale_turn(const uint16_t *src, int src_pitch, int lines, int smooth, uint16_t *dst);

/* The same, not turned: into a 320x240 landscape frame of dst_pitch pixels
 * a line (screens without the CX II's layout). dst must not overlap src. */
void scale_frame(const uint16_t *src, int src_pitch, int lines, int smooth, uint16_t *dst, int dst_pitch);

/* The same pixel by pixel from the definition above, for the tests. */
void scale_reference(const uint16_t *src, int src_pitch, int lines, int smooth, uint16_t *dst, int dst_pitch);

#endif
