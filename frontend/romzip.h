/* ROMs in .zip files, as they are usually downloaded: "Game.zip" renamed to
 * "Game.zip.tns" on the calculator. The ROM is the first file in the zip
 * with a ROM's extension (.sfc, .smc, .swc, .fig), or else its biggest file
 * (files under 32 KB never count).
 * Stored and deflated files are read (what zip tools write). */
#ifndef ROMZIP_H
#define ROMZIP_H

#include <stddef.h>
#include <stdint.h>

/* Whether the file starts like a zip. */
int romzip_is_zip(const char *path);
/* The ROM's name inside the zip; 0 when there's no file to take. */
int romzip_rom_name(const char *path, char *name, size_t size);
/* Unpacks the ROM into buffer (at most max bytes). Returns its size, or 0
 * if it couldn't be read or is bigger than max. */
uint32_t romzip_read(const char *path, uint8_t *buffer, uint32_t max);

#endif
