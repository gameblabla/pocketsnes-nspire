#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zlib.h>

#include "romzip.h"

static uint32_t get16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static uint32_t get32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24); }

struct ZipEntry
{
    char name[256];
    uint32_t method, crc, packed, size, header_offset;
};

int romzip_is_zip(const char *path)
{
    unsigned char head[4];
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    int zip = fread(head, 1, 4, f) == 4 && get32(head) == 0x04034B50;
    fclose(f);
    return zip;
}

static int has_rom_extension(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && (strcasecmp(dot, ".sfc") == 0 || strcasecmp(dot, ".smc") == 0 ||
                   strcasecmp(dot, ".swc") == 0 || strcasecmp(dot, ".fig") == 0);
}

/* Finds the ROM's entry through the central directory at the end of the
 * zip (sizes are always right there, even when the file's own header
 * leaves them out). */
static int find_rom(FILE *f, struct ZipEntry *rom)
{
    unsigned char tail[22 + 0xFFFF];
    if (fseek(f, 0, SEEK_END) != 0)
        return 0;
    long file_size = ftell(f);
    long tail_size = file_size < (long) sizeof(tail) ? file_size : (long) sizeof(tail);
    if (tail_size < 22 || fseek(f, file_size - tail_size, SEEK_SET) != 0 ||
        fread(tail, 1, tail_size, f) != (size_t) tail_size)
        return 0;

    /* The end record: the last "PK\5\6". */
    long end = -1;
    for (long i = tail_size - 22; i >= 0; i--)
        if (get32(tail + i) == 0x06054B50)
        {
            end = i;
            break;
        }
    if (end < 0)
        return 0;
    uint32_t count = get16(tail + end + 10), offset = get32(tail + end + 16);

    int found = 0, by_extension = 0;
    if (fseek(f, offset, SEEK_SET) != 0)
        return 0;
    for (uint32_t i = 0; i < count; i++)
    {
        unsigned char header[46];
        if (fread(header, 1, 46, f) != 46 || get32(header) != 0x02014B50)
            break;
        uint32_t name_len = get16(header + 28), extra = get16(header + 30), comment = get16(header + 32);
        struct ZipEntry entry;
        size_t keep = name_len < sizeof(entry.name) - 1 ? name_len : sizeof(entry.name) - 1;
        if (fread(entry.name, 1, keep, f) != keep)
            break;
        entry.name[keep] = 0;
        entry.method = get16(header + 10);
        entry.crc = get32(header + 16);
        entry.packed = get32(header + 20);
        entry.size = get32(header + 24);
        entry.header_offset = get32(header + 42);
        if (fseek(f, (long) (name_len - keep + extra + comment), SEEK_CUR) != 0)
            break;

        size_t len = strlen(entry.name);
        /* The core can't take files under 32 KB: no SNES ROM is that small. */
        if (!len || entry.name[len - 1] == '/' || (entry.method != 0 && entry.method != 8) || entry.size < 0x8000)
            continue;
        /* The first ROM by its extension, or else the biggest file. */
        int take = has_rom_extension(entry.name) ? !by_extension : !by_extension && (!found || entry.size > rom->size);
        if (take)
        {
            *rom = entry;
            found = 1;
            by_extension = has_rom_extension(entry.name);
        }
    }
    return found;
}

int romzip_rom_name(const char *path, char *name, size_t size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    struct ZipEntry rom;
    int found = find_rom(f, &rom);
    fclose(f);
    if (!found)
        return 0;
    /* Only the file's name, not folders inside the zip. */
    const char *slash = strrchr(rom.name, '/');
    snprintf(name, size, "%s", slash ? slash + 1 : rom.name);
    return 1;
}

uint32_t romzip_read(const char *path, uint8_t *buffer, uint32_t max)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    struct ZipEntry rom;
    uint32_t result = 0;
    unsigned char local[30];

    if (!find_rom(f, &rom) || rom.size > max || fseek(f, rom.header_offset, SEEK_SET) != 0 ||
        fread(local, 1, 30, f) != 30 || get32(local) != 0x04034B50 ||
        fseek(f, (long) (get16(local + 26) + get16(local + 28)), SEEK_CUR) != 0)
    {
        fclose(f);
        return 0;
    }

    if (rom.method == 0)
    {
        if (fread(buffer, 1, rom.size, f) == rom.size)
            result = rom.size;
    }
    else
    {
        z_stream z;
        memset(&z, 0, sizeof(z));
        static unsigned char chunk[16384];
        uint32_t left = rom.packed;
        if (inflateInit2(&z, -MAX_WBITS) == Z_OK)
        {
            z.next_out = buffer;
            z.avail_out = rom.size;
            int status = Z_OK;
            while (status == Z_OK)
            {
                if (z.avail_in == 0)
                {
                    uint32_t want = left < sizeof(chunk) ? left : sizeof(chunk);
                    if (want == 0 || fread(chunk, 1, want, f) != want)
                        break;
                    left -= want;
                    z.next_in = chunk;
                    z.avail_in = want;
                }
                status = inflate(&z, Z_NO_FLUSH);
            }
            if (status == Z_STREAM_END && z.total_out == rom.size)
                result = rom.size;
            inflateEnd(&z);
        }
    }
    fclose(f);
    /* A damaged zip mustn't start as a damaged game. */
    if (result && crc32(crc32(0, NULL, 0), buffer, result) != rom.crc)
        result = 0;
    return result;
}
