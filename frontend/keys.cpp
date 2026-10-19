#include <stddef.h>
#include <stdio.h>

#include "config.h"
#include "bindings.h"
#include "platform.h"

/* Key names in lr-gpsp-nspire's order (nspire_key_strings), plus the arrows. */
static const char *const key_names[NUM_KEYS + 1] =
{
    "none",
    "return", "enter", NULL, "(-)", "space", "Z", "Y", "0", "?!", "home", NULL, NULL, NULL, NULL, NULL, NULL,
    "X", "W", "V", "3", "U", "T", "S", "1", "pi", "trig", "10^x", NULL, NULL, NULL, NULL, NULL,
    "R", "Q", "P", "6", "O", "N", "M", "4", "EE", "x^2", NULL, NULL, NULL, NULL, NULL, NULL,
    "L", "K", "J", "9", "I", "H", "G", "7", "/", "e^x", NULL, NULL, NULL, NULL, NULL, NULL,
    "F", "E", "D", NULL, "C", "B", "A", "=", "*", "^", NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, "var", "-", ")", ".", "(", "5", "cat", "frac", "del", "scratch", NULL, NULL, NULL, NULL, NULL,
    "flag", "click", "+", "doc", "2", "menu", "8", "esc", NULL, "tab", NULL, NULL, NULL, NULL, NULL, NULL,
    "up", "up+right", "right", "right+down", "down", "down+left", "left", "left+up", "shift", "ctrl", ",", NULL, NULL, NULL, NULL, NULL
};

const char *key_name(int key)
{
    if (key < 0 || key > NUM_KEYS)
        return NULL;
    return key_names[key];
}

int key_is_reserved(int key)
{
    return (key >= KEY_UP && key <= KEY_LEFTUP) || key == KEY_CLICK;
}

/* ---- Bindings (config.h) ---- */

int binding_held(struct Binding b)
{
    return b.key && platform_key_down(b.key) && (!b.with || platform_key_down(b.with));
}

int binding_pressed(struct Binding b)
{
    return binding_held(b) && (platform_key_pressed(b.key) || (b.with && platform_key_pressed(b.with)));
}

const char *binding_name(struct Binding b, char *text, size_t size)
{
    const char *key = key_name(b.key), *with = key_name(b.with);

    if (!b.key || !key)
        snprintf(text, size, "(none)");
    else if (b.with && with)
        snprintf(text, size, "%s+%s", with, key);
    else
        snprintf(text, size, "%s", key);
    return text;
}
