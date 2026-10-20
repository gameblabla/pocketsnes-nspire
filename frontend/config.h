#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/* Everything a key can be bound to: the SNES buttons, then the hotkeys, in
 * the order the input menus list them. */
enum Action
{
    ACTION_UP,
    ACTION_DOWN,
    ACTION_LEFT,
    ACTION_RIGHT,
    ACTION_A,
    ACTION_B,
    ACTION_X,
    ACTION_Y,
    ACTION_L,
    ACTION_R,
    ACTION_START,
    ACTION_SELECT,
    ACTION_MENU,
    ACTION_QUIT,
    ACTION_FAST_FORWARD,
    ACTION_SAVE_STATE,
    ACTION_LOAD_STATE,
    ACTION_LOAD_NEWEST,
    ACTION_NEXT_SLOT,
    ACTION_PREVIOUS_SLOT,
    ACTION_SHOW_FPS,
    ACTION_RESTART,
    ACTION_BRIGHTER,
    ACTION_DIMMER,
    NUM_ACTIONS
};

#define FIRST_HOTKEY ACTION_MENU

/* Each action has KEY_SLOTS bindings, so two keys can do the same thing, and
 * one key can be in several actions' bindings (one key pressing A and B). A
 * binding is a key, or a combination: 'key' while 'with' is held (hold
 * 'with', then press 'key'). 0 is no key. */
#define KEY_SLOTS 2

struct Binding
{
    uint8_t key;
    uint8_t with;
};

enum FrameskipType
{
    FRAMESKIP_AUTO,
    FRAMESKIP_MANUAL,
    FRAMESKIP_OFF,
    NUM_FRAMESKIP_TYPES
};

#define MAX_FRAMESKIP_VALUE 9

/* CPU speeds (cfg.cpu_speed): the calculator's own, the highest one the
 * overclock test passed, then raised ones in steps of 12 MHz. */
enum CpuSpeed
{
    CPU_SPEED_NORMAL,
    CPU_SPEED_TESTED,
    CPU_SPEED_408,
    CPU_SPEED_420,
    CPU_SPEED_432,
    CPU_SPEED_444,
    CPU_SPEED_456,
    CPU_SPEED_468,
    CPU_SPEED_480,
    CPU_SPEED_492,
    CPU_SPEED_504,
    NUM_CPU_SPEEDS
};

/* The SNES CPU's emulation (cfg.cpu_core): the ARM assembler core from
 * psnes (pocketsnes/arm) or the C core. Games with an SA-1 always run on the
 * C core, and so does the PC build. */
enum CpuCore
{
    CPU_CORE_ARM,
    CPU_CORE_C,
    NUM_CPU_CORES
};

/* The picture's size (cfg.screen_size): the SNES's own pixels, or
 * stretched to the whole screen (scale.h), like a TV showed it; "crop"
 * leaves out the 8 lines at the top and the bottom first, which a TV's edge
 * hid and some games (Yoshi's Island) leave black. */
enum ScreenSize
{
    SCREEN_SIZE_ORIGINAL,
    SCREEN_SIZE_FULL,
    SCREEN_SIZE_FULL_CROP,
    NUM_SCREEN_SIZES
};

/* The multiplier of 12 MHz for a raised speed, 0 for the normal one (and for
 * "highest tested" before the overclock test has passed a speed). */
int config_cpu_multiplier(uint32_t cpu_speed);

/* The highest speed in MHz the overclock test passed on this calculator, 0
 * if none. Kept in the settings for all games; set it with
 * config_set_tested_mhz, which saves. */
uint32_t config_tested_mhz(void);
void config_set_tested_mhz(uint32_t mhz);

struct Config
{
    /* Settings: for all games, or a game's own (config_set_game_settings). */
    uint32_t frameskip_type;
    uint32_t frameskip_value;
    uint32_t show_fps;          /* 0 off, 1 on, 2 detailed */
    uint32_t auto_increment;    /* saving goes to a new slot */
    uint32_t save_on_exit;      /* leaving a game saves a state first */
    uint32_t auto_slot;         /* ... into the auto slot (states.h), not 001-999 */
    uint32_t quit_new_save;     /* ... into a new slot; 0: over the newest state */
    uint32_t load_on_start;     /* starting a game loads its newest state */
    uint32_t sram_autosave;
    uint32_t compress_saves;    /* new states and in-game saves are compressed */
    uint32_t state_pictures;    /* states keep a small picture, shown when picking one to load */
    uint32_t screen_dma;        /* game frames by DMA (faster, can tear) */
    uint32_t screen_size;       /* enum ScreenSize */
    uint32_t smooth;            /* stretched: repeated pixels blended (scale.h) */
    uint32_t cpu_speed;         /* enum CpuSpeed */
    uint32_t cpu_core;          /* enum CpuCore */
    uint32_t brightness;        /* 0 the calculator's own, 1-10 (brightness.h) */
    /* Keys: for all games, or a game's own (config_set_game_keys). */
    struct Binding keys[NUM_ACTIONS][KEY_SLOTS];
    /* For all games only. */
    char rom_dir[512];
};

/* What the program uses right now: the settings for all games, with the
 * open game's own settings or keys in their place when it has them. */
extern struct Config cfg;
extern const char *const action_names[NUM_ACTIONS];

void config_default_keys(void);
/* Every setting and key for all games back to the default (the game list's
 * Settings); the ROM folder and the overclock test's result stay, and so do
 * games' own settings. Saves it. */
void config_reset_all(void);

/* Loads the settings for all games from the program file, or else
 * pocketsnes.cfg.tns in zPocketSNES_saves (missing settings get their
 * defaults). */
void config_load(void);
/* There was no settings file to load: PocketSNES runs for the first time. */
int  config_first_start(void);
/* Writes the settings for all games to pocketsnes.cfg.tns, and the open
 * game's own settings and keys to its own file, "<ROM name>.cfg.tns" in
 * zPocketSNES_saves (removed when it has neither). */
void config_save(void);

/* Uses a game's own settings and keys where it has them (the menu and the
 * game); config_close_game() goes back to the ones for all games. */
void config_open_game(const char *rom_path);
void config_close_game(void);
int  config_game_is_open(void);

/* Whether the open game has its own settings / keys. Turning one on starts
 * from the current values; turning it off goes back to the ones for all
 * games (the game's own are forgotten when the config is saved). */
int  config_game_has_settings(void);
void config_set_game_settings(int own);
int  config_game_has_keys(void);
void config_set_game_keys(int own);

/* Puts the CPU speed back to normal for all games, and for the game at
 * rom_path if it has its own settings (NULL: none), and saves. */
void config_reset_cpu_speed(const char *rom_path);

#endif
