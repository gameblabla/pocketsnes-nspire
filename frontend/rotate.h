/* Copies a 320x240 landscape frame into a 240x320 portrait buffer, for LCDs
 * that scan in portrait (the CX II). Pixel (x, y) goes to column y or
 * 239 - y of row x or 319 - x; which one is measured from lcd_blit at
 * startup (see platform_nspire.cpp). */
#ifndef ROTATE_H
#define ROTATE_H

#include <stdint.h>

#define ROTATE_FLIP_COLUMNS 1   /* output column = 239 - y instead of y */
#define ROTATE_FLIP_ROWS    2   /* output row = 319 - x instead of x */

/* src: 320x240, 320 pixels per line. dst: 240x320, 240 pixels per line,
 * 4-byte aligned. */
void rotate_frame(const uint16_t *src, uint16_t *dst, int flags);

/* Turns a w x h piece of a landscape frame into a portrait buffer the way
 * the CX II needs it (ROTATE_FLIP_COLUMNS): pixel (x, y) goes to column
 * 239 - y of row x. src points at pixel (x, y) and has src_pitch pixels a
 * line; dst is the whole 240x320 buffer, 4-byte aligned. */
void rotate_block(const uint16_t *src, int src_pitch, int x, int y, int w, int h, uint16_t *dst);

/* The plain pixel-by-pixel version, for checking rotate_frame. */
void rotate_frame_reference(const uint16_t *src, uint16_t *dst, int flags);

#endif
