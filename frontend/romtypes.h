/* ROM files open PocketSNES from the calculator's own file list through
 * Ndless's file associations: lines "ext.sfc=_PocketSNES" (the program's
 * name without .tns) in My Documents/ndless/ndless.cfg.tns. Ndless's
 * cfg_register_fileext looks for that file through every folder of My
 * Documents (libndls locate) each time it is called; called for each of the
 * five types at every start, that froze the calculator for most of a minute
 * when it had many files (issue #11). This reads the file where Ndless keeps
 * it, once, and writes it back only when a line has to change. */
#ifndef ROMTYPES_H
#define ROMTYPES_H

#include <stddef.h>

/* The file's text with the ROM types pointing at program: a type with no
 * line gets one at the end, a line of an older PocketSNES (another name
 * starting with "pocketsnes" or "_pocketsnes") gets the new name, and a type
 * another program has stays with it. text is the file's contents (len
 * bytes; NULL for no file). Returns NULL if nothing changes, else a new
 * text of *new_len bytes, to free(). */
char *romtypes_update(const char *text, size_t len, const char *program, size_t *new_len);

/* romtypes_update on the file at cfg_path, written back if it changed (and
 * created if it isn't there). Returns 1 if it wrote to the file. */
int romtypes_register(const char *cfg_path, const char *program);

#endif
