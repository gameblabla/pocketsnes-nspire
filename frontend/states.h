/* Save state slots. A game's states live next to its in-game save, in
 * "<My Documents>/PocketSNES_saves/<ROM name>.svNNN.tns" for slots 1 to 999,
 * and "<ROM name>.svauto.tns" for the auto slot (paths.h). */
#ifndef STATES_H
#define STATES_H

#include <stddef.h>

#define MAX_STATE_SLOTS 999
/* The slot apart from 1-999 that saving when leaving can use (Saving > Auto
 * save slot). It counts for states_newest(), never for a new slot. */
#define AUTO_SLOT (MAX_STATE_SLOTS + 1)

/* Points the slot functions at a ROM and scans its existing states. */
void states_open_game(const char *rom_path);

/* "<My Documents>/PocketSNES_saves", created on first use. */
const char *states_dir(void);
void states_make_dir(void);
/* In-game (battery) save, named the way PocketSNES 3.0 names it. */
void states_sram_path(char *path, size_t size);
/* The game's in-game save into the core, and back to its file: plain
 * ("<ROM name>.srm.tns", what PC emulators read) or, with Compress saves on,
 * gzipped ("<ROM name>.srm.gz.tns"); the other form is removed. Loading
 * takes the newer of the two, and a PC emulator's "<ROM name without .sfc>
 * .srm.tns" when the game has neither. */
void states_load_sram(void);
void states_save_sram(void);

/* For the save manager: the game's in-game save (slot 0) and states (the
 * auto slot first, then 1-999), with their sizes. */
struct SaveFile
{
    int slot;
    long bytes;
    int compressed;
};
int states_list(struct SaveFile *files, int max);
/* Compresses or uncompresses one file (slot 0: the in-game save). 1 if it
 * is that way afterwards. */
int states_set_compressed(int slot, int compress);
/* Removes one (slot 0: the in-game save). 1 on success. */
int states_delete(int slot);

int states_exists(int slot);
int states_count(void);
/* The most recently saved slot (the auto slot too), or 0 if the game has
 * none. */
int states_newest(void);
/* Where an auto-increment save goes: the slot after the highest one used,
 * or the first free slot once 999 is taken. 0 when every slot is used. */
int states_next_new_slot(void);

/* Return 1 on success. states_load returns 0 for an empty slot and -1 when
 * the file can't be read or isn't a PocketSNES state. */
int states_save(int slot);
int states_load(int slot);

/* "slot 012", or "the auto slot". */
const char *states_slot_name(int slot, char *text, size_t size);
/* The same with a capital: "Slot 012", "The auto slot". */
const char *states_slot_name_cap(int slot, char *text, size_t size);

#endif
