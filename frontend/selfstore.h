/* The settings for all games, kept inside the program file itself, so they
 * go wherever the program goes. They sit in a fixed-size area after the
 * program (Ndless loads the whole file, the program's loader only uses its
 * own part), with a footer that marks it:
 *
 *   SELFSTORE_SIZE bytes: the settings text, then zeros
 *   16 bytes: "PSNESCFG", the text's length, SELFSTORE_SIZE (little-endian)
 *
 * The first save adds the area; later ones write over it in place, so the
 * program's own bytes are never written again. */
#ifndef SELFSTORE_H
#define SELFSTORE_H

#include <stddef.h>

#define SELFSTORE_SIZE 8192

/* The text stored in the program file (malloc'd, 0-terminated), or NULL when
 * it has none. */
char *selfstore_read(const char *program_path);
/* Stores text in the program file, unless it holds that already. Returns 1
 * if the file has it afterwards. */
int selfstore_write(const char *program_path, const char *text);

#endif
