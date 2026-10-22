/* Runs the Snes9x core: game loop, frame pacing, hotkeys and save files. */
#ifndef EMU_H
#define EMU_H

#include <stddef.h>
#include <stdint.h>

enum EmuExit
{
    EMU_BACK_TO_BROWSER,
    EMU_QUIT
};

int  emu_init(void);
void emu_deinit(void);

int  emu_load_game(const char *rom_path);
/* Why emu_load_game() last failed, as a sentence. */
const char *emu_load_error(void);
/* Plays until the player leaves the game. Starts from the game's newest
 * state if may_load_state and "load the newest state when starting" are on,
 * and saves a state before leaving with "save a state when leaving" on. */
enum EmuExit emu_run(int may_load_state);
/* Writes the in-game save and the settings, and goes back to the settings
 * for all games. */
void emu_close_game(void);

/* At startup: if PocketSNES didn't close normally while a game ran at a
 * raised CPU speed, returns the multiplier (and the game's path in
 * crashed_rom), and forgets it; otherwise 0. */
int  emu_check_cpu_speed_crash(char *crashed_rom, size_t size);

/* For the menu. */
const char *emu_rom_path(void);
const char *emu_game_title(void);
int  emu_slot(void);
void emu_set_slot(int slot);
/* The slot a save goes to right now (a new one with auto-increment), or 0
 * when every slot is used. */
int  emu_save_target(void);
/* The slot 'delta' steps from 'slot', round 001-999 and the auto slot
 * (before 001) when Saving > Auto save slot is on or it has a state. */
int  emu_step_slot(int slot, int delta);
/* The menu's Save state has its own slot, apart from Load state's (and the
 * hotkeys'): 0 for "a new slot" (auto-increment on) or a slot 'delta' steps
 * round. emu_menu_save_target is where a save there goes now (0: every slot
 * is used). */
int  emu_menu_save_slot(void);
int  emu_menu_save_target(void);
void emu_step_menu_save_slot(int delta);
int  emu_save_state(int slot);    /* 1 on success */
/* The picture a save made now would keep (STATE_PICTURE_W x
 * STATE_PICTURE_H), or NULL when there isn't one of the game as it is. */
const uint16_t *emu_save_picture(void);
int  emu_load_state(int slot);    /* 1 ok, 0 empty slot, -1 unreadable */
void emu_reset_game(void);
/* Shows a short message over the game. */
void emu_show_message(const char *text);
/* The CPU clock in MHz the game last ran at (measured on the calculator),
 * or 0 if not known yet. */
uint32_t emu_game_mhz(void);
/* Runs the game from the current point in a few speed modes for about 40
 * seconds, then shows the results and writes them to
 * pocketsnes_results.txt.tns (replacing the last ones). With close_after_seconds set, the results screen
 * closes by itself. The game is put back where it was. */
void emu_speed_test(int close_after_seconds);
/* For the speed test build: plays 5 seconds so the caches are warm and the
 * clock has settled, writes pocketsnes_diag.txt.tns, then runs
 * the speed test and closes it. */
void emu_benchmark(void);

/* The overclock test's game check. emu_game_check_record plays the game
 * from where it is, at the speed set now, for about the seconds given, and
 * keeps a fingerprint of the SNES's memory after every frame: 1 when done,
 * 0 without the memory for it, -1 stopped with esc. Each
 * emu_game_check_replay then plays the same frames again from the same
 * point, showing note, and compares: 1 the same, 0 different, -1 stopped.
 * emu_game_check_end puts the game back where it was. */
int  emu_game_check_record(uint32_t seconds, const char *note);
int  emu_game_check_replay(const char *note);
void emu_game_check_end(void);

#endif
