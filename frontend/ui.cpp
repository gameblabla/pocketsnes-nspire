#include <stdio.h>
#include <string.h>

#include "ui.h"

static const uint16_t sfc_colors[4] = { COLOR_SFC_RED, COLOR_SFC_YELLOW, COLOR_SFC_GREEN, COLOR_SFC_BLUE };

void ui_frame(const char *title, const char *right)
{
    draw_gradient(0, 0, SCREEN_W, SCREEN_H, COLOR_BG_TOP, COLOR_BG_BOTTOM);
    draw_gradient(0, 0, SCREEN_W, 22, COLOR_BAR_TOP, COLOR_BAR_BOTTOM);
    for (int i = 0; i < 4; i++)
        draw_rect(i * SCREEN_W / 4, 22, SCREEN_W / 4, 2, sfc_colors[i]);

    int title_end;
    if (title)
    {
        draw_string_fit(FONT_MEDIUM, title, 10, 3, right ? 190 : 300, COLOR_TEXT);
        title_end = 10 + text_width(FONT_MEDIUM, title);
        if (title_end > 200)
            title_end = 200;
    }
    else
        title_end = ui_logo(FONT_MEDIUM, 10, 3);
    if (right)
    {
        int room = 312 - title_end - 12;
        int width = text_width(FONT_SMALL, right);
        if (width > room)
            width = room;
        draw_string_fit(FONT_SMALL, right, 312 - width, 6, room, COLOR_TEXT_DIM);
    }
}

int ui_logo(const struct Font *font, int x, int y)
{
    x = draw_string(font, "Pocket", x, y, COLOR_TEXT);
    const char *snes = "SNES";
    for (int i = 0; i < 4; i++)
    {
        char letter[2] = { snes[i], 0 };
        x = draw_string(font, letter, x, y, sfc_colors[i]);
    }
    return x;
}

/* ---- Key caps ---- */

#define CAP_H 13

int ui_key_width(const char *key)
{
    if (strcmp(key, "up/down") == 0)
        return 6 + 7 + 2 + 7;
    if (strcmp(key, "left/right") == 0)
        return 6 + 4 + 3 + 4;
    return 8 + text_width(FONT_SMALL, key);
}

int ui_key(const char *key, int x, int y, bool lit)
{
    int w = ui_key_width(key);
    uint16_t fill = lit ? COLOR_SELECT_EDGE : COLOR_CHIP;
    uint16_t ink = lit ? COLOR_BG_BOTTOM : COLOR_CHIP_TEXT;

    draw_round_rect(x, y, w, CAP_H, 3, fill);
    draw_rect(x + 2, y + CAP_H - 1, w - 4, 1, lit ? COLOR_SELECT : COLOR_PANEL_EDGE);   /* the cap's lower edge */
    if (strcmp(key, "up/down") == 0)
    {
        draw_icon(ICON_ARROW_UP, x + 3, y + 4, ink);
        draw_icon(ICON_ARROW_DOWN, x + 3 + 9, y + 4, ink);
    }
    else if (strcmp(key, "left/right") == 0)
    {
        draw_icon(ICON_ARROW_LEFT, x + 3, y + 3, ink);
        draw_icon(ICON_ARROW_RIGHT, x + 3 + 7, y + 3, ink);
    }
    else
        draw_string(FONT_SMALL, key, x + 4, y + 1, ink);
    return w;
}

void ui_footer(const struct UiHint *hints, int count)
{
    draw_gradient(0, UI_FOOTER_Y, SCREEN_W, SCREEN_H - UI_FOOTER_Y, COLOR_BAR_BOTTOM, COLOR_BG_BOTTOM);
    draw_rect(0, UI_FOOTER_Y, SCREEN_W, 1, COLOR_PANEL_EDGE);
    int x = 6;
    for (int i = 0; i < count; i++)
    {
        int w = ui_key_width(hints[i].key) + 3 + text_width(FONT_SMALL, hints[i].action);
        if (x + w > SCREEN_W - 4)
            break;
        x += ui_key(hints[i].key, x, UI_FOOTER_Y + 3, false) + 3;
        x = draw_string(FONT_SMALL, hints[i].action, x, UI_FOOTER_Y + 4, COLOR_TEXT_DIM) + 10;
    }
}

/* ---- Lists ---- */

static void draw_switch(int right, int y, bool on)
{
    int x = right - 22;
    draw_round_rect(x, y, 22, 11, 5, on ? COLOR_GOOD : COLOR_TEXT_FAINT);
    draw_round_rect(on ? x + 12 : x + 1, y + 1, 9, 9, 4, COLOR_WHITE);
}

/* A row with an icon, or a key cap where the icon goes ('key' not NULL). */
static void row(int y, int icon, uint16_t icon_color, const char *key, const char *label, const char *value,
                int flags)
{
    bool moving = flags & ROW_MOVING;
    bool selected = moving || (flags & ROW_SELECTED);
    uint16_t ink = flags & ROW_FAINT ? COLOR_TEXT_FAINT : selected ? COLOR_TEXT : COLOR_TEXT_DIM;

    if (moving)
    {
        draw_round_rect(5, y, 304, UI_ROW_H, 4, COLOR_SELECT);
        draw_round_frame(5, y, 304, UI_ROW_H, 4, COLOR_VALUE);
    }
    else if (selected)
    {
        draw_round_rect(5, y, 304, UI_ROW_H, 4, COLOR_SELECT);
        draw_rect(5, y + 3, 2, UI_ROW_H - 6, COLOR_SELECT_EDGE);
    }
    int x = 12;
    if (key)
    {
        ui_key(key, x, y + 1, selected);
        x += 18;
    }
    else if (icon != ICON_NONE)
    {
        draw_icon(icon, x, y + 2, selected ? COLOR_TEXT : icon_color);
        x += 18;
    }

    int right = 300;
    if (moving)
    {
        draw_icon(ICON_ARROW_UP, 297, y + 3, COLOR_VALUE);
        draw_icon(ICON_ARROW_DOWN, 297, y + 8, COLOR_VALUE);
        right -= 10;
    }
    if (flags & ROW_SUBMENU)
        draw_icon(ICON_CHEVRON, 300, y + 4, selected ? COLOR_TEXT : COLOR_TEXT_FAINT);
    int value_w = 0;
    if (flags & ROW_TOGGLE)
    {
        draw_switch(right + 4, y + 2, value && strcmp(value, "on") == 0);
        value_w = 26;
    }
    else if (value && value[0])
    {
        bool arrows = selected && (flags & ROW_ARROWS);
        int text_right = arrows ? right - 2 : right + 4;
        int max_value = 150;
        int w = text_width(FONT_SMALL, value);
        if (w > max_value)
            w = max_value;
        draw_string_fit(FONT_SMALL, value, text_right - w, y + 2, max_value,
                        selected ? COLOR_VALUE : (flags & ROW_FAINT ? COLOR_TEXT_FAINT : COLOR_TEXT_DIM));
        if (arrows)
        {
            draw_icon(ICON_ARROW_LEFT, text_right - w - 8, y + 4, COLOR_VALUE);
            draw_icon(ICON_ARROW_RIGHT, right + 2, y + 4, COLOR_VALUE);
        }
        value_w = w + (arrows ? 12 : 0);
    }
    draw_string_fit(FONT_SMALL, label, x, y + 2, right - x - value_w - 8, ink);
}

void ui_row(int y, int icon, uint16_t icon_color, const char *label, const char *value, int flags)
{
    row(y, icon, icon_color, NULL, label, value, flags);
}

void ui_row_key(int y, const char *key, const char *label, const char *value, int flags)
{
    row(y, ICON_NONE, 0, key, label, value, flags);
}

void ui_scrollbar(int first, int shown, int total, int y, int h)
{
    if (total <= shown)
        return;
    draw_rect(314, y, 2, h, COLOR_PANEL_EDGE);
    int thumb = h * shown / total;
    if (thumb < 8)
        thumb = 8;
    int top = y + (h - thumb) * first / (total - shown);
    draw_rect(313, top, 4, thumb, COLOR_SELECT_EDGE);
}

int ui_wrap(const char *text, int columns, char lines[][64], int max_lines)
{
    int n = 0;
    if (columns > 63)
        columns = 63;
    while (*text && n < max_lines)
    {
        while (*text == ' ')
            text++;
        int len = (int) strlen(text);
        if (len > columns)
        {
            len = columns;
            while (len > 0 && text[len] != ' ')
                len--;
            if (len == 0)
                len = columns;
        }
        snprintf(lines[n++], 64, "%.*s", len, text);
        text += len;
    }
    return n;
}

void ui_help(const char *text)
{
    char lines[2][64];
    draw_round_rect(5, UI_HELP_Y, 310, 34, 5, COLOR_PANEL);
    draw_round_frame(5, UI_HELP_Y, 310, 34, 5, COLOR_PANEL_EDGE);
    int n = text ? ui_wrap(text, UI_HELP_COLUMNS + 2, lines, 2) : 0;
    for (int i = 0; i < n; i++)
        draw_string(FONT_SMALL, lines[i], 12, UI_HELP_Y + 5 + i * 13, COLOR_TEXT_DIM);
}

/* ---- Dialogs and messages ---- */

void ui_dialog(const char *title, const char *const *lines, int count,
               const struct UiHint *hints, int hint_count)
{
    char wrapped[10][64];
    int n = 0, first_lines = 0;
    for (int i = 0; i < count && n < 10; i++)
    {
        if (!lines[i])
            continue;
        n += ui_wrap(lines[i], 42, wrapped + n, 10 - n);
        if (i == 0)
            first_lines = n;
    }

    int title_h = title ? 22 : 0;
    int h = 14 + title_h + n * 13 + (hint_count ? 28 : 8);
    int y = (SCREEN_H - h) / 2;
    draw_shade(0, 0, SCREEN_W, SCREEN_H, 1);
    draw_round_rect(22, y + 3, 276, h, 7, COLOR_BG_BOTTOM);   /* a shadow */
    draw_round_rect(20, y, 276, h, 7, COLOR_PANEL);
    draw_round_frame(20, y, 276, h, 7, COLOR_PANEL_EDGE);
    for (int i = 0; i < 4; i++)
        draw_rect(40 + i * 59, y + 4, 59, 2, sfc_colors[i]);
    if (title)
        draw_string_center(FONT_MEDIUM, title, 158, y + 12, COLOR_TEXT);
    for (int i = 0; i < n; i++)
        draw_string_center(FONT_SMALL, wrapped[i], 158, y + 14 + title_h + i * 13,
                           title || i >= first_lines ? COLOR_TEXT_DIM : COLOR_TEXT);
    if (hint_count)
    {
        int total = 0;
        for (int i = 0; i < hint_count; i++)
            total += ui_key_width(hints[i].key) + 3 + text_width(FONT_SMALL, hints[i].action) + (i ? 10 : 0);
        int x = 158 - total / 2;
        for (int i = 0; i < hint_count; i++)
        {
            x += ui_key(hints[i].key, x, y + h - 21, false) + 3;
            x = draw_string(FONT_SMALL, hints[i].action, x, y + h - 20, COLOR_TEXT_DIM) + 10;
        }
    }
}

void ui_toast(const char *text, int y, int rect[4])
{
    int w = text_width(FONT_SMALL, text) + 18;
    if (w > SCREEN_W - 8)
        w = SCREEN_W - 8;
    int x = (SCREEN_W - w) / 2;
    draw_round_rect(x, y, w, 16, 5, COLOR_PANEL);
    draw_round_frame(x, y, w, 16, 5, COLOR_PANEL_EDGE);
    draw_rect(x + 3, y + 4, 2, 8, COLOR_SELECT_EDGE);
    draw_string_fit(FONT_SMALL, text, x + 10, y + 2, w - 14, COLOR_TEXT);
    rect[0] = x;
    rect[1] = y;
    rect[2] = w;
    rect[3] = 16;
}
