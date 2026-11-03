/* Where PocketSNES keeps its files: one "zPocketSNES_files" folder in My
 * Documents holds every game's save states, in-game saves and settings, and
 * PocketSNES's own files (settings for all games, speed test results, crash
 * markers). The name says which emulator the saves are for, and the z puts
 * it below the game folders in My Documents (the calculator lists folders
 * A to Z, ignoring case). It isn't in the ROM folder, so _PocketSNES.tns is
 * the first thing there (a folder would be listed above it). Older versions
 * kept a ".pocketsnes" (later a "PocketSNES_saves") folder in each ROM
 * folder, then a "PocketSNES_saves" folder in My Documents, then
 * "zPocketSNES_saves" (until 2026-11-02), and their own files next to the
 * program: paths_move_old_files moves those in. */
#ifndef PATHS_H
#define PATHS_H

#include <stddef.h>

#define SAVE_FOLDER "zPocketSNES_files"
/* The saves folder's names in older versions: PocketSNES_saves in My
 * Documents or a ROM folder, then zPocketSNES_saves in My Documents. */
#define OLD_SAVE_FOLDER "PocketSNES_saves"
#define OLD_Z_SAVE_FOLDER "zPocketSNES_saves"

/* "<My Documents>/zPocketSNES_files/<name>" (the folder itself for name ""). */
void paths_save_file(char *path, size_t size, const char *name);
/* The same, creating the folder if it isn't there (paths_save_dir_make). */
void paths_data_file(char *path, size_t size, const char *name);
void paths_save_dir_make(void);

/* The file with the order of the games in the ROM folder dir (browser.cpp):
 * "game_order.txt.tns" for My Documents itself, "game_order_snes.txt.tns"
 * for My Documents/snes, "game_order_snes_rpg.txt.tns" for snes/rpg. */
void paths_order_file(char *path, size_t size, const char *dir);

/* Moves the files in "<dir>/.pocketsnes" and "<dir>/PocketSNES_saves" into
 * the saves folder (a file already there wins) and removes the old folders
 * when they're empty. For the program's folder (with_program_files, once at
 * start), "<My Documents>/zPocketSNES_saves" and "PocketSNES_saves" and
 * PocketSNES's own files next to the program move in too. Cheap when
 * there's nothing to move. */
void paths_move_old_files(const char *dir, int with_program_files);

/* Whether a name in a folder is PocketSNES's: its save folder, its program
 * (pocketsnes*.tns, _pocketsnes*.tns) or its old files. Not shown as games. */
int paths_is_pocketsnes_name(const char *name);

/* The OS's list of the files in My Documents is a copy it keeps, and it
 * doesn't see files a program adds, removes or renames. platform_shutdown
 * has it read them all again, which takes many seconds on a calculator with
 * many files, so only when the list would show something that isn't there:
 * a name in the saves folder at start (paths_file_list_start) gone at exit
 * (paths_file_list_changed), or a change elsewhere (old files moved in, the
 * saves folder made, ndless.cfg.tns: paths_file_list_touched). Files only
 * added to the saves folder (a new state) or written over don't count: the
 * list shows them after the next refresh. */
void paths_file_list_start(void);
void paths_file_list_touched(void);
int paths_file_list_changed(void);

#endif
