/* What the Ndless toolchain's newlib leaves out (Ndless provides it on the
 * calculator), for the test programs run under qemu-arm: the malloc family,
 * abort and _isatty. First fit over _sbrk, free blocks kept in address order
 * and merged with their neighbours. */
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <reent.h>

typedef struct Block { size_t size; struct Block *next; } Block;   /* size includes the header */
#define HDR ((sizeof(Block) + 7) & ~(size_t) 7)
static Block *free_list;

static size_t round_size(size_t n) { return (n + HDR + 7) & ~(size_t) 7; }

void *malloc(size_t n)
{
    if (n == 0) n = 1;
    size_t need = round_size(n);
    Block **pp = &free_list;
    for (Block *b = free_list; b; pp = &b->next, b = b->next)
        if (b->size >= need)
        {
            if (b->size - need >= HDR + 16)
            {
                Block *rest = (Block *) ((char *) b + need);
                rest->size = b->size - need;
                rest->next = b->next;
                *pp = rest;
                b->size = need;
            }
            else
                *pp = b->next;
            return (char *) b + HDR;
        }
    size_t grow = need < (1 << 20) ? (1 << 20) : need;
    extern void *_sbrk(ptrdiff_t);
    char *p = _sbrk(grow);
    if (p == (char *) -1) return NULL;
    Block *b = (Block *) p;
    b->size = grow;
    if (grow - need >= HDR + 16)
    {
        Block *rest = (Block *) (p + need);
        rest->size = grow - need;
        b->size = need;
        /* hand the rest to free() so it merges */
        extern void free(void *);
        free((char *) rest + HDR);
    }
    return p + HDR;
}

void free(void *ptr)
{
    if (!ptr) return;
    Block *b = (Block *) ((char *) ptr - HDR);
    Block **pp = &free_list, *prev = NULL;
    while (*pp && *pp < b) { prev = *pp; pp = &(*pp)->next; }
    b->next = *pp;
    *pp = b;
    if (b->next && (char *) b + b->size == (char *) b->next)
    {
        b->size += b->next->size;
        b->next = b->next->next;
    }
    if (prev && (char *) prev + prev->size == (char *) b)
    {
        prev->size += b->size;
        prev->next = b->next;
    }
}

void *calloc(size_t a, size_t b)
{
    size_t n = a * b;
    if (b && n / b != a) return NULL;
    void *p = malloc(n);
    if (p) memset(p, 0, n);
    return p;
}

void *realloc(void *ptr, size_t n)
{
    if (!ptr) return malloc(n);
    Block *b = (Block *) ((char *) ptr - HDR);
    size_t have = b->size - HDR;
    if (have >= n) return ptr;
    void *q = malloc(n);
    if (q) { memcpy(q, ptr, have); free(ptr); }
    return q;
}

void *_malloc_r(struct _reent *r, size_t n) { (void) r; return malloc(n); }
void _free_r(struct _reent *r, void *p) { (void) r; free(p); }
void *_calloc_r(struct _reent *r, size_t a, size_t b) { (void) r; return calloc(a, b); }
void *_realloc_r(struct _reent *r, void *p, size_t n) { (void) r; return realloc(p, n); }
void abort(void) { _exit(134); }
__attribute__((weak)) int _isatty(int fd) { return fd <= 2; }
