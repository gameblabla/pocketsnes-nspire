#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "romtypes.h"

static const char *const rom_types[] = { "sfc", "smc", "fig", "swc", "zip" };
#define NUM_TYPES (sizeof(rom_types) / sizeof(rom_types[0]))

/* Ndless's own file is a few hundred bytes; a bigger one than this is left
 * alone. */
#define MAX_CFG_SIZE (256 * 1024)

struct Text
{
    char *data;
    size_t len, capacity;
    int failed;
};

static void add(struct Text *t, const char *s, size_t n)
{
    if (t->failed)
        return;
    if (t->len + n > t->capacity)
    {
        size_t capacity = (t->len + n) * 2 + 64;
        char *grown = (char *) realloc(t->data, capacity);
        if (!grown)
        {
            t->failed = 1;
            return;
        }
        t->data = grown;
        t->capacity = capacity;
    }
    memcpy(t->data + t->len, s, n);
    t->len += n;
}

/* The key and the value of a line, taken the way Ndless reads them (libndls
 * config.c): '#' starts a comment, spaces around the key and the value don't
 * count, the key ends at a space or '=' and the value at a space. Returns 0
 * for a line without a key. */
static int parse_line(const char *line, size_t len, const char **key, size_t *key_len,
                      const char **value, size_t *value_len)
{
    size_t end = 0;
    while (end < len && line[end] != '#' && line[end] != '\r' && line[end] != '\n')
        end++;
    const char *equals = (const char *) memchr(line, '=', end);
    if (!equals)
        return 0;
    const char *k = line;
    while (k < equals && *k == ' ')
        k++;
    const char *k_end = k;
    while (k_end < equals && *k_end != ' ')
        k_end++;
    if (k_end == k)
        return 0;
    const char *v = equals + 1, *line_end = line + end;
    while (v < line_end && *v == ' ')
        v++;
    const char *v_end = v;
    while (v_end < line_end && *v_end != ' ')
        v_end++;
    *key = k;
    *key_len = k_end - k;
    *value = v;
    *value_len = v_end - v;
    return 1;
}

/* Which of rom_types the key "ext.<type>" is for, or NUM_TYPES. */
static size_t key_type(const char *key, size_t key_len)
{
    if (key_len <= 4 || strncmp(key, "ext.", 4) != 0)
        return NUM_TYPES;
    for (size_t type = 0; type < NUM_TYPES; type++)
        if (key_len - 4 == strlen(rom_types[type]) && memcmp(key + 4, rom_types[type], key_len - 4) == 0)
            return type;
    return NUM_TYPES;
}

/* An older PocketSNES's name (the program was renamed, or this is a new
 * version under another name). */
static int is_old_pocketsnes(const char *value, size_t len, const char *program)
{
    if (len == strlen(program) && memcmp(value, program, len) == 0)
        return 0;
    return (len >= 10 && strncasecmp(value, "pocketsnes", 10) == 0) ||
           (len >= 11 && strncasecmp(value, "_pocketsnes", 11) == 0);
}

char *romtypes_update(const char *text, size_t len, const char *program, size_t *new_len)
{
    struct Text out = { NULL, 0, 0, 0 };
    int has_type[NUM_TYPES] = { 0 };
    int changed = 0;
    size_t program_len = strlen(program);

    for (size_t at = 0; text && at < len; )
    {
        const char *line = text + at;
        const char *newline = (const char *) memchr(line, '\n', len - at);
        size_t line_len = newline ? (size_t) (newline - line) + 1 : len - at;
        const char *key, *value;
        size_t key_len, value_len, type = NUM_TYPES;
        if (parse_line(line, line_len, &key, &key_len, &value, &value_len))
            type = key_type(key, key_len);
        if (type < NUM_TYPES && is_old_pocketsnes(value, value_len, program))
        {
            /* Only the name changes; spaces, a comment and the line's end stay. */
            add(&out, line, value - line);
            add(&out, program, program_len);
            add(&out, value + value_len, line + line_len - (value + value_len));
            changed = 1;
        }
        else
            add(&out, line, line_len);
        if (type < NUM_TYPES)
            has_type[type] = 1;
        at += line_len;
    }

    /* The types that have no program yet, as cfg_register_fileext adds them. */
    for (size_t type = 0; type < NUM_TYPES; type++)
    {
        if (has_type[type])
            continue;
        if (out.len && out.data[out.len - 1] != '\n')
            add(&out, "\n", 1);
        add(&out, "ext.", 4);
        add(&out, rom_types[type], strlen(rom_types[type]));
        add(&out, "=", 1);
        add(&out, program, program_len);
        add(&out, "\n", 1);
        changed = 1;
    }

    if (!changed || out.failed)
    {
        free(out.data);
        return NULL;
    }
    *new_len = out.len;
    return out.data;
}

int romtypes_register(const char *cfg_path, const char *program)
{
    char *text = NULL;
    size_t len = 0;
    FILE *f = fopen(cfg_path, "rb");
    if (f)
    {
        /* Only a file read whole is written back. */
        long size = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
        if (size < 0 || size > MAX_CFG_SIZE || fseek(f, 0, SEEK_SET) != 0 ||
            !(text = (char *) malloc(size ? size : 1)) || fread(text, 1, size, f) != (size_t) size)
        {
            fclose(f);
            free(text);
            return 0;
        }
        fclose(f);
        len = size;
    }

    size_t new_len = 0;
    char *updated = romtypes_update(text, len, program, &new_len);
    free(text);
    if (!updated)
        return 0;
    f = fopen(cfg_path, "wb");
    int written = f != NULL;
    if (f)
    {
        fwrite(updated, 1, new_len, f);
        fclose(f);
    }
    free(updated);
    return written;
}
