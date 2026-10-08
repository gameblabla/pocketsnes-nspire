/* What the core needs from a frontend, for the assembler CPU core tests
 * (tests/asm). Nothing is drawn, played or read from keys. */
#include <stdlib.h>
#include <string.h>

#include "snes9x.h"
#include "memmap.h"

bool JustifierOffscreen (void) { return true; }
void JustifierButtons (uint32 &) {}
void S9xProcessSound (unsigned int) {}
/* As frontend/emu.cpp has them (the core makes file names with them). */
void _makepath (char *path, const char *, const char *dir, const char *fname, const char *ext)
{
    if (dir && *dir)
    {
	strcpy (path, dir);
	strcat (path, "/");
    }
    else
	*path = 0;
    strcat (path, fname);
    if (ext && *ext)
    {
	strcat (path, ".");
	strcat (path, ext);
    }
}

void _splitpath (const char *path, char *drive, char *dir, char *fname, char *ext)
{
    const char *slash = strrchr (path, '/');
    const char *dot = strrchr (path, '.');

    *drive = 0;
    if (dot && slash && dot < slash)
	dot = NULL;
    if (slash)
    {
	memcpy (dir, path, slash - path);
	dir [slash - path] = 0;
	path = slash + 1;
    }
    else
	*dir = 0;
    if (dot)
    {
	memcpy (fname, path, dot - path);
	fname [dot - path] = 0;
	strcpy (ext, dot + 1);
    }
    else
    {
	strcpy (fname, path);
	*ext = 0;
    }
}

extern "C"
{
void S9xGenerateSound (void) {}
void S9xLoadSDD1Data (void) {}
uint32 S9xPerfTicks (void) { return 0; }
void S9xPerfRenderDone (uint32) {}
void S9xBeforeDrawingLines (uint32) {}
bool8 S9xDeinitUpdate (int, int, bool8) { return TRUE; }
void S9xSyncSpeed (void) {}
const char *S9xGetFilename (const char *extension) { return extension; }
bool8 S9xReadMousePosition (int, int &, int &, uint32 &) { return FALSE; }
bool8 S9xReadSuperScopePosition (int &, int &, uint32 &) { return FALSE; }
void S9xAutoSaveSRAM (void) {}
void S9xExit (void) { exit (1); }
void S9xMessage (int, int, const char *) {}
void S9xSetPalette (void) {}
const char *S9xGetFilenameInc (const char *extension) { return extension; }
const char *S9xBasename (const char *path) { return path; }
}
