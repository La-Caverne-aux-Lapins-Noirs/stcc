#include "stcc.h"
const char *diag_file = "stcc";
int diag_line;
void die(const char *f, ...)
{
    va_list ap;
    fprintf(stderr, "%s:%d: error: ", diag_file, diag_line);
    va_start(ap, f);
    vfprintf(stderr, f, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}
void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        die("out of memory");
    return p;
}
void *xcalloc(size_t n, size_t s)
{
    void *p = calloc(n ? n : 1, s);
    if (!p)
        die("out of memory");
    return p;
}
void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p)
        die("out of memory");
    return p;
}
char *slice(const char *s, size_t n)
{
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}
char *xstrdup(const char *s)
{
    return slice(s, strlen(s));
}
char *fmt(const char *f, ...)
{
    va_list a, b;
    int n;
    char *s;
    if (!f)
        die("internal null format");
    va_start(a, f);
    va_copy(b, a);
    n = vsnprintf(NULL, 0, f, a);
    va_end(a);
    if (n < 0)
        die("formatting failed");
    s = xmalloc((size_t)n + 1);
    vsnprintf(s, (size_t)n + 1, f, b);
    va_end(b);
    return s;
}
void putc_s(Str *b, int c)
{
    if (b->n + 2 > b->cap) {
        b->cap = b->cap ? b->cap * 2 : 256;
        b->s = xrealloc(b->s, b->cap);
    }
    b->s[b->n++] = (char)c;
    b->s[b->n] = 0;
}
void puts_s(Str *b, const char *s)
{
    while (*s)
        putc_s(b, *s++);
}
void printf_s(Str *b, const char *f, ...)
{
    va_list a, bp;
    int n;
    char *s;
    if (!f)
        die("internal null format");
    va_start(a, f);
    va_copy(bp, a);
    n = vsnprintf(NULL, 0, f, a);
    va_end(a);
    if (n < 0)
        die("formatting failed");
    s = xmalloc((size_t)n + 1);
    vsnprintf(s, (size_t)n + 1, f, bp);
    va_end(bp);
    puts_s(b, s);
    free(s);
}
char *read_text(const char *p)
{
    FILE *f = fopen(p, "rb");
    long n;
    char *s;
    if (!f)
        die("cannot open %s: %s", p, strerror(errno));
    if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 || (unsigned long)n > STCC_MAX_INPUT)
        die("input too large: %s", p);
    rewind(f);
    s = xmalloc((size_t)n + 1);
    if (fread(s, 1, (size_t)n, f) != (size_t)n)
        die("read failed: %s", p);
    s[n] = 0;
    fclose(f);
    return s;
}
void write_text(const char *p, const char *s)
{
    FILE *f = fopen(p, "wb");
    if (!f)
        die("cannot write %s", p);
    if (fwrite(s, 1, strlen(s), f) != strlen(s) || fclose(f))
        die("write failed: %s", p);
}
void byte(Bytes *b, unsigned v)
{
    if (b->n == b->cap) {
        b->cap = b->cap ? b->cap * 2 : 256;
        b->p = xrealloc(b->p, b->cap);
    }
    b->p[b->n++] = (unsigned char)v;
}
void word(Bytes *b, unsigned v)
{
    byte(b, v >> 8);
    byte(b, v);
}
void lng(Bytes *b, uint32_t v)
{
    word(b, v >> 16);
    word(b, v);
}
int samei(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++))
            return 0;
    }
    return *a == *b;
}
char *trim(char *s)
{
    char *e;
    while (isspace((unsigned char)*s))
        s++;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        *--e = 0;
    return s;
}

/* Devpac expressions use 32-bit arithmetic and Devpac precedence, which is
   deliberately different from C. Relocations carry a load-base coefficient. */
typedef struct {
    const char *p;
    Lookup get;
    void *ctx;
} EX;
static Value exparse(EX *, int);
static Value atom(EX *e)
{
    Value v = {0, 0};
    const char *start;
    char *end;
    int c, radix;
    while (isspace((unsigned char)*e->p))
        e->p++;
    c = *e->p;
    if (c == '+' || c == '-' || c == '~') {
        e->p++;
        v = atom(e);
        if (c == '-') {
            v.v = -v.v;
            v.base = -v.base;
        }
        if (c == '~') {
            if (v.base)
                die("nonlinear relocation");
            v.v = (int32_t) ~(uint32_t)v.v;
        }
        return v;
    }
    if (c == '(') {
        e->p++;
        v = exparse(e, 0);
        while (isspace((unsigned char)*e->p))
            e->p++;
        if (*e->p++ != ')')
            die("expected ')' in expression");
        return v;
    }
    if (c == '\'' || c == '\"') {
        int q = c, n = 0;
        e->p++;
        while (*e->p) {
            c = (unsigned char)*e->p++;
            if (c == q) {
                if (*e->p == q)
                    e->p++;
                else
                    return v;
            }
            if (++n > 4)
                die("assembler character constant exceeds four bytes");
            v.v = (int32_t)(((uint32_t)v.v << 8) | (unsigned)c);
        }
        die("unterminated character constant");
    }
    if (isdigit(c) || c == '$' || c == '%' || (c == '@' && e->p[1] >= '0' && e->p[1] <= '7')) {
        radix = 10;
        if (c == '$') {
            radix = 16;
            e->p++;
        } else if (c == '%') {
            radix = 2;
            e->p++;
        } else if (c == '@') {
            radix = 8;
            e->p++;
        } else if (c == '0' && (e->p[1] == 'x' || e->p[1] == 'X')) {
            radix = 16;
            e->p += 2;
        }
        v.v = (int64_t)strtoull(e->p, &end, radix);
        if (end == e->p || v.v > UINT32_MAX)
            die("invalid 32-bit numeric constant");
        e->p = end;
        return v;
    }
    if (isalpha(c) || c == '_' || c == '.' || c == '@') {
        start = e->p++;
        while (isalnum((unsigned char)*e->p) || (*e->p && strchr("_.@", *e->p)))
            e->p++;
        return e->get(e->ctx, slice(start, (size_t)(e->p - start)));
    }
    die("expected assembler expression near '%s'", e->p);
}
static const char *ops[] = {"!", "|", "^",  "&",  "=", "==", "<>", "!=", "<=", ">=",
                            "<", ">", "<<", ">>", "+", "-",  "*",  "/",  "%",  NULL};
static const int prec[] = {4, 4, 4, 4, 1, 1, 1, 1, 1, 1, 1, 1, 5, 5, 2, 2, 3, 3, 3};
static Value exparse(EX *e, int min)
{
    Value a = atom(e), b;
    int i, found, pr, cmp;
    size_t length;
    uint32_t x, y, result;
    for (;;) {
        while (isspace((unsigned char)*e->p))
            e->p++;
        found = -1;
        length = 0;
        for (i = 0; ops[i]; i++)
            if (!strncmp(e->p, ops[i], strlen(ops[i])) && strlen(ops[i]) > length) {
                found = i;
                length = strlen(ops[i]);
            }
        if (found < 0 || (pr = prec[found]) < min)
            return a;
        e->p += length;
        b = exparse(e, pr + 1);
        if (found == 14 || found == 15) {
            if (found == 14) {
                a.v += b.v;
                a.base += b.base;
            } else {
                a.v -= b.v;
                a.base -= b.base;
            }
            if (!a.base)
                a.v = (int32_t)(uint32_t)a.v;
            continue;
        }
        cmp = found >= 4 && found <= 11;
        if ((a.base || b.base) && !(cmp && a.base == b.base))
            die("operator '%s' requires absolute values", ops[found]);
        x = (uint32_t)a.v;
        y = (uint32_t)b.v;
        result = 0;
        switch (found) {
        case 0:
        case 1:
            result = x | y;
            break;
        case 2:
            result = x ^ y;
            break;
        case 3:
            result = x & y;
            break;
        case 4:
        case 5:
            result = x == y;
            break;
        case 6:
        case 7:
            result = x != y;
            break;
        case 8:
            result = (int32_t)x <= (int32_t)y;
            break;
        case 9:
            result = (int32_t)x >= (int32_t)y;
            break;
        case 10:
            result = (int32_t)x < (int32_t)y;
            break;
        case 11:
            result = (int32_t)x > (int32_t)y;
            break;
        case 12:
        case 13:
            if (y >= 32)
                die("assembler shift count must be 0..31");
            result = found == 12 ? x << y : x >> y;
            break;
        case 16:
            result = x * y;
            break;
        case 17:
        case 18:
            if (!y)
                die("division by zero");
            result = (uint32_t)(found == 17 ? (int64_t)(int32_t)x / (int32_t)y
                                            : (int64_t)(int32_t)x % (int32_t)y);
            break;
        }
        if (cmp)
            result = result ? UINT32_MAX : 0;
        a.v = (int32_t)result;
        a.base = 0;
    }
}
Value expression(const char *s, Lookup f, void *ctx)
{
    EX e;
    Value v;
    e.p = s;
    e.get = f;
    e.ctx = ctx;
    v = exparse(&e, 0);
    while (isspace((unsigned char)*e.p))
        e.p++;
    if (*e.p)
        die("unexpected assembler expression text '%s'", e.p);
    return v;
}
