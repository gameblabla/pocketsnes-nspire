/* Where PocketSNES keeps its files. Each ROM folder gets a "PocketSNES_saves"
 * folder for its games' save states, in-game saves and settings; the one
 * next to the program also holds PocketSNES's own files (settings for all
 * games, speed test results, crash markers). The name says which emulator
 * the saves are for. Older versions used a hidden ".pocketsnes" folder, and
 * kept their own files next to the program: paths_move_old_files moves
 * those in. */
#ifndef PATHS_H
#define PATHS_H

#include <stddef.h>

#define SAVE_FOLDER "PocketSNES_saves"

/* "<dir>/PocketSNES_saves/<name>" ("<dir>/PocketSNES_saves" for name ""). */
void paths_save_file(char *path, size_t size, const char *dir, const char *name);
/* The same for the folder of a file: "<ROM folder>/PocketSNES_saves/<name>". */
void paths_save_file_for(char *path, size_t size, const char *file_path, const char *name);
/* A PocketSNES file in the save folder next to the program, which is
 * created if it isn't there (paths_data_dir_make). */
void paths_data_file(char *path, size_t size, const char *name);
void paths_data_dir_make(void);

/* Moves the files in "<dir>/.pocketsnes" into "<dir>/PocketSNES_saves" (a file already
 * there wins) and removes the old folder when it's empty. For the program's
 * folder (with_program_files), PocketSNES's own files next to the program
 * move in too. Cheap when there's nothing to move. */
void paths_move_old_files(const char *dir, int with_program_files);

/* Whether a name in a folder is PocketSNES's: its save folder, its program
 * (pocketsnes*.tns, _pocketsnes*.tns) or its old files. Not shown as games. */
int paths_is_pocketsnes_name(const char *name);

#endif
