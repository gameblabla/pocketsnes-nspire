#include <stdio.h>
#include <string.h>

#include "brightness.h"
#include "config.h"
#include "draw.h"
#include "emu.h"
#include "gui.h"
#include "bindings.h"
#include "menu.h"
#include "overclock.h"
#include "platform.h"
#include "savemanager.h"
#include "states.h"
#include "ui.h"
#include "version.h"

/* The key screens: an action's name, then its two keys as key caps. */
#define BINDING_KEY_X(column) (140 + (column) * 86)

enum OptionType
{
    OPTION_ACTION,
    OPTION_SUBMENU,    /* no submenu: back to the menu this one was opened from */
    OPTION_CHOICE,     /* value indexes 'choices' */
    OPTION_NUMBER,     /* value is 0..max */
    OPTION_BINDING,    /* the two key slots of action 'action_id' */
    OPTION_LOAD_SLOT,  /* left/right pick the slot, select loads it */
    OPTION_SAVE_SLOT   /* left/right pick the slot, select saves */
};

/* What an action asks the menu to do next. */
enum Step
{
    STEP_STAY,
    STEP_RESUME,
    STEP_NEW_GAME,
    STEP_EXIT,
    STEP_BACK_TO_LIST,
    STEP_START,
    STEP_START_WITHOUT_STATE,
    STEP_MOVE,
    STEP_SORT
};

struct Menu;

struct Option
{
    enum OptionType type;
    const char *label;
    const char *help;              /* at most two lines of the help panel (98 characters) */
    int line;                      /* rows are listed in order; a gap in these numbers is a gap on screen */
    uint32_t *value;
    const char *const *choices;
    int max;                       /* last choice / largest number */
    enum Step (*action)(void);
    struct Menu *submenu;
    void (*changed)(void);         /* called after a choice or number changes */
    int action_id;                 /* OPTION_BINDING */
    int icon;
    const char *shown = nullptr;   /* OPTION_ACTION: text shown at the right, or NULL */
};

struct Menu
{
    struct Option *options;
    int count;
    const char *title;             /* NULL: a main menu, titled with the game */
};

static char status[64];
static uint32_t status_until;
static int key_column;            /* the key slot picked on the key screens */
static int waiting_for_keys;      /* a key slot is being set */
static int in_game;               /* the in-game menu, not the game list's */
static const char *list_rom;      /* the game list's menu: the game, or NULL for all games */

static void set_status(const char *text)
{
    snprintf(status, sizeof(status), "%s", text);
    status_until = platform_ticks() + platform_tick_hz() * 3;
}

/* 'format' has a %s for the slot ("slot 012", "the auto slot"; with a
 * capital when 'cap' is set). */
static void set_slot_status(const char *format, int slot, bool cap = false)
{
    char name[32], text[64];
    if (cap)
        states_slot_name_cap(slot, name, sizeof(name));
    else
        states_slot_name(slot, name, sizeof(name));
    snprintf(text, sizeof(text), format, name);
    set_status(text);
}

/* ---- Actions ---- */

static enum Step load_state(void)
{
    int slot = emu_slot();
    int result = emu_load_state(slot);
    if (result == 1)
    {
        char name[32], text[48];
        snprintf(text, sizeof(text), "Loaded %s", states_slot_name(slot, name, sizeof(name)));
        emu_show_message(text);
        return STEP_RESUME;
    }
    if (result == 0)
        set_slot_status("%s is empty.", slot, true);
    else
        set_slot_status("Couldn't load %s.", slot);
    return STEP_STAY;
}

/* The picture of the state shown at Load state (draw_state_picture). */
static int picture_slot = -1;   /* the slot in picture_pixels, -1 for none read */
static int picture_found;
static uint16_t picture_pixels[STATE_PICTURE_W * STATE_PICTURE_H];

static enum Step save_state(void)
{
    picture_slot = -1;   /* it may go over the slot whose picture is shown */
    int slot = emu_save_target();
    if (!slot)
        set_status("All 999 slots are used.");
    else if (emu_save_state(slot))
        set_slot_status("Saved to %s.", slot);
    else
        set_slot_status("Couldn't save %s. Out of space?", slot);
    return STEP_STAY;
}

/* Puts one group of actions back to the default keys. */
static void reset_actions(int first, int last)
{
    struct Binding kept[NUM_ACTIONS][KEY_SLOTS];
    memcpy(kept, cfg.keys, sizeof(kept));
    config_default_keys();
    for (int i = 0; i < NUM_ACTIONS; i++)
        if (i < first || i > last)
            memcpy(cfg.keys[i], kept[i], sizeof(kept[i]));
}

static enum Step reset_buttons(void)
{
    reset_actions(0, FIRST_HOTKEY - 1);
    set_status("SNES buttons are back to the default keys.");
    return STEP_STAY;
}

static enum Step reset_hotkeys(void)
{
    reset_actions(FIRST_HOTKEY, NUM_ACTIONS - 1);
    set_status("Hotkeys are back to the defaults.");
    return STEP_STAY;
}

static void game_name(const char *path, char *name, size_t size);

/* A game's saves: the one running, or the one the game list's menu is on. */
static enum Step open_save_manager(void)
{
    char title[64];
    const char *rom = in_game ? emu_rom_path() : list_rom;
    if (!rom)
    {
        gui_message("The save manager is for one game: use it", "from the game's menu in the game list, or in a game.");
        return STEP_STAY;
    }
    if (!in_game)
        states_open_game(rom);
    game_name(rom, title, sizeof(title));
    save_manager_run(title);
    picture_slot = -1;   /* the shown state may be gone */
    return STEP_STAY;
}

static enum Step reset_everything(void)
{
    if (gui_confirm("Put every setting and key back", "to the defaults, for all games?"))
    {
        config_reset_all();
        set_status("Every setting and key is back to the default.");
    }
    return STEP_STAY;
}

static enum Step run_speed_test(void)
{
    emu_speed_test(0);
    return STEP_STAY;
}

static void update_speed_names(void);

static enum Step run_overclock_test(void)
{
    overclock_test_run(in_game);
    update_speed_names();
    return STEP_STAY;
}

static enum Step load_new_game(void) { return STEP_NEW_GAME; }
static enum Step return_to_game(void) { return STEP_RESUME; }
static enum Step exit_app(void) { return STEP_EXIT; }
static enum Step back_to_list(void) { return STEP_BACK_TO_LIST; }
static enum Step start_game(void) { return STEP_START; }
static enum Step start_without_state(void) { return STEP_START_WITHOUT_STATE; }
static enum Step move_in_list(void) { return STEP_MOVE; }
static enum Step sort_list(void) { return STEP_SORT; }

static enum Step restart_game(void)
{
    emu_reset_game();
    emu_show_message("Game restarted");
    return STEP_RESUME;
}

/* Whether the game's settings and keys are its own (config.h). */
static uint32_t own_settings, own_keys;
static void own_settings_changed(void) { config_set_game_settings((int) own_settings); }
static void own_keys_changed(void) { config_set_game_keys((int) own_keys); }

/* ---- Menus ---- */

static const char *const frameskip_types[] = { "automatic", "manual", "off" };
static const char *const off_on[] = { "off", "on" };
static const char *const fps_modes[] = { "off", "on", "detailed" };
static const char *const sram_write_modes[] = { "on exit", "automatic" };
static const char *const scopes[] = { "all games", "this game" };
static const char *const screen_outputs[] = { "no tearing", "DMA" };
static const char *const screen_sizes[] = { "original", "full screen", "full, crop edges" };
static_assert(sizeof(screen_sizes) / sizeof(screen_sizes[0]) == NUM_SCREEN_SIZES, "a name for each screen size");
/* "normal" shows the calculator's own clock (288 MHz while USB is plugged
 * in), "highest tested" the overclock test's result. */
static char normal_cpu_speed[24] = "normal";
static char tested_cpu_speed[32] = "highest tested";
static char tested_result[24] = "no result";
static const char *const cpu_speeds[] =
{
    normal_cpu_speed, tested_cpu_speed, "408 MHz", "420 MHz", "432 MHz", "444 MHz", "456 MHz",
    "468 MHz", "480 MHz", "492 MHz", "504 MHz"
};
static_assert(sizeof(cpu_speeds) / sizeof(cpu_speeds[0]) == NUM_CPU_SPEEDS, "a name for each CPU speed");

static const char *const cpu_cores[] = { "ARM (fast)", "C" };
static_assert(sizeof(cpu_cores) / sizeof(cpu_cores[0]) == NUM_CPU_CORES, "a name for each CPU core");

static const char *const brightness_levels[] =
{
    "calculator's", "1 (dimmest)", "2", "3", "4", "5", "6", "7", "8", "9", "10 (brightest)"
};
static_assert(sizeof(brightness_levels) / sizeof(brightness_levels[0]) == BRIGHTNESS_LEVELS + 1,
              "a name for each brightness");

static void update_speed_names(void)
{
    if (platform_cpu_normal_mhz())
        snprintf(normal_cpu_speed, sizeof(normal_cpu_speed), "normal (%u MHz)",
                 (unsigned) platform_cpu_normal_mhz());
    uint32_t tested = config_tested_mhz();
    if (tested)
    {
        snprintf(tested_cpu_speed, sizeof(tested_cpu_speed), "highest tested (%u MHz)", (unsigned) tested);
        snprintf(tested_result, sizeof(tested_result), "%u MHz", (unsigned) tested);
    }
    else
    {
        snprintf(tested_cpu_speed, sizeof(tested_cpu_speed), "highest tested (none)");
        snprintf(tested_result, sizeof(tested_result), "no result");
    }
}

#define CHOICE(label, help, line, value, choices, max, icon) \
    { OPTION_CHOICE, label, help, line, value, choices, max, NULL, NULL, NULL, 0, icon }
#define SUBMENU(label, help, line, submenu, icon) \
    { OPTION_SUBMENU, label, help, line, NULL, NULL, 0, NULL, submenu, NULL, 0, icon }
#define ACTION(label, help, line, action, icon) \
    { OPTION_ACTION, label, help, line, NULL, NULL, 0, action, NULL, NULL, 0, icon }
#define BACK_OPTION(line) \
    { OPTION_SUBMENU, "Back", "Back to the menu before.", line, NULL, NULL, 0, NULL, NULL, NULL, 0, ICON_BACK }

#define GRAPHICS_OPTIONS \
    CHOICE("Frameskip", \
           "Automatic (best) skips drawing only when the game falls behind. Off draws every frame.", \
           0, &cfg.frameskip_type, frameskip_types, NUM_FRAMESKIP_TYPES - 1, ICON_NONE), \
    { OPTION_NUMBER, "Frames to skip", \
      "Automatic: the most frames skipped in a row. Manual: frames skipped per frame drawn.", \
      1, &cfg.frameskip_value, NULL, MAX_FRAMESKIP_VALUE, NULL, NULL, NULL, 0, ICON_NONE }, \
    CHOICE("FPS counter", \
           "Frames drawn per second: 60/60 is full speed. Detailed adds timings (also logged).", \
           2, &cfg.show_fps, fps_modes, 2, ICON_NONE), \
    CHOICE("Screen size", \
           "Full screen stretches the picture like a TV did. Crop edges also cuts the 8 lines TVs hid.", \
           3, &cfg.screen_size, screen_sizes, NUM_SCREEN_SIZES - 1, ICON_NONE), \
    CHOICE("Smooth stretching", \
           "On: full screen blends the pixels it doubles, so all look the same size. Off: sharp.", \
           4, &cfg.smooth, off_on, 1, ICON_NONE), \
    CHOICE("Screen output", \
           "No tearing (best): every frame shows whole. DMA: a little faster, can tear (original size).", \
           5, &cfg.screen_dma, screen_outputs, 1, ICON_NONE), \
    CHOICE("Brightness", \
           "The screen while PocketSNES runs (CX II); yours comes back after. Keys: + and -.", \
           6, &cfg.brightness, brightness_levels, BRIGHTNESS_LEVELS, ICON_NONE), \
    CHOICE("CPU speed", \
           "Overclocks the CX II in games. Highest tested is what the overclock test passed.", \
           7, &cfg.cpu_speed, cpu_speeds, NUM_CPU_SPEEDS - 1, ICON_NONE), \
    { OPTION_ACTION, "Overclock test", \
      "Plays the open game at 408-504 MHz until it goes wrong: about 3 minutes.", \
      8, NULL, NULL, 0, run_overclock_test, NULL, NULL, 0, ICON_SPEED, tested_result }, \
    CHOICE("CPU core", \
           "ARM: the SNES CPU in ARM assembler (faster). C: the old core, if a game acts up.", \
           9, &cfg.cpu_core, cpu_cores, NUM_CPU_CORES - 1, ICON_NONE)

static struct Option graphics_options[] =
{
    GRAPHICS_OPTIONS,
    ACTION("Run speed test",
           "Plays from here for about 40 seconds in six ways, both CPU cores, and shows the frame rates.",
           10, run_speed_test, ICON_STAR),
    BACK_OPTION(11),
};

/* The same from the game list, which has no game running to test. */
static struct Option list_graphics_options[] =
{
    GRAPHICS_OPTIONS,
    BACK_OPTION(10),
};

static struct Option savestate_options[] =
{
    CHOICE("Auto-increment slot",
           "On: every save goes to a new slot, so nothing is overwritten. Off: the selected slot.",
           0, &cfg.auto_increment, off_on, 1, ICON_NONE),
    CHOICE("Save when leaving",
           "On: quitting, or loading another game, saves a state first to carry on from.",
           1, &cfg.save_on_exit, off_on, 1, ICON_NONE),
    CHOICE("Auto save slot",
           "On: that save goes to its own slot, Auto (before 001), and slots 001-999 are left alone.",
           2, &cfg.auto_slot, off_on, 1, ICON_NONE),
    CHOICE("New save on quit",
           "On: that save goes to a new slot. Off: it saves over your newest state. (Not with Auto.)",
           3, &cfg.quit_new_save, off_on, 1, ICON_NONE),
    CHOICE("Load newest on start",
           "On: games start from their newest state. The game list's menu can skip it once.",
           4, &cfg.load_on_start, off_on, 1, ICON_NONE),
    CHOICE("Write in-game saves",
           "When the game's own save goes to the calculator: soon after it saves, or on exit.",
           5, &cfg.sram_autosave, sram_write_modes, 1, ICON_NONE),
    CHOICE("Compress saves",
           "On: new states and in-game saves take less space. Off: in-game saves copy straight to a PC.",
           6, &cfg.compress_saves, off_on, 1, ICON_NONE),
    CHOICE("State pictures",
           "On: each new state keeps a small picture of the game, shown when you pick a slot to load.",
           7, &cfg.state_pictures, off_on, 1, ICON_NONE),
    ACTION("Save manager", "This game's saves and their sizes: compress, uncompress or delete them.",
           8, open_save_manager, ICON_DISK),
    BACK_OPTION(10),
};

#define BINDING_HELP "Left/right: key 1 or 2. Enter: set it (hold one key, press another: a combo). Del: clear."

#define BINDING_OPTION(action, line) \
    { OPTION_BINDING, NULL, BINDING_HELP, line, NULL, NULL, 0, NULL, NULL, NULL, action, ICON_NONE }

static struct Option button_options[] =
{
    BINDING_OPTION(ACTION_UP, 0),
    BINDING_OPTION(ACTION_DOWN, 1),
    BINDING_OPTION(ACTION_LEFT, 2),
    BINDING_OPTION(ACTION_RIGHT, 3),
    BINDING_OPTION(ACTION_A, 4),
    BINDING_OPTION(ACTION_B, 5),
    BINDING_OPTION(ACTION_X, 6),
    BINDING_OPTION(ACTION_Y, 7),
    BINDING_OPTION(ACTION_L, 8),
    BINDING_OPTION(ACTION_R, 9),
    BINDING_OPTION(ACTION_START, 10),
    BINDING_OPTION(ACTION_SELECT, 11),
    ACTION("Reset to defaults", "Puts the default keys back. The arrows always work as the d-pad.",
           13, reset_buttons, ICON_RESTART),
    BACK_OPTION(14),
};

static struct Option hotkey_options[] =
{
    BINDING_OPTION(ACTION_MENU, 0),
    BINDING_OPTION(ACTION_QUIT, 1),
    BINDING_OPTION(ACTION_FAST_FORWARD, 2),
    BINDING_OPTION(ACTION_SAVE_STATE, 3),
    BINDING_OPTION(ACTION_LOAD_STATE, 4),
    BINDING_OPTION(ACTION_LOAD_NEWEST, 5),
    BINDING_OPTION(ACTION_NEXT_SLOT, 6),
    BINDING_OPTION(ACTION_PREVIOUS_SLOT, 7),
    BINDING_OPTION(ACTION_SHOW_FPS, 8),
    BINDING_OPTION(ACTION_RESTART, 9),
    BINDING_OPTION(ACTION_BRIGHTER, 10),
    BINDING_OPTION(ACTION_DIMMER, 11),
    ACTION("Reset to defaults", "esc menu, Q quit, F fast, S save, L load, N newest, ( ) slots, R restart, + - brightness.",
           13, reset_hotkeys, ICON_RESTART),
    BACK_OPTION(14),
};

static struct Option about_options[] =
{
    BACK_OPTION(0),
};

#define MENU(name, options, title) \
    static struct Menu name = { options, sizeof(options) / sizeof(options[0]), title }

MENU(graphics_menu, graphics_options, "Graphics & speed");
MENU(list_graphics_menu, list_graphics_options, "Graphics & speed");
MENU(savestate_menu, savestate_options, "Saving");
MENU(button_menu, button_options, "SNES buttons");
MENU(hotkey_menu, hotkey_options, "Hotkeys");
MENU(about_menu, about_options, "Controls & about");

#define SCOPE_OPTIONS(line) \
    { OPTION_CHOICE, "Settings for", \
      "This game: graphics, speed and saving changes made here apply to this game only.", \
      line, &own_settings, scopes, 1, NULL, NULL, own_settings_changed, 0, ICON_SLIDERS }, \
    { OPTION_CHOICE, "Keys for", \
      "This game: button and hotkey changes made here apply to this game only.", \
      line + 1, &own_keys, scopes, 1, NULL, NULL, own_keys_changed, 0, ICON_KEY }

#define SETTINGS_SUBMENUS(line, graphics) \
    SUBMENU("Graphics & speed", "Frameskip, FPS counter, screen size and output, brightness, CPU speed, core.", \
            line, graphics, ICON_SPEED), \
    SUBMENU("Saving", "Auto-increment, saving when you leave (and where), loading on start, in-game saves.", \
            line + 1, &savestate_menu, ICON_DISK), \
    SUBMENU("SNES buttons", "Which calculator keys press the SNES buttons, two keys each.", \
            line + 2, &button_menu, ICON_PAD), \
    SUBMENU("Hotkeys", "Keys for the menu, quitting, fast forward, saving, loading and more.", \
            line + 3, &hotkey_menu, ICON_KEYBOARD)

static struct Option main_options[] =
{
    ACTION("Resume game", "Close the menu and keep playing.", 0, return_to_game, ICON_PLAY),
    { OPTION_SAVE_SLOT, "Save state",
      "Saves the game right here. With auto-increment on, every save gets a new slot.",
      1, NULL, NULL, 0, save_state, NULL, NULL, 0, ICON_SAVE },
    { OPTION_LOAD_SLOT, "Load state",
      "Loads the selected slot. Left/right pick the slot (hold them to go faster).",
      2, NULL, NULL, 0, load_state, NULL, NULL, 0, ICON_LOAD },
    SETTINGS_SUBMENUS(4, &graphics_menu),
    SCOPE_OPTIONS(8),
    ACTION("Restart game", "Resets the SNES, like its reset button. Unsaved progress is lost.",
           11, restart_game, ICON_RESTART),
    ACTION("Load another game", "Back to the game list. Saves a state first if that's switched on.",
           12, load_new_game, ICON_CART),
    SUBMENU("Controls & about", "All your keys on one page, and the credits.", 13, &about_menu, ICON_INFO),
    ACTION("Exit PocketSNES", "Back to the calculator. Saves a state first if that's switched on.",
           14, exit_app, ICON_POWER),
};

/* The game list's menu on a game. */
static struct Option list_game_options[] =
{
    ACTION("Play", "Starts the game, from its newest state if Load newest on start is on.",
           0, start_game, ICON_PLAY),
    ACTION("Play without a state", "Starts from the game's own save only, e.g. when a state is broken.",
           1, start_without_state, ICON_CART),
    ACTION("Move in the list", "Up/down move the game, enter puts it there (tab in the list does this too).",
           2, move_in_list, ICON_MOVE),
    ACTION("Sort the list A to Z", "Puts the games in this folder back in alphabetical order.",
           3, sort_list, ICON_SORT),
    SETTINGS_SUBMENUS(5, &list_graphics_menu),
    SCOPE_OPTIONS(9),
    ACTION("Save manager", "This game's saves and their sizes: compress, uncompress or delete them.",
           11, open_save_manager, ICON_DISK),
    ACTION("Back to the game list", "Close this menu.", 12, back_to_list, ICON_BACK),
};

/* ... and on a folder: the settings for all games. */
static struct Option list_options[] =
{
    SETTINGS_SUBMENUS(0, &list_graphics_menu),
    SUBMENU("Controls & about", "All your keys on one page, and the credits.", 4, &about_menu, ICON_INFO),
    ACTION("Reset all settings", "Every setting and key back to the default, for all games.", 5,
           reset_everything, ICON_RESTART),
    ACTION("Back to the game list", "Close this menu.", 6, back_to_list, ICON_BACK),
};

MENU(main_menu, main_options, NULL);
MENU(list_game_menu, list_game_options, NULL);
MENU(list_menu, list_options, "Settings");

/* ---- Drawing ---- */

static const char *file_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* A ROM's name without ".tns" and its own extension: "Super Mario World". */
static void game_name(const char *path, char *name, size_t size)
{
    snprintf(name, size, "%s", file_name(path));
    for (int i = 0; i < 2; i++)
    {
        char *dot = strrchr(name, '.');
        if (dot && dot != name && strlen(dot) <= 4)
            *dot = 0;
    }
}

/* The value shown at the right of a row, or "" for none. */
static void option_value(const struct Option *option, char *text, size_t size)
{
    text[0] = 0;
    switch (option->type)
    {
    case OPTION_CHOICE:
        snprintf(text, size, "%s", option->choices[*option->value]);
        break;
    case OPTION_NUMBER:
        snprintf(text, size, "%d", (int) *option->value);
        break;
    case OPTION_LOAD_SLOT:
        if (emu_slot() == AUTO_SLOT)
            snprintf(text, size, "auto slot  %s", states_exists(AUTO_SLOT) ? "saved" : "empty");
        else
            snprintf(text, size, "slot %03d  %s", emu_slot(), states_exists(emu_slot()) ? "saved" : "empty");
        break;
    case OPTION_ACTION:
        if (option->shown)
            snprintf(text, size, "%s", option->shown);
        break;
    case OPTION_SAVE_SLOT:
        if (!cfg.auto_increment && emu_slot() == AUTO_SLOT)
            snprintf(text, size, "auto slot");
        else if (!cfg.auto_increment)
            snprintf(text, size, "slot %03d", emu_slot());
        else if (emu_save_target())
            snprintf(text, size, "new slot %03d", emu_save_target());
        else
            snprintf(text, size, "all slots used");
        break;
    default:
        break;
    }
}

static int option_flags(const struct Option *option, int selected)
{
    int flags = selected ? ROW_SELECTED : 0;
    if (option->type == OPTION_CHOICE && option->choices == off_on)
        flags |= ROW_TOGGLE;
    else if (option->type == OPTION_CHOICE || option->type == OPTION_NUMBER ||
             option->type == OPTION_LOAD_SLOT || (option->type == OPTION_SAVE_SLOT && !cfg.auto_increment))
        flags |= ROW_ARROWS;
    if (option->type == OPTION_SUBMENU && option->submenu)
        flags |= ROW_SUBMENU;
    return flags;
}

/* An action's keys as key caps from x; the picked one lit when 'lit' >= 0. */
static void draw_binding_keys(int action, int y, int lit)
{
    for (int column = 0; column < KEY_SLOTS; column++)
    {
        char name[24];
        int x = BINDING_KEY_X(column);
        if (lit == column && waiting_for_keys)
        {
            int w = ui_key_width("press keys...");
            draw_round_rect(x, y + 1, w, 13, 3, COLOR_VALUE);
            draw_string(FONT_SMALL, "press keys...", x + 4, y + 2, COLOR_BG_BOTTOM);
            continue;
        }
        if (!cfg.keys[action][column].key)
        {
            draw_string(FONT_SMALL, "-", x + 4, y + 2, lit == column ? COLOR_TEXT : COLOR_TEXT_FAINT);
            if (lit == column)
                draw_round_frame(x, y + 1, 18, 13, 3, COLOR_SELECT_EDGE);
            continue;
        }
        ui_key(binding_name(cfg.keys[action][column], name, sizeof(name)), x, y + 1, lit == column);
    }
}

/* Where each row goes (pixels below the first), with a gap where the
 * options' line numbers skip one. Returns the height of them all. */
static int layout(const struct Menu *menu, int *pos)
{
    int y = 0;
    for (int i = 0; i < menu->count; i++)
    {
        if (i > 0 && menu->options[i].line > menu->options[i - 1].line + 1)
            y += 6;
        pos[i] = y;
        y += UI_ROW_H;
    }
    return y;
}

static void draw_controls_page(void);

/* Save state pictures (Saving > State pictures): the selected slot's
 * picture on a card at the right, under the Load state row (at row_bottom),
 * over the values of the rows below it. */
#define PICTURE_CARD_W (STATE_PICTURE_W + 6)
#define PICTURE_CARD_H (STATE_PICTURE_H + 6)

static void draw_state_picture(int slot, int row_bottom)
{
    int x = 309 - PICTURE_CARD_W, y = row_bottom + 2;
    if (y + PICTURE_CARD_H > UI_HELP_Y - 2)
        return;
    if (slot != picture_slot)
    {
        picture_found = states_read_picture(slot, picture_pixels);
        picture_slot = slot;
    }
    draw_round_rect(x, y, PICTURE_CARD_W, PICTURE_CARD_H, 4, COLOR_PANEL);
    draw_round_frame(x, y, PICTURE_CARD_W, PICTURE_CARD_H, 4, COLOR_PANEL_EDGE);
    if (picture_found)
        draw_pixels(x + 3, y + 3, STATE_PICTURE_W, STATE_PICTURE_H, picture_pixels);
    else
        draw_string_center(FONT_SMALL, states_exists(slot) ? "No picture" : "Empty slot", x + PICTURE_CARD_W / 2,
                           y + (PICTURE_CARD_H - TEXT_H) / 2, COLOR_TEXT_FAINT);
}

static void draw_menu(const struct Menu *menu, int selected, int *scroll)
{
    char title[64], right[64], value[48];
    const struct Option *current = &menu->options[selected];

    /* The header: the game, or the menu, and whose settings these are. */
    right[0] = 0;
    if (menu->title)
        snprintf(title, sizeof(title), "%s", menu->title);
    else if (in_game)
        game_name(emu_rom_path(), title, sizeof(title));
    else
        game_name(list_rom, title, sizeof(title));

    if (menu == &main_menu)
    {
        if (states_count() && states_newest() == AUTO_SLOT)
            snprintf(right, sizeof(right), "%d saved, newest auto", states_count());
        else if (states_count())
            snprintf(right, sizeof(right), "%d saved, newest %03d", states_count(), states_newest());
        else
            snprintf(right, sizeof(right), "no saved states yet");
    }
    else if (menu == &about_menu)
        snprintf(right, sizeof(right), "version %s", POCKETSNES_VERSION);
    else if (menu == &button_menu || menu == &hotkey_menu)
        snprintf(right, sizeof(right), "%s", config_game_is_open() && config_game_has_keys() ? "this game only" : "all games");
    else if (menu == &graphics_menu && emu_game_mhz())
        snprintf(right, sizeof(right), "%s, ran at %u MHz",
                 config_game_has_settings() ? "this game" : "all games", (unsigned) emu_game_mhz());
    else if (menu == &graphics_menu || menu == &list_graphics_menu || menu == &savestate_menu)
        snprintf(right, sizeof(right), "%s",
                 config_game_is_open() && config_game_has_settings() ? "this game only" : "all games");
    else if (menu == &list_menu)
        snprintf(right, sizeof(right), "all games");
    ui_frame(title, right[0] ? right : NULL);

    if (menu == &about_menu)
        draw_controls_page();
    else
    {
        int pos[32];
        int height = layout(menu, pos);
        /* The key screens have their "Key 1" / "Key 2" heading above the rows. */
        int top = (menu == &button_menu || menu == &hotkey_menu) ? 8 : 0;
        int view = UI_HELP_Y - 4 - UI_LIST_Y - top;
        if (pos[selected] < *scroll)
            *scroll = pos[selected];
        if (pos[selected] + UI_ROW_H > *scroll + view)
            *scroll = pos[selected] + UI_ROW_H - view;
        if (height <= view)
            *scroll = 0;

        if (menu == &button_menu || menu == &hotkey_menu)
        {
            draw_string(FONT_SMALL, "Key 1", BINDING_KEY_X(0) + 2, UI_LIST_Y - 3, COLOR_TEXT_FAINT);
            draw_string(FONT_SMALL, "Key 2", BINDING_KEY_X(1) + 2, UI_LIST_Y - 3, COLOR_TEXT_FAINT);
        }
        for (int i = 0; i < menu->count; i++)
        {
            const struct Option *option = &menu->options[i];
            int y = UI_LIST_Y + top + pos[i] - *scroll;
            if (y < UI_LIST_Y + top - 2 || y + UI_ROW_H > UI_HELP_Y - 2)
                continue;
            if (option->type == OPTION_BINDING)
            {
                ui_row(y, ICON_NONE, 0, action_names[option->action_id], NULL, i == selected ? ROW_SELECTED : 0);
                draw_binding_keys(option->action_id, y, i == selected ? key_column : -1);
                continue;
            }
            option_value(option, value, sizeof(value));
            ui_row(y, option->icon, option->icon == ICON_POWER ? COLOR_SFC_RED : COLOR_TEXT_DIM,
                   option->label, value, option_flags(option, i == selected));
        }
        ui_scrollbar(*scroll, view, height, UI_LIST_Y + top, view);
        if (in_game && current->type == OPTION_LOAD_SLOT && cfg.state_pictures)
            draw_state_picture(emu_slot(), UI_LIST_Y + top + pos[selected] - *scroll + UI_ROW_H);
    }

    /* The help panel, or what just happened. */
    if (status[0] && (int32_t) (status_until - platform_ticks()) > 0)
    {
        draw_round_rect(5, UI_HELP_Y, 310, 34, 5, COLOR_PANEL);
        draw_round_frame(5, UI_HELP_Y, 310, 34, 5, COLOR_VALUE);
        draw_string_fit(FONT_SMALL, status, 12, UI_HELP_Y + 11, 296, COLOR_VALUE);
    }
    else if (menu != &about_menu)
        ui_help(current->help);
    else
        ui_help("Menus: arrows or 8 5 4 6, enter, esc. Game list: number keys start games, tab moves one.");

    /* The keys that do something here. */
    struct UiHint hints[5];
    int n = 0;
    const char *back = menu->title == NULL || menu == &list_menu ? (in_game ? "Resume" : "Close") : "Back";
    if (menu == &about_menu)
        hints[n++] = (struct UiHint) { "esc", "Back" };
    else if (current->type == OPTION_BINDING)
    {
        hints[n++] = (struct UiHint) { "up/down", "Move" };
        hints[n++] = (struct UiHint) { "left/right", "Key 1/2" };
        hints[n++] = (struct UiHint) { "enter", "Set" };
        hints[n++] = (struct UiHint) { "del", "Clear" };
        hints[n++] = (struct UiHint) { "esc", "Back" };
    }
    else
    {
        hints[n++] = (struct UiHint) { "up/down", "Move" };
        if (option_flags(current, 1) & (ROW_ARROWS | ROW_TOGGLE))
            hints[n++] = (struct UiHint) { "left/right", "Change" };
        else
            hints[n++] = (struct UiHint) { "enter", "Select" };
        hints[n++] = (struct UiHint) { "esc", back };
    }
    ui_footer(hints, n);
}

/* ---- The controls page (Controls & about, and the welcome screen) ---- */

/* An SNES button: the face buttons in the Super Famicom's colours. */
static int draw_button_badge(int action, int x, int y)
{
    static const struct { int action; const char *text; uint16_t color; } badges[] =
    {
        { ACTION_A, "A", COLOR_SFC_RED }, { ACTION_B, "B", COLOR_SFC_YELLOW },
        { ACTION_X, "X", COLOR_SFC_BLUE }, { ACTION_Y, "Y", COLOR_SFC_GREEN },
    };
    for (size_t i = 0; i < sizeof(badges) / sizeof(badges[0]); i++)
        if (badges[i].action == action)
        {
            draw_round_rect(x, y, 13, 13, 6, badges[i].color);
            draw_string(FONT_SMALL, badges[i].text, x + 4, y + 1, COLOR_WHITE);
            return 13;
        }
    const char *name = action_names[action];
    int w = text_width(FONT_SMALL, name) + 8;
    draw_round_rect(x, y, w, 13, 6, COLOR_PANEL_EDGE);
    draw_string(FONT_SMALL, name, x + 4, y + 1, COLOR_TEXT);
    return w;
}

/* Width of an action's keys as key caps ("-" for none). */
static int action_keys_width(int action)
{
    char name[24];
    int w = 0, any = 0;
    for (int slot = 0; slot < KEY_SLOTS; slot++)
        if (cfg.keys[action][slot].key)
        {
            w += (any ? text_width(FONT_SMALL, "or") + 5 : 0) +
                 ui_key_width(binding_name(cfg.keys[action][slot], name, sizeof(name)));
            any = 1;
        }
    return any ? w : TEXT_W + 2;
}

static void draw_action_keys(int action, int x, int y)
{
    char name[24];
    int any = 0;
    for (int slot = 0; slot < KEY_SLOTS; slot++)
        if (cfg.keys[action][slot].key)
        {
            if (any)
                x = draw_string(FONT_SMALL, "or", x + 2, y + 1, COLOR_TEXT_FAINT) + 3;
            x += ui_key(binding_name(cfg.keys[action][slot], name, sizeof(name)), x, y, false);
            any = 1;
        }
    if (!any)
        draw_string(FONT_SMALL, "-", x + 2, y + 1, COLOR_TEXT_FAINT);
}

/* Items laid out left to right, on to the next line when they don't fit. */
struct Flow
{
    int x, y;
};

static void flow_place(struct Flow *flow, int width, int *x, int *y)
{
    if (flow->x > 12 && flow->x + width > 308)
    {
        flow->x = 12;
        flow->y += 16;
    }
    *x = flow->x;
    *y = flow->y;
    flow->x += width + 10;
}

static int badge_width(int action)
{
    return action == ACTION_A || action == ACTION_B || action == ACTION_X || action == ACTION_Y
           ? 13 : text_width(FONT_SMALL, action_names[action]) + 8;
}

/* A hotkey's name on the key pages, shortened where the full one is long. */
static const char *short_action_name(int action)
{
    if (action == ACTION_BRIGHTER)
        return "Brighter";
    if (action == ACTION_DIMMER)
        return "Dimmer";
    return action_names[action];
}

/* The keys as they are set now, from y down; returns the y below them. */
static int draw_controls(int y)
{
    char keys[4][24];
    struct Flow flow;
    int x, cy;

    draw_string(FONT_SMALL, "SNES BUTTONS", 12, y, COLOR_TEXT_FAINT);
    y += 14;
    /* The d-pad: the arrows always work, then its own keys. */
    draw_round_rect(12, y, 13, 13, 3, COLOR_PANEL_EDGE);
    draw_rect(17, y + 2, 3, 9, COLOR_TEXT);
    draw_rect(14, y + 5, 9, 3, COLOR_TEXT);
    x = draw_string(FONT_SMALL, "arrows", 31, y + 1, COLOR_TEXT_DIM) + 6;
    int dpad = 1;
    for (int i = 0; i < 4; i++)
    {
        struct Binding b = cfg.keys[ACTION_UP + i][0];
        dpad &= b.key && !b.with;
        binding_name(b, keys[i], sizeof(keys[i]));
    }
    if (dpad)
    {
        x = draw_string(FONT_SMALL, "or", x, y + 1, COLOR_TEXT_FAINT) + 5;
        for (int i = 0; i < 4; i++)
            x += ui_key(keys[i], x, y, false) + 2;
        draw_string(FONT_SMALL, "(up down left right)", x + 4, y + 1, COLOR_TEXT_FAINT);
    }

    flow.x = 12;
    flow.y = y + 17;
    for (int action = ACTION_A; action <= ACTION_SELECT; action++)
    {
        if (action == ACTION_L)
        {
            flow.x = 12;   /* the shoulder buttons and Start/Select on their own line */
            flow.y += 16;
        }
        int w = badge_width(action);
        flow_place(&flow, w + 4 + action_keys_width(action), &x, &cy);
        draw_button_badge(action, x, cy);
        draw_action_keys(action, x + w + 4, cy);
    }

    y = flow.y + 21;
    draw_string(FONT_SMALL, "HOTKEYS", 12, y, COLOR_TEXT_FAINT);
    flow.x = 12;
    flow.y = y + 14;
    for (int i = FIRST_HOTKEY; i < NUM_ACTIONS; i++)
    {
        if (!cfg.keys[i][0].key && !cfg.keys[i][1].key)
            continue;
        int w = text_width(FONT_SMALL, short_action_name(i));
        flow_place(&flow, w + 4 + action_keys_width(i), &x, &cy);
        draw_string(FONT_SMALL, short_action_name(i), x, cy + 1, COLOR_TEXT_DIM);
        draw_action_keys(i, x + w + 4, cy);
    }
    return flow.y + 16;
}

static void draw_controls_page(void)
{
    int y = draw_controls(UI_LIST_Y + 1) + 6;
    if (y <= UI_HELP_Y - 30)
    {
        draw_string(FONT_SMALL, "Snes9x 1.43 by the Snes9x team. PocketSNES by", 12, UI_HELP_Y - 28, COLOR_TEXT_FAINT);
        draw_string(FONT_SMALL, "Nebuleon, TI-Nspire port by gameblabla.", 12, UI_HELP_Y - 16, COLOR_TEXT_FAINT);
    }
}

static void draw_welcome(const void *data)
{
    static const char *const tips[] =
    {
        "Copy ROMs over named like game.sfc.tns.",
        "Quitting saves your place; you carry on later.",
        "Settings: esc in a game, menu in the game list.",
    };
    (void) data;

    draw_gradient(0, 0, SCREEN_W, SCREEN_H, COLOR_BG_TOP, COLOR_BG_BOTTOM);
    int w = 10 * 12;
    ui_logo(FONT_LARGE, (SCREEN_W - w) / 2, 4);
    draw_string_center(FONT_SMALL, "Super Nintendo games on your TI-Nspire", SCREEN_W / 2, 29, COLOR_TEXT_DIM);
    for (int i = 0; i < 4; i++)
        draw_rect(100 + i * 30, 43, 30, 2, i == 0 ? COLOR_SFC_RED : i == 1 ? COLOR_SFC_YELLOW
                                                 : i == 2 ? COLOR_SFC_GREEN : COLOR_SFC_BLUE);
    int y = draw_controls(52) + 4;
    for (size_t i = 0; i < sizeof(tips) / sizeof(tips[0]) && y + 12 < UI_FOOTER_Y; i++, y += 13)
    {
        draw_round_rect(12, y + 4, 4, 4, 2, COLOR_SELECT_EDGE);
        draw_string(FONT_SMALL, tips[i], 20, y, COLOR_TEXT);
    }
    static const struct UiHint start[] = { { "any key", "Start" } };
    ui_footer(start, 1);
}

void menu_show_welcome(void)
{
    gui_show_until_key(draw_welcome, NULL);
}

/* ---- Input ---- */

static int bindable(int key)
{
    return key && key_name(key) && !key_is_reserved(key);
}

/* Sets the picked key slot of the selected line to the next key pressed, or
 * to a combination: a key held while another is pressed. */
static void choose_binding(const struct Menu *menu, int selected, int *scroll)
{
    const struct Option *option = &menu->options[selected];
    int held = 0;
    struct Binding chosen = { 0, 0 };

    waiting_for_keys = 1;
    draw_menu(menu, selected, scroll);
    gui_present();
    gui_wait_release();

    while (!chosen.key)
    {
        platform_poll_keys();
        if (platform_quit_requested())
            break;
        if (!held)
        {
            int key = platform_first_key_down();
            if (bindable(key))
                held = key;
        }
        else if (!platform_key_down(held))
            chosen.key = (uint8_t) held;   /* let go alone: just this key */
        else
            for (int key = 1; key <= NUM_KEYS; key++)
                if (key != held && bindable(key) && platform_key_down(key))
                {
                    chosen.key = (uint8_t) key;
                    chosen.with = (uint8_t) held;
                    break;
                }
        draw_menu(menu, selected, scroll);
        gui_present();
    }
    waiting_for_keys = 0;
    if (chosen.key)
        cfg.keys[option->action_id][key_column] = chosen;
    gui_wait_release();
}

static void change_value(const struct Option *option, int delta)
{
    if (option->type == OPTION_CHOICE || option->type == OPTION_NUMBER)
    {
        int count = option->max + 1;
        *option->value = (uint32_t) (((int) *option->value + delta % count + count) % count);
        if (option->changed)
            option->changed();
    }
    else if (option->type == OPTION_BINDING)
        key_column = delta > 0 ? 1 : 0;
    else if (option->type == OPTION_LOAD_SLOT || option->type == OPTION_SAVE_SLOT)
    {
        int step = gui_repeat_count() > 20 ? 10 : 1;
        emu_set_slot(emu_step_slot(emu_slot(), delta * step));
    }
}

/* The entry of 'top' that opens 'menu', to land on when coming back. */
static int entry_of(const struct Menu *top, const struct Menu *menu)
{
    for (int i = 0; i < top->count; i++)
        if (top->options[i].submenu == menu)
            return i;
    return 0;
}

static enum Step run(struct Menu *top)
{
    struct Menu *menu = top;
    int selected = 0, scroll = 0;
    enum Step step = STEP_STAY;

    status[0] = 0;
    key_column = 0;
    waiting_for_keys = 0;
    update_speed_names();
    own_settings = (uint32_t) config_game_has_settings();
    own_keys = (uint32_t) config_game_has_keys();

    gui_wait_release();
    while (step == STEP_STAY)
    {
        draw_menu(menu, selected, &scroll);
        gui_present();

        struct Option *option = &menu->options[selected];
        switch (gui_input())
        {
        case GUI_UP:
            selected = (selected + menu->count - 1) % menu->count;
            break;
        case GUI_DOWN:
            selected = (selected + 1) % menu->count;
            break;
        case GUI_LEFT:
            change_value(option, -1);
            break;
        case GUI_RIGHT:
            change_value(option, 1);
            break;
        case GUI_CLEAR:
            if (option->type == OPTION_BINDING)
                cfg.keys[option->action_id][key_column].key = cfg.keys[option->action_id][key_column].with = 0;
            break;
        case GUI_SELECT:
            if (option->type == OPTION_SUBMENU && option->submenu)
            {
                menu = option->submenu;
                selected = scroll = 0;
            }
            else if (option->type == OPTION_SUBMENU)
            {
                selected = entry_of(top, menu);
                menu = top;
                scroll = 0;
            }
            else if (option->type == OPTION_BINDING)
                choose_binding(menu, selected, &scroll);
            else if (option->type == OPTION_CHOICE)
                change_value(option, 1);
            else if (option->action)
                step = option->action();
            break;
        case GUI_BACK:
        case GUI_MENU:
            if (menu == top)
                step = in_game ? STEP_RESUME : STEP_BACK_TO_LIST;
            else
            {
                selected = entry_of(top, menu);
                menu = top;
                scroll = 0;
            }
            break;
        case GUI_QUIT:
            step = STEP_EXIT;
            break;
        default:
            break;
        }
    }

    config_save();
    return step;
}

enum MenuResult menu_run(void)
{
    in_game = 1;
    list_rom = NULL;
    picture_slot = -1;
    enum Step step = run(&main_menu);
    return step == STEP_NEW_GAME ? MENU_LOAD_NEW_GAME : step == STEP_EXIT ? MENU_EXIT : MENU_RESUME;
}

enum ListMenuResult menu_run_list(const char *rom_path)
{
    in_game = 0;
    list_rom = rom_path;
    enum Step step = run(rom_path ? &list_game_menu : &list_menu);
    switch (step)
    {
    case STEP_START:
        return LIST_MENU_START;
    case STEP_START_WITHOUT_STATE:
        return LIST_MENU_START_WITHOUT_STATE;
    case STEP_MOVE:
        return LIST_MENU_MOVE;
    case STEP_SORT:
        return LIST_MENU_SORT;
    case STEP_EXIT:
        return LIST_MENU_QUIT;
    default:
        return LIST_MENU_BACK;
    }
}
