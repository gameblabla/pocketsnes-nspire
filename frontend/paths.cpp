#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "paths.h"
#include "platform.h"

#define ORDER_FILE "game_order.txt.tns"

/* "/" is the top folder: paths under it start with "/" alone. */
static const char *folder_prefix(const char *dir)
{
    return strcmp(dir, "/") == 0 ? "" : dir;
}

void paths_save_file(char *path, size_t size, const char *name)
{
    snprintf(path, size, "%s/%s%s%s", folder_prefix(platform_documents_dir()), SAVE_FOLDER,
             name[0] ? "/" : "", name);
}

void paths_save_dir_make(void)
{
    char path[600];
    paths_save_file(path, sizeof(path), "");
    mkdir(path, 0755);
}

void paths_data_file(char *path, size_t size, const char *name)
{
    paths_save_dir_make();
    paths_save_file(path, size, name);
}

/* The order file's name for dir, without the folder. */
static void order_name(char *name, size_t size, const char *dir)
{
    const char *documents = platform_documents_dir();
    size_t len = strlen(documents);
    const char *rel = dir;
    if (strncmp(dir, documents, len) == 0 && (dir[len] == '/' || dir[len] == 0))
        rel = dir + len;
    while (*rel == '/')
        rel++;
    if (!*rel)
    {
        snprintf(name, size, "%s", ORDER_FILE);
        return;
    }
    int written = snprintf(name, size, "game_order_%s", rel);
    for (char *c = name + 11; *c; c++)
        if (*c == '/')
            *c = '_';
    if (written >= 0 && (size_t) written + 8 < size)
        strcat(name, ".txt.tns");
}

void paths_order_file(char *path, size_t size, const char *dir)
{
    char name[300];
    order_name(name, sizeof(name), dir);
    paths_save_file(path, size, name);
}

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

/* Moves from to to unless to is there already. */
static void move_file(const char *from, const char *to)
{
    if (!file_exists(to))
        rename(from, to);
}

/* PocketSNES's own files, which older versions kept next to the program. */
static const char *const program_files[] =
{
    "pocketsnes.cfg.tns", "pocketsnes_results.txt.tns", "pocketsnes_overclock.txt.tns",
    "pocketsnes_diag.txt.tns", "pocketsnes_perf.txt.tns", "pocketsnes_clock.tns",
    "pocketsnes_octest.tns", "pocketsnes_sram.bin.tns"
};

/* Everything in old_dir into the saves folder, then old_dir goes. Its game
 * order was for dir. */
static void move_folder(const char *old_dir, const char *dir)
{
    char from[900], to[900];
    DIR *old = opendir(old_dir);
    if (!old)
        return;
    paths_save_dir_make();
    /* The names first, then the moves: renaming while the folder is read
     * can skip files. */
    char *names = NULL;
    size_t used = 0, capacity = 0;
    struct dirent *entry;
    while ((entry = readdir(old)) != NULL)
    {
        size_t len = strlen(entry->d_name) + 1;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (used + len > capacity)
        {
            size_t grown_capacity = capacity ? capacity * 2 + len : 4096;
            char *grown = (char *) realloc(names, grown_capacity);
            if (!grown)
                break;
            names = grown;
            capacity = grown_capacity;
        }
        memcpy(names + used, entry->d_name, len);
        used += len;
    }
    closedir(old);
    for (size_t at = 0; at < used; at += strlen(names + at) + 1)
    {
        snprintf(from, sizeof(from), "%s/%s", old_dir, names + at);
        if (strcmp(names + at, ORDER_FILE) == 0)
            paths_order_file(to, sizeof(to), dir);
        else
            paths_save_file(to, sizeof(to), names + at);
        move_file(from, to);
    }
    free(names);
    rmdir(old_dir);   /* only goes if everything moved */
}

void paths_move_old_files(const char *dir, int with_program_files)
{
    char old_dir[600], from[900], to[900], saves[600];

    if (with_program_files)
        for (size_t i = 0; i < sizeof(program_files) / sizeof(program_files[0]); i++)
        {
            snprintf(from, sizeof(from), "%s/%s", folder_prefix(dir), program_files[i]);
            if (!file_exists(from))
                continue;
            paths_data_file(to, sizeof(to), program_files[i]);
            move_file(from, to);
        }

    snprintf(old_dir, sizeof(old_dir), "%s/.pocketsnes", folder_prefix(dir));
    move_folder(old_dir, dir);
    /* The saves folder in the ROM folder, unless that's My Documents, where
     * it is the saves folder. */
    snprintf(old_dir, sizeof(old_dir), "%s/%s", folder_prefix(dir), SAVE_FOLDER);
    paths_save_file(saves, sizeof(saves), "");
    if (strcmp(old_dir, saves) != 0)
        move_folder(old_dir, dir);
}

int paths_is_pocketsnes_name(const char *name)
{
    return strcasecmp(name, SAVE_FOLDER) == 0 || strncasecmp(name, "pocketsnes", 10) == 0 ||
           strncasecmp(name, "_pocketsnes", 11) == 0;
}
