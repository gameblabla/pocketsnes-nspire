#include <string.h>

#include "draw.h"

static void hline(uint16_t *row, int x0, int x1, uint16_t color)
{
    if (x0 < 0)
        x0 = 0;
    if (x1 > SCREEN_W)
        x1 = SCREEN_W;
    for (int x = x0; x < x1; x++)
        row[x] = color;
}

void draw_fill(uint16_t color)
{
    uint32_t *p = (uint32_t *) platform_screen();
    uint32_t both = color | (uint32_t) color << 16;
    for (int i = 0; i < SCREEN_W * SCREEN_H / 2; i++)
        p[i] = both;
}

void draw_rect(int x, int y, int w, int h, uint16_t color)
{
    uint16_t *screen = platform_screen();
    for (int j = y < 0 ? 0 : y; j < y + h && j < SCREEN_H; j++)
        hline(screen + j * SCREEN_W, x, x + w, color);
}

static uint16_t mix(uint16_t a, uint16_t b, int part, int whole)
{
    int r = (a >> 11) + (((b >> 11) - (a >> 11)) * part) / whole;
    int g = ((a >> 5) & 63) + ((((b >> 5) & 63) - ((a >> 5) & 63)) * part) / whole;
    int bl = (a & 31) + (((b & 31) - (a & 31)) * part) / whole;
    return (uint16_t) (r << 11 | g << 5 | bl);
}

void draw_gradient(int x, int y, int w, int h, uint16_t top, uint16_t bottom)
{
    uint16_t *screen = platform_screen();
    for (int j = 0; j < h; j++)
        if (y + j >= 0 && y + j < SCREEN_H)
            hline(screen + (y + j) * SCREEN_W, x, x + w, mix(top, bottom, j, h > 1 ? h - 1 : 1));
}

static int isqrt(int n)
{
    int r = 0;
    while ((r + 1) * (r + 1) <= n)
        r++;
    return r;
}

/* How far row j of a rounded box of height h starts in from its sides. */
static int corner_inset(int r, int j, int h)
{
    if (j >= r && j < h - r)
        return 0;
    int i = j < r ? j : h - 1 - j;
    int d = 2 * (r - i) - 1;
    return r - (isqrt(4 * r * r - d * d) + 1) / 2;
}

void draw_round_rect(int x, int y, int w, int h, int r, uint16_t color)
{
    uint16_t *screen = platform_screen();
    for (int j = 0; j < h; j++)
        if (y + j >= 0 && y + j < SCREEN_H)
        {
            int in = corner_inset(r, j, h);
            hline(screen + (y + j) * SCREEN_W, x + in, x + w - in, color);
        }
}

void draw_round_frame(int x, int y, int w, int h, int r, uint16_t color)
{
    uint16_t *screen = platform_screen();
    for (int j = 0; j < h; j++)
    {
        if (y + j < 0 || y + j >= SCREEN_H)
            continue;
        uint16_t *row = screen + (y + j) * SCREEN_W;
        int in = corner_inset(r, j, h);
        if (j == 0 || j == h - 1)
        {
            hline(row, x + in, x + w - in, color);
            continue;
        }
        /* Down to where the row above or below starts, so the edge joins up. */
        int above = corner_inset(r, j - 1, h), below = corner_inset(r, j + 1, h);
        int reach = (above > below ? above : below) - 1;
        if (reach < in)
            reach = in;
        hline(row, x + in, x + reach + 1, color);
        hline(row, x + w - 1 - reach, x + w - in, color);
    }
}

void draw_shade(int x, int y, int w, int h, int amount)
{
    uint16_t *screen = platform_screen();
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    for (int j = 0; j < h; j++)
    {
        uint16_t *p = screen + (y + j) * SCREEN_W + x;
        for (int i = 0; i < w; i++)
            p[i] = amount >= 2 ? (uint16_t) ((p[i] >> 2) & 0x39E7) : (uint16_t) ((p[i] >> 1) & 0x7BEF);
    }
}

/* The next character of UTF-8 text (bytes that aren't UTF-8 count as
 * Latin-1), moving past it. */
static unsigned next_char(const char **text)
{
    const unsigned char *p = (const unsigned char *) *text;
    unsigned c = *p++;
    if (c >= 0xC0 && c < 0xE0 && (p[0] & 0xC0) == 0x80)
        c = (c & 0x1F) << 6 | (*p++ & 0x3F);
    else if (c >= 0xE0 && c < 0xF0 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80)
    {
        c = (c & 0x0F) << 12 | (p[0] & 0x3F) << 6 | (p[1] & 0x3F);
        p += 2;
    }
    else if (c >= 0xF0 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80)
    {
        c = '?';
        p += 3;
    }
    *text = (const char *) p;
    return c;
}

static void draw_char(const struct Font *font, unsigned c, int x, int y, uint16_t color)
{
    if (x + font->w <= 0 || x >= SCREEN_W || y + font->h <= 0 || y >= SCREEN_H)
        return;
    const uint16_t *rows = font->rows + font->map[c < 256 ? c : '?'] * font->h;
    uint16_t *screen = platform_screen();
    for (int j = 0; j < font->h; j++)
    {
        uint16_t bits = rows[j];
        if (!bits || y + j < 0 || y + j >= SCREEN_H)
            continue;
        uint16_t *row = screen + (y + j) * SCREEN_W;
        for (int i = 0; i < font->w; i++)
            if ((bits & (0x8000u >> i)) && x + i >= 0 && x + i < SCREEN_W)
                row[x + i] = color;
    }
}

int text_width(const struct Font *font, const char *text)
{
    int n = 0;
    while (*text)
    {
        next_char(&text);
        n++;
    }
    return n * font->w;
}

int draw_string(const struct Font *font, const char *text, int x, int y, uint16_t color)
{
    while (*text)
    {
        draw_char(font, next_char(&text), x, y, color);
        x += font->w;
    }
    return x;
}

void draw_string_fit(const struct Font *font, const char *text, int x, int y, int max_w, uint16_t color)
{
    if (text_width(font, text) <= max_w)
    {
        draw_string(font, text, x, y, color);
        return;
    }
    int room = max_w - 3 * font->w;
    while (*text && room >= font->w)
    {
        draw_char(font, next_char(&text), x, y, color);
        x += font->w;
        room -= font->w;
    }
    draw_string(font, "...", x, y, color);
}

void draw_string_right(const struct Font *font, const char *text, int right, int y, uint16_t color)
{
    draw_string(font, text, right - text_width(font, text), y, color);
}

void draw_string_center(const struct Font *font, const char *text, int center, int y, uint16_t color)
{
    draw_string(font, text, center - text_width(font, text) / 2, y, color);
}

void draw_icon(int icon, int x, int y, uint16_t color)
{
    if (icon <= ICON_NONE || icon >= NUM_ICONS)
        return;
    const struct Icon *ic = &icons[icon];
    uint16_t *screen = platform_screen();
    for (int j = 0; j < ic->h; j++)
    {
        if (y + j < 0 || y + j >= SCREEN_H)
            continue;
        uint16_t *row = screen + (y + j) * SCREEN_W;
        for (int i = 0; i < ic->w; i++)
            if ((ic->rows[j] & (0x8000u >> i)) && x + i >= 0 && x + i < SCREEN_W)
                row[x + i] = color;
    }
}
