#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "selfstore.h"

#define FOOTER_SIZE 16
static const char magic[8] = { 'P', 'S', 'N', 'E', 'S', 'C', 'F', 'G' };

static uint32_t get32(const unsigned char *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24);
}

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char) v;
    p[1] = (unsigned char) (v >> 8);
    p[2] = (unsigned char) (v >> 16);
    p[3] = (unsigned char) (v >> 24);
}

/* Where the area starts in the open file, or -1 without one; *length gets
 * the text's length. The file's size goes to *file_size. */
static long find_area(FILE *f, long *file_size, uint32_t *length)
{
    unsigned char footer[FOOTER_SIZE];
    if (fseek(f, 0, SEEK_END) != 0)
        return -1;
    *file_size = ftell(f);
    if (*file_size < FOOTER_SIZE + SELFSTORE_SIZE || fseek(f, *file_size - FOOTER_SIZE, SEEK_SET) != 0 ||
        fread(footer, 1, FOOTER_SIZE, f) != FOOTER_SIZE || memcmp(footer, magic, 8) != 0 ||
        get32(footer + 12) != SELFSTORE_SIZE || get32(footer + 8) >= SELFSTORE_SIZE)
        return -1;
    *length = get32(footer + 8);
    return *file_size - FOOTER_SIZE - SELFSTORE_SIZE;
}

static char *read_area(FILE *f, long start, uint32_t length)
{
    char *text = (char *) malloc(length + 1);
    if (!text)
        return NULL;
    if (fseek(f, start, SEEK_SET) != 0 || fread(text, 1, length, f) != length)
    {
        free(text);
        return NULL;
    }
    text[length] = 0;
    return text;
}

char *selfstore_read(const char *program_path)
{
    FILE *f = fopen(program_path, "rb");
    if (!f)
        return NULL;
    long file_size;
    uint32_t length;
    long start = find_area(f, &file_size, &length);
    char *text = start >= 0 ? read_area(f, start, length) : NULL;
    fclose(f);
    return text;
}

int selfstore_write(const char *program_path, const char *text)
{
    size_t length = strlen(text);
    if (length >= SELFSTORE_SIZE)
        return 0;
    FILE *f = fopen(program_path, "r+b");
    if (!f)
        return 0;

    long file_size;
    uint32_t old_length;
    long start = find_area(f, &file_size, &old_length);
    if (start >= 0)
    {
        char *old = read_area(f, start, old_length);
        int same = old && old_length == length && memcmp(old, text, length) == 0;
        free(old);
        if (same)
        {
            fclose(f);
            return 1;
        }
    }
    else
        start = file_size;

    /* The whole area and its footer in one write. */
    unsigned char *area = (unsigned char *) calloc(1, SELFSTORE_SIZE + FOOTER_SIZE);
    int ok = 0;
    if (area)
    {
        memcpy(area, text, length);
        memcpy(area + SELFSTORE_SIZE, magic, 8);
        put32(area + SELFSTORE_SIZE + 8, (uint32_t) length);
        put32(area + SELFSTORE_SIZE + 12, SELFSTORE_SIZE);
        ok = start >= 0 && fseek(f, start, SEEK_SET) == 0 &&
             fwrite(area, 1, SELFSTORE_SIZE + FOOTER_SIZE, f) == SELFSTORE_SIZE + FOOTER_SIZE;
        free(area);
    }
    if (fclose(f) != 0)
        ok = 0;
    return ok;
}
