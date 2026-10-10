#include <ctype.h>
#include <stdint.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <zlib.h>

#include "snes9x.h"
#include "memmap.h"
#include "snapshot.h"
#include "paths.h"
#include "romzip.h"
#include "states.h"

/* A state file is this header followed by the zlib-compressed snapshot. */
#define STATE_MAGIC     "PSST"
#define STATE_VERSION   1
#define MAX_RAW_SIZE    (4 * 1024 * 1024)

struct StateHeader
{
    char     magic[4];
    uint32_t version;
    uint32_t counter;      /* save order: the newest state has the highest */
    uint32_t raw_size;
    uint32_t packed_size;
    uint32_t rom_crc;
    uint32_t reserved[2];
};

static char save_dir[512];
static char base_name[256];   /* ROM file name without its last extension */
static uint8_t used[AUTO_SLOT + 1];
static uint32_t counters[AUTO_SLOT + 1];

static void state_path(int slot, char *path, size_t size)
{
    if (slot == AUTO_SLOT)
        snprintf(path, size, "%s/%s.svauto.tns", save_dir, base_name);
    else
        snprintf(path, size, "%s/%s.sv%03d.tns", save_dir, base_name, slot);
}

/* Slot number of a file name like "Zelda.sfc.sv012.tns" (or ".svauto.tns"),
 * or 0. */
static int slot_from_name(const char *name)
{
    size_t base_len = strlen(base_name);
    const char *p = name + base_len;

    if (strncasecmp(name, base_name, base_len) != 0)
        return 0;
    if (strcasecmp(p, ".svauto.tns") == 0)
        return AUTO_SLOT;
    if (strlen(name) != base_len + 10)
        return 0;
    if (strncasecmp(p, ".sv", 3) != 0 || strcasecmp(p + 6, ".tns") != 0)
        return 0;
    if (!isdigit((unsigned char) p[3]) || !isdigit((unsigned char) p[4]) || !isdigit((unsigned char) p[5]))
        return 0;
    int slot = (p[3] - '0') * 100 + (p[4] - '0') * 10 + (p[5] - '0');
    return slot >= 1 && slot <= MAX_STATE_SLOTS ? slot : 0;
}

static int read_header(const char *path, struct StateHeader *header)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    int ok = fread(header, sizeof(*header), 1, f) == 1 &&
             memcmp(header->magic, STATE_MAGIC, 4) == 0 && header->version == STATE_VERSION;
    fclose(f);
    return ok;
}

/* A zipped ROM's saves are named after the zip ("Game.zip.sv001.tns"). A zip
 * that took the place of the unzipped ROM takes over that ROM's saves, named
 * after the ROM in the zip, unless that ROM is still in the folder. */
static void take_over_unzipped_saves(const char *rom_path)
{
    char inner[256], path[900], to[900];
    if (!romzip_is_zip(rom_path) || !romzip_rom_name(rom_path, inner, sizeof(inner)) ||
        strcasecmp(inner, base_name) == 0)
        return;
    const char *slash = strrchr(rom_path, '/');
    snprintf(path, sizeof(path), "%.*s%s.tns", slash ? (int) (slash - rom_path + 1) : 0, rom_path, inner);
    struct stat st;
    if (stat(path, &st) == 0)
        return;
    snprintf(path, sizeof(path), "%.*s%s", slash ? (int) (slash - rom_path + 1) : 0, rom_path, inner);
    if (stat(path, &st) == 0)
        return;

    DIR *dir = opendir(save_dir);
    if (!dir)
        return;
    size_t base_len = strlen(base_name), inner_len = strlen(inner);
    char (*names)[256] = NULL;
    int count = 0, capacity = 0, own = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        const char *name = entry->d_name;
        if (strncasecmp(name, base_name, base_len) == 0 && name[base_len] == '.')
            own = 1;
        if (strncasecmp(name, inner, inner_len) != 0 || name[inner_len] != '.' || strlen(name) >= 256)
            continue;
        if (count == capacity)
        {
            capacity = capacity ? capacity * 2 : 16;
            char (*grown)[256] = (char (*)[256]) realloc(names, capacity * sizeof(*names));
            if (!grown)
                break;
            names = grown;
        }
        snprintf(names[count++], 256, "%s", name);
    }
    closedir(dir);
    for (int i = 0; !own && i < count; i++)
    {
        snprintf(path, sizeof(path), "%s/%s", save_dir, names[i]);
        snprintf(to, sizeof(to), "%s/%s%s", save_dir, base_name, names[i] + inner_len);
        rename(path, to);
    }
    free(names);
}

void states_open_game(const char *rom_path)
{
    const char *slash = strrchr(rom_path, '/');
    const char *file = slash ? slash + 1 : rom_path;

    paths_save_file_for(save_dir, sizeof(save_dir), rom_path, "");
    if (slash)
    {
        char dir[512];
        snprintf(dir, sizeof(dir), "%.*s", (int) (slash - rom_path), rom_path);
        paths_move_old_files(dir[0] ? dir : "/", 0);
    }

    snprintf(base_name, sizeof(base_name), "%s", file);
    char *dot = strrchr(base_name, '.');
    if (dot && dot != base_name)
        *dot = 0;

    take_over_unzipped_saves(rom_path);

    memset(used, 0, sizeof(used));
    memset(counters, 0, sizeof(counters));

    DIR *dir = opendir(save_dir);
    if (!dir)
        return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        int slot = slot_from_name(entry->d_name);
        if (!slot)
            continue;

        char path[800];
        struct StateHeader header;
        state_path(slot, path, sizeof(path));
        used[slot] = 1;
        counters[slot] = read_header(path, &header) ? header.counter : 0;
    }
    closedir(dir);
}

const char *states_dir(void)
{
    return save_dir;
}

void states_make_dir(void)
{
    mkdir(save_dir, 0755);
}

void states_sram_path(char *path, size_t size)
{
    snprintf(path, size, "%s/%s.srm.tns", save_dir, base_name);
}

int states_exists(int slot)
{
    return slot >= 1 && slot <= AUTO_SLOT && used[slot];
}

int states_count(void)
{
    int count = 0;
    for (int slot = 1; slot <= AUTO_SLOT; slot++)
        count += used[slot];
    return count;
}

int states_newest(void)
{
    int newest = 0;
    for (int slot = 1; slot <= AUTO_SLOT; slot++)
        if (used[slot] && (!newest || counters[slot] >= counters[newest]))
            newest = slot;
    return newest;
}

int states_next_new_slot(void)
{
    int highest = 0;
    for (int slot = 1; slot <= MAX_STATE_SLOTS; slot++)
        if (used[slot])
            highest = slot;
    if (highest < MAX_STATE_SLOTS)
        return highest + 1;
    for (int slot = 1; slot <= MAX_STATE_SLOTS; slot++)
        if (!used[slot])
            return slot;
    return 0;
}

int states_save(int slot)
{
    uint8 *raw = NULL, *packed = NULL;
    uint32 raw_size = 0;
    uLongf packed_size;
    char path[800], temp_path[820];
    struct StateHeader header;
    int ok = 0;

    if (slot < 1 || slot > AUTO_SLOT || !S9xFreezeToMemory(&raw, &raw_size))
        return 0;

    packed_size = compressBound(raw_size);
    packed = (uint8 *) malloc(packed_size);
    if (!packed || compress2(packed, &packed_size, raw, raw_size, Z_BEST_SPEED) != Z_OK)
        goto done;

    memset(&header, 0, sizeof(header));
    memcpy(header.magic, STATE_MAGIC, 4);
    header.version = STATE_VERSION;
    header.raw_size = raw_size;
    header.packed_size = (uint32_t) packed_size;
    header.rom_crc = Memory.ROMCRC32;
    header.counter = 1;
    for (int i = 1; i <= AUTO_SLOT; i++)
        if (used[i] && counters[i] >= header.counter)
            header.counter = counters[i] + 1;

    /* Write a temporary file first so a full disk can't destroy the old state. */
    states_make_dir();
    state_path(slot, path, sizeof(path));
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);
    {
        FILE *f = fopen(temp_path, "wb");
        if (!f)
            goto done;
        int written = fwrite(&header, sizeof(header), 1, f) == 1 &&
                      fwrite(packed, 1, packed_size, f) == packed_size;
        if (fclose(f) != 0 || !written)
        {
            remove(temp_path);
            goto done;
        }
    }
    remove(path);
    if (rename(temp_path, path) != 0)
    {
        remove(temp_path);
        goto done;
    }

    used[slot] = 1;
    counters[slot] = header.counter;
    ok = 1;

done:
    free(packed);
    free(raw);
    return ok;
}

int states_load(int slot)
{
    char path[800];
    struct StateHeader header;
    uint8 *packed = NULL, *raw = NULL;
    int result = -1;

    if (!states_exists(slot))
        return 0;

    state_path(slot, path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        used[slot] = 0;
        return 0;
    }

    if (fread(&header, sizeof(header), 1, f) == 1 &&
        memcmp(header.magic, STATE_MAGIC, 4) == 0 && header.version == STATE_VERSION &&
        header.raw_size <= MAX_RAW_SIZE && header.packed_size <= MAX_RAW_SIZE)
    {
        packed = (uint8 *) malloc(header.packed_size);
        raw = (uint8 *) malloc(header.raw_size);
        uLongf raw_size = header.raw_size;

        if (packed && raw && fread(packed, 1, header.packed_size, f) == header.packed_size &&
            uncompress(raw, &raw_size, packed, header.packed_size) == Z_OK &&
            raw_size == header.raw_size &&
            S9xUnfreezeFromMemory(raw, header.raw_size))
            result = 1;
    }
    fclose(f);
    free(packed);
    free(raw);
    return result;
}

const char *states_slot_name(int slot, char *text, size_t size)
{
    if (slot == AUTO_SLOT)
        snprintf(text, size, "the auto slot");
    else
        snprintf(text, size, "slot %03d", slot);
    return text;
}

const char *states_slot_name_cap(int slot, char *text, size_t size)
{
    states_slot_name(slot, text, size);
    if (text[0] >= 'a' && text[0] <= 'z')
        text[0] = (char) (text[0] - 'a' + 'A');
    return text;
}
