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
#include "config.h"
#include "paths.h"
#include "romzip.h"
#include "states.h"

/* A state file is this header followed by the snapshot, zlib-compressed,
 * or as it is when the header's flags say STATE_STORED (Saving > Compress
 * saves off, or the save manager). */
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
    uint32_t flags;
    uint32_t reserved;
};

#define STATE_STORED 1

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

    memset(&header, 0, sizeof(header));
    if (cfg.compress_saves)
    {
        packed_size = compressBound(raw_size);
        packed = (uint8 *) malloc(packed_size);
        if (!packed || compress2(packed, &packed_size, raw, raw_size, Z_BEST_SPEED) != Z_OK)
            goto done;
    }
    else
    {
        packed_size = raw_size;
        header.flags = STATE_STORED;
    }

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
                      fwrite(packed ? packed : raw, 1, packed_size, f) == packed_size;
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
            ((header.flags & STATE_STORED) ? header.packed_size == header.raw_size &&
                                             (memcpy(raw, packed, raw_size), 1)
                                           : uncompress(raw, &raw_size, packed, header.packed_size) == Z_OK) &&
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

/* ---- In-game saves ----
 *
 * "<ROM name>.srm.tns" as the game wrote it (what PC emulators read, as
 * "<ROM name without .sfc>.srm"), or "<ROM name>.srm.gz.tns" gzipped when
 * Compress saves is on (a PC unpacks it like any .gz). */

static void sram_paths(char *plain, char *gz, size_t size)
{
    snprintf(plain, size, "%s/%s.srm.tns", save_dir, base_name);
    snprintf(gz, size, "%s/%s.srm.gz.tns", save_dir, base_name);
}

static long file_bytes(const char *path, long *mtime)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return -1;
    if (mtime)
        *mtime = (long) st.st_mtim.tv_sec;
    return (long) st.st_size;
}

/* A whole small file (in-game saves are 128 KB at most), malloc'd. */
static uint8_t *read_whole(const char *path, size_t *size, size_t max)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    uint8_t *data = (uint8_t *) malloc(max + 1);
    size_t got = data ? fread(data, 1, max + 1, f) : 0;
    fclose(f);
    if (!data || got > max)
    {
        free(data);
        return NULL;
    }
    *size = got;
    return data;
}

/* Writes data to path through a temporary file, so a full disk can't
 * destroy what was there. */
static int write_whole(const char *path, const void *data, size_t size)
{
    char temp[900];
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    FILE *f = fopen(temp, "wb");
    if (!f)
        return 0;
    int ok = fwrite(data, 1, size, f) == size;
    if (fclose(f) != 0)
        ok = 0;
    if (ok)
    {
        remove(path);
        ok = rename(temp, path) == 0;
    }
    if (!ok)
        remove(temp);
    return ok;
}

/* gzip (windowBits 31) or gunzip (47: gzip or zlib) of a buffer. */
static uint8_t *gz_convert(const uint8_t *in, size_t in_size, size_t *out_size, int pack)
{
    size_t capacity = pack ? in_size + in_size / 1000 + 64 : 0x20000 + 0x400;
    uint8_t *out = (uint8_t *) malloc(capacity);
    if (!out)
        return NULL;
    z_stream z;
    memset(&z, 0, sizeof(z));
    int ok = (pack ? deflateInit2(&z, Z_BEST_COMPRESSION, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY)
                   : inflateInit2(&z, 47)) == Z_OK;
    if (ok)
    {
        z.next_in = (Bytef *) in;
        z.avail_in = (uInt) in_size;
        z.next_out = out;
        z.avail_out = (uInt) capacity;
        ok = (pack ? deflate(&z, Z_FINISH) : inflate(&z, Z_FINISH)) == Z_STREAM_END;
        *out_size = z.total_out;
        if (pack)
            deflateEnd(&z);
        else
            inflateEnd(&z);
    }
    if (!ok)
    {
        free(out);
        return NULL;
    }
    return out;
}

/* Converts one file to the other form and removes the first. */
static int gz_file(const char *from, const char *to, int pack)
{
    size_t size, out_size;
    uint8_t *data = read_whole(from, &size, 0x40000);
    if (!data)
        return 0;
    uint8_t *out = gz_convert(data, size, &out_size, pack);
    int ok = out && write_whole(to, out, out_size);
    free(out);
    free(data);
    if (ok)
        remove(from);
    return ok;
}

void states_load_sram(void)
{
    char plain[900], gz[900], temp[900];
    long plain_time = 0, gz_time = 0;
    sram_paths(plain, gz, sizeof(plain));

    /* One from a PC emulator, named after the ROM without its extension
     * ("Game.srm.tns" for "Game.sfc.tns"), is taken when the game has none. */
    if (file_bytes(plain, NULL) < 0 && file_bytes(gz, NULL) < 0)
    {
        char pc_name[300], pc_path[900];
        snprintf(pc_name, sizeof(pc_name), "%s", base_name);
        char *dot = strrchr(pc_name, '.');
        if (dot && dot != pc_name)
        {
            *dot = 0;
            snprintf(pc_path, sizeof(pc_path), "%s/%s.srm.tns", save_dir, pc_name);
            if (file_bytes(pc_path, NULL) >= 0)
                rename(pc_path, plain);
        }
    }

    int has_plain = file_bytes(plain, &plain_time) >= 0, has_gz = file_bytes(gz, &gz_time) >= 0;
    /* Both: the newer one (one copied in by hand, say). */
    if (has_gz && (!has_plain || gz_time > plain_time))
    {
        size_t size, out_size;
        uint8_t *data = read_whole(gz, &size, 0x40000);
        uint8_t *raw = data ? gz_convert(data, size, &out_size, 0) : NULL;
        snprintf(temp, sizeof(temp), "%s/%s.srm.load.tns", save_dir, base_name);
        if (raw && write_whole(temp, raw, out_size))
        {
            Memory.LoadSRAM (temp);
            remove(temp);
        }
        else
            Memory.LoadSRAM (plain);   /* clears it, as for no save */
        free(raw);
        free(data);
    }
    else
        Memory.LoadSRAM (plain);
}

void states_save_sram(void)
{
    char plain[900], gz[900];
    sram_paths(plain, gz, sizeof(plain));
    states_make_dir();
    if (!cfg.compress_saves)
    {
        if (Memory.SaveSRAM (plain))
            remove(gz);
        return;
    }
    /* The core writes it plain; then it's packed. */
    char temp[900];
    snprintf(temp, sizeof(temp), "%s/%s.srm.save.tns", save_dir, base_name);
    remove(temp);
    if (Memory.SaveSRAM (temp) && file_bytes(temp, NULL) >= 0 && gz_file(temp, gz, 1))
        remove(plain);
    remove(temp);
}

/* ---- The save manager ---- */

int states_list(struct SaveFile *files, int max)
{
    char plain[900], gz[900], path[900];
    int count = 0;
    long bytes;

    sram_paths(plain, gz, sizeof(plain));
    if ((bytes = file_bytes(gz, NULL)) >= 0 && count < max)
    {
        files[count].slot = 0;
        files[count].bytes = bytes;
        files[count++].compressed = 1;
    }
    else if ((bytes = file_bytes(plain, NULL)) >= 0 && count < max)
    {
        files[count].slot = 0;
        files[count].bytes = bytes;
        files[count++].compressed = 0;
    }
    for (int n = 0; n <= MAX_STATE_SLOTS && count < max; n++)
    {
        int slot = n == 0 ? AUTO_SLOT : n;
        if (!used[slot])
            continue;
        struct StateHeader header;
        state_path(slot, path, sizeof(path));
        bytes = file_bytes(path, NULL);
        if (bytes < 0)
            continue;
        files[count].slot = slot;
        files[count].bytes = bytes;
        files[count++].compressed = read_header(path, &header) && !(header.flags & STATE_STORED);
    }
    return count;
}

int states_set_compressed(int slot, int compress)
{
    char path[900];
    if (slot == 0)
    {
        char plain[900], gz[900];
        sram_paths(plain, gz, sizeof(plain));
        if (compress)
            return file_bytes(gz, NULL) >= 0 || gz_file(plain, gz, 1);
        return file_bytes(plain, NULL) >= 0 && file_bytes(gz, NULL) < 0 ? 1 : gz_file(gz, plain, 0);
    }
    if (!states_exists(slot))
        return 0;

    state_path(slot, path, sizeof(path));
    size_t size;
    uint8_t *data = read_whole(path, &size, MAX_RAW_SIZE + sizeof(struct StateHeader));
    if (!data || size < sizeof(struct StateHeader))
    {
        free(data);
        return 0;
    }
    struct StateHeader header;
    memcpy(&header, data, sizeof(header));
    const uint8_t *payload = data + sizeof(header);
    int stored = (header.flags & STATE_STORED) != 0;
    if (memcmp(header.magic, STATE_MAGIC, 4) != 0 || header.version != STATE_VERSION ||
        header.packed_size != size - sizeof(header) || header.raw_size > MAX_RAW_SIZE)
    {
        free(data);
        return 0;
    }
    if (stored == !compress)
    {
        free(data);
        return 1;   /* already so */
    }

    uLongf out_size = compress ? compressBound(header.raw_size) : header.raw_size;
    uint8_t *out = (uint8_t *) malloc(sizeof(header) + out_size);
    int ok = out != NULL;
    if (ok && compress)
        ok = compress2(out + sizeof(header), &out_size, payload, header.raw_size, Z_BEST_SPEED) == Z_OK;
    else if (ok)
        ok = uncompress(out + sizeof(header), &out_size, payload, header.packed_size) == Z_OK &&
             out_size == header.raw_size;
    if (ok)
    {
        header.packed_size = (uint32_t) out_size;
        header.flags = compress ? header.flags & ~STATE_STORED : header.flags | STATE_STORED;
        memcpy(out, &header, sizeof(header));
        ok = write_whole(path, out, sizeof(header) + out_size);
    }
    free(out);
    free(data);
    return ok;
}

int states_delete(int slot)
{
    char path[900], gz[900];
    if (slot == 0)
    {
        sram_paths(path, gz, sizeof(path));
        int a = remove(path) == 0, b = remove(gz) == 0;
        return a || b;
    }
    if (!states_exists(slot))
        return 0;
    state_path(slot, path, sizeof(path));
    if (remove(path) != 0)
        return 0;
    used[slot] = 0;
    counters[slot] = 0;
    return 1;
}
