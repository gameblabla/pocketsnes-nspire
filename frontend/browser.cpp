#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "browser.h"
#include "config.h"
#include "draw.h"
#include "gui.h"
#include "menu.h"
#include "paths.h"
#include "ui.h"

#define LIST_ROWS ((UI_FOOTER_Y - 4 - UI_LIST_Y) / UI_ROW_H)

struct Entry
{
    char name[256];
    int is_dir;
    int saves;    /* save states in the folder's PocketSNES_saves */
    int rank;     /* place in the folder's game order, INT_MAX if not in it */
};

static struct Entry *entries;
static int entry_count, entry_capacity;
static int has_parent;   /* entries[0] is ".." */
static int first_game;   /* folders come first, then the games from here */
static char current_dir[512];

static int ends_with(const char *text, const char *suffix)
{
    size_t text_len = strlen(text), suffix_len = strlen(suffix);
    return text_len >= suffix_len && strcasecmp(text + text_len - suffix_len, suffix) == 0;
}

/* ROMs are "*.tns" files; this hides PocketSNES's own files next to them. */
static int is_rom_name(const char *name)
{
    size_t len = strlen(name);

    if (ends_with(name, ".srm.tns") || ends_with(name, ".cfg.tns") || paths_is_pocketsnes_name(name))
        return 0;
    if (len > 10 && strncasecmp(name + len - 10, ".sv", 3) == 0 && ends_with(name, ".tns"))
        return 0;
    if (ends_with(name, ".tns"))
        return 1;
#ifndef _TINSPIRE
    /* The PC build also takes plain ROM files. */
    if (ends_with(name, ".sfc") || ends_with(name, ".smc") || ends_with(name, ".swc") ||
        ends_with(name, ".fig"))
        return 1;
#endif
    return 0;
}

/* Folders A to Z, then the games in the folder's order, then the games
 * that aren't in it, A to Z. */
static int compare_entries(const void *a, const void *b)
{
    const struct Entry *x = (const struct Entry *) a, *y = (const struct Entry *) b;
    if (x->is_dir != y->is_dir)
        return y->is_dir - x->is_dir;
    if (x->rank != y->rank)
        return x->rank < y->rank ? -1 : 1;
    return strcasecmp(x->name, y->name);
}

static void add_entry(const char *name, int is_dir)
{
    if (entry_count == entry_capacity)
    {
        int capacity = entry_capacity ? entry_capacity * 2 : 64;
        struct Entry *grown = (struct Entry *) realloc(entries, capacity * sizeof(*entries));
        if (!grown)
            return;
        entries = grown;
        entry_capacity = capacity;
    }
    snprintf(entries[entry_count].name, sizeof(entries[entry_count].name), "%s", name);
    entries[entry_count].is_dir = is_dir;
    entries[entry_count].saves = 0;
    entries[entry_count].rank = INT_MAX;
    entry_count++;
}

/* "<folder>/PocketSNES_saves/<name>". */
static void folder_file(char *path, size_t size, const char *name)
{
    paths_save_file(path, size, current_dir, name);
}

/* The order of the games in a folder, when it has been changed (Move): one
 * ROM file name per line in "<folder>/PocketSNES_saves/game_order.txt.tns". */
#define ORDER_FILE "game_order.txt.tns"

static void read_order(void)
{
    char path[800], line[300];
    folder_file(path, sizeof(path), ORDER_FILE);
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    int rank = 0;
    while (fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\r\n")] = 0;
        for (int i = has_parent; i < entry_count; i++)
            if (!entries[i].is_dir && entries[i].rank == INT_MAX && strcmp(entries[i].name, line) == 0)
            {
                entries[i].rank = rank++;
                break;
            }
    }
    fclose(f);
}

/* Writes the games' order as it is now. Returns 0 if it couldn't. */
static int write_order(void)
{
    char path[800];
    folder_file(path, sizeof(path), "");
    mkdir(path, 0755);
    folder_file(path, sizeof(path), ORDER_FILE);
    FILE *f = fopen(path, "w");
    if (!f)
        return 0;
    int ok = 1;
    for (int i = first_game; i < entry_count; i++)
        if (fprintf(f, "%s\n", entries[i].name) < 0)
            ok = 0;
    return fclose(f) == 0 && ok;
}

/* The entry of game number n (1 is the first game), or -1. */
static int nth_game(int n)
{
    return n >= 1 && first_game + n - 1 < entry_count ? first_game + n - 1 : -1;
}

/* How many save states each game has: "<ROM name without .tns>.svNNN.tns"
 * in the folder's PocketSNES_saves (states.cpp). */
static void count_saves(void)
{
    char path[800];
    folder_file(path, sizeof(path), "");
    DIR *dir = opendir(path);
    if (!dir)
        return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        const char *name = entry->d_name;
        size_t len = strlen(name);
        /* ".sv" three digits ".tns" */
        if (len < 11 || !ends_with(name, ".tns") || strncasecmp(name + len - 10, ".sv", 3) != 0)
            continue;
        size_t base = len - 10;
        for (int i = has_parent; i < entry_count; i++)
        {
            const char *rom = entries[i].name;
            if (!entries[i].is_dir && strlen(rom) == base + 4 && strncmp(rom, name, base) == 0 &&
                ends_with(rom, ".tns"))
            {
                entries[i].saves++;
                break;
            }
        }
    }
    closedir(dir);
}

static void read_dir(void)
{
    paths_move_old_files(current_dir, 0);
    entry_count = 0;
    has_parent = strcmp(current_dir, "/") != 0;
    if (has_parent)
        add_entry("..", 1);

    DIR *dir = opendir(current_dir);
    if (!dir)
        return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        char path[800];
        struct stat st;

        if (entry->d_name[0] == '.' || strcasecmp(entry->d_name, SAVE_FOLDER) == 0)
            continue;
        snprintf(path, sizeof(path), "%s/%s", strcmp(current_dir, "/") == 0 ? "" : current_dir,
                 entry->d_name);
        int is_dir = stat(path, &st) == 0 && S_ISDIR(st.st_mode);
        if (is_dir || is_rom_name(entry->d_name))
            add_entry(entry->d_name, is_dir);
    }
    closedir(dir);

    read_order();
    qsort(entries + has_parent, entry_count - has_parent, sizeof(*entries), compare_entries);
    first_game = has_parent;
    while (first_game < entry_count && entries[first_game].is_dir)
        first_game++;
    count_saves();
}

static int dir_exists(const char *path)
{
    DIR *dir = opendir(path);
    if (!dir)
        return 0;
    closedir(dir);
    return 1;
}

/* A ROM's name as shown: without ".tns" and the ROM's own extension. */
static void display_name(const char *file, char *name, size_t size)
{
    snprintf(name, size, "%s", file);
    for (int i = 0; i < 2; i++)
    {
        char *dot = strrchr(name, '.');
        if (dot && dot != name && strlen(dot) <= 4)
            *dot = 0;
    }
}

/* The game list. 'moving': the selected game is being moved up or down.
 * The first ten games have the number keys that start them (1-9, then 0). */
static void draw_browser(int selected, int top, int moving)
{
    char folder[600], name[256], saves[24];

    /* The folder, with the calculator's documents folder by its name, and
     * only its end when it's long. */
    if (strncmp(current_dir, "/documents", 10) == 0 && (current_dir[10] == '/' || !current_dir[10]))
        snprintf(folder, sizeof(folder), "My Documents%s", current_dir + 10);
    else
        snprintf(folder, sizeof(folder), "%s", current_dir);
    size_t len = strlen(folder);
    if (len > 26)
    {
        const char *tail = folder + len - 23;
        const char *slash = strchr(tail, '/');
        char shortened[32];
        snprintf(shortened, sizeof(shortened), "...%s", slash ? slash : tail);
        snprintf(folder, sizeof(folder), "%s", shortened);
    }
    ui_frame(NULL, folder);

    for (int row = 0; row < LIST_ROWS && top + row < entry_count; row++)
    {
        const struct Entry *entry = &entries[top + row];
        int y = UI_LIST_Y + row * UI_ROW_H;
        int flags = top + row == selected ? (moving ? ROW_MOVING : ROW_SELECTED) : 0;
        if (entry->is_dir && strcmp(entry->name, "..") == 0)
            ui_row(y, ICON_BACK, COLOR_TEXT_FAINT, "Up a folder", NULL, flags);
        else if (entry->is_dir)
            ui_row(y, ICON_FOLDER, COLOR_FOLDER, entry->name, NULL, flags | ROW_SUBMENU);
        else
        {
            display_name(entry->name, name, sizeof(name));
            saves[0] = 0;
            if (entry->saves)
                snprintf(saves, sizeof(saves), entry->saves == 1 ? "1 save" : "%d saves", entry->saves);
            int number = top + row - first_game + 1;
            if (number <= 10)
            {
                char key[4];
                snprintf(key, sizeof(key), "%d", number % 10);
                ui_row_key(y, key, name, saves, flags);
            }
            else
                ui_row(y, ICON_CART, COLOR_TEXT_DIM, name, saves, flags);
        }
    }
    ui_scrollbar(top, LIST_ROWS, entry_count, UI_LIST_Y, LIST_ROWS * UI_ROW_H);

    if (entry_count == has_parent)
    {
        static const char *const hint[] =
        {
            "Copy SNES ROMs to the calculator, named",
            "like game.sfc.tns: it only takes .tns",
            "files. Or open another folder.",
        };
        int y = 92;
        draw_round_rect(30, y, 260, 82, 7, COLOR_PANEL);
        draw_round_frame(30, y, 260, 82, 7, COLOR_PANEL_EDGE);
        draw_icon(ICON_CART, 44, y + 12, COLOR_TEXT_FAINT);
        draw_string(FONT_MEDIUM, "No games in this folder", 64, y + 10, COLOR_TEXT);
        for (int i = 0; i < 3; i++)
            draw_string(FONT_SMALL, hint[i], 44, y + 34 + i * 13, COLOR_TEXT_DIM);
    }

    struct UiHint hints[4];
    int n = 0;
    if (moving)
    {
        hints[n++] = (struct UiHint) { "up/down", "Move" };
        hints[n++] = (struct UiHint) { "enter", "Done" };
        hints[n++] = (struct UiHint) { "esc", "Cancel" };
    }
    else if (entry_count && entries[selected].is_dir)
    {
        hints[n++] = (struct UiHint) { "enter", "Open" };
        hints[n++] = (struct UiHint) { "menu", "Settings" };
        hints[n++] = (struct UiHint) { "left/right", "Page" };
        hints[n++] = (struct UiHint) { "esc", "Quit" };
    }
    else
    {
        if (entry_count)
        {
            hints[n++] = (struct UiHint) { "enter", "Play" };
            hints[n++] = (struct UiHint) { "tab", "Move" };
        }
        hints[n++] = (struct UiHint) { "menu", "Settings" };
        hints[n++] = (struct UiHint) { "esc", "Quit" };
    }
    ui_footer(hints, n);
}

/* Moves entries[from] to 'to', the ones between moving up or down one. */
static void move_entry(int from, int to)
{
    if (from == to)
        return;
    struct Entry moved = entries[from];
    if (from < to)
        memmove(&entries[from], &entries[from + 1], (size_t) (to - from) * sizeof(*entries));
    else
        memmove(&entries[to + 1], &entries[to], (size_t) (from - to) * sizeof(*entries));
    entries[to] = moved;
}

/* The full path of an entry in the current folder. */
static void entry_path(const struct Entry *entry, char *path, size_t size)
{
    snprintf(path, size, "%s/%s", strcmp(current_dir, "/") == 0 ? "" : current_dir, entry->name);
}

enum BrowserResult browser_run(char *path, size_t size)
{
    int selected = 0, top = 0;
    int moving = 0, move_from = 0;   /* the selected game is being moved, from move_from */
    enum BrowserResult chosen = BROWSER_QUIT;

    snprintf(current_dir, sizeof(current_dir), "%s",
             dir_exists(cfg.rom_dir) ? cfg.rom_dir : platform_exe_dir());
    read_dir();
    gui_wait_release();
    gui_digits_move(false);   /* number keys start games here */

    /* Coming back from a game, start on that game. */
    const char *last = strrchr(path, '/');
    if (last)
        for (int i = 0; i < entry_count; i++)
            if (strcmp(entries[i].name, last + 1) == 0)
                selected = i;

    for (;;)
    {
        if (selected >= entry_count)
            selected = entry_count ? entry_count - 1 : 0;
        if (selected < top)
            top = selected;
        if (selected >= top + LIST_ROWS)
            top = selected - LIST_ROWS + 1;

        draw_browser(selected, top, moving);
        gui_present();

        enum GuiAction action = gui_input();
        if (action == GUI_QUIT)
            break;

        /* Moving a game: the arrows (or 8/2/5/4/6) move it among the games,
         * enter or tab puts it there and saves the order, esc puts it back. */
        if (moving)
        {
            if (action == GUI_UP || action == GUI_DOWN || action == GUI_LEFT || action == GUI_RIGHT)
            {
                int step = action == GUI_UP ? -1 : action == GUI_DOWN ? 1
                         : action == GUI_LEFT ? -LIST_ROWS : LIST_ROWS;
                int target = selected + step;
                if (target < first_game)
                    target = first_game;
                if (target > entry_count - 1)
                    target = entry_count - 1;
                move_entry(selected, target);
                selected = target;
            }
            else if (action == GUI_SELECT || platform_key_pressed(KEY_TAB))
            {
                moving = 0;
                gui_digits_move(false);
                if (selected != move_from && !write_order())
                    gui_message("Couldn't save the order of the games.", "The calculator may be out of space.");
            }
            else if (action == GUI_BACK)
            {
                move_entry(selected, move_from);
                selected = move_from;
                moving = 0;
                gui_digits_move(false);
            }
            continue;
        }

        if (action == GUI_BACK)
            break;
        if (action == GUI_MENU)
        {
            /* The settings of the game under the cursor, or of all games. */
            char rom[800];
            int on_game = entry_count > 0 && !entries[selected].is_dir;
            if (on_game)
            {
                entry_path(&entries[selected], rom, sizeof(rom));
                config_open_game(rom);
            }
            gui_digits_move(true);
            enum ListMenuResult result = menu_run_list(on_game ? rom : NULL);
            gui_digits_move(false);
            if (on_game)
                config_close_game();
            if (result == LIST_MENU_QUIT)
                break;
            if (result == LIST_MENU_START || result == LIST_MENU_START_WITHOUT_STATE)
            {
                snprintf(path, size, "%s", rom);
                snprintf(cfg.rom_dir, sizeof(cfg.rom_dir), "%s", current_dir);
                chosen = result == LIST_MENU_START ? BROWSER_START : BROWSER_START_WITHOUT_STATE;
                break;
            }
            if (result == LIST_MENU_MOVE && on_game)
            {
                moving = 1;
                move_from = selected;
                gui_digits_move(true);
            }
            if (result == LIST_MENU_SORT)
            {
                char order[800], keep[256];
                snprintf(keep, sizeof(keep), "%s", entries[selected].name);
                folder_file(order, sizeof(order), ORDER_FILE);
                remove(order);
                read_dir();
                for (int i = 0; i < entry_count; i++)
                    if (strcmp(entries[i].name, keep) == 0)
                        selected = i;
            }
            gui_wait_release();
            continue;
        }
        if (entry_count == 0)
            continue;

        /* A number key starts that game: 1 the first, 0 the tenth. */
        int digit = gui_digit_pressed();
        if (digit >= 0)
        {
            int game = nth_game(digit ? digit : 10);
            if (game < 0)
                continue;
            selected = game;
            action = GUI_SELECT;
        }
        else if (platform_key_pressed(KEY_TAB))
        {
            if (!entries[selected].is_dir)
            {
                moving = 1;
                move_from = selected;
                gui_digits_move(true);
            }
            continue;
        }

        if (action == GUI_UP)
            selected = (selected + entry_count - 1) % entry_count;
        else if (action == GUI_DOWN)
            selected = (selected + 1) % entry_count;
        else if (action == GUI_LEFT)
            selected = selected > LIST_ROWS ? selected - LIST_ROWS : 0;
        else if (action == GUI_RIGHT)
            selected = selected + LIST_ROWS < entry_count ? selected + LIST_ROWS : entry_count - 1;
        else if (action == GUI_SELECT)
        {
            struct Entry *entry = &entries[selected];

            if (entry->is_dir && strcmp(entry->name, "..") == 0)
            {
                /* Go up and put the cursor on the folder we came out of. */
                char child[256];
                char *slash = strrchr(current_dir, '/');
                snprintf(child, sizeof(child), "%s", slash ? slash + 1 : "");
                if (slash == current_dir)
                    current_dir[1] = 0;
                else if (slash)
                    *slash = 0;
                read_dir();
                selected = top = 0;
                for (int i = 0; i < entry_count; i++)
                    if (strcmp(entries[i].name, child) == 0)
                        selected = i;
            }
            else if (entry->is_dir)
            {
                size_t len = strlen(current_dir);
                snprintf(current_dir + len, sizeof(current_dir) - len, "%s%s",
                         len && current_dir[len - 1] == '/' ? "" : "/", entry->name);
                read_dir();
                selected = top = 0;
            }
            else
            {
                entry_path(entry, path, size);
                snprintf(cfg.rom_dir, sizeof(cfg.rom_dir), "%s", current_dir);
                chosen = BROWSER_START;
                break;
            }
        }
    }

    gui_digits_move(true);
    free(entries);
    entries = NULL;
    entry_count = entry_capacity = 0;
    gui_wait_release();
    return chosen;
}
