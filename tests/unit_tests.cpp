/* Unit tests for the PC build: the tile drawers against the original
 * Snes9x 1.43 ones, the frame turning, the settings files and the key
 * bindings. Built and run by tests/run.sh (make unit-tests). Prints the
 * failures and returns 1 if there were any. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "snes9x.h"
#include "memmap.h"
#include "ppu.h"
#include "gfx.h"
#include "tile.h"

#include "bindings.h"
#include "brightness.h"
#include "config.h"
#include "selfstore.h"
#include "platform.h"
#include "rotate.h"

static int failures, checks;

#define CHECK(cond, ...) \
    do { \
        checks++; \
        if (!(cond)) \
        { \
            failures++; \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__); \
            printf("\n"); \
        } \
    } while (0)

static unsigned rng_state = 12345;

static unsigned rnd(unsigned n)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return (rng_state >> 8) % n;
}

/* ---- A stand-in for the calculator: keys and the program's folder ---- */

static uint32_t keys_down[4], keys_before[4];
static char exe_dir[512];        /* "<temp>/snes", like the calculator's My Documents/snes */
static char documents_dir[512];  /* "<temp>" */

static void set_key(int key, int down)
{
    if (down)
        keys_down[(key - 1) >> 5] |= 1u << ((key - 1) & 31);
    else
        keys_down[(key - 1) >> 5] &= ~(1u << ((key - 1) & 31));
}

/* The next "poll": what is down now was down before. */
static void next_poll(void)
{
    memcpy(keys_before, keys_down, sizeof(keys_down));
}

int platform_key_down(int key)
{
    if (key <= 0 || key > NUM_KEYS)
        return 0;
    return (keys_down[(key - 1) >> 5] >> ((key - 1) & 31)) & 1;
}

int platform_key_pressed(int key)
{
    if (key <= 0 || key > NUM_KEYS)
        return 0;
    return ((keys_down[(key - 1) >> 5] & ~keys_before[(key - 1) >> 5]) >> ((key - 1) & 31)) & 1;
}

const char *platform_exe_dir(void)
{
    return exe_dir;
}

const char *platform_documents_dir(void)
{
    return documents_dir;
}

static char program_file[600];   /* "" until test_selfstore */

const char *platform_program_path(void)
{
    return program_file[0] ? program_file : NULL;
}

int platform_os_backlight(void)
{
    return 112;
}

void platform_set_backlight(int)
{
}

/* ---- Tile drawing ---- */

/* The drawers under test (tile.cpp; gfx.cpp declares them the same way). */
void DrawTile16 (uint32 Tile, uint32 Offset, uint32 StartLine, uint32 LineCount);
void DrawClippedTile16 (uint32 Tile, uint32 Offset, uint32 StartPixel, uint32 Width,
                        uint32 StartLine, uint32 LineCount);

/* The original drawers (Snes9x 1.43's tile.cpp, WRITE_4PIXELS16 with the
 * RENDER_TILE and RENDER_CLIPPED_TILE macros of tile.h). */
static uint8 ConvertTile (uint8 *, uint32)
{
    return TRUE;   /* never called: the test's tiles are all cached */
}

static inline void REF_WRITE_4PIXELS16 (uint32 Offset, uint8 *Pixels, uint16 *ScreenColors)
{
    uint8  Pixel;
    uint16 *Screen = (uint16 *) GFX.S + (int32) Offset;
    uint8  *Depth = GFX.DB + (int32) Offset;

    for (uint8 N = 0; N < 4; N++)
    {
        if (GFX.Z1 > Depth [N] && (Pixel = Pixels[N]))
        {
            Screen [N] = ScreenColors [Pixel];
            Depth [N] = GFX.Z2;
        }
    }
}

static inline void REF_WRITE_4PIXELS16_FLIPPED (uint32 Offset, uint8 *Pixels, uint16 *ScreenColors)
{
    uint8  Pixel;
    uint16 *Screen = (uint16 *) GFX.S + (int32) Offset;
    uint8  *Depth = GFX.DB + (int32) Offset;

    for (uint8 N = 0; N < 4; N++)
    {
        if (GFX.Z1 > Depth [N] && (Pixel = Pixels[3 - N]))
        {
            Screen [N] = ScreenColors [Pixel];
            Depth [N] = GFX.Z2;
        }
    }
}

static void RefDrawTile16 (uint32 Tile, uint32 Offset, uint32 StartLine, uint32 LineCount)
{
    TILE_PREAMBLE
    register uint8 *bp;

    RENDER_TILE(REF_WRITE_4PIXELS16, REF_WRITE_4PIXELS16_FLIPPED, 4)
}

static void RefDrawClippedTile16 (uint32 Tile, uint32 Offset, uint32 StartPixel, uint32 Width,
                                  uint32 StartLine, uint32 LineCount)
{
    TILE_PREAMBLE
    register uint8 *bp;

    TILE_CLIP_PREAMBLE
    RENDER_CLIPPED_TILE(REF_WRITE_4PIXELS16, REF_WRITE_4PIXELS16_FLIPPED, 4)
}

#define TEST_PITCH 320
#define TEST_LINES 40
#define TILES      2048

static uint8 tile_cache[TILES * 64] __attribute__ ((aligned (8)));
static uint8 tile_cached[TILES];
static uint16 screen_new[TEST_PITCH * TEST_LINES], screen_ref[TEST_PITCH * TEST_LINES];
static uint8 depth_new[TEST_PITCH * TEST_LINES], depth_ref[TEST_PITCH * TEST_LINES];

static void setup_tiles(void)
{
    BG.TileShift = 5;
    BG.TileAddress = 0;
    BG.NameSelect = 0x2000;
    BG.Buffer = tile_cache;
    BG.Buffered = tile_cached;
    BG.PaletteShift = 4;
    BG.PaletteMask = 7;
    BG.StartPalette = 0;
    BG.DirectColourMode = FALSE;
    GFX.PPL = TEST_PITCH;

    for (int t = 0; t < TILES; t++)
    {
        /* Some tiles blank, some all opaque, most a mix with transparent
         * (0) pixels; some lines all transparent. */
        int kind = (int) rnd(10);
        tile_cached[t] = kind == 0 ? BLANK_TILE : TRUE;
        for (int i = 0; i < 64; i++)
        {
            uint8 pixel = (uint8) rnd(16);
            if (kind == 1)
                pixel |= 1;
            else if (kind >= 2 && (rnd(3) == 0 || ((i >> 3) == kind)))
                pixel = 0;
            tile_cache[t * 64 + i] = kind == 0 ? 0 : pixel;
        }
    }
    for (int i = 0; i < 256; i++)
        IPPU.ScreenColors[i] = (uint16) (rnd(0x10000) | 1);
}

static void fill_buffers(void)
{
    for (int i = 0; i < TEST_PITCH * TEST_LINES; i++)
    {
        screen_new[i] = screen_ref[i] = (uint16) rnd(0x10000);
        depth_new[i] = depth_ref[i] = (uint8) rnd(48);
    }
}

static int buffers_match(void)
{
    return memcmp(screen_new, screen_ref, sizeof(screen_new)) == 0 &&
           memcmp(depth_new, depth_ref, sizeof(depth_new)) == 0;
}

static void test_tile_drawing(void)
{
    setup_tiles();
    int mismatches = 0, clipped_mismatches = 0;

    for (int i = 0; i < 20000; i++)
    {
        fill_buffers();
        uint32 tile = rnd(0x400) | (rnd(8) << 10) | (rnd(4) << 14);   /* number, palette, flips */
        uint32 line = rnd(8), count = 1 + rnd(8 - line);
        uint32 offset = (2 + rnd(20)) * TEST_PITCH + 8 + rnd(TEST_PITCH - 24);
        GFX.Z1 = (uint8) rnd(48);
        GFX.Z2 = rnd(2) ? GFX.Z1 : (uint8) rnd(48);

        if (i % 2 == 0)
        {
            GFX.S = (uint8 *) screen_new;
            GFX.DB = depth_new;
            DrawTile16 (tile, offset, line * 8, count);
            GFX.S = (uint8 *) screen_ref;
            GFX.DB = depth_ref;
            RefDrawTile16 (tile, offset, line * 8, count);
            mismatches += !buffers_match();
        }
        else
        {
            uint32 start = rnd(8), width = 1 + rnd(8 - start);
            GFX.S = (uint8 *) screen_new;
            GFX.DB = depth_new;
            DrawClippedTile16 (tile, offset, start, width, line * 8, count);
            GFX.S = (uint8 *) screen_ref;
            GFX.DB = depth_ref;
            RefDrawClippedTile16 (tile, offset, start, width, line * 8, count);
            clipped_mismatches += !buffers_match();
        }
    }
    CHECK(mismatches == 0, "DrawTile16 differs from the original in %d of 10000 cases", mismatches);
    CHECK(clipped_mismatches == 0, "DrawClippedTile16 differs from the original in %d of 10000 cases",
          clipped_mismatches);
}

/* ---- Turning frames for the CX II LCD ---- */

static uint16_t frame[320 * 240];
static uint32_t fast_words[240 * 320 / 2], slow_words[240 * 320 / 2];

static void test_rotation(void)
{
    uint16_t *fast = (uint16_t *) fast_words, *slow = (uint16_t *) slow_words;

    for (int i = 0; i < 320 * 240; i++)
        frame[i] = (uint16_t) (i * 40503u >> 7);
    for (int flags = 0; flags < 4; flags++)
    {
        memset(fast_words, 0, sizeof(fast_words));
        memset(slow_words, 0xFF, sizeof(slow_words));
        rotate_frame(frame, fast, flags);
        rotate_frame_reference(frame, slow, flags);
        CHECK(memcmp(fast, slow, sizeof(fast_words)) == 0, "rotate_frame orientation %d differs", flags);
    }

    /* rotate_block: random rectangles, then the whole frame in one piece. */
    rotate_frame_reference(frame, slow, ROTATE_FLIP_COLUMNS);
    memset(fast_words, 0, sizeof(fast_words));
    for (int y = 0, h; y < 240; y += h)
    {
        h = 1 + (int) rnd(17);
        if (y + h > 240)
            h = 240 - y;
        for (int x = 0, w; x < 320; x += w)
        {
            w = 1 + (int) rnd(160);
            if (x + w > 320)
                w = 320 - x;
            rotate_block(frame + y * 320 + x, 320, x, y, w, h, fast);
        }
    }
    CHECK(memcmp(fast, slow, sizeof(fast_words)) == 0, "rotate_block in pieces differs");
    for (int i = 0; i < 300; i++)
    {
        int x = (int) rnd(320), y = (int) rnd(240);
        int w = 1 + (int) rnd(320 - x), h = 1 + (int) rnd(240 - y);
        rotate_block(frame + y * 320 + x, 320, x, y, w, h, fast);
    }
    CHECK(memcmp(fast, slow, sizeof(fast_words)) == 0, "rotate_block of random rectangles differs");
    memset(fast_words, 0, sizeof(fast_words));
    rotate_block(frame, 320, 0, 0, 320, 240, fast);
    CHECK(memcmp(fast, slow, sizeof(fast_words)) == 0, "rotate_block of the whole frame differs");
}

/* ---- Settings files ---- */

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (f)
    {
        fputs(text, f);
        fclose(f);
    }
}

/* Is there a line exactly like 'line' in the file? */
static int file_has_line(const char *path, const char *line)
{
    char buffer[600];
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    int found = 0;
    while (!found && fgets(buffer, sizeof(buffer), f))
    {
        buffer[strcspn(buffer, "\r\n")] = 0;
        found = strcmp(buffer, line) == 0;
    }
    fclose(f);
    return found;
}

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static void test_config(void)
{
    char global_path[600], rom[600], game_path[700];
    snprintf(global_path, sizeof(global_path), "%s/PocketSNES_saves/pocketsnes.cfg.tns", documents_dir);
    snprintf(rom, sizeof(rom), "%s/Game (USA).sfc.tns", exe_dir);
    snprintf(game_path, sizeof(game_path), "%s/PocketSNES_saves/Game (USA).sfc.cfg.tns", documents_dir);
    remove(global_path);

    /* Defaults with no file. */
    config_load();
    CHECK(cfg.auto_increment == 1 && cfg.save_on_exit == 1 && cfg.load_on_start == 1,
          "defaults: auto-increment, save when leaving and load on start are on");
    CHECK(cfg.cpu_speed == CPU_SPEED_NORMAL && cfg.screen_dma == 0, "defaults: normal CPU speed, no DMA");
    CHECK(cfg.keys[ACTION_A][0].key == KEY_SHIFT && !cfg.keys[ACTION_A][0].with && !cfg.keys[ACTION_A][1].key,
          "defaults: A is shift, with no second key");
    CHECK(cfg.keys[ACTION_LOAD_NEWEST][0].key == KEY_N && cfg.keys[ACTION_SHOW_FPS][0].key == 0,
          "defaults: Load newest on N, the FPS counter hotkey has no key");
    CHECK(cfg.frameskip_type == FRAMESKIP_OFF && cfg.show_fps == 1, "defaults: frameskip off, FPS counter on");
    CHECK(cfg.auto_slot == 0 && cfg.quit_new_save == 1 && cfg.brightness == 0,
          "defaults: no auto slot, a new save on quit, the calculator's own brightness");
    CHECK(cfg.keys[ACTION_BRIGHTER][0].key == KEY_PLUS && cfg.keys[ACTION_DIMMER][0].key == KEY_MINUS,
          "defaults: brightness on + and -");
    CHECK(cfg.keys[ACTION_START][0].key == KEY_CAT && cfg.keys[ACTION_SELECT][0].key == KEY_FRAC,
          "defaults: Start on cat, Select on frac");

    /* A version 1 file (no config_version): its choices are kept, the three
     * save options come on. */
    write_file(global_path, "frameskip_type=2\nshow_fps=1\nauto_increment=0\nauto_resume=0\n"
                            "key_a=121\nkey_save_state=23\nrom_dir=/documents/snes\n");
    config_load();
    CHECK(cfg.frameskip_type == 2 && cfg.show_fps == 1, "version 1: frameskip and FPS kept");
    CHECK(cfg.auto_increment == 1 && cfg.save_on_exit == 1 && cfg.load_on_start == 1,
          "version 1: auto-increment, save when leaving and load on start on");
    CHECK(cfg.keys[ACTION_A][0].key == KEY_SHIFT && cfg.keys[ACTION_SAVE_STATE][0].key == KEY_S,
          "version 1: keys kept");
    CHECK(strcmp(cfg.rom_dir, "/documents/snes") == 0, "version 1: ROM folder kept");

    /* Version 2: what was written is what is read, combinations and second
     * keys included; bad keys are dropped. */
    write_file(global_path, "config_version=2\nauto_increment=0\nsave_on_exit=0\nload_on_start=1\n"
                            "cpu_speed=3\nscreen_dma=1\nkey_a=6\nkey_a_2=121+20\nkey_save_state_2=121+23\n"
                            "key_b=999\nkey_x=113\nkey_y=121+121\nkey_next_slot=0\nkey_next_slot_2=20\n");
    config_load();
    CHECK(cfg.auto_increment == 0 && cfg.save_on_exit == 0 && cfg.load_on_start == 1,
          "version 2: save options read as written");
    CHECK(cfg.cpu_speed == CPU_SPEED_480 && cfg.screen_dma == 1, "version 2: CPU speed and DMA read");
    CHECK(cfg.keys[ACTION_A][0].key == KEY_Z && cfg.keys[ACTION_A][1].with == KEY_SHIFT &&
          cfg.keys[ACTION_A][1].key == KEY_3, "version 2: A is Z, and shift+3");
    CHECK(cfg.keys[ACTION_SAVE_STATE][1].with == KEY_SHIFT && cfg.keys[ACTION_SAVE_STATE][1].key == KEY_S,
          "version 2: Save state's second key is shift+S");
    CHECK(cfg.keys[ACTION_NEXT_SLOT][1].key == KEY_3 && !cfg.keys[ACTION_NEXT_SLOT][0].key,
          "version 2: a second key alone");
    CHECK(!cfg.keys[ACTION_B][0].key, "a key number that doesn't exist is dropped");
    CHECK(!cfg.keys[ACTION_X][0].key, "a touchpad arrow (reserved) is dropped");
    CHECK(!cfg.keys[ACTION_Y][0].key, "a combination of a key with itself is dropped");
    CHECK(cfg.quit_new_save == 0, "no quit_new_save line: it follows auto-increment (off), as leaving did");
    CHECK(cfg.keys[ACTION_BRIGHTER][0].key == KEY_PLUS && cfg.keys[ACTION_DIMMER][0].key == KEY_MINUS,
          "a file from before the brightness keys gets them");

    write_file(global_path, "config_version=3\nauto_increment=0\nquit_new_save=1\nauto_slot=1\n"
                            "brightness=7\nkey_brighter=0\nkey_dimmer=52\n");
    config_load();
    CHECK(cfg.quit_new_save == 1 && cfg.auto_slot == 1 && cfg.brightness == 7,
          "New save on quit, the auto slot and brightness read as written");
    CHECK(!cfg.keys[ACTION_BRIGHTER][0].key && cfg.keys[ACTION_DIMMER][0].key == KEY_9,
          "the brightness keys read as written");
    /* Files from before the brightness keys: a key the file already uses
     * for something else isn't given to them as well. With PocketSNES's
     * Select on -, Brightness down has no key; up gets +, which is free. */
    write_file(global_path, "config_version=3\nkey_start=2\nkey_select=83\nkey_start_2=0\nkey_select_2=0\n");
    config_load();
    CHECK(cfg.keys[ACTION_SELECT][0].key == KEY_MINUS && cfg.keys[ACTION_BRIGHTER][0].key == KEY_PLUS &&
          !cfg.keys[ACTION_DIMMER][0].key,
          "a newer hotkey's default key is left out when the file already uses it");
    /* With - as Start and + as Select, neither gets one. */
    write_file(global_path, "config_version=3\nkey_start=83\nkey_select=99\nkey_start_2=0\nkey_select_2=0\n");
    config_load();
    CHECK(cfg.keys[ACTION_START][0].key == KEY_MINUS && cfg.keys[ACTION_SELECT][0].key == KEY_PLUS &&
          !cfg.keys[ACTION_BRIGHTER][0].key && !cfg.keys[ACTION_DIMMER][0].key,
          "... both, when the file uses + and -");
    write_file(global_path, "config_version=3\nbrightness=99\nauto_slot=5\n");
    config_load();
    CHECK(cfg.brightness == BRIGHTNESS_LEVELS && cfg.auto_slot == 1, "out of range values are clamped");
    write_file(global_path, "config_version=2\nauto_increment=0\nsave_on_exit=0\nload_on_start=1\n"
                            "cpu_speed=3\nscreen_dma=1\nkey_a=6\nkey_a_2=121+20\nkey_save_state_2=121+23\n"
                            "key_b=999\nkey_x=113\nkey_y=121+121\nkey_next_slot=0\nkey_next_slot_2=20\n");
    config_load();

    /* Saving and loading again gives the same. */
    struct Config before = cfg;
    config_save();
    config_load();
    CHECK(memcmp(cfg.keys, before.keys, sizeof(cfg.keys)) == 0 && cfg.cpu_speed == before.cpu_speed &&
          cfg.auto_increment == before.auto_increment && cfg.screen_dma == before.screen_dma,
          "saving and loading gives back the same settings and keys");
    CHECK(file_has_line(global_path, "key_a_2=121+20"), "a combination is written as with+key");

    /* Settings for one game. */
    remove(game_path);
    config_open_game(rom);
    CHECK(!config_game_has_settings() && !config_game_has_keys(), "a new game uses the settings for all games");
    CHECK(cfg.auto_increment == 0, "... with their values");
    config_set_game_settings(1);
    cfg.auto_increment = 1;
    cfg.frameskip_type = FRAMESKIP_MANUAL;
    config_save();
    CHECK(file_has_line(game_path, "own_settings=1") && file_has_line(game_path, "auto_increment=1"),
          "the game's own settings are written to its file");
    CHECK(file_has_line(global_path, "auto_increment=0"), "... and not to the one for all games");
    config_close_game();
    CHECK(cfg.auto_increment == 0 && cfg.frameskip_type != FRAMESKIP_MANUAL,
          "closing the game goes back to the settings for all games");
    config_open_game(rom);
    CHECK(config_game_has_settings() && cfg.auto_increment == 1 && cfg.frameskip_type == FRAMESKIP_MANUAL,
          "opening the game again uses its own settings");
    CHECK(cfg.keys[ACTION_A][0].key == KEY_Z, "... and the keys for all games");

    /* Own keys too, then neither: the file goes away. */
    config_set_game_keys(1);
    cfg.keys[ACTION_START][1].key = KEY_1;
    config_save();
    CHECK(file_has_line(game_path, "own_keys=1") && file_has_line(game_path, "key_start_2=24"),
          "the game's own keys are written to its file");
    config_set_game_settings(0);
    config_set_game_keys(0);
    CHECK(cfg.auto_increment == 0 && cfg.keys[ACTION_START][1].key == 0,
          "turning both off goes back to the ones for all games");
    config_save();
    CHECK(!file_exists(game_path), "a game with no settings or keys of its own has no file");
    config_close_game();

    /* After a freeze at a raised speed: back to normal for all games and in
     * the game's own settings. */
    config_open_game(rom);
    config_set_game_settings(1);
    cfg.cpu_speed = CPU_SPEED_456;
    config_save();
    config_close_game();
    cfg.cpu_speed = CPU_SPEED_480;
    config_save();
    config_reset_cpu_speed(rom);
    CHECK(cfg.cpu_speed == CPU_SPEED_NORMAL && file_has_line(global_path, "cpu_speed=0"),
          "CPU speed reset for all games");
    CHECK(file_has_line(game_path, "cpu_speed=0") && file_has_line(game_path, "own_settings=1"),
          "CPU speed reset in the game's own settings");
    CHECK(config_cpu_multiplier(CPU_SPEED_NORMAL) == 0 && config_cpu_multiplier(CPU_SPEED_480) == 40 &&
          config_cpu_multiplier(CPU_SPEED_432) == 36, "CPU speeds are 12 MHz times 36, 38, 40");
}

/* ---- Brightness levels ---- */

/* ---- Settings kept in the program file (selfstore.h) ---- */

static long file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    return size;
}

static void test_selfstore(void)
{
    char backup[700];
    unsigned char program[3000], back[3000];
    for (size_t i = 0; i < sizeof(program); i++)
        program[i] = (unsigned char) (i * 7 + 1);
    snprintf(program_file, sizeof(program_file), "%s/_PocketSNES.tns", exe_dir);
    snprintf(backup, sizeof(backup), "%s/PocketSNES_saves/pocketsnes.cfg.tns", documents_dir);
    FILE *f = fopen(program_file, "wb");
    fwrite(program, 1, sizeof(program), f);
    fclose(f);

    CHECK(selfstore_read(program_file) == NULL, "a program without settings in it has none");
    config_load();
    cfg.show_fps = 2;
    cfg.brightness = 4;
    config_save();
    CHECK(file_size(program_file) == (long) sizeof(program) + SELFSTORE_SIZE + 16,
          "saving adds the settings area to the program file");
    cfg.brightness = 9;
    config_save();
    CHECK(file_size(program_file) == (long) sizeof(program) + SELFSTORE_SIZE + 16,
          "... and saving again writes over it, the size stays");
    f = fopen(program_file, "rb");
    size_t got = fread(back, 1, sizeof(back), f);
    fclose(f);
    CHECK(got == sizeof(back) && memcmp(back, program, sizeof(program)) == 0, "... and the program's own bytes are untouched");

    /* Settings come from the program even without the copy. */
    remove(backup);
    config_load();
    CHECK(cfg.show_fps == 2 && cfg.brightness == 9, "settings are read from the program file");
    /* A new version copied over: no settings in it, so the copy is used. */
    config_save();
    f = fopen(program_file, "wb");
    fwrite(program, 1, sizeof(program), f);
    fclose(f);
    config_load();
    CHECK(cfg.show_fps == 2 && cfg.brightness == 9, "a new program file picks up the copy in PocketSNES_saves");
    config_save();
    char *text = selfstore_read(program_file);
    CHECK(text && strstr(text, "brightness=9\n"), "... and stores it in itself at the next save");
    free(text);
    program_file[0] = 0;
}

static void test_reset_all(void)
{
    config_load();
    cfg.show_fps = 0;
    cfg.frameskip_type = FRAMESKIP_AUTO;
    cfg.keys[ACTION_START][0].key = KEY_ENTER;
    snprintf(cfg.rom_dir, sizeof(cfg.rom_dir), "/documents/games");
    config_save();
    config_reset_all();
    config_load();
    CHECK(cfg.show_fps == 1 && cfg.frameskip_type == FRAMESKIP_OFF && cfg.keys[ACTION_START][0].key == KEY_CAT,
          "Reset all settings: settings and keys back to the defaults, and saved");
    CHECK(strcmp(cfg.rom_dir, "/documents/games") == 0, "... and the ROM folder stays");
}

static void test_brightness(void)
{
    CHECK(brightness_value(10) == 0 && brightness_value(1) == 225 && brightness_value(7) == 75,
          "levels 10, 7 and 1 are the backlight values 0, 75 and 225");
    CHECK(brightness_value(0) == 225 && brightness_value(11) == 0, "levels out of range are clamped");
    int ok = 1;
    for (int level = 1; level <= BRIGHTNESS_LEVELS; level++)
        ok &= brightness_level(brightness_value(level)) == level;
    CHECK(ok, "the level of each level's value is that level");
    CHECK(brightness_level(112) == 6 && brightness_level(255) == 1 && brightness_level(-5) == 10,
          "the nearest level of other values (the calculator's own)");
}

/* ---- Key bindings ---- */

static void test_bindings(void)
{
    struct Binding ctrl = { KEY_CTRL, 0 }, shift_s = { KEY_S, KEY_SHIFT }, none = { 0, 0 };
    char name[32];

    memset(keys_down, 0, sizeof(keys_down));
    next_poll();
    CHECK(!binding_held(ctrl) && !binding_pressed(ctrl) && !binding_held(none), "nothing held");

    set_key(KEY_CTRL, 1);
    CHECK(binding_held(ctrl) && binding_pressed(ctrl), "a key going down is held and pressed");
    next_poll();
    CHECK(binding_held(ctrl) && !binding_pressed(ctrl), "a key still down is held, not pressed again");

    set_key(KEY_SHIFT, 1);
    next_poll();
    CHECK(!binding_held(shift_s), "the first key of a combination alone isn't the combination");
    set_key(KEY_S, 1);
    CHECK(binding_held(shift_s) && binding_pressed(shift_s), "hold shift, press S: the combination");
    next_poll();
    CHECK(binding_held(shift_s) && !binding_pressed(shift_s), "... held, not pressed again");
    set_key(KEY_SHIFT, 0);
    next_poll();
    CHECK(!binding_held(shift_s), "letting go of shift ends it");

    memset(keys_down, 0, sizeof(keys_down));
    next_poll();
    set_key(KEY_S, 1);
    next_poll();
    set_key(KEY_SHIFT, 1);
    CHECK(binding_pressed(shift_s), "S then shift also makes the combination");

    CHECK(strcmp(binding_name(ctrl, name, sizeof(name)), "ctrl") == 0, "name of a key");
    CHECK(strcmp(binding_name(shift_s, name, sizeof(name)), "shift+S") == 0, "name of a combination");
    CHECK(strcmp(binding_name(none, name, sizeof(name)), "(none)") == 0, "name of no key");
}

/* ---- What the core needs from a frontend (emu.cpp), unused here ---- */

bool JustifierOffscreen (void) { return true; }
void JustifierButtons (uint32 &) {}
void S9xProcessSound (unsigned int) {}
void _makepath (char *path, const char *, const char *, const char *, const char *) { *path = 0; }
void _splitpath (const char *, char *drive, char *dir, char *fname, char *ext)
{
    *drive = *dir = *fname = *ext = 0;
}

extern "C"
{
void S9xGenerateSound (void) {}
void S9xLoadSDD1Data (void) {}
uint32 S9xPerfTicks (void) { return 0; }
void S9xPerfRenderDone (uint32) {}
void S9xBeforeDrawingLines (uint32) {}
bool8 S9xInitUpdate () { return FALSE; }
bool8 S9xDeinitUpdate (int, int, bool8) { return TRUE; }
void S9xSyncSpeed (void) {}
const char *S9xGetFilename (const char *extension) { return extension; }
uint32 S9xReadJoypad (int) { return 0; }
bool8 S9xReadMousePosition (int, int &, int &, uint32 &) { return FALSE; }
bool8 S9xReadSuperScopePosition (int &, int &, uint32 &) { return FALSE; }
void S9xAutoSaveSRAM (void) {}
}

int main(void)
{
    char dir_template[] = "/tmp/pocketsnes-unit-XXXXXX";
    const char *dir = mkdtemp(dir_template);
    if (!dir)
    {
        perror("mkdtemp");
        return 1;
    }
    snprintf(documents_dir, sizeof(documents_dir), "%s", dir);
    snprintf(exe_dir, sizeof(exe_dir), "%s/snes", dir);
    mkdir(exe_dir, 0755);

    test_tile_drawing();
    test_rotation();
    test_config();
    test_brightness();
    test_selfstore();
    test_reset_all();
    test_bindings();

    char command[700];
    snprintf(command, sizeof(command), "rm -rf '%s'", dir);
    if (system(command) != 0)
        printf("couldn't remove %s\n", dir);

    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
