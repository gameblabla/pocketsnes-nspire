/* PC side of platform.h, for testing without a calculator.
 *
 * Window mode (SDL2): the calculator keys are on the PC keyboard, see
 * host_keys below. Headless mode (--headless, or when no window can be
 * opened): the keys are driven by a script (--script FILE) and time is
 * virtual, so a run is repeatable and needs no display. Script commands, one
 * per line, each processed at a key poll (the game polls once per frame):
 *
 *   wait N          do nothing for N polls
 *   press KEY [N]   hold KEY for N polls (default 2)
 *   down KEY / up KEY
 *   shot FILE       save the last shown frame as a PNG (in --shots DIR)
 *   cost TICKS      pretend every poll takes this long (32768 ticks = 1 s)
 *   drawcost TICKS  pretend every shown frame takes this long
 *   echo TEXT       print TEXT
 *   quit            close, like closing the window
 *
 * KEY is a name from keys.cpp ("esc", "enter", "ctrl", "up", "S", ...) or a
 * key number. The script quits when it runs out of lines.
 *
 * For the tests, --log FILE appends a line to FILE for every CPU clock
 * change asked for and every message shown in the game, --stuck-clock acts
 * like a calculator whose clock doesn't change when asked, --clock-limit N
 * like one that doesn't go past multiplier N (12N MHz), --glitch-above N
 * like one that runs games wrongly above multiplier N (the overclock test's
 * game check sees a difference), --program FILE uses FILE as the program file
 * that holds the settings (selfstore.h), and --backlight N sets the pretend
 * calculator's own backlight value (0 brightest, 225 darkest; changes are
 * logged as "backlight N"). */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <zlib.h>

#ifdef HOST_SDL
#include <SDL2/SDL.h>
#endif

#include "platform.h"
#include "rotate.h"

#define HOST_TICK_HZ 32768

/* Three screens taking turns, like on the calculator. */
#define GUARD_LINES 16
/* Interlaced Mode 5 draws up to twice the lines (the old 480-line buffer). */
#define BELOW_LINES 272
static uint16_t screen_memory[PLATFORM_MAX_SCREENS][(GUARD_LINES + SCREEN_H + BELOW_LINES) * SCREEN_W];
static int current_screen, copy_mode;
static uint16_t shown[SCREEN_W * SCREEN_H];
static uint32_t keys_now[4], keys_old[4];
static char exe_dir[PATH_MAX] = ".";
static int headless;
static int quit_requested;

static uint64_t virtual_ticks;
static uint32_t cost_per_poll, cost_per_present;

static FILE *script;
static int script_wait;
static uint32_t script_keys[4];
static struct { int key; int polls; } releases[16];
static char shots_dir[1024] = ".";
static char log_path[1024];
static int os_backlight = 112, backlight = 112;   /* see platform_set_backlight */
static int clock_stuck;
static int clock_limit = 63;
static int glitch_above = 63;
static char program_path[1024];

#ifdef HOST_SDL
static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;

static const struct { SDL_Scancode code; int key; } host_keys[] =
{
    { SDL_SCANCODE_UP, KEY_UP }, { SDL_SCANCODE_DOWN, KEY_DOWN },
    { SDL_SCANCODE_LEFT, KEY_LEFT }, { SDL_SCANCODE_RIGHT, KEY_RIGHT },
    { SDL_SCANCODE_RETURN, KEY_ENTER }, { SDL_SCANCODE_KP_ENTER, KEY_RETURN },
    { SDL_SCANCODE_ESCAPE, KEY_ESC }, { SDL_SCANCODE_TAB, KEY_TAB },
    { SDL_SCANCODE_BACKSPACE, KEY_DEL }, { SDL_SCANCODE_DELETE, KEY_DEL },
    { SDL_SCANCODE_LCTRL, KEY_CTRL }, { SDL_SCANCODE_RCTRL, KEY_CTRL },
    { SDL_SCANCODE_LSHIFT, KEY_SHIFT }, { SDL_SCANCODE_RSHIFT, KEY_SHIFT },
    { SDL_SCANCODE_SPACE, KEY_SPACE }, { SDL_SCANCODE_MINUS, KEY_MINUS },
    { SDL_SCANCODE_KP_MINUS, KEY_MINUS }, { SDL_SCANCODE_EQUALS, KEY_EQUALS },
    { SDL_SCANCODE_PERIOD, KEY_PERIOD }, { SDL_SCANCODE_COMMA, KEY_COMMA },
    { SDL_SCANCODE_SLASH, KEY_DIVIDE }, { SDL_SCANCODE_KP_PLUS, KEY_PLUS },
    { SDL_SCANCODE_KP_MULTIPLY, KEY_MULTIPLY }, { SDL_SCANCODE_HOME, KEY_HOME },
    { SDL_SCANCODE_F1, KEY_MENU }, { SDL_SCANCODE_F2, KEY_VAR },
    { SDL_SCANCODE_F3, KEY_DOC }, { SDL_SCANCODE_F4, KEY_SCRATCHPAD },
    { SDL_SCANCODE_F5, KEY_CAT }, { SDL_SCANCODE_F6, KEY_FRAC },
    { SDL_SCANCODE_LEFTBRACKET, KEY_LPAREN }, { SDL_SCANCODE_RIGHTBRACKET, KEY_RPAREN },
    { SDL_SCANCODE_0, KEY_0 }, { SDL_SCANCODE_1, KEY_1 }, { SDL_SCANCODE_2, KEY_2 },
    { SDL_SCANCODE_3, KEY_3 }, { SDL_SCANCODE_4, KEY_4 }, { SDL_SCANCODE_5, KEY_5 },
    { SDL_SCANCODE_6, KEY_6 }, { SDL_SCANCODE_7, KEY_7 }, { SDL_SCANCODE_8, KEY_8 },
    { SDL_SCANCODE_9, KEY_9 },
    { SDL_SCANCODE_KP_0, KEY_0 }, { SDL_SCANCODE_KP_1, KEY_1 }, { SDL_SCANCODE_KP_2, KEY_2 },
    { SDL_SCANCODE_KP_3, KEY_3 }, { SDL_SCANCODE_KP_4, KEY_4 }, { SDL_SCANCODE_KP_5, KEY_5 },
    { SDL_SCANCODE_KP_6, KEY_6 }, { SDL_SCANCODE_KP_7, KEY_7 }, { SDL_SCANCODE_KP_8, KEY_8 },
    { SDL_SCANCODE_KP_9, KEY_9 },
    { SDL_SCANCODE_A, KEY_A }, { SDL_SCANCODE_B, KEY_B }, { SDL_SCANCODE_C, KEY_C },
    { SDL_SCANCODE_D, KEY_D }, { SDL_SCANCODE_E, KEY_E }, { SDL_SCANCODE_F, KEY_F },
    { SDL_SCANCODE_G, KEY_G }, { SDL_SCANCODE_H, KEY_H }, { SDL_SCANCODE_I, KEY_I },
    { SDL_SCANCODE_J, KEY_J }, { SDL_SCANCODE_K, KEY_K }, { SDL_SCANCODE_L, KEY_L },
    { SDL_SCANCODE_M, KEY_M }, { SDL_SCANCODE_N, KEY_N }, { SDL_SCANCODE_O, KEY_O },
    { SDL_SCANCODE_P, KEY_P }, { SDL_SCANCODE_Q, KEY_Q }, { SDL_SCANCODE_R, KEY_R },
    { SDL_SCANCODE_S, KEY_S }, { SDL_SCANCODE_T, KEY_T }, { SDL_SCANCODE_U, KEY_U },
    { SDL_SCANCODE_V, KEY_V }, { SDL_SCANCODE_W, KEY_W }, { SDL_SCANCODE_X, KEY_X },
    { SDL_SCANCODE_Y, KEY_Y }, { SDL_SCANCODE_Z, KEY_Z },
};
#endif

static void set_key(uint32_t *keys, int key, int down)
{
    if (key <= 0 || key > NUM_KEYS)
        return;
    if (down)
        keys[(key - 1) >> 5] |= 1u << ((key - 1) & 31);
    else
        keys[(key - 1) >> 5] &= ~(1u << ((key - 1) & 31));
}

static int parse_key(const char *name)
{
    if (isdigit((unsigned char) name[0]) && name[1] != 0)
        return atoi(name);
    for (int key = 1; key <= NUM_KEYS; key++)
        if (key_name(key) && strcasecmp(key_name(key), name) == 0)
            return key;
    fprintf(stderr, "script: unknown key '%s'\n", name);
    return KEY_NONE;
}

static void write_png(const char *path, const uint16_t *pixels)
{
    const uint32_t row = 1 + SCREEN_W * 3;
    uint8_t *raw = (uint8_t *) malloc(row * SCREEN_H);
    uLongf packed_size = compressBound(row * SCREEN_H);
    uint8_t *packed = (uint8_t *) malloc(packed_size);
    FILE *f = fopen(path, "wb");

    if (!raw || !packed || !f)
    {
        fprintf(stderr, "can't write %s\n", path);
        free(raw);
        free(packed);
        if (f)
            fclose(f);
        return;
    }

    for (int y = 0; y < SCREEN_H; y++)
    {
        uint8_t *out = raw + y * row;
        *out++ = 0;
        for (int x = 0; x < SCREEN_W; x++)
        {
            uint16_t c = pixels[y * SCREEN_W + x];
            *out++ = (uint8_t) (((c >> 11) << 3) | (c >> 13));
            *out++ = (uint8_t) ((((c >> 5) & 63) << 2) | ((c >> 9) & 3));
            *out++ = (uint8_t) (((c & 31) << 3) | ((c >> 2) & 7));
        }
    }
    compress2(packed, &packed_size, raw, row * SCREEN_H, 6);

    static const uint8_t signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    uint8_t ihdr[13] = { 0, 0, SCREEN_W >> 8, SCREEN_W & 255, 0, 0, SCREEN_H >> 8, SCREEN_H & 255,
                         8, 2, 0, 0, 0 };
    fwrite(signature, 1, 8, f);

    const struct { const char *type; const uint8_t *data; uint32_t size; } chunks[3] =
    {
        { "IHDR", ihdr, 13 }, { "IDAT", packed, (uint32_t) packed_size }, { "IEND", NULL, 0 }
    };
    for (int i = 0; i < 3; i++)
    {
        uint8_t len[4] = { (uint8_t) (chunks[i].size >> 24), (uint8_t) (chunks[i].size >> 16),
                           (uint8_t) (chunks[i].size >> 8), (uint8_t) chunks[i].size };
        uLong crc = crc32(0, (const Bytef *) chunks[i].type, 4);
        if (chunks[i].size)
            crc = crc32(crc, chunks[i].data, chunks[i].size);
        uint8_t crc_bytes[4] = { (uint8_t) (crc >> 24), (uint8_t) (crc >> 16), (uint8_t) (crc >> 8), (uint8_t) crc };
        fwrite(len, 1, 4, f);
        fwrite(chunks[i].type, 1, 4, f);
        if (chunks[i].size)
            fwrite(chunks[i].data, 1, chunks[i].size, f);
        fwrite(crc_bytes, 1, 4, f);
    }
    fclose(f);
    free(raw);
    free(packed);
}

static void script_step(void)
{
    char line[512];

    for (int i = 0; i < 16; i++)
    {
        if (releases[i].key && --releases[i].polls <= 0)
        {
            set_key(script_keys, releases[i].key, 0);
            releases[i].key = 0;
        }
    }

    if (script_wait > 0)
    {
        script_wait--;
        return;
    }

    while (fgets(line, sizeof(line), script))
    {
        char cmd[32] = "", arg[400] = "";
        int n = 0;

        line[strcspn(line, "\r\n")] = 0;
        if (sscanf(line, "%31s %399s %d", cmd, arg, &n) < 1 || cmd[0] == '#')
            continue;

        if (strcmp(cmd, "wait") == 0)
        {
            script_wait = atoi(arg) - 1;
            return;
        }
        else if (strcmp(cmd, "press") == 0)
        {
            int key = parse_key(arg);
            set_key(script_keys, key, 1);
            for (int i = 0; i < 16; i++)
            {
                if (!releases[i].key)
                {
                    releases[i].key = key;
                    releases[i].polls = n > 0 ? n : 2;
                    break;
                }
            }
        }
        else if (strcmp(cmd, "down") == 0)
            set_key(script_keys, parse_key(arg), 1);
        else if (strcmp(cmd, "up") == 0)
            set_key(script_keys, parse_key(arg), 0);
        else if (strcmp(cmd, "shot") == 0)
        {
            char path[1500];
            snprintf(path, sizeof(path), "%s/%s", shots_dir, arg);
            write_png(path, shown);
        }
        else if (strcmp(cmd, "cost") == 0)
            cost_per_poll = (uint32_t) atoi(arg);
        else if (strcmp(cmd, "drawcost") == 0)
            cost_per_present = (uint32_t) atoi(arg);
        else if (strcmp(cmd, "echo") == 0)
            printf("script: %s\n", line + 5);
        else if (strcmp(cmd, "quit") == 0)
        {
            quit_requested = 1;
            return;
        }
        else
            fprintf(stderr, "script: unknown command '%s'\n", cmd);
    }
    quit_requested = 1;
}

int platform_init(int *argc, char **argv)
{
    int out = 1, scale = 2;
    const char *script_path = NULL;

    const char *slash = strrchr(argv[0], '/');
    if (slash)
        snprintf(exe_dir, sizeof(exe_dir), "%.*s", (int) (slash - argv[0]), argv[0]);

    for (int i = 1; i < *argc; i++)
    {
        if (strcmp(argv[i], "--headless") == 0)
            headless = 1;
        else if (strcmp(argv[i], "--script") == 0 && i + 1 < *argc)
            script_path = argv[++i];
        else if (strcmp(argv[i], "--shots") == 0 && i + 1 < *argc)
            snprintf(shots_dir, sizeof(shots_dir), "%s", argv[++i]);
        else if (strcmp(argv[i], "--exe-dir") == 0 && i + 1 < *argc)
            snprintf(exe_dir, sizeof(exe_dir), "%s", argv[++i]);
        else if (strcmp(argv[i], "--scale") == 0 && i + 1 < *argc)
            scale = atoi(argv[++i]);
        else if (strcmp(argv[i], "--log") == 0 && i + 1 < *argc)
            snprintf(log_path, sizeof(log_path), "%s", argv[++i]);
        else if (strcmp(argv[i], "--stuck-clock") == 0)
            clock_stuck = 1;
        else if (strcmp(argv[i], "--clock-limit") == 0 && i + 1 < *argc)
            clock_limit = atoi(argv[++i]);
        else if (strcmp(argv[i], "--program") == 0 && i + 1 < *argc)
            snprintf(program_path, sizeof(program_path), "%s", argv[++i]);
        else if (strcmp(argv[i], "--glitch-above") == 0 && i + 1 < *argc)
            glitch_above = atoi(argv[++i]);
        else if (strcmp(argv[i], "--backlight") == 0 && i + 1 < *argc)
            os_backlight = backlight = atoi(argv[++i]);
        else
            argv[out++] = argv[i];
    }
    *argc = out;
    argv[out] = NULL;

    /* The calculator always gives absolute paths; do the same here. */
    char absolute[PATH_MAX];
    if (realpath(exe_dir, absolute))
        snprintf(exe_dir, sizeof(exe_dir), "%s", absolute);

    if (script_path)
    {
        script = fopen(script_path, "r");
        if (!script)
        {
            fprintf(stderr, "can't open script %s\n", script_path);
            return 0;
        }
        headless = 1;
    }

#ifdef HOST_SDL
    if (!headless)
    {
        if (SDL_Init(SDL_INIT_VIDEO) == 0)
        {
            window = SDL_CreateWindow("PocketSNES (PC test build)", SDL_WINDOWPOS_CENTERED,
                                      SDL_WINDOWPOS_CENTERED, SCREEN_W * scale, SCREEN_H * scale, 0);
            renderer = window ? SDL_CreateRenderer(window, -1, 0) : NULL;
            texture = renderer ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                                   SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H) : NULL;
        }
        if (!texture)
        {
            fprintf(stderr, "no window (%s), running headless\n", SDL_GetError());
            headless = 1;
        }
    }
#else
    (void) scale;
    headless = 1;
#endif
    return 1;
}

void platform_shutdown(void)
{
    platform_set_backlight(-1);
    if (script)
        fclose(script);
#ifdef HOST_SDL
    if (window)
        SDL_Quit();
#endif
}

uint16_t *platform_screen(void)
{
    return screen_memory[current_screen] + GUARD_LINES * SCREEN_W;
}

int platform_screen_index(void)
{
    return current_screen;
}

uint16_t *platform_screen_nowait(void)
{
    return platform_screen();
}

void platform_wait_lines(int last_line)
{
    (void) last_line;
}

int platform_screen_spare_lines(void)
{
    return BELOW_LINES;
}

uint8_t *platform_fast_depth_buffer(int *lines)
{
    (void) lines;
    return NULL;
}

/* Shows what's in 'shown'. */
static void show_frame(void)
{
    if (headless)
        virtual_ticks += cost_per_present;
#ifdef HOST_SDL
    if (!headless)
    {
        SDL_UpdateTexture(texture, NULL, shown, SCREEN_W * 2);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }
#endif
}

void platform_present(void)
{
    memcpy(shown, platform_screen(), sizeof(shown));
    show_frame();
    if (!copy_mode)
        current_screen = (current_screen + 1) % PLATFORM_MAX_SCREENS;
}

/* Game frames go into portrait buffers like on the CX II, and are turned
 * back for showing, so the tests check the same steps. */
static uint16_t portrait_screens[PLATFORM_MAX_SCREENS][SCREEN_W * SCREEN_H];
static int current_portrait;
static uint16_t *frame_target;

int platform_frame_begin(void)
{
    frame_target = portrait_screens[current_portrait];
    return PLATFORM_MAX_SCREENS + current_portrait;
}

void platform_frame_copy(int x, int y, int w, int h)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w > 0 && h > 0)
        rotate_block(platform_screen() + y * SCREEN_W + x, SCREEN_W, x, y, w, h, frame_target);
}

void platform_frame_present(void)
{
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++)
            shown[y * SCREEN_W + x] = frame_target[x * SCREEN_H + SCREEN_H - 1 - y];
    show_frame();
    current_portrait = (current_portrait + 1) % PLATFORM_MAX_SCREENS;
}

const char *platform_screen_mode_name(void)
{
    return "PC";
}

uint32_t platform_cpu_mhz(void)
{
    return 0;
}

void platform_log(const char *text)
{
    if (!log_path[0])
        return;
    FILE *f = fopen(log_path, "a");
    if (f)
    {
        fprintf(f, "%s\n", text);
        fclose(f);
    }
}

/* The PC only remembers (and logs) what it was asked, and reports the clock
 * asked for as the one measured: 12 MHz times the multiplier, or 396 MHz,
 * the CX II's own, for 0 or with --stuck-clock. */
static int cpu_multiplier;

uint32_t platform_set_cpu_multiplier(int multiplier)
{
    if (multiplier != cpu_multiplier)
    {
        char line[32];
        snprintf(line, sizeof(line), "clock x%d", multiplier);
        platform_log(line);
    }
    cpu_multiplier = multiplier;
    return multiplier && !clock_stuck && multiplier <= clock_limit ? (uint32_t) multiplier * 12 : 396;
}

uint32_t platform_cpu_normal_mhz(void)
{
    return 396;
}

const char *platform_program_path(void)
{
    return program_path[0] ? program_path : NULL;
}

int platform_test_glitch(void)
{
    return cpu_multiplier > glitch_above && cpu_multiplier <= clock_limit && !clock_stuck;
}

/* A pretend backlight register: the calculator's own value is
 * --backlight N (112 if not given), and each change goes to the test log
 * as "backlight N". */

int platform_os_backlight(void)
{
    return os_backlight;
}

void platform_set_backlight(int value)
{
    if (value < 0)
        value = os_backlight;
    if (value == backlight)
        return;
    backlight = value;
    char line[32];
    snprintf(line, sizeof(line), "backlight %d", value);
    platform_log(line);
}

int platform_cpu_multiplier(void)
{
    return cpu_multiplier ? cpu_multiplier : 33;
}

void platform_clock_report(char *text, size_t size)
{
    snprintf(text, size, "PC build, clock x%d asked for", platform_cpu_multiplier());
}

void platform_set_game_frames_by_dma(int dma)
{
    (void) dma;
}

void platform_write_diagnostics(const char *path)
{
    FILE *f = fopen(path, "a");
    if (f)
    {
        fprintf(f, "== diagnostics\nPC test build, nothing to report\n");
        fclose(f);
    }
}

void platform_write_memory_tests(const char *path)
{
    (void) path;
}

/* Any other way of showing frames uses one screen, like lcd_blit did. */
int platform_set_screen_output(int output)
{
    copy_mode = output != OUTPUT_BEST;
    if (copy_mode)
        current_screen = 0;
    return 1;
}

void platform_poll_keys(void)
{
    memcpy(keys_old, keys_now, sizeof(keys_now));

    if (headless)
    {
        static unsigned long polls;
        static int trace = -1;
        if (trace < 0)
            trace = getenv("PSNES_TRACE") != NULL;

        virtual_ticks += cost_per_poll;
        if (script)
            script_step();
        else
            quit_requested = 1;
        memcpy(keys_now, script_keys, sizeof(keys_now));
        if (trace)
            fprintf(stderr, "poll %lu: keys %08x %08x %08x %08x\n", ++polls,
                    keys_now[0], keys_now[1], keys_now[2], keys_now[3]);
        return;
    }

#ifdef HOST_SDL
    SDL_Event event;
    while (SDL_PollEvent(&event))
        if (event.type == SDL_QUIT)
            quit_requested = 1;

    const Uint8 *state = SDL_GetKeyboardState(NULL);
    memset(keys_now, 0, sizeof(keys_now));
    for (size_t i = 0; i < sizeof(host_keys) / sizeof(host_keys[0]); i++)
        if (state[host_keys[i].code])
            set_key(keys_now, host_keys[i].key, 1);
#endif
}

int platform_key_down(int key)
{
    if (key <= 0 || key > NUM_KEYS)
        return 0;
    return (keys_now[(key - 1) >> 5] >> ((key - 1) & 31)) & 1;
}

int platform_key_pressed(int key)
{
    if (key <= 0 || key > NUM_KEYS)
        return 0;
    return ((keys_now[(key - 1) >> 5] & ~keys_old[(key - 1) >> 5]) >> ((key - 1) & 31)) & 1;
}

int platform_any_key_down(void)
{
    return (keys_now[0] | keys_now[1] | keys_now[2] | keys_now[3]) != 0;
}

int platform_first_key_down(void)
{
    for (int key = 1; key <= NUM_KEYS; key++)
        if (platform_key_down(key))
            return key;
    return 0;
}

uint32_t platform_ticks(void)
{
    if (headless)
        return (uint32_t) virtual_ticks;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t) (((uint64_t) ts.tv_sec * HOST_TICK_HZ) +
                       ((uint64_t) ts.tv_nsec * HOST_TICK_HZ) / 1000000000u);
}

uint32_t platform_tick_hz(void)
{
    return HOST_TICK_HZ;
}

void platform_wait_until(uint32_t tick)
{
    if (headless)
    {
        int32_t ahead = (int32_t) (tick - (uint32_t) virtual_ticks);
        if (ahead > 0)
            virtual_ticks += (uint32_t) ahead;
        return;
    }

    for (;;)
    {
        int32_t ahead = (int32_t) (tick - platform_ticks());
        if (ahead <= 0)
            break;
        if (ahead > HOST_TICK_HZ / 500)
        {
            struct timespec ts = { 0, (long) ((uint64_t) (ahead - HOST_TICK_HZ / 1000) * 1000000000u / HOST_TICK_HZ) };
            nanosleep(&ts, NULL);
        }
    }
}

const char *platform_exe_dir(void)
{
    return exe_dir;
}

int platform_quit_requested(void)
{
    return quit_requested;
}
