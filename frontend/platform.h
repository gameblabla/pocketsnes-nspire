/* Hardware layer: the calculator build (platform_nspire.cpp) and the PC test
 * build (platform_host.cpp) both implement this. */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#define SCREEN_W 320
#define SCREEN_H 240

#define RGB565(r, g, b) ((uint16_t) (((r) << 11) | ((g) << 5) | (b)))

/* Keys are numbered like lr-gpsp-nspire: 1 + the bit index across the four
 * 32-bit keypad words at 0x900E0010..0x900E001C (CX / CX II layout), so a key
 * map saved by one works with the other's names. 0 means "no key". */
#define NUM_KEYS        128
#define KEY_NONE        0
#define KEY_RETURN      1
#define KEY_ENTER       2
#define KEY_NEGATIVE    4
#define KEY_SPACE       5
#define KEY_Z           6
#define KEY_Y           7
#define KEY_0           8
#define KEY_HOME        10
#define KEY_X           17
#define KEY_W           18
#define KEY_V           19
#define KEY_3           20
#define KEY_U           21
#define KEY_T           22
#define KEY_S           23
#define KEY_1           24
#define KEY_R           33
#define KEY_Q           34
#define KEY_P           35
#define KEY_6           36
#define KEY_O           37
#define KEY_N           38
#define KEY_M           39
#define KEY_4           40
#define KEY_L           49
#define KEY_K           50
#define KEY_J           51
#define KEY_9           52
#define KEY_I           53
#define KEY_H           54
#define KEY_G           55
#define KEY_7           56
#define KEY_DIVIDE      57
#define KEY_F           65
#define KEY_E           66
#define KEY_D           67
#define KEY_C           69
#define KEY_B           70
#define KEY_A           71
#define KEY_EQUALS      72
#define KEY_MULTIPLY    73
#define KEY_VAR         82
#define KEY_MINUS       83
#define KEY_RPAREN      84
#define KEY_PERIOD      85
#define KEY_LPAREN      86
#define KEY_5           87
#define KEY_CAT         88
#define KEY_FRAC        89
#define KEY_DEL         90
#define KEY_SCRATCHPAD  91
#define KEY_CLICK       98
#define KEY_PLUS        99
#define KEY_DOC         100
#define KEY_2           101
#define KEY_MENU        102
#define KEY_8           103
#define KEY_ESC         104
#define KEY_TAB         106
#define KEY_UP          113
#define KEY_UPRIGHT     114
#define KEY_RIGHT       115
#define KEY_RIGHTDOWN   116
#define KEY_DOWN        117
#define KEY_DOWNLEFT    118
#define KEY_LEFT        119
#define KEY_LEFTUP      120
#define KEY_SHIFT       121
#define KEY_CTRL        122
#define KEY_COMMA       123

/* Name shown for a key, or NULL if the key doesn't exist. */
const char *key_name(int key);
/* Keys that can't be bound to an action (the touchpad arrows and click). */
int key_is_reserved(int key);

/* Returns 0 if the program can't run here. May remove the arguments it
 * handles itself from argv. */
int  platform_init(int *argc, char **argv);
void platform_shutdown(void);

/* The game and menus draw into platform_screen(), a 320x240 RGB565 buffer
 * that platform_present() then shows. There may be several buffers that take
 * turns (the calculator uses two while the DMA controller copies frames out,
 * the PC test build three to check the frontend copes), so after a present
 * platform_screen() may be a different buffer, still holding whatever was
 * drawn into it a few frames ago.
 * platform_screen_index() says which one it is.
 * Every buffer has spare memory before and after it, because the Snes9x
 * renderer can write slightly past the picture. */
#define PLATFORM_MAX_SCREENS 3
uint16_t *platform_screen(void);
int platform_screen_index(void);
void platform_present(void);

/* On the calculator the frame before may still be being copied out of the
 * buffer in the background. platform_screen() waits for that to finish; the
 * game's renderer instead takes the buffer from platform_screen_nowait()
 * and calls platform_wait_lines() before drawing into lines up to
 * 'last_line' (0-239). */
uint16_t *platform_screen_nowait(void);
void platform_wait_lines(int last_line);

/* Game frames, which mostly change in the same places: once a frame is
 * drawn into platform_screen(), platform_frame_begin() returns the number of
 * the buffer it will be shown from; like platform_screen_index(), a buffer
 * keeps what was put into it frames ago, and the numbers don't overlap with
 * platform_screen_index()'s. platform_frame_copy() puts a rectangle of
 * platform_screen() into it (the whole screen when it is out of date, or
 * only what changed), and platform_frame_present() shows it. On the CX II
 * the rectangles are turned into a portrait buffer that the LCD is pointed
 * at, at its next refresh (no tearing). Elsewhere platform_frame_copy() does
 * nothing and platform_screen() is shown as with platform_present(). */
int  platform_frame_begin(void);
void platform_frame_copy(int x, int y, int w, int h);
void platform_frame_present(void);

/* Lines of spare memory after the 240 lines of platform_screen(). */
int platform_screen_spare_lines(void);

/* A depth buffer for the renderer in faster memory, or NULL: SCREEN_W bytes
 * a line, with PLATFORM_DEPTH_GUARD lines of room before line 0 and *lines
 * lines from line 0. */
#define PLATFORM_DEPTH_GUARD 8
uint8_t *platform_fast_depth_buffer(int *lines);

/* Ways of getting frames to the screen, for the speed test to compare. */
enum ScreenOutput
{
    OUTPUT_BEST,       /* the fastest that works here, used for playing */
    OUTPUT_DMA,        /* CX II: the DMA controller copies whole frames to the OS's LCD buffer */
    OUTPUT_FLIP,       /* CX II: whole frames turned in software, the LCD pointed at them */
    OUTPUT_LCD_BLIT    /* Ndless's lcd_blit */
};

/* Shows frames another way until it's set back to OUTPUT_BEST. Returns 0 if
 * that way isn't available here, or is what's used anyway. */
int platform_set_screen_output(int output);

/* How frames reach the screen, for the speed test results. */
const char *platform_screen_mode_name(void);

/* The CPU clock in MHz, measured with a loop of known length (0 on the PC). */
uint32_t platform_cpu_mhz(void);

/* The CPU clock of the CX II is 12 MHz times a multiplier (33: 396 MHz, the
 * calculator's own on battery; 24: 288 MHz on USB). Raising it speeds up
 * memory too, and too high a value freezes the calculator.
 * platform_set_cpu_multiplier(0) puts back the calculator's own setting,
 * which platform_shutdown() also does. Returns the clock afterwards in MHz,
 * measured after a change (the PC build reports what was asked for), or 0
 * where it can't be set. */
uint32_t platform_set_cpu_multiplier(int multiplier);
/* The calculator's own clock in MHz (0 if not known), for the menu. */
uint32_t platform_cpu_normal_mhz(void);
/* The multiplier in the clock register now, or 0 if it can't be read. */
int platform_cpu_multiplier(void);
/* A line about the clock register and the last change, for the speed test
 * results. */
void platform_clock_report(char *text, size_t size);
/* For the PC tests: 1 when the PC build is acting like a calculator that
 * runs games wrongly at the clock set now (--glitch-above). Always 0 on the
 * calculator. */
int platform_test_glitch(void);

/* The program's own file, where the settings for all games are kept
 * (selfstore.h), or NULL where there is none (the PC build without
 * --program FILE). */
const char *platform_program_path(void);

/* The screen's backlight on a CX II, in the value the OS writes to its
 * register: 0 (brightest) to 225 (darkest). platform_os_backlight() is the
 * value at startup (the calculator's own brightness), or -1 where it can't
 * be changed (not a CX II). platform_set_backlight(-1) puts the calculator's
 * own back, as platform_shutdown() does. */
int platform_os_backlight(void);
void platform_set_backlight(int value);

/* Game frames (platform_frame_begin) shown by the DMA controller, a little
 * faster but not in step with the LCD (it can tear), instead of turned into
 * the buffer the LCD shows next. Only on the CX II. */
void platform_set_game_frames_by_dma(int dma);

/* Speed test build: writes what the screen, memory and timer look like
 * from the program's side to the file at 'path' (a new file), for working out why some
 * ways of showing frames don't work. */
void platform_write_diagnostics(const char *path);

/* Speed test build, run last because it could crash: write speeds of memory
 * the OS may own (the calculator's on-chip SRAM), with its contents saved
 * and put back, appended to the same file. */
void platform_write_memory_tests(const char *path);

/* Keys are sampled by platform_poll_keys(); the other calls report that sample. */
void platform_poll_keys(void);
int  platform_key_down(int key);
int  platform_key_pressed(int key);   /* went down at the last poll */
int  platform_any_key_down(void);
int  platform_first_key_down(void);   /* lowest held key number, or 0 */

/* Free-running tick counter; platform_tick_hz() ticks per second. */
uint32_t platform_ticks(void);
uint32_t platform_tick_hz(void);
void platform_wait_until(uint32_t tick);

/* Directory holding the program, for the config file. No trailing slash. */
const char *platform_exe_dir(void);
/* The calculator's My Documents ("/documents"), where the saves folder goes
 * (paths.h). The PC build uses the folder above the program's (or
 * --documents DIR). No trailing slash. */
const char *platform_documents_dir(void);

/* PC build only: the window was closed. Always 0 on the calculator. */
int platform_quit_requested(void);

/* PC build only: a line for the test log (--log). Nothing on the calculator. */
void platform_log(const char *text);

#endif
