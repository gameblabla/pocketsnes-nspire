#include "brightness.h"
#include "config.h"
#include "draw.h"
#include "gui.h"
#include "bindings.h"
#include "platform.h"
#include "ui.h"

static enum GuiAction held_direction;
static uint32_t repeat_at;
static int repeats;
static uint32_t frame_end;
static bool digits_move = true;

static bool action_pressed(int action)
{
    for (int slot = 0; slot < KEY_SLOTS; slot++)
        if (binding_pressed(cfg.keys[action][slot]))
            return true;
    return false;
}

enum GuiAction gui_input(void)
{
    platform_poll_keys();
    if (platform_quit_requested())
        return GUI_QUIT;

    /* The brightness keys work in the menus and the game list too. */
    bool brighter = action_pressed(ACTION_BRIGHTER), dimmer = action_pressed(ACTION_DIMMER);
    if (brighter != dimmer)
        brightness_step(brighter ? 1 : -1);

    if (platform_key_pressed(KEY_ENTER) || platform_key_pressed(KEY_CLICK) ||
        platform_key_pressed(KEY_RETURN))
        return GUI_SELECT;
    if (platform_key_pressed(KEY_MENU))
        return GUI_MENU;
    if (platform_key_pressed(KEY_ESC))
        return GUI_BACK;
    if (action_pressed(ACTION_MENU))
        return GUI_BACK;
    if (platform_key_pressed(KEY_DEL))
        return GUI_CLEAR;

    enum GuiAction direction = GUI_NONE;
    if (platform_key_down(KEY_UP) || (digits_move && platform_key_down(KEY_8)))
        direction = GUI_UP;
    else if (platform_key_down(KEY_DOWN) || (digits_move && (platform_key_down(KEY_2) || platform_key_down(KEY_5))))
        direction = GUI_DOWN;
    else if (platform_key_down(KEY_LEFT) || (digits_move && platform_key_down(KEY_4)))
        direction = GUI_LEFT;
    else if (platform_key_down(KEY_RIGHT) || (digits_move && platform_key_down(KEY_6)))
        direction = GUI_RIGHT;

    uint32_t now = platform_ticks();
    uint32_t hz = platform_tick_hz();
    if (direction != held_direction)
    {
        held_direction = direction;
        repeat_at = now + hz * 2 / 5;
        repeats = 0;
        return direction;
    }
    if (direction != GUI_NONE && (int32_t) (now - repeat_at) >= 0)
    {
        repeat_at = now + hz / 15;
        repeats++;
        return direction;
    }
    return GUI_NONE;
}

int gui_repeat_count(void)
{
    return repeats;
}

void gui_digits_move(bool on)
{
    digits_move = on;
}

int gui_digit_pressed(void)
{
    static const int digit_keys[10] = { KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9 };
    for (int i = 0; i < 10; i++)
        if (platform_key_pressed(digit_keys[i]))
            return i;
    return -1;
}

void gui_present(void)
{
    brightness_update();
    platform_present();

    uint32_t now = platform_ticks();
    if ((int32_t) (frame_end - now) > (int32_t) platform_tick_hz() || (int32_t) (frame_end - now) < 0)
        frame_end = now;
    frame_end += platform_tick_hz() / 60;
    platform_wait_until(frame_end);
}

void gui_wait_release(void)
{
    do
    {
        platform_poll_keys();
        platform_wait_until(platform_ticks() + platform_tick_hz() / 100);
    } while (platform_any_key_down() && !platform_quit_requested());
    held_direction = GUI_NONE;
}

void gui_show_until_key(void (*draw)(const void *data), const void *data)
{
    draw(data);
    gui_present();
    gui_wait_release();
    do
    {
        platform_poll_keys();
        draw(data);
        gui_present();
    } while (!platform_any_key_down() && !platform_quit_requested());
    gui_wait_release();
}

struct MessageLines
{
    const char *line1, *line2;
};

static void draw_message(const void *data)
{
    const struct MessageLines *m = (const struct MessageLines *) data;
    static const struct UiHint ok[] = { { "any key", "OK" } };
    const char *lines[2] = { m->line1, m->line2 };
    ui_frame(NULL, NULL);
    ui_dialog(NULL, lines, 2, ok, 1);
}

void gui_message(const char *line1, const char *line2)
{
    struct MessageLines m = { line1, line2 };
    gui_show_until_key(draw_message, &m);
}
