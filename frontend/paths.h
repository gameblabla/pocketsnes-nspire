/* Where PocketSNES keeps its files: one "PocketSNES_saves" folder in My
 * Documents holds every game's save states, in-game saves and settings, and
 * PocketSNES's own files (settings for all games, speed test results, crash
 * markers). The name says which emulator the saves are for. It isn't in the
 * ROM folder, so _PocketSNES.tns is the first thing there (a folder would be
 * listed above it). Older versions kept a ".pocketsnes" (later a
 * "PocketSNES_saves") folder in each ROM folder, and their own files next
 * to the program: paths_move_old_files moves those in. */
#ifndef PATHS_H
#define PATHS_H

#include <stddef.h>

#define SAVE_FOLDER "PocketSNES_saves"

/* "<My Documents>/PocketSNES_saves/<name>" (the folder itself for name ""). */
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
 * when they're empty. For the program's folder (with_program_files),
 * PocketSNES's own files next to the program move in too. Cheap when there's
 * nothing to move. */
void paths_move_old_files(const char *dir, int with_program_files);

/* Whether a name in a folder is PocketSNES's: its save folder, its program
 * (pocketsnes*.tns, _pocketsnes*.tns) or its old files. Not shown as games. */
int paths_is_pocketsnes_name(const char *name);

#endif
