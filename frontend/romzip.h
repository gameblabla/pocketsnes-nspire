/* ROMs in .zip files, as they are usually downloaded: "Game.zip" renamed to
 * "Game.zip.tns" on the calculator. The ROM is the first file in the zip
 * with a ROM's extension (.sfc, .smc, .swc, .fig), or else its biggest file
 * (files under 32 KB and other consoles' ROMs, .gba .agb .gb .gbc .nes .nds,
 * never count).
 * Stored and deflated files are read (what zip tools write). */
#ifndef ROMZIP_H
#define ROMZIP_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Whether the file starts like a zip. */
int romzip_is_zip(const char *path);
/* The ROM's name inside the zip; 0 when there's no file to take. */
int romzip_rom_name(const char *path, char *name, size_t size);
/* Whether the zip has a GBA ROM in it (a game for gpSP). */
int romzip_holds_gba(const char *path);
/* Unpacks the ROM into buffer (at most max bytes). Returns its size, or 0
 * if it couldn't be read or is bigger than max. */
uint32_t romzip_read(const char *path, uint8_t *buffer, uint32_t max);

/* Reading a ROM (a zip's, or a plain file's in memmap.cpp) calls this after
 * every 128 KB; nonzero stops the read (the player cancelled): it sets
 * rom_read_cancelled and the read gives 0 bytes. NULL: never stops. */
extern int (*rom_read_poll)(void);
extern int rom_read_cancelled;
/* fread in 128 KB pieces with rom_read_poll between them: the bytes read,
 * 0 if cancelled. */
size_t rom_read_pieces(void *dst, size_t max, FILE *f);

#endif
