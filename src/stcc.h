#ifndef STCC_H
#define STCC_H
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

#define VERSION "0.2.0"
#define STCC_MAX_INPUT (16u * 1024u * 1024u)
#define GROW(a, n, c)                                                                              \
    do {                                                                                           \
        if ((n) >= (c)) {                                                                          \
            (c) = (c) ? (c) * 2 : 16;                                                              \
            (a) = xrealloc((a), (size_t)(c) * sizeof *(a));                                        \
        }                                                                                          \
    } while (0)
typedef struct {
    char *s;
    size_t n, cap;
} Str;
typedef struct {
    unsigned char *p;
    uint32_t n, cap;
} Bytes;
extern const char *diag_file;
extern int diag_line;
#if defined(__GNUC__)
#define NORETURN __attribute__((noreturn))
#define PRINTF_FMT(a, b) __attribute__((format(printf, a, b)))
#else
#define NORETURN
#define PRINTF_FMT(a, b)
#endif
NORETURN void die(const char *, ...) PRINTF_FMT(1, 2);
void *xmalloc(size_t);
void *xcalloc(size_t, size_t);
void *xrealloc(void *, size_t);
char *xstrdup(const char *);
char *slice(const char *, size_t);
char *fmt(const char *, ...) PRINTF_FMT(1, 2);
char *read_text(const char *);
void write_text(const char *, const char *);
void puts_s(Str *, const char *);
void putc_s(Str *, int);
void printf_s(Str *, const char *, ...) PRINTF_FMT(2, 3);
void byte(Bytes *, unsigned);
void word(Bytes *, unsigned);
void lng(Bytes *, uint32_t);
int samei(const char *, const char *);
char *trim(char *);

typedef struct {
    int64_t v;
    int base;
} Value;
typedef Value (*Lookup)(void *, const char *);
Value expression(const char *, Lookup, void *);

typedef struct {
    char *name;
    int sec, global;
    uint32_t off;
    char *alias;
} Symbol;
typedef struct {
    int sec, width, relative, check;
    uint32_t off, pc;
    char *expr, *file;
    int line;
} Fixup;
typedef struct Object {
    char *name;
    Bytes seg[3];
    Symbol *sym;
    int nsym, csym;
    Fixup *fix;
    int nfix, cfix;
    uint32_t start[3];
} Object;
Object *object_new(const char *);
Symbol *symbol(Object *, const char *, int);
void define(Object *, const char *, int, uint32_t, const char *);
void add_fix(Object *, int, uint32_t, int, int, uint32_t, int, const char *);
void object_write(Object *, const char *);
Object *object_read(const char *);
void link_tos(Object **, int, const char *, const char *, const char *);
Object *assemble(const char *, const char *);
char *preprocess(const char *, char **, int, char **, int);
char *compile_c(const char *, const char *);
int c_preprocessor_condition(const char *);
#endif
