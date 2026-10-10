#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "paths.h"
#include "platform.h"

/* "/" is the top folder: paths under it start with "/" alone. */
static const char *folder_prefix(const char *dir)
{
    return strcmp(dir, "/") == 0 ? "" : dir;
}

void paths_save_file(char *path, size_t size, const char *dir, const char *name)
{
    snprintf(path, size, "%s/%s%s%s", folder_prefix(dir), SAVE_FOLDER, name[0] ? "/" : "", name);
}

void paths_save_file_for(char *path, size_t size, const char *file_path, const char *name)
{
    const char *slash = strrchr(file_path, '/');
    char dir[600];
    if (slash)
        snprintf(dir, sizeof(dir), "%.*s", (int) (slash - file_path), file_path);
    else
        snprintf(dir, sizeof(dir), ".");
    paths_save_file(path, size, dir[0] ? dir : "/", name);
}

void paths_data_dir_make(void)
{
    char path[600];
    paths_save_file(path, sizeof(path), platform_exe_dir(), "");
    mkdir(path, 0755);
}

void paths_data_file(char *path, size_t size, const char *name)
{
    paths_data_dir_make();
    paths_save_file(path, size, platform_exe_dir(), name);
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

void paths_move_old_files(const char *dir, int with_program_files)
{
    char old_dir[600], from[900], to[900], new_dir[600];

    snprintf(old_dir, sizeof(old_dir), "%s/.pocketsnes", folder_prefix(dir));
    paths_save_file(new_dir, sizeof(new_dir), dir, "");

    if (with_program_files)
        for (size_t i = 0; i < sizeof(program_files) / sizeof(program_files[0]); i++)
        {
            snprintf(from, sizeof(from), "%s/%s", folder_prefix(dir), program_files[i]);
            if (!file_exists(from))
                continue;
            mkdir(new_dir, 0755);
            paths_save_file(to, sizeof(to), dir, program_files[i]);
            move_file(from, to);
        }

    DIR *old = opendir(old_dir);
    if (!old)
        return;
    mkdir(new_dir, 0755);
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
        paths_save_file(to, sizeof(to), dir, names + at);
        move_file(from, to);
    }
    free(names);
    rmdir(old_dir);   /* only goes if everything moved */
}

int paths_is_pocketsnes_name(const char *name)
{
    return strcasecmp(name, SAVE_FOLDER) == 0 || strncasecmp(name, "pocketsnes", 10) == 0 ||
           strncasecmp(name, "_pocketsnes", 11) == 0;
}
