#include <dirent.h>
#include <stdint.h>
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

static int file_list_touched;

void paths_save_dir_make(void)
{
    char path[600];
    struct stat st;
    paths_save_file(path, sizeof(path), "");
    if (stat(path, &st) != 0 && mkdir(path, 0755) == 0)
        file_list_touched = 1;   /* a new folder in My Documents */
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
    if (!file_exists(to) && rename(from, to) == 0)
        file_list_touched = 1;
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
    if (rmdir(old_dir) == 0)   /* only goes if everything moved */
        file_list_touched = 1;
}

void paths_move_old_files(const char *dir, int with_program_files)
{
    char old_dir[600], from[900], to[900];

    if (with_program_files)
    {
        /* The saves folder's names before (its game orders are already
         * named after their folders): zPocketSNES_saves, and before it had
         * the z, PocketSNES_saves. */
        const char *documents = platform_documents_dir();
        snprintf(old_dir, sizeof(old_dir), "%s/%s", folder_prefix(documents), OLD_Z_SAVE_FOLDER);
        move_folder(old_dir, documents);
        snprintf(old_dir, sizeof(old_dir), "%s/%s", folder_prefix(documents), OLD_SAVE_FOLDER);
        move_folder(old_dir, documents);

        for (size_t i = 0; i < sizeof(program_files) / sizeof(program_files[0]); i++)
        {
            snprintf(from, sizeof(from), "%s/%s", folder_prefix(dir), program_files[i]);
            if (!file_exists(from))
                continue;
            paths_data_file(to, sizeof(to), program_files[i]);
            move_file(from, to);
        }
    }

    snprintf(old_dir, sizeof(old_dir), "%s/.pocketsnes", folder_prefix(dir));
    move_folder(old_dir, dir);
    /* The saves folder in the ROM folder (or the old one in My Documents). */
    snprintf(old_dir, sizeof(old_dir), "%s/%s", folder_prefix(dir), OLD_SAVE_FOLDER);
    move_folder(old_dir, dir);
}

int paths_is_pocketsnes_name(const char *name)
{
    return strcasecmp(name, SAVE_FOLDER) == 0 || strcasecmp(name, OLD_Z_SAVE_FOLDER) == 0 ||
           strncasecmp(name, "pocketsnes", 10) == 0 ||
           strncasecmp(name, "_pocketsnes", 11) == 0;
}

/* The names in the saves folder as sorted hashes (FNV-1a, ignoring case
 * like the OS), malloc'd; NULL with count 0 if there are none. */
static int compare_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *) a, y = *(const uint32_t *) b;
    return x < y ? -1 : x > y;
}

static uint32_t *save_folder_hashes(size_t *count)
{
    char path[600];
    paths_save_file(path, sizeof(path), "");
    *count = 0;
    DIR *dir = opendir(path);
    if (!dir)
        return NULL;
    uint32_t *hashes = NULL;
    size_t capacity = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (*count == capacity)
        {
            size_t grown_capacity = capacity ? capacity * 2 : 256;
            uint32_t *grown = (uint32_t *) realloc(hashes, grown_capacity * sizeof(*hashes));
            if (!grown)
                break;
            hashes = grown;
            capacity = grown_capacity;
        }
        uint32_t hash = 2166136261u;
        for (const unsigned char *c = (const unsigned char *) entry->d_name; *c; c++)
        {
            unsigned char lower = *c >= 'A' && *c <= 'Z' ? *c + 32 : *c;
            hash = (hash ^ lower) * 16777619u;
        }
        hashes[(*count)++] = hash;
    }
    closedir(dir);
    if (*count)
        qsort(hashes, *count, sizeof(*hashes), compare_u32);
    return hashes;
}

static uint32_t *start_names;
static size_t start_count;

void paths_file_list_start(void)
{
    free(start_names);
    start_names = save_folder_hashes(&start_count);
    file_list_touched = 0;
}

void paths_file_list_touched(void)
{
    file_list_touched = 1;
}

int paths_file_list_changed(void)
{
    if (file_list_touched)
        return 1;
    size_t now_count;
    uint32_t *now = save_folder_hashes(&now_count);
    int gone = 0;
    for (size_t i = 0; i < start_count && !gone; i++)
        gone = !now || !bsearch(&start_names[i], now, now_count, sizeof(*now), compare_u32);
    free(now);
    return gone;
}
