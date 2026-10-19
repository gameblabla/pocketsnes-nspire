/* What every screen is made of: the background, a header with a title, a
 * footer with key hints, list rows, a help panel, dialogs, and messages over
 * the game. Laid out for the 320x240 screen:
 *
 *   0..21    header bar (title, and dim text at the right)
 *   22..23   the four Super Famicom colours
 *   30..     list rows, UI_ROW_H each
 *   184..217 help panel (two lines)
 *   221..239 key hints
 */
#ifndef UI_H
#define UI_H

#include <stdbool.h>

#include "draw.h"

#define UI_HEADER_H 24
#define UI_LIST_Y   29
#define UI_ROW_H    15
#define UI_HELP_Y   184
#define UI_HELP_COLUMNS 48
#define UI_FOOTER_Y 221
/* Rows that fit between the header and the help panel. */
#define UI_LIST_ROWS ((UI_HELP_Y - 3 - UI_LIST_Y) / UI_ROW_H)

/* The background and the header: 'title' (NULL: the PocketSNES logo) and
 * 'right' in dim text at the right (may be NULL). */
void ui_frame(const char *title, const char *right);
/* "PocketSNES", with "SNES" in the Super Famicom's colours. Returns its width. */
int  ui_logo(const struct Font *font, int x, int y);

/* A key as a little key cap. "up/down" and "left/right" are drawn as
 * arrows. Returns its width. */
int  ui_key(const char *key, int x, int y, bool lit);
int  ui_key_width(const char *key);

struct UiHint
{
    const char *key, *action;
};
void ui_footer(const struct UiHint *hints, int count);

enum
{
    ROW_SELECTED = 1,
    ROW_ARROWS = 2,    /* left/right change the value: arrows around it when selected */
    ROW_SUBMENU = 4,   /* opens another screen: a chevron at the right */
    ROW_FAINT = 8,
    ROW_TOGGLE = 16,   /* the value is on or off ("on" is on): drawn as a switch */
    ROW_MOVING = 32    /* being moved up or down the list: a gold frame and arrows */
};
/* A list row at y: an icon (ICON_NONE for none), a label, and a value at the
 * right (may be NULL). */
void ui_row(int y, int icon, uint16_t icon_color, const char *label, const char *value, int flags);
/* The same with a key cap where the icon goes, like "1" for a key to press. */
void ui_row_key(int y, const char *key, const char *label, const char *value, int flags);
/* Where a list of 'total' rows showing 'shown' from 'first' is. */
void ui_scrollbar(int first, int shown, int total, int y, int h);
/* The help panel: the text wrapped into two lines. */
void ui_help(const char *text);
/* Word-wraps text into lines of at most 'columns' characters; returns how
 * many lines (at most max_lines) were filled. */
int  ui_wrap(const char *text, int columns, char lines[][64], int max_lines);

/* A box in the middle of the screen (draw the screen behind it first): a
 * title, lines of text (wrapped), and key hints at the bottom. */
void ui_dialog(const char *title, const char *const *lines, int count,
               const struct UiHint *hints, int hint_count);

/* A message over the game, centred, its top at y. The rectangle it covers
 * goes in rect (x, y, w, h). */
void ui_toast(const char *text, int y, int rect[4]);

#endif
