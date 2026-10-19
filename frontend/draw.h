/* Drawing into platform_screen() (RGB565): the colours, fonts and icons the
 * screens are made of, and the shapes. Everything is clipped to the screen. */
#ifndef DRAW_H
#define DRAW_H

#include <stdint.h>

#include "fonts.h"
#include "icons.h"
#include "platform.h"

/* 0xRRGGBB as RGB565. */
#define HEX(rgb) ((uint16_t) ((((rgb) >> 8) & 0xF800) | (((rgb) >> 5) & 0x07E0) | (((rgb) >> 3) & 0x001F)))

/* The theme: deep indigo, lavender for what's selected (the SNES's buttons),
 * amber values, and the Super Famicom's four button colours as accents. */
#define COLOR_BG_TOP      HEX(0x1C2040)
#define COLOR_BG_BOTTOM   HEX(0x0A0C19)
#define COLOR_BAR_TOP     HEX(0x2B3166)
#define COLOR_BAR_BOTTOM  HEX(0x1D2147)
#define COLOR_PANEL       HEX(0x151933)
#define COLOR_PANEL_EDGE  HEX(0x30376A)
#define COLOR_SELECT      HEX(0x3A4095)
#define COLOR_SELECT_EDGE HEX(0xA7ACFF)
#define COLOR_TEXT        HEX(0xF0F1FA)
#define COLOR_TEXT_DIM    HEX(0xA5AACB)
#define COLOR_TEXT_FAINT  HEX(0x666D96)
#define COLOR_VALUE       HEX(0xFFD27A)
#define COLOR_GOOD        HEX(0x6EE19A)
#define COLOR_WARN        HEX(0xFFC15E)
#define COLOR_BAD         HEX(0xFF7B7B)
#define COLOR_CHIP        HEX(0x2E3366)
#define COLOR_CHIP_TEXT   HEX(0xE3E5F7)
#define COLOR_FOLDER      HEX(0xF4C02F)
#define COLOR_SFC_RED     HEX(0xE2463C)
#define COLOR_SFC_YELLOW  HEX(0xF4C02F)
#define COLOR_SFC_GREEN   HEX(0x43B35E)
#define COLOR_SFC_BLUE    HEX(0x3F75D6)
#define COLOR_BLACK       HEX(0x000000)
#define COLOR_WHITE       HEX(0xFFFFFF)

/* Body text, titles, and the big logo. */
#define FONT_SMALL  (&font_6x12)
#define FONT_MEDIUM (&font_8x16)
#define FONT_LARGE  (&font_12x24)
#define TEXT_W 6
#define TEXT_H 12

void draw_fill(uint16_t color);
void draw_rect(int x, int y, int w, int h, uint16_t color);
/* A vertical gradient from 'top' to 'bottom'. */
void draw_gradient(int x, int y, int w, int h, uint16_t top, uint16_t bottom);
/* Rounded corners of radius r. */
void draw_round_rect(int x, int y, int w, int h, int r, uint16_t color);
void draw_round_frame(int x, int y, int w, int h, int r, uint16_t color);
/* Darkens what's there: to a half (1) or a quarter (2). */
void draw_shade(int x, int y, int w, int h, int amount);

/* Text is UTF-8; characters the font lacks show as spaces. Only the pixels
 * of the letters are drawn. draw_string returns the x after the text. */
int  text_width(const struct Font *font, const char *text);
int  draw_string(const struct Font *font, const char *text, int x, int y, uint16_t color);
/* Cut to max_w pixels, ending in "..." when it doesn't fit. */
void draw_string_fit(const struct Font *font, const char *text, int x, int y, int max_w, uint16_t color);
void draw_string_right(const struct Font *font, const char *text, int right, int y, uint16_t color);
void draw_string_center(const struct Font *font, const char *text, int center, int y, uint16_t color);

void draw_icon(int icon, int x, int y, uint16_t color);

#endif
