#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "config.h"
#include "brightness.h"
#include "platform.h"

/* Saved as "name=value" lines so older and newer builds can share the file.
 * Version 2 added the second key slots, key combinations, save_on_exit and
 * load_on_start (which replace auto_resume) and per-game files. Version 3
 * numbers the CPU speeds differently (enum CpuSpeed) and adds
 * highest_tested_mhz. */
#define CONFIG_FILE "pocketsnes.cfg.tns"
#define CONFIG_VERSION 3

struct Config cfg;

/* The settings and keys for all games. cfg is a copy of this, with the open
 * game's own settings or keys in their place. */
static struct Config global;
static char game_file[800];   /* the open game's own file, or "" */
static int game_own_settings, game_own_keys;
static int settings_file_found;
static uint32_t tested_mhz;   /* the overclock test's result, for all games */

const char *const action_names[NUM_ACTIONS] =
{
    "D-pad up", "D-pad down", "D-pad left", "D-pad right",
    "A", "B", "X", "Y", "L", "R", "Start", "Select",
    "Menu", "Quit", "Fast forward", "Save state", "Load state",
    "Load newest", "Next slot", "Previous slot", "FPS counter", "Restart game",
    "Brightness up", "Brightness down"
};

/* Short names used in the config file. */
static const char *const action_ids[NUM_ACTIONS] =
{
    "up", "down", "left", "right", "a", "b", "x", "y", "l", "r", "start", "select",
    "menu", "quit", "fast_forward", "save_state", "load_state",
    "load_newest", "next_slot", "previous_slot", "show_fps", "restart",
    "brighter", "dimmer"
};

/* The PocketSNES button layout (README.md), plus the hotkeys. The newer
 * hotkeys start unbound, except brightness: + and (-) (- is Select). */
static const uint8_t default_keys[NUM_ACTIONS] =
{
    KEY_8, KEY_5, KEY_4, KEY_6,
    KEY_CTRL, KEY_SHIFT, KEY_VAR, KEY_DEL, KEY_TAB, KEY_MENU, KEY_ENTER, KEY_MINUS,
    KEY_ESC, KEY_Q, KEY_F, KEY_S, KEY_L,
    0, 0, 0, 0, 0,
    KEY_PLUS, KEY_NEGATIVE
};

static void config_path(char *path, size_t size)
{
    snprintf(path, size, "%s/%s", platform_exe_dir(), CONFIG_FILE);
}

static void default_keys_into(struct Config *c)
{
    memset(c->keys, 0, sizeof(c->keys));
    for (int i = 0; i < NUM_ACTIONS; i++)
        c->keys[i][0].key = default_keys[i];
}

void config_default_keys(void)
{
    default_keys_into(&cfg);
}

static void default_settings_into(struct Config *c)
{
    c->frameskip_type = FRAMESKIP_AUTO;
    c->frameskip_value = 5;
    c->show_fps = 0;
    c->auto_increment = 1;
    c->save_on_exit = 1;
    c->auto_slot = 0;
    c->quit_new_save = 1;
    c->load_on_start = 1;
    c->sram_autosave = 1;
    c->screen_dma = 0;
    c->cpu_speed = CPU_SPEED_NORMAL;
    c->cpu_core = CPU_CORE_ARM;
    c->brightness = 0;
}

static void copy_settings(struct Config *to, const struct Config *from)
{
    to->frameskip_type = from->frameskip_type;
    to->frameskip_value = from->frameskip_value;
    to->show_fps = from->show_fps;
    to->auto_increment = from->auto_increment;
    to->save_on_exit = from->save_on_exit;
    to->auto_slot = from->auto_slot;
    to->quit_new_save = from->quit_new_save;
    to->load_on_start = from->load_on_start;
    to->sram_autosave = from->sram_autosave;
    to->screen_dma = from->screen_dma;
    to->cpu_speed = from->cpu_speed;
    to->cpu_core = from->cpu_core;
    to->brightness = from->brightness;
}

static uint32_t clamp(long value, uint32_t max)
{
    return value < 0 ? 0 : value > (long) max ? max : (uint32_t) value;
}

static int bindable(long key)
{
    return key > 0 && key <= NUM_KEYS && key_name((int) key) && !key_is_reserved((int) key);
}

/* "K" or "W+K" (hold W, press K); 0 is no key. */
static struct Binding parse_binding(const char *value)
{
    struct Binding b = { 0, 0 };
    char *end;
    long first = strtol(value, &end, 10);

    if (*end == '+')
    {
        long second = strtol(end + 1, NULL, 10);
        if (bindable(first) && bindable(second) && first != second)
        {
            b.with = (uint8_t) first;
            b.key = (uint8_t) second;
        }
    }
    else if (bindable(first))
        b.key = (uint8_t) first;
    return b;
}

static void write_binding(FILE *f, const char *id, int slot, struct Binding b)
{
    fprintf(f, "key_%s%s=", id, slot ? "_2" : "");
    if (b.with && b.key)
        fprintf(f, "%u+%u\n", (unsigned) b.with, (unsigned) b.key);
    else
        fprintf(f, "%u\n", (unsigned) b.key);
}

/* What a file had in it, for working out older files and per-game ones. */
struct FileInfo
{
    int found, version, own_settings, own_keys;
    int auto_resume;   /* version 1's setting, -1 if absent */
    long cpu_speed;    /* as written, -1 if absent (its meaning depends on the version) */
    long tested_mhz;   /* -1 if absent */
    int quit_new_save; /* -1 if absent: then it follows auto_increment, as leaving did before */
    uint8_t key_lines[NUM_ACTIONS];   /* the file set the action's keys */
};

/* Version 1 and 2 files had normal, 432, 456 and 480 MHz. */
static uint32_t cpu_speed_from_file(long value, int version)
{
    static const uint32_t old_speeds[] = { CPU_SPEED_NORMAL, CPU_SPEED_432, CPU_SPEED_456, CPU_SPEED_480 };
    if (version < 3)
        return old_speeds[clamp(value, 3)];
    return clamp(value, NUM_CPU_SPEEDS - 1);
}

static int uses_key(struct Binding b, uint8_t key)
{
    return b.key && (b.key == key || b.with == key);
}

/* An action a file doesn't mention (one newer than the file) keeps its
 * default keys only where the file's actions don't use them already: e.g.
 * Brightness up's + when + is someone's Select. */
static void drop_clashing_defaults(struct Config *c, const struct FileInfo *info)
{
    for (int a = 0; a < NUM_ACTIONS; a++)
    {
        if (info->key_lines[a])
            continue;
        for (int slot = 0; slot < KEY_SLOTS; slot++)
        {
            uint8_t key = c->keys[a][slot].key;
            for (int other = 0; key && other < NUM_ACTIONS; other++)
                if (info->key_lines[other] &&
                    (uses_key(c->keys[other][0], key) || uses_key(c->keys[other][1], key)))
                {
                    c->keys[a][slot].key = c->keys[a][slot].with = 0;
                    key = 0;
                }
        }
    }
}

/* Reads "name=value" lines into c, over whatever it holds. */
static void read_file(const char *path, struct Config *c, struct FileInfo *info)
{
    char line[600];

    memset(info, 0, sizeof(*info));
    info->version = 1;
    info->auto_resume = -1;
    info->cpu_speed = info->tested_mhz = -1;
    info->quit_new_save = -1;
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    info->found = 1;

    while (fgets(line, sizeof(line), f))
    {
        char *value = strchr(line, '=');
        if (!value)
            continue;
        *value++ = 0;
        value[strcspn(value, "\r\n")] = 0;
        long number = strtol(value, NULL, 10);

        if (strcmp(line, "config_version") == 0)
            info->version = (int) number;
        else if (strcmp(line, "own_settings") == 0)
            info->own_settings = number != 0;
        else if (strcmp(line, "own_keys") == 0)
            info->own_keys = number != 0;
        else if (strcmp(line, "frameskip_type") == 0)
            c->frameskip_type = clamp(number, NUM_FRAMESKIP_TYPES - 1);
        else if (strcmp(line, "frameskip_value") == 0)
            c->frameskip_value = clamp(number, MAX_FRAMESKIP_VALUE);
        else if (strcmp(line, "show_fps") == 0)
            c->show_fps = clamp(number, 2);
        else if (strcmp(line, "auto_increment") == 0)
            c->auto_increment = clamp(number, 1);
        else if (strcmp(line, "save_on_exit") == 0)
            c->save_on_exit = clamp(number, 1);
        else if (strcmp(line, "load_on_start") == 0)
            c->load_on_start = clamp(number, 1);
        else if (strcmp(line, "auto_resume") == 0)
            info->auto_resume = (int) clamp(number, 1);
        else if (strcmp(line, "sram_autosave") == 0)
            c->sram_autosave = clamp(number, 1);
        else if (strcmp(line, "screen_dma") == 0)
            c->screen_dma = clamp(number, 1);
        else if (strcmp(line, "cpu_speed") == 0)
            info->cpu_speed = number;
        else if (strcmp(line, "cpu_core") == 0)
            c->cpu_core = clamp(number, NUM_CPU_CORES - 1);
        else if (strcmp(line, "brightness") == 0)
            c->brightness = clamp(number, BRIGHTNESS_LEVELS);
        else if (strcmp(line, "auto_slot") == 0)
            c->auto_slot = clamp(number, 1);
        else if (strcmp(line, "quit_new_save") == 0)
            info->quit_new_save = (int) clamp(number, 1);
        else if (strcmp(line, "highest_tested_mhz") == 0)
            info->tested_mhz = number;
        else if (strcmp(line, "rom_dir") == 0)
            snprintf(c->rom_dir, sizeof(c->rom_dir), "%s", value);
        else if (strncmp(line, "key_", 4) == 0)
        {
            const char *id = line + 4;
            size_t len = strlen(id);
            int slot = len > 2 && strcmp(id + len - 2, "_2") == 0;
            for (int i = 0; i < NUM_ACTIONS; i++)
                if (strlen(action_ids[i]) == len - (slot ? 2 : 0) &&
                    strncmp(id, action_ids[i], strlen(action_ids[i])) == 0)
                {
                    c->keys[i][slot] = parse_binding(value);
                    info->key_lines[i] = 1;
                }
        }
    }
    fclose(f);
    if (info->cpu_speed >= 0)
        c->cpu_speed = cpu_speed_from_file(info->cpu_speed, info->version);
    drop_clashing_defaults(c, info);
    c->quit_new_save = info->quit_new_save >= 0 ? (uint32_t) info->quit_new_save : c->auto_increment;
}

void config_load(void)
{
    char path[600];
    struct FileInfo info;

    memset(&global, 0, sizeof(global));
    default_settings_into(&global);
    default_keys_into(&global);
    snprintf(global.rom_dir, sizeof(global.rom_dir), "%s", platform_exe_dir());

    config_path(path, sizeof(path));
    read_file(path, &global, &info);
    settings_file_found = info.found;
    tested_mhz = info.tested_mhz > 0 && info.tested_mhz <= 756 ? (uint32_t) info.tested_mhz : 0;
    if (info.found && info.version < 2)
    {
        /* Version 1 always wrote auto_increment, so its old default (off)
         * can't be told from a choice: it takes the new default (on). Its
         * auto-resume did what save_on_exit and load_on_start do now. */
        global.auto_increment = 1;
        if (info.auto_resume == 1)
            global.save_on_exit = global.load_on_start = 1;
    }

    cfg = global;
    game_file[0] = 0;
    game_own_settings = game_own_keys = 0;
}

static void write_settings(FILE *f, const struct Config *c)
{
    fprintf(f, "frameskip_type=%u\n", (unsigned) c->frameskip_type);
    fprintf(f, "frameskip_value=%u\n", (unsigned) c->frameskip_value);
    fprintf(f, "show_fps=%u\n", (unsigned) c->show_fps);
    fprintf(f, "auto_increment=%u\n", (unsigned) c->auto_increment);
    fprintf(f, "save_on_exit=%u\n", (unsigned) c->save_on_exit);
    fprintf(f, "load_on_start=%u\n", (unsigned) c->load_on_start);
    fprintf(f, "sram_autosave=%u\n", (unsigned) c->sram_autosave);
    fprintf(f, "screen_dma=%u\n", (unsigned) c->screen_dma);
    fprintf(f, "cpu_speed=%u\n", (unsigned) c->cpu_speed);
    fprintf(f, "cpu_core=%u\n", (unsigned) c->cpu_core);
    fprintf(f, "brightness=%u\n", (unsigned) c->brightness);
    fprintf(f, "auto_slot=%u\n", (unsigned) c->auto_slot);
    fprintf(f, "quit_new_save=%u\n", (unsigned) c->quit_new_save);
}

static void write_keys(FILE *f, const struct Config *c)
{
    for (int i = 0; i < NUM_ACTIONS; i++)
        for (int slot = 0; slot < KEY_SLOTS; slot++)
            write_binding(f, action_ids[i], slot, c->keys[i][slot]);
}

void config_save(void)
{
    char path[600];

    /* What isn't the game's own belongs to all games. */
    if (!game_file[0] || !game_own_settings)
        copy_settings(&global, &cfg);
    if (!game_file[0] || !game_own_keys)
        memcpy(global.keys, cfg.keys, sizeof(global.keys));
    snprintf(global.rom_dir, sizeof(global.rom_dir), "%s", cfg.rom_dir);

    config_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (f)
    {
        fprintf(f, "config_version=%d\n", CONFIG_VERSION);
        write_settings(f, &global);
        fprintf(f, "highest_tested_mhz=%u\n", (unsigned) tested_mhz);
        fprintf(f, "rom_dir=%s\n", global.rom_dir);
        write_keys(f, &global);
        fclose(f);
    }

    if (!game_file[0])
        return;
    if (!game_own_settings && !game_own_keys)
    {
        remove(game_file);
        return;
    }
    /* The game's folder for saves may not exist yet. */
    char dir[800];
    snprintf(dir, sizeof(dir), "%s", game_file);
    char *slash = strrchr(dir, '/');
    if (slash)
    {
        *slash = 0;
        mkdir(dir, 0755);
    }
    f = fopen(game_file, "w");
    if (!f)
        return;
    fprintf(f, "config_version=%d\n", CONFIG_VERSION);
    if (game_own_settings)
    {
        fprintf(f, "own_settings=1\n");
        write_settings(f, &cfg);
    }
    if (game_own_keys)
    {
        fprintf(f, "own_keys=1\n");
        write_keys(f, &cfg);
    }
    fclose(f);
}

void config_open_game(const char *rom_path)
{
    /* "<ROM folder>/.pocketsnes/<ROM name without its last extension>.cfg.tns",
     * next to the game's saves (states.cpp). */
    const char *slash = strrchr(rom_path, '/');
    const char *file = slash ? slash + 1 : rom_path;
    char base[256];
    snprintf(base, sizeof(base), "%s", file);
    char *dot = strrchr(base, '.');
    if (dot && dot != base)
        *dot = 0;
    if (slash)
        snprintf(game_file, sizeof(game_file), "%.*s/.pocketsnes/%s.cfg.tns",
                 (int) (slash - rom_path), rom_path, base);
    else
        snprintf(game_file, sizeof(game_file), "./.pocketsnes/%s.cfg.tns", base);

    char rom_dir[512];
    snprintf(rom_dir, sizeof(rom_dir), "%s", cfg.rom_dir);
    cfg = global;
    snprintf(cfg.rom_dir, sizeof(cfg.rom_dir), "%s", rom_dir);

    /* Read the game's file over a copy, and take only the parts it owns. */
    struct Config own = global;
    struct FileInfo info;
    read_file(game_file, &own, &info);
    game_own_settings = info.own_settings;
    game_own_keys = info.own_keys;
    if (game_own_settings)
        copy_settings(&cfg, &own);
    if (game_own_keys)
        memcpy(cfg.keys, own.keys, sizeof(cfg.keys));
}

void config_close_game(void)
{
    char rom_dir[512];
    snprintf(rom_dir, sizeof(rom_dir), "%s", cfg.rom_dir);
    if (!game_own_settings)
        copy_settings(&global, &cfg);
    if (!game_own_keys)
        memcpy(global.keys, cfg.keys, sizeof(global.keys));
    cfg = global;
    snprintf(cfg.rom_dir, sizeof(cfg.rom_dir), "%s", rom_dir);
    game_file[0] = 0;
    game_own_settings = game_own_keys = 0;
}

int config_first_start(void)
{
    return !settings_file_found;
}

int config_game_is_open(void)
{
    return game_file[0] != 0;
}

int config_game_has_settings(void)
{
    return game_own_settings;
}

void config_set_game_settings(int own)
{
    if (!game_file[0] || own == game_own_settings)
        return;
    game_own_settings = own;
    if (!own)
        copy_settings(&cfg, &global);
}

int config_game_has_keys(void)
{
    return game_own_keys;
}

void config_set_game_keys(int own)
{
    if (!game_file[0] || own == game_own_keys)
        return;
    game_own_keys = own;
    if (!own)
        memcpy(cfg.keys, global.keys, sizeof(cfg.keys));
}

int config_cpu_multiplier(uint32_t cpu_speed)
{
    if (cpu_speed == CPU_SPEED_TESTED)
        return (int) (tested_mhz / 12);
    if (cpu_speed >= CPU_SPEED_408 && cpu_speed < NUM_CPU_SPEEDS)
        return 34 + (int) (cpu_speed - CPU_SPEED_408);
    return 0;
}

uint32_t config_tested_mhz(void)
{
    return tested_mhz;
}

void config_set_tested_mhz(uint32_t mhz)
{
    tested_mhz = mhz;
    config_save();
}

void config_reset_cpu_speed(const char *rom_path)
{
    /* For all games, and the open game's own settings if it has them... */
    global.cpu_speed = CPU_SPEED_NORMAL;
    cfg.cpu_speed = CPU_SPEED_NORMAL;
    config_save();

    /* ... or the given game's, when it isn't open. */
    if (rom_path && rom_path[0] && !game_file[0])
    {
        config_open_game(rom_path);
        if (game_own_settings)
        {
            cfg.cpu_speed = CPU_SPEED_NORMAL;
            config_save();
        }
        config_close_game();
    }
}
