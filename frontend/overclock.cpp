#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "draw.h"
#include "emu.h"
#include "gui.h"
#include "overclock.h"
#include "platform.h"
#include "ui.h"

/* 408 to 504 MHz: the CX II runs at 396 MHz (x33), and the best calculators
 * reported reach about 492 MHz. Raising the CPU clock raises the memory's
 * too (its bus runs at half the CPU clock) with the same timings, so the
 * memory is usually what gives out first. */
#define FIRST_MULTIPLIER 34
#define LAST_MULTIPLIER  42
#define SPEEDS (LAST_MULTIPLIER - FIRST_MULTIPLIER + 1)
#define ROUNDS 2
#define GAME_SECONDS 6     /* of the game recorded at normal speed */
#define MEMORY_SECONDS 2   /* of memory and CPU checks after it, each round */

#define MARKER  "pocketsnes_octest.tns"
#define RESULTS "pocketsnes_overclock.txt.tns"

enum Outcome { NOT_TRIED, PASSED, NOT_REACHED, GAME_ERRORS, MEMORY_ERRORS, CPU_ERRORS, STOPPED };

struct SpeedResult
{
    enum Outcome outcome;
    uint32_t mhz;         /* measured after the last switch to it */
    uint32_t megabytes;   /* memory checked at it */
};

static struct SpeedResult results[SPEEDS];
static int testing;            /* the speed being tried, -1 for none */
static int test_round;
static uint32_t round_start;
static uint32_t best_mhz;      /* the highest that passed in this run */

static int speed_mhz(int index)
{
    return (FIRST_MULTIPLIER + index) * 12;
}

/* Within 2% of 12 MHz times the multiplier. */
static int reached(uint32_t mhz, int multiplier)
{
    uint32_t want = (uint32_t) multiplier * 12;
    return mhz * 50 >= want * 49 && mhz * 50 <= want * 51;
}

static void file_path(char *path, size_t size, const char *name)
{
    snprintf(path, size, "%s/%s", platform_exe_dir(), name);
}

/* ---- The checks ----
 *
 * Memory: a buffer far bigger than the 8 KB data cache, so every access goes
 * to memory, filled with a pattern from each word's address and a seed, then
 * read back and turned into its inverse, then read back again; then a new
 * seed. CPU: a mix of multiplies, shifts and adds over data in the cache,
 * whose result must match the one worked out at normal speed. */
static uint32_t *buffer;
static size_t buffer_words;
static int phase;          /* 0 write, 1 check and invert, 2 check the inverse */
static size_t position;    /* next word of the phase */
static uint32_t seed;
static uint64_t bytes_checked;
static size_t slice_words;
static uint32_t cpu_data[256];
static uint32_t cpu_expected;

static inline uint32_t pattern(size_t index, uint32_t s)
{
    return (uint32_t) index * 0x9E3779B1u ^ s;
}

/* One slice of the memory check. Returns the number of wrong words. */
static uint32_t memory_slice(size_t n)
{
    if (position + n > buffer_words)
        n = buffer_words - position;
    uint32_t *p = buffer + position;
    uint32_t bad = 0;
    switch (phase)
    {
    case 0:
        for (size_t i = 0; i < n; i++)
            p[i] = pattern(position + i, seed);
        break;
    case 1:
        for (size_t i = 0; i < n; i++)
        {
            uint32_t want = pattern(position + i, seed);
            bad += p[i] != want;
            p[i] = ~want;
        }
        break;
    default:
        for (size_t i = 0; i < n; i++)
            bad += p[i] != ~pattern(position + i, seed);
        break;
    }
    bytes_checked += (uint64_t) n * 4;
    position += n;
    if (position >= buffer_words)
    {
        position = 0;
        if (++phase == 3)
        {
            phase = 0;
            seed = seed * 1664525u + 1013904223u;
        }
    }
    return bad;
}

static uint32_t cpu_check(void)
{
    uint32_t a = 0x12345678u, b = 0x9ABCDEF0u;
    for (int r = 0; r < 32; r++)
        for (int i = 0; i < 256; i++)
        {
            a = (a ^ cpu_data[i]) * 0x01000193u + (b >> 3);
            b = (b + a) ^ (a << 7) ^ (b >> 11);
        }
    return a ^ b;
}

/* As big a buffer as there's memory for, 8 MB at most. */
static int start_checks(void)
{
    for (size_t megabytes = 8; megabytes >= 1 && !buffer; megabytes /= 2)
    {
        buffer = (uint32_t *) malloc(megabytes << 20);
        buffer_words = (megabytes << 20) / 4;
    }
    if (!buffer)
        return 0;
    phase = 0;
    position = 0;
    seed = 0x2545F491u;
    slice_words = 16384;   /* 64 KB, grown to fill most of a frame */
    for (int i = 0; i < 256; i++)
        cpu_data[i] = (uint32_t) i * 0x45D9F3Bu ^ 0x3C6EF372u;
    cpu_expected = cpu_check();
    return 1;
}

static void stop_checks(void)
{
    free(buffer);
    buffer = NULL;
}

/* ---- Files ---- */

static void write_marker(int index)
{
    char path[600];
    file_path(path, sizeof(path), MARKER);
    FILE *f = fopen(path, "w");
    if (f)
    {
        fprintf(f, "testing=%d\npassed=%u\n", speed_mhz(index), (unsigned) best_mhz);
        fclose(f);
    }
}

static const char *outcome_text(const struct SpeedResult *r)
{
    switch (r->outcome)
    {
    case PASSED: return "passed";
    case NOT_REACHED: return "didn't switch";
    case GAME_ERRORS: return "game went wrong";
    case MEMORY_ERRORS: return "memory errors";
    case CPU_ERRORS: return "CPU errors";
    case STOPPED: return "stopped";
    default: return "";
    }
}

static void write_results(void)
{
    char path[600];
    file_path(path, sizeof(path), RESULTS);
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    fprintf(f, "PocketSNES overclock test (%d rounds per speed: %d s of the game, %d s of checks)\n", ROUNDS,
            GAME_SECONDS, MEMORY_SECONDS);
    for (int i = 0; i < SPEEDS; i++)
    {
        const struct SpeedResult *r = &results[i];
        if (r->outcome == NOT_TRIED && i != testing)
            continue;
        if (i == testing)
            fprintf(f, "%d MHz: being tested (a freeze here means it's too fast)\n", speed_mhz(i));
        else
            fprintf(f, "%d MHz: %s (measured %u MHz, %u MB checked)\n", speed_mhz(i), outcome_text(r),
                    (unsigned) r->mhz, (unsigned) r->megabytes);
    }
    if (best_mhz)
        fprintf(f, "Highest tested speed: %u MHz\n", (unsigned) best_mhz);
    else
        fprintf(f, "No raised speed passed.\n");
    fclose(f);
}

/* ---- Screens ---- */

static void draw_list(void)
{
    char right[40], value[40];
    if (best_mhz)
        snprintf(right, sizeof(right), "passed up to %u MHz", (unsigned) best_mhz);
    else
        snprintf(right, sizeof(right), "%d speeds", SPEEDS);
    ui_frame("Overclock test", right);

    for (int i = 0; i < SPEEDS; i++)
    {
        char label[16];
        const struct SpeedResult *r = &results[i];
        snprintf(label, sizeof(label), "%d MHz", speed_mhz(i));
        int flags = 0;
        if (i == testing)
        {
            uint32_t seconds = (platform_ticks() - round_start) / platform_tick_hz();
            snprintf(value, sizeof(value), "round %d of %d, %u s", test_round + 1, ROUNDS, (unsigned) seconds);
            flags = ROW_SELECTED;
        }
        else
            snprintf(value, sizeof(value), "%s", outcome_text(r));
        uint16_t color = r->outcome == PASSED ? COLOR_GOOD : r->outcome == NOT_TRIED ? COLOR_TEXT_FAINT : COLOR_BAD;
        if (r->outcome == NOT_TRIED && i != testing)
            flags |= ROW_FAINT;
        int passed = r->outcome == PASSED && i != testing;
        ui_row(UI_LIST_Y + i * UI_ROW_H, passed ? ICON_STAR : ICON_SPEED, color, label, value, flags);
    }
}

static void draw_testing(void)
{
    draw_list();
    ui_help("Game and memory checks at each speed. A freeze is OK: reset, and PocketSNES keeps the result.");
    static const struct UiHint stop[] = { { "esc", "Stop" } };
    ui_footer(stop, 1);
}

static void draw_done(const void *data)
{
    (void) data;
    char first[64], second[64] = "", third[64] = "";
    draw_list();
    static const struct UiHint ok[] = { { "any key", "OK" } };
    ui_footer(ok, 1);

    if (best_mhz)
        snprintf(first, sizeof(first), "Highest tested speed: %u MHz", (unsigned) best_mhz);
    else
        snprintf(first, sizeof(first), "No raised speed passed");
    for (int i = 0; i < SPEEDS; i++)
        if (results[i].outcome != NOT_TRIED && results[i].outcome != PASSED)
            snprintf(second, sizeof(second), "%d MHz: %s.", speed_mhz(i), outcome_text(&results[i]));
    if (best_mhz)
        snprintf(third, sizeof(third), "Use it: CPU speed > \"highest tested\".");
    const char *lines[3] = { first, second[0] ? second : NULL, third[0] ? third : NULL };
    ui_dialog(NULL, lines, 3, NULL, 0);
}

/* ---- The test ---- */

/* One round at the speed being tried: the recorded seconds of the game,
 * then memory and CPU checks until the time is up. Returns the outcome
 * (PASSED when nothing went wrong). */
static enum Outcome run_round(int index)
{
    struct SpeedResult *r = &results[index];
    char note[64];
    snprintf(note, sizeof(note), "Overclock test: %d MHz, %d of %d (esc stops it)", speed_mhz(index),
             test_round + 1, ROUNDS);
    int game = emu_game_check_replay(note);
    if (game < 0)
        return STOPPED;
    if (game == 0)
        return GAME_ERRORS;

    uint64_t bytes_before = bytes_checked;
    round_start = platform_ticks();
    uint32_t length = MEMORY_SECONDS * platform_tick_hz();
    enum Outcome outcome = PASSED;

    while (platform_ticks() - round_start < length)
    {
        /* About 14 ms of checks a frame (the slice grows until it takes
         * that long; where time only moves between frames, as in the PC
         * test build, it stays small). */
        uint32_t start = platform_ticks();
        uint32_t bad = memory_slice(slice_words);
        uint32_t took = platform_ticks() - start;
        if (took && took < platform_tick_hz() * 12 / 1000 && slice_words * 2 <= buffer_words)
            slice_words *= 2;
        else if (took > platform_tick_hz() * 18 / 1000 && slice_words > 4096)
            slice_words /= 2;
        if (bad)
        {
            outcome = MEMORY_ERRORS;
            break;
        }
        if (cpu_check() != cpu_expected)
        {
            outcome = CPU_ERRORS;
            break;
        }

        draw_testing();
        gui_present();
        enum GuiAction action = gui_input();
        if (action == GUI_BACK || action == GUI_QUIT)
        {
            outcome = STOPPED;
            break;
        }
    }
    r->megabytes += (uint32_t) ((bytes_checked - bytes_before) >> 20);
    return outcome;
}

void overclock_test_run(int in_game)
{
    char path[600];

    if (!platform_cpu_normal_mhz())
    {
        gui_message("The overclock test is for the TI-Nspire CX II.", NULL);
        return;
    }
    if (!in_game)
    {
        gui_message("The overclock test plays a game.", "Start one, then run it from its menu.");
        return;
    }
    memset(results, 0, sizeof(results));
    testing = -1;
    best_mhz = 0;
    bytes_checked = 0;
    if (!start_checks())
    {
        gui_message("Not enough free memory for the overclock test.", NULL);
        return;
    }
    gui_wait_release();

    /* What the game does at normal speed, to compare with. */
    int recorded = emu_game_check_record(GAME_SECONDS, "Overclock test: normal speed first (esc stops it)");
    if (recorded <= 0)
    {
        emu_game_check_end();
        stop_checks();
        if (!recorded)
            gui_message("Not enough free memory for the overclock test.", NULL);
        gui_wait_release();
        return;
    }

    int stopped = 0;
    for (int i = 0; i < SPEEDS; i++)
    {
        struct SpeedResult *r = &results[i];
        int multiplier = FIRST_MULTIPLIER + i;

        /* The marker and the results so far go to files at normal speed. */
        testing = i;
        write_marker(i);
        write_results();

        r->outcome = PASSED;
        for (test_round = 0; test_round < ROUNDS && r->outcome == PASSED; test_round++)
        {
            r->mhz = platform_set_cpu_multiplier(multiplier);
            if (!reached(r->mhz, multiplier))
                r->outcome = NOT_REACHED;
            else
                r->outcome = run_round(i);
            platform_set_cpu_multiplier(0);
        }
        testing = -1;
        if (r->outcome != PASSED)
        {
            stopped = r->outcome == STOPPED;
            break;
        }
        best_mhz = (uint32_t) speed_mhz(i);
    }

    stop_checks();
    emu_game_check_end();
    file_path(path, sizeof(path), MARKER);
    remove(path);
    write_results();
    /* A full run replaces the last result; one stopped with esc only adds
     * to it. */
    if (!stopped || best_mhz > config_tested_mhz())
        config_set_tested_mhz(best_mhz);
    gui_wait_release();
    gui_show_until_key(draw_done, NULL);
}

int overclock_check_freeze(void)
{
    char path[600], line[64];
    file_path(path, sizeof(path), MARKER);
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    unsigned tested = 0, passed = 0;
    while (fgets(line, sizeof(line), f))
    {
        sscanf(line, "testing=%u", &tested);
        sscanf(line, "passed=%u", &passed);
    }
    fclose(f);
    remove(path);

    config_set_tested_mhz(passed);
    char first[64], second[64];
    snprintf(first, sizeof(first), "The overclock test froze at %u MHz.", tested);
    if (passed)
        snprintf(second, sizeof(second), "Highest tested speed: %u MHz.", passed);
    else
        snprintf(second, sizeof(second), "No raised speed passed.");
    gui_message(first, second);
    return 1;
}
