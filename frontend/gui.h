/* Input and helpers shared by the menu and the ROM browser. */
#ifndef GUI_H
#define GUI_H

#include <stdbool.h>
#include <stdint.h>

enum GuiAction
{
    GUI_NONE,
    GUI_UP,
    GUI_DOWN,
    GUI_LEFT,
    GUI_RIGHT,
    GUI_SELECT,
    GUI_BACK,
    GUI_MENU,    /* the calculator's menu key */
    GUI_CLEAR,   /* del */
    GUI_QUIT     /* PC build: the window was closed */
};

/* Polls the keys once. Up/down/left/right repeat while held. Navigation uses
 * fixed keys like lr-gpsp-nspire: arrows or 8/2/5/4/6, enter or click to
 * select, esc (or a key bound to Menu) to go back; the menu key and del are
 * reported for the screens that use them. */
enum GuiAction gui_input(void);
/* How many times the held direction has auto-repeated so far. */
int gui_repeat_count(void);

/* Whether 8/2/5/4/6 move like the arrows in gui_input() (they do unless
 * switched off: the game list uses the number keys to start games). */
void gui_digits_move(bool on);
/* The number key (0-9) that went down at the last poll, or -1. */
int gui_digit_pressed(void);

/* Shows the back buffer and waits out the rest of a 1/60 s frame. */
void gui_present(void);

/* Waits until no key is held, so a key that closed a screen doesn't also act
 * on the next one. */
void gui_wait_release(void);

/* Shows what 'draw' draws until a key is pressed and let go. It draws again
 * for every frame: the screen buffers take turns (platform.h), so a screen
 * drawn only once would alternate with an old one. */
void gui_show_until_key(void (*draw)(const void *data), const void *data);

/* Draws a message and waits for a key. */
void gui_message(const char *line1, const char *line2);
/* Asks: enter is yes (1), esc no (0). */
int gui_confirm(const char *line1, const char *line2);

#endif
