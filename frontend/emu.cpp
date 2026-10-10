#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snes9x.h"
#include "memmap.h"
#include "cpuexec.h"
#include "ppu.h"
#include "gfx.h"
#include "display.h"
#include "apu.h"
#include "soundux.h"
#include "snapshot.h"

#include "config.h"
#include "brightness.h"
#include "draw.h"
#include "emu.h"
#include "gui.h"
#include "bindings.h"
#include "menu.h"
#include "paths.h"
#include "romzip.h"
#include "platform.h"
#include "states.h"
#include "ui.h"

#ifndef BUILD_NAME
#define BUILD_NAME "dev"
#endif

/* Fast forward draws one frame in this many, like lr-gpsp-nspire. */
#define FAST_FORWARD_DRAW_EVERY 5

#define MESSAGE_Y (SCREEN_H - 20)
#define PICTURE_LEFT ((SCREEN_W - SNES_WIDTH) / 2)

/* The renderer draws whole 8x8 tiles, so it can write up to a tile past the
 * SNES picture, and now and then a pixel before it. That much border around
 * the picture is wiped after every frame. */
#define SPILL 8

enum
{
    PENDING_MENU = 1,
    PENDING_QUIT = 2,
    PENDING_SAVE = 4,
    PENDING_LOAD = 8,
    PENDING_LOAD_NEWEST = 16,
    PENDING_RESTART = 32
};

/* How frames are paced; the speed test also uses the uncapped ones. */
enum Pace
{
    PACE_NORMAL,
    PACE_UNCAPPED_DRAW_ALL,
    PACE_UNCAPPED_DRAW_NONE
};

/* The game is drawn straight into the platform's screen buffers. The
 * renderer's other buffers (sub screen and depth buffers) have spare lines on
 * both sides for the same reason as SPILL, and they are static: the renderer
 * keeps the distance between buffers in 32-bit ints, which heap addresses on
 * a 64-bit PC would overflow. */
#define PITCH        (SCREEN_W * 2)
#define RENDER_LINES 512
#define GUARD_LINES  16
/* The renderer finds the sub screen's depth buffer at a distance from the
 * main one kept in an unsigned 32-bit number (GFX.DepthDelta), so it must
 * come after it. On the calculator a negative distance would still wrap
 * around to the right place; on a 64-bit PC it doesn't. */
static struct
{
    uint8 sub_screen[PITCH * (RENDER_LINES + 2 * GUARD_LINES)];
    uint8 zbuffer[PITCH / 2 * (RENDER_LINES + 2 * GUARD_LINES)];
    uint8 sub_zbuffer[PITCH / 2 * (RENDER_LINES + 2 * GUARD_LINES)];
} full __attribute__ ((aligned (32)));

static int picture_top = (SCREEN_H - SNES_HEIGHT) / 2;
static int depth_lines;   /* lines from the top of the depth buffer */
static int picture_height = SNES_HEIGHT;
static unsigned screens_to_clear = ~0u;   /* one bit per screen buffer */

static char rom_path[512];
static char game_title[ROM_NAME_LEN + 1];
static int current_slot = 1;

static uint32 joypad;
static int pending;
static bool fast_forward;
static enum Pace pace = PACE_NORMAL;
static bool speed_test_running;

/* Frame pacing. The schedule is kept in 16.16 fixed-point ticks. */
static uint32_t frame_period;
static uint32_t next_frame, next_frame_frac;
static uint32_t skipped_frames, manual_counter, fast_forward_counter;

/* FPS overlay, measured like lr-gpsp-nspire: frames drawn per second. */
static uint32_t fps_start, fps_frames, fps_shown;
static bool fps_started;

/* Where the time goes, in timer ticks since 'start'. The SNES CPU and chips
 * get whatever isn't drawing, showing the frame or waiting. */
struct Perf
{
    uint32_t start, frames, drawn, render, output, idle;
};

/* Per emulated frame, in tenths: frames per second and milliseconds. */
struct PerfStats
{
    uint32_t game_fps, drawn_fps, cpu_ms, render_ms, output_ms, idle_ms;
};

static struct Perf perf;          /* running totals */
static struct Perf perf_window;   /* totals when the detailed line was last updated */
static char perf_line[96];

/* With the detailed FPS display on, a line per second is kept and appended
 * to pocketsnes_perf.txt.tns when the menu is opened or the game left (the
 * calculator only takes .tns files over USB). */
#define PERF_LOG_LINES 600
static char perf_log[PERF_LOG_LINES][128];
static int perf_log_count;

static char message[64];
static uint32_t message_until;
static bool message_visible;

static bool sram_dirty;
static uint32_t sram_dirty_tick;

/* The clock the game runs at: measured after every change (platform.h),
 * shown in a message when it changes, and at the start of a game when it is
 * raised. A raised speed the clock didn't reach isn't tried again until the
 * setting changes. */
static uint32_t game_mhz;
static int shown_multiplier = -1;   /* the speed last shown in this game; -1: none yet */
static int failed_multiplier;

static void game_speed(void);
static void normal_speed(void);
static void left_game(void);

/* Every screen buffer gets cleared before the game draws into it again. */
static void clear_screens_later(void)
{
    screens_to_clear = ~0u;
}

static void perf_reset(void)
{
    memset(&perf, 0, sizeof(perf));
    perf.start = platform_ticks();
    perf_window = perf;
}

static struct PerfStats perf_stats(const struct Perf *since, uint32_t now)
{
    struct PerfStats s;
    uint64_t hz = platform_tick_hz();
    uint32_t elapsed = now - since->start;
    uint32_t frames = perf.frames - since->frames;
    uint32_t render = perf.render - since->render;
    uint32_t output = perf.output - since->output;
    uint32_t idle = perf.idle - since->idle;
    uint32_t cpu = elapsed > render + output + idle ? elapsed - render - output - idle : 0;
    uint64_t per_frame = hz * (frames ? frames : 1);

    s.game_fps = elapsed ? (uint32_t) (frames * hz * 10 / elapsed) : 0;
    s.drawn_fps = elapsed ? (uint32_t) ((perf.drawn - since->drawn) * hz * 10 / elapsed) : 0;
    s.cpu_ms = (uint32_t) (cpu * 10000ull / per_frame);
    s.render_ms = (uint32_t) (render * 10000ull / per_frame);
    s.output_ms = (uint32_t) (output * 10000ull / per_frame);
    s.idle_ms = (uint32_t) (idle * 10000ull / per_frame);
    return s;
}

#define TENTHS(v) (unsigned) ((v) / 10), (unsigned) ((v) % 10)

static void perf_log_flush(void)
{
    char path[600];

    if (!perf_log_count)
        return;
    paths_data_file(path, sizeof(path), "pocketsnes_perf.txt.tns");
    FILE *f = fopen(path, "a");
    if (f)
    {
        fprintf(f, "== %s (build %s), every second:\n", game_title, BUILD_NAME);
        for (int i = 0; i < perf_log_count; i++)
            fprintf(f, "%s\n", perf_log[i]);
        fclose(f);
    }
    perf_log_count = 0;
}

/* Updates the detailed FPS line once a second. */
static void perf_tick(void)
{
    uint32_t now = platform_ticks();

    if (now - perf_window.start < platform_tick_hz())
        return;

    struct PerfStats s = perf_stats(&perf_window, now);
    snprintf(perf_line, sizeof(perf_line), "game %u.%u fps  cpu %u.%u gfx %u.%u out %u.%u idle %u.%u",
             TENTHS(s.game_fps), TENTHS(s.cpu_ms), TENTHS(s.render_ms), TENTHS(s.output_ms),
             TENTHS(s.idle_ms));
    if (cfg.show_fps == 2 && !speed_test_running && perf_log_count < PERF_LOG_LINES)
        snprintf(perf_log[perf_log_count++], sizeof(perf_log[0]),
                 "game %u.%u fps, drawn %u.%u fps; per frame: cpu %u.%u, gfx %u.%u, out %u.%u, idle %u.%u ms",
                 TENTHS(s.game_fps), TENTHS(s.drawn_fps), TENTHS(s.cpu_ms), TENTHS(s.render_ms),
                 TENTHS(s.output_ms), TENTHS(s.idle_ms));
    perf_window = perf;
    perf_window.start = now;
}

static void reset_timing(void)
{
    uint32_t frame_us = Settings.PAL ? Settings.FrameTimePAL : Settings.FrameTimeNTSC;
    frame_period = (uint32_t) (((uint64_t) platform_tick_hz() << 16) * frame_us / 1000000);
    next_frame = platform_ticks();
    next_frame_frac = 0;
    skipped_frames = 0;
    fps_started = false;
    perf_reset();
}

/* The second number of the FPS overlay: how many frames a second should be
 * drawn at full speed with the current frameskip (lr-gpsp-nspire's formula). */
static uint32_t fps_expected(void)
{
    uint32_t per_100s = Settings.PAL ? 5000 : 6010;
    uint32_t n = 0;

    if (fast_forward)
        n = cfg.frameskip_type == FRAMESKIP_OFF ? 0 : FAST_FORWARD_DRAW_EVERY - 1;
    else if (cfg.frameskip_type == FRAMESKIP_MANUAL)
        n = cfg.frameskip_value;
    return (per_100s + 50 * (n + 1)) / (100 * (n + 1));
}

void emu_show_message(const char *text)
{
    snprintf(message, sizeof(message), "%s", text);
    message_until = platform_ticks() + platform_tick_hz() * 2;
    message_visible = true;
    clear_screens_later();

    char line[sizeof(message) + 16];
    snprintf(line, sizeof(line), "message: %s", message);
    platform_log(line);
}

/* Adds to the message on screen if there's room on its line, or shows a new
 * one. */
static void add_message(const char *text)
{
    size_t both = strlen(message) + 2 + strlen(text);
    if (message_visible && (int32_t) (message_until - platform_ticks()) > 0 &&
        both < sizeof(message) && both <= SCREEN_W / TEXT_W - 1)
    {
        char line[sizeof(message)];
        snprintf(line, sizeof(line), "%s, %s", message, text);
        emu_show_message(line);
    }
    else
        emu_show_message(text);
}

/* 'format' has a %s for the slot: "slot 012" or "the auto slot", or with a
 * capital when 'cap' is set. */
static void show_slot_message(const char *format, int slot, bool cap = false)
{
    char name[32], text[64];
    if (cap)
        states_slot_name_cap(slot, name, sizeof(name));
    else
        states_slot_name(slot, name, sizeof(name));
    snprintf(text, sizeof(text), format, name);
    emu_show_message(text);
}

/* ---- Snes9x port interface ---- */

bool JustifierOffscreen (void)
{
    return true;
}

void JustifierButtons (uint32 &)
{
}

void S9xProcessSound (unsigned int)
{
}

/* Windows-style path helpers the core uses while loading ROMs. */
void _makepath (char *path, const char *, const char *dir, const char *fname, const char *ext)
{
    if (dir && *dir)
    {
        strcpy (path, dir);
        strcat (path, "/");
    }
    else
        *path = 0;
    strcat (path, fname);
    if (ext && *ext)
    {
        strcat (path, ".");
        strcat (path, ext);
    }
}

void _splitpath (const char *path, char *drive, char *dir, char *fname, char *ext)
{
    const char *slash = strrchr (path, '/');
    const char *dot = strrchr (path, '.');

    *drive = 0;
    if (dot && slash && dot < slash)
        dot = NULL;

    if (slash)
    {
        memcpy (dir, path, slash - path);
        dir [slash - path] = 0;
        path = slash + 1;
    }
    else
        *dir = 0;

    if (dot)
    {
        memcpy (fname, path, dot - path);
        fname [dot - path] = 0;
        strcpy (ext, dot + 1);
    }
    else
    {
        strcpy (fname, path);
        *ext = 0;
    }
}

extern "C"
{

void S9xExit ()
{
}

void S9xGenerateSound (void)
{
}

void S9xSetPalette ()
{
}

void S9xExtraUsage ()
{
}

void S9xParseArg (char **, int &, int)
{
}

void S9xLoadSDD1Data (void)
{
}

uint32 S9xPerfTicks (void)
{
    return platform_ticks();
}

void S9xPerfRenderDone (uint32 start)
{
    perf.render += platform_ticks() - start;
}

void S9xBeforeDrawingLines (uint32 last_line)
{
    platform_wait_lines(picture_top + (int) last_line + SPILL);
}

/* Called before the core draws a frame: point it into the next screen. */
bool8 S9xInitUpdate ()
{
    int height = PPU.ScreenHeight < SCREEN_H ? PPU.ScreenHeight : SCREEN_H;
    if (height != picture_height)
    {
        picture_height = height;
        picture_top = (SCREEN_H - height) / 2;
        clear_screens_later();
    }

    unsigned screen_bit = 1u << platform_screen_index();
    if (screens_to_clear & screen_bit)
    {
        draw_fill(COLOR_BLACK);
        screens_to_clear &= ~screen_bit;
    }

    GFX.Screen = (uint8 *) (platform_screen_nowait() + picture_top * SCREEN_W + PICTURE_LEFT);
    /* How far down the renderer may draw (interlaced lines go past the
     * picture): the screen, its depth buffer and the sub screen buffers. */
    uint32 rows = SCREEN_H - picture_top + platform_screen_spare_lines();
    if (rows > (uint32) depth_lines)
        rows = depth_lines;
    if (rows > RENDER_LINES + GUARD_LINES)
        rows = RENDER_LINES + GUARD_LINES;
    GFX.RenderRows = rows;
    return TRUE;
}

/* Text on a dark plate over the frame, put in the frame being shown. The
 * plate is opaque: the border around the picture isn't redrawn every frame. */
static void overlay_text(const char *text, int x, int y, uint16_t color)
{
    int w = text_width(FONT_SMALL, text) + 6;
    draw_round_rect(x, y, w, 14, 3, COLOR_PANEL);
    draw_string(FONT_SMALL, text, x + 3, y + 1, color);
    platform_frame_copy(x, y, w, 14);
}

/* Called when the frame is drawn: add the overlays and show it. Only what
 * changed goes into the frame shown (platform_frame_begin): the picture and
 * the text over it, or everything when that frame buffer is out of date. */
bool8 S9xDeinitUpdate (int, int, bool8)
{
    uint32_t start = platform_ticks();

    draw_rect(PICTURE_LEFT - SPILL, picture_top, SPILL, picture_height, COLOR_BLACK);
    draw_rect(PICTURE_LEFT + SNES_WIDTH, picture_top, SPILL, picture_height, COLOR_BLACK);
    draw_rect(0, picture_top + picture_height, SCREEN_W, SPILL, COLOR_BLACK);

    if (message_visible && (int32_t) (start - message_until) >= 0)
    {
        message_visible = false;
        clear_screens_later();
    }

    /* Counts the time between drawn frames, starting from the first one. */
    uint32_t elapsed = start - fps_start;
    if (!fps_started)
    {
        fps_started = true;
        fps_start = start;
        fps_frames = 0;
    }
    else
    {
        fps_frames++;
        if (elapsed >= platform_tick_hz() / 2)
        {
            fps_shown = (uint32_t) (((uint64_t) fps_frames * platform_tick_hz() + elapsed / 2) / elapsed);
            fps_start = start;
            fps_frames = 0;
        }
    }

    /* A frame buffer that is out of date gets everything, but only once
     * platform_screen() has been cleared too: a message that just timed out
     * is still in it until the next frame. */
    unsigned frame_bit = 1u << platform_frame_begin();
    unsigned screen_bit = 1u << platform_screen_index();
    if ((screens_to_clear & frame_bit) && !(screens_to_clear & screen_bit))
    {
        platform_frame_copy(0, 0, SCREEN_W, SCREEN_H);
        screens_to_clear &= ~frame_bit;
    }
    else
        platform_frame_copy(PICTURE_LEFT, picture_top, SNES_WIDTH, picture_height);

    if (cfg.show_fps)
    {
        /* Green at full speed, amber below, red below three quarters. */
        char text[24];
        uint32_t expected = fps_expected();
        snprintf(text, sizeof(text), "%u/%u", (unsigned) fps_shown, (unsigned) expected);
        overlay_text(text, 0, 1, fps_shown + 1 >= expected ? COLOR_GOOD
                                 : fps_shown * 4 >= expected * 3 ? COLOR_WARN : COLOR_BAD);
        if (cfg.show_fps == 2 && perf_line[0])
            overlay_text(perf_line, 0, 16, COLOR_TEXT);
    }
    if (message_visible)
    {
        int rect[4];
        ui_toast(message, MESSAGE_Y, rect);
        platform_frame_copy(rect[0], rect[1], rect[2], rect[3]);
    }

    platform_frame_present();
    perf.drawn++;
    perf.output += platform_ticks() - start;
    return TRUE;
}

/* The ROM's path with its extension replaced (the core looks for IPS patches
 * with this while loading). */
const char *S9xGetFilename (const char *extension)
{
    static char path[800];
    snprintf(path, sizeof(path), "%s", rom_path);
    char *slash = strrchr(path, '/');
    char *dot = strrchr(path, '.');
    if (dot && (!slash || dot > slash))
        *dot = 0;
    size_t len = strlen(path);
    snprintf(path + len, sizeof(path) - len, "%s", extension);
    return path;
}

uint32 S9xReadJoypad (int which)
{
    return which == 0 ? 0x80000000 | joypad : 0;
}

bool8 S9xReadMousePosition (int, int &, int &, uint32 &)
{
    return FALSE;
}

bool8 S9xReadSuperScopePosition (int &, int &, uint32 &)
{
    return FALSE;
}

const char *S9xGetFilenameInc (const char *extension)
{
    return extension;
}

const char *S9xBasename (const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* Called by the core about a second after the game writes to its battery
 * save. Games that keep writing would trigger it every second, so the file is
 * only written once the writes have stopped for a while (see emu_run). */
void S9xAutoSaveSRAM (void)
{
    sram_dirty = true;
    sram_dirty_tick = platform_ticks();
}

static bool key_is_bound(int key)
{
    for (int i = 0; i < NUM_ACTIONS; i++)
        for (int slot = 0; slot < KEY_SLOTS; slot++)
            if (cfg.keys[i][slot].key == key || cfg.keys[i][slot].with == key)
                return true;
    return false;
}

static bool held(int action)
{
    for (int slot = 0; slot < KEY_SLOTS; slot++)
        if (binding_held(cfg.keys[action][slot]))
            return true;
    return false;
}

/* Is the key part of a key combination bound to a hotkey and held now? */
static bool key_in_held_combo(int key)
{
    for (int i = FIRST_HOTKEY; i < NUM_ACTIONS; i++)
        for (int slot = 0; slot < KEY_SLOTS; slot++)
        {
            struct Binding b = cfg.keys[i][slot];
            if (b.with && (b.key == key || b.with == key) && binding_held(b))
                return true;
        }
    return false;
}

/* A hotkey fires when one of its bindings has just been pressed. A single
 * key that is part of a combination being held doesn't fire its own hotkey:
 * with S bound to Load state and shift+S to Save state, shift+S only saves. */
static bool hotkey_pressed(int action)
{
    for (int slot = 0; slot < KEY_SLOTS; slot++)
    {
        struct Binding b = cfg.keys[action][slot];
        if (binding_pressed(b) && (b.with || !key_in_held_combo(b.key)))
            return true;
    }
    return false;
}

static bool has_binding(int action)
{
    for (int slot = 0; slot < KEY_SLOTS; slot++)
        if (cfg.keys[action][slot].key)
            return true;
    return false;
}

static void read_joypad(void)
{
    bool up = held(ACTION_UP) || platform_key_down(KEY_UP);
    bool down = held(ACTION_DOWN) || platform_key_down(KEY_DOWN);
    bool left = held(ACTION_LEFT) || platform_key_down(KEY_LEFT);
    bool right = held(ACTION_RIGHT) || platform_key_down(KEY_RIGHT);

    /* Number pad diagonals from PocketSNES 2.0, unless bound to something else. */
    if (platform_key_down(KEY_7) && !key_is_bound(KEY_7)) up = left = true;
    if (platform_key_down(KEY_9) && !key_is_bound(KEY_9)) up = right = true;
    if (platform_key_down(KEY_1) && !key_is_bound(KEY_1)) down = left = true;
    if (platform_key_down(KEY_3) && !key_is_bound(KEY_3)) down = right = true;
    if (up && down)
        up = down = false;
    if (left && right)
        left = right = false;

    uint32 buttons = 0;
    if (up)    buttons |= SNES_UP_MASK;
    if (down)  buttons |= SNES_DOWN_MASK;
    if (left)  buttons |= SNES_LEFT_MASK;
    if (right) buttons |= SNES_RIGHT_MASK;
    if (held(ACTION_A)) buttons |= SNES_A_MASK;
    if (held(ACTION_B)) buttons |= SNES_B_MASK;
    if (held(ACTION_X)) buttons |= SNES_X_MASK;
    if (held(ACTION_Y)) buttons |= SNES_Y_MASK;
    if (held(ACTION_L)) buttons |= SNES_TL_MASK;
    if (held(ACTION_R)) buttons |= SNES_TR_MASK;
    if (held(ACTION_START)) buttons |= SNES_START_MASK;
    if (held(ACTION_SELECT)) buttons |= SNES_SELECT_MASK;
    joypad = buttons;
}

static void change_slot(int delta)
{
    current_slot = emu_step_slot(current_slot, delta);
    show_slot_message(states_exists(current_slot) ? "%s (saved)" : "%s (empty)", current_slot, true);
}

static void read_hotkeys(void)
{
    /* Without a key for the menu, esc opens it, so it can't get lost. */
    if (hotkey_pressed(ACTION_MENU) || (!has_binding(ACTION_MENU) && platform_key_pressed(KEY_ESC)))
        pending |= PENDING_MENU;
    if (hotkey_pressed(ACTION_QUIT) || platform_quit_requested())
        pending |= PENDING_QUIT;
    if (hotkey_pressed(ACTION_SAVE_STATE))
        pending |= PENDING_SAVE;
    if (hotkey_pressed(ACTION_LOAD_STATE))
        pending |= PENDING_LOAD;
    if (hotkey_pressed(ACTION_LOAD_NEWEST))
        pending |= PENDING_LOAD_NEWEST;
    if (hotkey_pressed(ACTION_RESTART))
        pending |= PENDING_RESTART;
    if (hotkey_pressed(ACTION_NEXT_SLOT))
        change_slot(1);
    if (hotkey_pressed(ACTION_PREVIOUS_SLOT))
        change_slot(-1);
    if (hotkey_pressed(ACTION_SHOW_FPS))
    {
        cfg.show_fps = cfg.show_fps ? 0 : 1;
        emu_show_message(cfg.show_fps ? "FPS counter on" : "FPS counter off");
    }
    if (hotkey_pressed(ACTION_FAST_FORWARD))
    {
        fast_forward = !fast_forward;
        emu_show_message(fast_forward ? "Fast forward on" : "Fast forward off");
        reset_timing();
    }
    bool brighter = hotkey_pressed(ACTION_BRIGHTER), dimmer = hotkey_pressed(ACTION_DIMMER);
    if (brighter != dimmer)
    {
        int level = brightness_step(brighter ? 1 : -1);
        char text[32];
        if (level)
            snprintf(text, sizeof(text), "Brightness %d/%d", level, BRIGHTNESS_LEVELS);
        else
            snprintf(text, sizeof(text), "Brightness: CX II only");
        emu_show_message(text);
    }
}

/* Decides whether the next frame is drawn and waits until it is due. */
static void pace_frame(void)
{
    bool draw;

    if (pace != PACE_NORMAL || fast_forward)
    {
        if (pace == PACE_UNCAPPED_DRAW_ALL)
            draw = true;
        else if (pace == PACE_UNCAPPED_DRAW_NONE)
            draw = false;
        else if (cfg.frameskip_type == FRAMESKIP_OFF)
            draw = true;
        else
        {
            draw = fast_forward_counter == 0;
            fast_forward_counter = (fast_forward_counter + 1) % FAST_FORWARD_DRAW_EVERY;
        }
        next_frame = platform_ticks();
        next_frame_frac = 0;
        IPPU.RenderThisFrame = draw;
        return;
    }

    next_frame_frac += frame_period;
    next_frame += next_frame_frac >> 16;
    next_frame_frac &= 0xFFFF;

    int32_t late = (int32_t) (platform_ticks() - next_frame);
    int32_t period = (int32_t) (frame_period >> 16);

    switch (cfg.frameskip_type)
    {
    case FRAMESKIP_AUTO:
        draw = late <= 0 || skipped_frames >= cfg.frameskip_value;
        break;
    case FRAMESKIP_MANUAL:
        draw = manual_counter == 0;
        manual_counter = (manual_counter + 1) % (cfg.frameskip_value + 1);
        break;
    default:
        draw = true;
        break;
    }
    skipped_frames = draw ? 0 : skipped_frames + 1;

    if (late < 0)
    {
        uint32_t wait_start = platform_ticks();
        platform_wait_until(next_frame);
        perf.idle += platform_ticks() - wait_start;
    }
    else if (late > period * 6)
    {
        /* Too far behind to catch up; carry on from now instead. */
        next_frame = platform_ticks();
        next_frame_frac = 0;
    }
    IPPU.RenderThisFrame = draw;
}

/* Called by the core once per emulated frame, between frames. */
void S9xSyncSpeed (void)
{
    perf.frames++;
    brightness_update();   /* a game's own settings can have their own */
    platform_poll_keys();
    if (speed_test_running)
    {
        joypad = 0;
        if (platform_key_pressed(KEY_ESC) || platform_quit_requested())
            pending |= PENDING_MENU;
    }
    else
    {
        read_joypad();
        read_hotkeys();
    }
    pace_frame();
    perf_tick();
}

}  /* extern "C" */

/* ---- Frontend ---- */

int emu_init(void)
{
    ZeroMemory (&Settings, sizeof (Settings));

    Settings.JoystickEnabled = FALSE;
    Settings.SoundPlaybackRate = 0;
    Settings.Stereo = FALSE;
    Settings.SoundBufferSize = 0;
    Settings.CyclesPercentage = 100;
    Settings.DisableSoundEcho = TRUE;
    Settings.H_Max = SNES_CYCLES_PER_SCANLINE;
    Settings.SkipFrames = AUTO_FRAMERATE;
    Settings.Shutdown = Settings.ShutdownMaster = TRUE;
    Settings.FrameTimePAL = 20000;
    Settings.FrameTimeNTSC = 16667;
    Settings.FrameTime = Settings.FrameTimeNTSC;
    Settings.DisableMasterVolume = TRUE;
    Settings.Mouse = FALSE;
    Settings.SuperScope = FALSE;
    Settings.MultiPlayer5 = FALSE;
    Settings.ControllerOption = 0;
    Settings.InterpolatedSound = TRUE;
    Settings.StarfoxHack = TRUE;
    Settings.ForceTransparency = FALSE;
    Settings.Transparency = TRUE;
    Settings.SupportHiRes = FALSE;
    Settings.NetPlay = FALSE;
    Settings.ServerName [0] = 0;
    Settings.AutoSaveDelay = 1;
    Settings.ApplyCheats = FALSE;
    Settings.TurboMode = FALSE;
    Settings.TurboSkipFrames = 15;
    Settings.ThreadSound = FALSE;
    Settings.SoundSync = 1;
    Settings.FixFrequency = FALSE;
    Settings.SuperFX = TRUE;
    Settings.DSP1Master = TRUE;
    Settings.SA1 = TRUE;
    Settings.C4 = TRUE;
    Settings.SDD1 = TRUE;

    GFX.RealPitch = GFX.Pitch = PITCH;
    GFX.Screen = (uint8 *) (platform_screen() + picture_top * SCREEN_W + PICTURE_LEFT);
    GFX.SubScreen = full.sub_screen + PITCH * GUARD_LINES;
    /* The main screen's depth buffer goes in faster memory if there is some. */
    GFX.ZBuffer = platform_fast_depth_buffer(&depth_lines);
    if (!GFX.ZBuffer)
    {
        GFX.ZBuffer = full.zbuffer + PITCH / 2 * GUARD_LINES;
        depth_lines = RENDER_LINES + GUARD_LINES;
    }
    GFX.SubZBuffer = full.sub_zbuffer + PITCH / 2 * GUARD_LINES;
    GFX.Delta = (GFX.SubScreen - GFX.Screen) >> 1;
    GFX.PPL = GFX.Pitch >> 1;
    GFX.PPLx2 = GFX.Pitch;
    GFX.ZPitch = GFX.Pitch >> 1;

    Settings.HBlankStart = (256 * Settings.H_Max) / SNES_HCOUNTER_MAX;

    if (!Memory.Init () || !S9xGraphicsInit ())
    {
        emu_deinit();
        return 0;
    }
    return 1;
}

void emu_deinit(void)
{
    S9xGraphicsDeinit ();
    Memory.Deinit ();
}

static void write_sram(void)
{
    char path[800];

    sram_dirty = false;
    if (!Memory.SRAMSize && !Settings.SRTC)
        return;
    states_make_dir();
    states_sram_path(path, sizeof(path));
    Memory.SaveSRAM (path);
}

/* Why the last ROM didn't load, for the message. */
static const char *load_error = "";

const char *emu_load_error(void)
{
    return load_error;
}

/* Whether a file can be a ROM, before the core sees it: it crashes on files
 * under 32 KB, and the game list shows every .tns file, documents and
 * programs too. */
static bool looks_like_rom(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        load_error = "The file can't be opened.";
        return false;
    }
    char head[8] = { 0 };
    size_t got = fread(head, 1, sizeof(head), f);
    fseek(f, 0, SEEK_END);
    long bytes = ftell(f);
    fclose(f);

    char inner[256];
    if (got >= 4 && memcmp(head, "PK\3\4", 4) == 0)
    {
        if (romzip_rom_name(path, inner, sizeof(inner)))
            return true;
        load_error = "It's a zip with no SNES ROM in it.";
    }
    else if (got >= 6 && memcmp(head, "*TIMLP", 6) == 0)
        load_error = "It's a TI-Nspire document, not a SNES ROM.";
    else if (got >= 4 && memcmp(head, "PRG", 4) == 0)
        load_error = "It's a program, not a SNES ROM.";
    else if (bytes < 0x8000)
        load_error = "It's too small to be a SNES ROM.";
    else if (bytes > 0x800000 + 0x200)
        load_error = "It's bigger than 8 MB, the most a SNES ROM can be.";
    else
        return true;
    return false;
}

int emu_load_game(const char *path)
{
    char sram_path[800];

    snprintf(rom_path, sizeof(rom_path), "%s", path);
    if (!looks_like_rom(rom_path))
        return 0;
    load_error = "Is it a SNES ROM? It may also be too big for the memory free.";

    ui_frame(NULL, NULL);
    draw_round_rect(30, 92, 260, 50, 7, COLOR_PANEL);
    draw_round_frame(30, 92, 260, 50, 7, COLOR_PANEL_EDGE);
    draw_icon(ICON_CART, 44, 104, COLOR_SELECT_EDGE);
    draw_string(FONT_MEDIUM, "Loading", 64, 100, COLOR_TEXT);
    draw_string_fit(FONT_SMALL, S9xBasename(rom_path), 64, 120, 216, COLOR_TEXT_DIM);
    platform_present();

    if (!Memory.LoadROM (rom_path))
        return 0;
    S9xReset ();

    states_open_game(rom_path);
    config_open_game(rom_path);
    states_sram_path(sram_path, sizeof(sram_path));
    Memory.LoadSRAM (sram_path);
    sram_dirty = false;

    snprintf(game_title, sizeof(game_title), "%s", Memory.ROMName);
    for (int i = (int) strlen(game_title) - 1; i >= 0 && game_title[i] == ' '; i--)
        game_title[i] = 0;

    current_slot = states_newest() ? states_newest() : 1;
    return 1;
}

void emu_close_game(void)
{
    perf_log_flush();
    write_sram();
    config_save();
    config_close_game();
}

const char *emu_rom_path(void)
{
    return rom_path;
}

const char *emu_game_title(void)
{
    return game_title;
}

int emu_slot(void)
{
    return current_slot;
}

void emu_set_slot(int slot)
{
    current_slot = slot;
}

int emu_save_target(void)
{
    return cfg.auto_increment ? states_next_new_slot() : current_slot;
}

int emu_step_slot(int slot, int delta)
{
    /* Positions: the auto slot (when it's in use) at 0, then 1-999. */
    int with_auto = cfg.auto_slot || states_exists(AUTO_SLOT);
    int count = MAX_STATE_SLOTS + with_auto;
    int index = slot == AUTO_SLOT ? 0 : slot - 1 + with_auto;
    index = ((index + delta) % count + count) % count;
    if (with_auto)
        return index == 0 ? AUTO_SLOT : index;
    return index + 1;
}

/* Where the save made when leaving goes (Saving): the auto slot, a new slot,
 * or over the newest state. 0 when every slot is used. */
static int leaving_save_target(void)
{
    if (cfg.auto_slot)
        return AUTO_SLOT;
    if (!cfg.quit_new_save && states_newest())
        return states_newest();
    return states_next_new_slot();
}

int emu_save_state(int slot)
{
    if (!states_save(slot))
        return 0;
    current_slot = slot;
    return 1;
}

int emu_load_state(int slot)
{
    int result = states_load(slot);
    if (result == 1)
    {
        current_slot = slot;
        clear_screens_later();
    }
    return result;
}

void emu_reset_game(void)
{
    S9xReset ();
    clear_screens_later();
}

/* ---- Speed test ---- */

struct SpeedTestPart
{
    const char *name;
    const char *short_name;   /* for the results screen */
    enum Pace pace;
    uint32_t frameskip_type, frameskip_value;
    uint32_t seconds;
    int output;   /* enum ScreenOutput */
    int cpu_core; /* enum CpuCore, or -1 for the one set */
};

/* The last two compare the CPU cores with nothing drawn, where the time
 * per frame is all running the game. */
static const struct SpeedTestPart speed_test_parts[] =
{
    { "Normal play, frameskip off", "Normal play", PACE_NORMAL, FRAMESKIP_OFF, 0, 10, OUTPUT_BEST, -1 },
    { "No speed limit, every frame drawn", "No limit", PACE_UNCAPPED_DRAW_ALL, FRAMESKIP_OFF, 0, 5, OUTPUT_BEST, -1 },
    { "Same, other output", "No limit, DMA", PACE_UNCAPPED_DRAW_ALL, FRAMESKIP_OFF, 0, 5, OUTPUT_DMA, -1 },
    { "Same, other output", "No limit, flip", PACE_UNCAPPED_DRAW_ALL, FRAMESKIP_OFF, 0, 5, OUTPUT_FLIP, -1 },
    { "No speed limit, nothing drawn, ARM CPU core", "Undrawn, ARM", PACE_UNCAPPED_DRAW_NONE, FRAMESKIP_OFF, 0, 5,
      OUTPUT_BEST, CPU_CORE_ARM },
    { "No speed limit, nothing drawn, C CPU core", "Undrawn, C", PACE_UNCAPPED_DRAW_NONE, FRAMESKIP_OFF, 0, 5,
      OUTPUT_BEST, CPU_CORE_C },
};

static const char *const cpu_core_names[NUM_CPU_CORES] = { "ARM", "C" };

/* Runs the game on the CPU core set (core < 0) or on the one given. The C
 * core runs SA-1 games anyway (S9xMainLoop), and is the only one in the PC
 * build. */
static void use_cpu_core(int core)
{
    Settings.AsmCPU = (core < 0 ? (int) cfg.cpu_core : core) == CPU_CORE_ARM;
}

#define SPEED_TEST_PARTS (int) (sizeof(speed_test_parts) / sizeof(speed_test_parts[0]))

void emu_speed_test(int close_after_seconds)
{
    uint8 *snapshot;
    uint32 snapshot_size;
    char results[SPEED_TEST_PARTS][2][128];
    struct PerfStats stats[SPEED_TEST_PARTS];
    int supported[SPEED_TEST_PARTS];
    int done = 0;

    /* At the speed the game runs at (the menu runs at normal speed). */
    game_speed();
    uint32_t mhz = platform_cpu_mhz();
    char clock_report[200];
    platform_clock_report(clock_report, sizeof(clock_report));

    if (!S9xFreezeToMemory(&snapshot, &snapshot_size))
    {
        gui_message("Not enough memory for the speed test.", NULL);
        return;
    }

    struct Config saved_cfg = cfg;
    bool saved_fast_forward = fast_forward;
    fast_forward = false;
    speed_test_running = true;
    pending = 0;

    for (int i = 0; i < SPEED_TEST_PARTS && !(pending & PENDING_MENU); i++)
    {
        const struct SpeedTestPart *part = &speed_test_parts[i];
        char note[64];

        S9xUnfreezeFromMemory(snapshot, snapshot_size);
        cfg.frameskip_type = part->frameskip_type;
        cfg.frameskip_value = part->frameskip_value;
        cfg.show_fps = 2;
        pace = part->pace;
        use_cpu_core(part->cpu_core);
        int output_supported = platform_set_screen_output(part->output);
        snprintf(note, sizeof(note), "Speed test %d/%d (esc stops it)", i + 1, SPEED_TEST_PARTS);
        emu_show_message(note);
        reset_timing();

        struct Perf base = perf;
        uint32_t length = part->seconds * platform_tick_hz();
        while (output_supported && platform_ticks() - base.start < length &&
               perf.frames - base.frames < part->seconds * 300 && !(pending & PENDING_MENU))
            S9xMainLoop ();

        struct PerfStats s = perf_stats(&base, platform_ticks());
        stats[i] = s;
        supported[i] = output_supported;
        const char *output_name = platform_screen_mode_name();
        platform_set_screen_output(OUTPUT_BEST);
        snprintf(results[i][0], sizeof(results[i][0]), "%s [%s]:", part->name, output_name);
        if (!output_supported)
            snprintf(results[i][1], sizeof(results[i][1]), "  (not used on this screen)");
        else
            snprintf(results[i][1], sizeof(results[i][1]),
                     "  game %u.%u  drawn %u.%u  cpu %u.%u gfx %u.%u out %u.%u",
                     TENTHS(s.game_fps), TENTHS(s.drawn_fps), TENTHS(s.cpu_ms), TENTHS(s.render_ms),
                     TENTHS(s.output_ms));
        done = i + 1;
    }

    pace = PACE_NORMAL;
    cfg = saved_cfg;
    use_cpu_core(-1);
    fast_forward = saved_fast_forward;
    speed_test_running = false;
    pending = 0;
    S9xUnfreezeFromMemory(snapshot, snapshot_size);
    free(snapshot);
    clear_screens_later();
    normal_speed();
    shown_multiplier = -1;   /* the game shows its clock again when it resumes */

    /* A new file each run: appending to the old one has left it garbled on
     * the calculator now and then. */
    char path[600];
    paths_data_file(path, sizeof(path), "pocketsnes_results.txt.tns");
    FILE *f = fopen(path, "w");
    if (f)
    {
        fprintf(f, "== %s, build %s, cpu %u MHz, screen: %s, CPU core set: %s%s\n%s\n", game_title,
                BUILD_NAME, (unsigned) mhz, platform_screen_mode_name(), cpu_core_names[cfg.cpu_core],
                Settings.SA1 ? " (SA-1 game: C core)" : "", clock_report);
        for (int i = 0; i < done; i++)
            fprintf(f, "%s\n%s\n", results[i][0], results[i][1]);
        fclose(f);
    }

    /* Results screen; it closes by itself when close_after_seconds is set. */
    uint32_t shown_at = platform_ticks();
    gui_wait_release();
    for (;;)
    {
        char right[32] = "";
        if (mhz)
            snprintf(right, sizeof(right), "CPU %u MHz", (unsigned) mhz);
        ui_frame("Speed test", right[0] ? right : NULL);

        /* fps, then milliseconds per frame running the game, drawing it, showing it. */
        static const char *const columns[] = { "fps", "game", "draw", "show" };
        static const int column_right[] = { 152, 202, 252, 302 };
        draw_round_rect(5, UI_LIST_Y, 310, 18 + SPEED_TEST_PARTS * 18, 6, COLOR_PANEL);
        draw_round_frame(5, UI_LIST_Y, 310, 18 + SPEED_TEST_PARTS * 18, 6, COLOR_PANEL_EDGE);
        for (int c = 0; c < 4; c++)
            draw_string_right(FONT_SMALL, columns[c], column_right[c], UI_LIST_Y + 4, COLOR_TEXT_FAINT);
        for (int i = 0; i < done; i++)
        {
            int y = UI_LIST_Y + 20 + i * 18;
            draw_rect(12, y - 3, 296, 1, COLOR_PANEL_EDGE);
            draw_string(FONT_SMALL, speed_test_parts[i].short_name, 14, y, COLOR_TEXT);
            if (!supported[i])
            {
                draw_string_right(FONT_SMALL, "not on this screen", 302, y, COLOR_TEXT_FAINT);
                continue;
            }
            const uint32_t values[4] = { stats[i].game_fps, stats[i].cpu_ms, stats[i].render_ms, stats[i].output_ms };
            for (int c = 0; c < 4; c++)
            {
                char number[16];
                snprintf(number, sizeof(number), "%u.%u", TENTHS(values[c]));
                uint16_t color = COLOR_TEXT_DIM;
                if (c == 0)
                    color = stats[i].game_fps >= 595 ? COLOR_GOOD : stats[i].game_fps >= 450 ? COLOR_WARN : COLOR_BAD;
                draw_string_right(FONT_MEDIUM, number, column_right[c], y - 3, color);
            }
        }
        ui_help("60 fps is full speed. game/draw/show: ms per frame. Saved to pocketsnes_results.txt.");
        static const struct UiHint close_hint[] = { { "any key", "Close" } };
        static const struct UiHint closing[] = { { "wait", "Closing by itself" } };
        ui_footer(close_after_seconds ? closing : close_hint, 1);
        gui_present();
        platform_poll_keys();
        if (platform_any_key_down() || platform_quit_requested())
            break;
        if (close_after_seconds &&
            platform_ticks() - shown_at > (uint32_t) close_after_seconds * platform_tick_hz())
            break;
    }
    gui_wait_release();
    reset_timing();
}

void emu_benchmark(void)
{
    clear_screens_later();
    gui_wait_release();
    reset_timing();
    pending = 0;

    use_cpu_core(-1);
    uint32_t start = platform_ticks();
    while (platform_ticks() - start < 5 * platform_tick_hz())
        S9xMainLoop ();

    char path[600];
    paths_data_file(path, sizeof(path), "pocketsnes_diag.txt.tns");
    platform_write_diagnostics(path);

    pending = 0;
    emu_speed_test(3);
    left_game();
    platform_write_memory_tests(path);
}

/* ---- The overclock test's game check ---- */

/* About 2 KB of fingerprints is plenty: the recording is a few seconds. */
#define CHECK_MAX_FRAMES 600

static uint8 *check_snapshot;
static uint32 check_snapshot_size;
static uint32_t check_prints[CHECK_MAX_FRAMES];
static int check_frames;
static bool check_saved_fast_forward, check_saved_sram_dirty;
/* What a snapshot leaves out but the game can depend on: some CPU state,
 * open bus, and the stand-in for the sound chip, which answers with rand()
 * (ppu.cpp); each run starts from these and the same rand() seed, or games
 * that talk to the sound chip (SMW) run differently every time. */
static struct SCPUState check_cpu;
static struct SICPU check_icpu;
static struct SIAPU check_iapu;
static uint8 check_open_bus;

static void check_restore(void)
{
    S9xUnfreezeFromMemory(check_snapshot, check_snapshot_size);
    CPU = check_cpu;
    ICPU = check_icpu;
    IAPU = check_iapu;
    OpenBus = check_open_bus;
}

static uint32_t print_words(uint32_t h, const void *data, size_t bytes)
{
    const uint32_t *w = (const uint32_t *) data;
    for (size_t i = 0; i < bytes / 4; i++)
        h = (h ^ w[i]) * 16777619u;
    return h;
}

/* What the game has in the SNES's work RAM, video RAM, palette and sprite
 * table: a game that ran differently has something different there. */
static uint32_t memory_print(void)
{
    uint32_t h = 2166136261u;
    h = print_words(h, Memory.RAM, 0x20000);
    h = print_words(h, Memory.VRAM, 0x10000);
    h = print_words(h, PPU.CGDATA, sizeof(PPU.CGDATA));
    h = print_words(h, PPU.OAMData, sizeof(PPU.OAMData));
    return platform_test_glitch() ? h ^ 1 : h;
}

/* Plays from the snapshot with no keys pressed and every frame drawn,
 * recording the fingerprints (until the time is up) or comparing with
 * them. 1 done (and the same), 0 different, -1 stopped. */
static int check_play(bool record, uint32_t seconds, const char *note)
{
    check_restore();
    srand(1);
    clear_screens_later();
    emu_show_message(note);
    reset_timing();
    pending = 0;

    uint32_t start = platform_ticks(), length = seconds * platform_tick_hz();
    int frames = record ? CHECK_MAX_FRAMES : check_frames;
    for (int i = 0; i < frames; i++)
    {
        if (record && platform_ticks() - start >= length)
        {
            frames = i;
            break;
        }
        S9xMainLoop ();
        if (pending & PENDING_MENU)
            return -1;
        uint32_t print = memory_print();
        if (record)
            check_prints[i] = print;
        else if (print != check_prints[i])
            return 0;
    }
    if (record)
        check_frames = frames;
    return 1;
}

int emu_game_check_record(uint32_t seconds, const char *note)
{
    if (!S9xFreezeToMemory(&check_snapshot, &check_snapshot_size))
        return 0;
    check_cpu = CPU;
    check_icpu = ICPU;
    check_iapu = IAPU;
    check_open_bus = OpenBus;
    check_saved_fast_forward = fast_forward;
    check_saved_sram_dirty = sram_dirty;
    fast_forward = false;
    speed_test_running = true;   /* no keys, and esc stops it */
    pace = PACE_UNCAPPED_DRAW_ALL;
    use_cpu_core(-1);
    return check_play(true, seconds, note);
}

int emu_game_check_replay(const char *note)
{
    return check_play(false, 0, note);
}

void emu_game_check_end(void)
{
    if (!check_snapshot)
        return;
    pace = PACE_NORMAL;
    fast_forward = check_saved_fast_forward;
    speed_test_running = false;
    pending = 0;
    check_restore();
    sram_dirty = check_saved_sram_dirty;
    free(check_snapshot);
    check_snapshot = NULL;
    clear_screens_later();
    shown_multiplier = -1;   /* the game shows its clock again when it resumes */
    reset_timing();
}

/* ---- CPU speed ---- */

/* A raised CPU speed (Graphics & speed) is only used while a game runs:
 * menus, the game list and file writes run at the calculator's own speed.
 * Too high a speed can freeze the calculator, so while a game runs raised, a
 * marker file names the speed and the game; leaving the game cleanly removes
 * it. If it is still there at the next start, that speed is put back to
 * normal (emu_check_cpu_speed_crash). */
#define CLOCK_MARKER "pocketsnes_clock.tns"

static bool clock_marker_written;

/* Within 2% of 12 MHz times the multiplier. */
static bool clock_reached(uint32_t mhz, int multiplier)
{
    uint32_t want = (uint32_t) multiplier * 12;
    return mhz * 50 >= want * 49 && mhz * 50 <= want * 51;
}

static void clock_marker_path(char *path, size_t size)
{
    paths_data_file(path, size, CLOCK_MARKER);
}

static void game_speed(void)
{
    int multiplier = config_cpu_multiplier(cfg.cpu_speed);
    if (multiplier != failed_multiplier)
        failed_multiplier = 0;
    if (multiplier && !clock_marker_written && multiplier != failed_multiplier)
    {
        char path[600];
        clock_marker_path(path, sizeof(path));
        FILE *f = fopen(path, "w");
        if (!f)
            multiplier = 0;   /* no marker, no raised speed */
        else
        {
            fprintf(f, "%d\n%s\n", multiplier, rom_path);
            fclose(f);
            clock_marker_written = true;
        }
    }

    char text[48] = "";
    if (multiplier && multiplier == failed_multiplier)
    {
        game_mhz = platform_set_cpu_multiplier(0);
        if (multiplier != shown_multiplier)
            snprintf(text, sizeof(text), "CPU speed not raised (still %u MHz)", (unsigned) game_mhz);
    }
    else
    {
        game_mhz = platform_set_cpu_multiplier(multiplier);
        if (multiplier && game_mhz && !clock_reached(game_mhz, multiplier))
        {
            /* The clock didn't get there: back to normal, and say so. */
            failed_multiplier = multiplier;
            game_mhz = platform_set_cpu_multiplier(0);
            snprintf(text, sizeof(text), "CPU speed not raised (still %u MHz)", (unsigned) game_mhz);
        }
        else if (multiplier != shown_multiplier && (multiplier || shown_multiplier > 0) && game_mhz)
            snprintf(text, sizeof(text), multiplier ? "CPU %u MHz" : "CPU %u MHz (normal)",
                     (unsigned) game_mhz);
    }
    if (text[0])
        add_message(text);
    shown_multiplier = multiplier;
    platform_set_game_frames_by_dma((int) cfg.screen_dma);
}

uint32_t emu_game_mhz(void)
{
    return game_mhz;
}

static void normal_speed(void)
{
    platform_set_cpu_multiplier(0);
}

/* Leaving the game cleanly: normal speed, and no marker. */
static void left_game(void)
{
    normal_speed();
    if (clock_marker_written)
    {
        char path[600];
        clock_marker_path(path, sizeof(path));
        remove(path);
        clock_marker_written = false;
    }
}

int emu_check_cpu_speed_crash(char *crashed_rom, size_t size)
{
    char path[600];
    clock_marker_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    int multiplier = 0;
    char line[600] = "";
    if (fscanf(f, "%d", &multiplier) != 1)
        multiplier = 0;
    if (fgets(line, sizeof(line), f) && fgets(line, sizeof(line), f))
        line[strcspn(line, "\r\n")] = 0;
    else
        line[0] = 0;
    fclose(f);
    remove(path);
    snprintf(crashed_rom, size, "%s", line);
    return multiplier > 0 ? multiplier : 33;
}

/* ---- Playing ---- */

static void save_state_hotkey(void)
{
    normal_speed();
    int slot = emu_save_target();
    if (!slot)
        emu_show_message("All 999 slots are used");
    else if (emu_save_state(slot))
        show_slot_message("Saved %s", slot);
    else
        show_slot_message("Couldn't save %s", slot);
    game_speed();
}

static void load_state_hotkey(void)
{
    int result = emu_load_state(current_slot);
    if (result == 1)
        show_slot_message("Loaded %s", current_slot);
    else if (result == 0)
        show_slot_message("%s is empty", current_slot, true);
    else
        show_slot_message("Couldn't load %s", current_slot);
}

/* With "save a state when leaving" on, leaving a game (Q, Exit or Load new
 * game) saves a state to come back to. */
static void save_before_leaving(void)
{
    if (!cfg.save_on_exit)
        return;

    int slot = leaving_save_target();
    int rect[4];
    ui_toast("Saving your place...", MESSAGE_Y, rect);
    platform_present();
    if (!slot || !emu_save_state(slot))
        gui_message("Couldn't save a state to resume from.",
                    slot ? "The calculator may be out of space." : "All 999 slots are used.");
}

enum EmuExit emu_run(int may_load_state)
{
    fast_forward = false;
    pending = 0;
    message_visible = false;
    shown_multiplier = -1;
    perf_line[0] = 0;
    clear_screens_later();

    if (may_load_state && cfg.load_on_start && states_newest())
    {
        int slot = states_newest();
        if (emu_load_state(slot) == 1)
            show_slot_message("Resumed from %s", slot);
        else
            show_slot_message("Couldn't resume from %s", slot);
    }

    gui_wait_release();
    game_speed();
    reset_timing();

    for (;;)
    {
        use_cpu_core(-1);   /* the menu may have changed it */
        S9xMainLoop ();

        if (pending & PENDING_SAVE)
        {
            save_state_hotkey();
            reset_timing();
        }
        if (pending & PENDING_LOAD)
        {
            load_state_hotkey();
            reset_timing();
        }
        if (pending & PENDING_LOAD_NEWEST)
        {
            if (states_newest())
            {
                current_slot = states_newest();
                load_state_hotkey();
            }
            else
                emu_show_message("No saved states yet");
            reset_timing();
        }
        if (pending & PENDING_RESTART)
        {
            emu_reset_game();
            emu_show_message("Game restarted");
            reset_timing();
        }
        if (pending & PENDING_MENU)
        {
            normal_speed();
            perf_log_flush();
            message_visible = false;   /* only messages from the menu show after it */
            enum MenuResult result = menu_run();
            if (result != MENU_RESUME)
            {
                save_before_leaving();
                left_game();
                return result == MENU_EXIT ? EMU_QUIT : EMU_BACK_TO_BROWSER;
            }
            clear_screens_later();
            gui_wait_release();
            game_speed();
            reset_timing();
        }
        if (pending & PENDING_QUIT)
        {
            normal_speed();
            save_before_leaving();
            left_game();
            return EMU_QUIT;
        }
        pending = 0;

        if (sram_dirty && cfg.sram_autosave &&
            platform_ticks() - sram_dirty_tick > platform_tick_hz() * 2)
        {
            normal_speed();
            write_sram();
            game_speed();
            reset_timing();
        }
    }
}
