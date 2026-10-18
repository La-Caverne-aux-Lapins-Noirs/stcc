#include "stcc.h"
Object *object_new(const char *n)
{
    Object *o = xcalloc(1, sizeof *o);
    o->name = xstrdup(n);
    return o;
}
Symbol *symbol(Object *o, const char *n, int create)
{
    int i;
    Symbol *s;
    for (i = 0; i < o->nsym; i++)
        if (!strcmp(o->sym[i].name, n))
            return &o->sym[i];
    if (!create)
        return NULL;
    GROW(o->sym, o->nsym, o->csym);
    s = &o->sym[o->nsym++];
    memset(s, 0, sizeof *s);
    s->name = xstrdup(n);
    s->sec = -1;
    return s;
}
void define(Object *o, const char *n, int sec, uint32_t off, const char *alias)
{
    Symbol *s = symbol(o, n, 1);
    if (s->sec != -1)
        die("duplicate symbol '%s'", n);
    s->sec = sec;
    s->off = off;
    if (alias)
        s->alias = xstrdup(alias);
}
void add_fix(Object *o, int sec, uint32_t off, int width, int rel, uint32_t pc, int check,
             const char *expr)
{
    Fixup *r;
    GROW(o->fix, o->nfix, o->cfix);
    r = &o->fix[o->nfix++];
    r->sec = sec;
    r->off = off;
    r->width = width;
    r->relative = rel;
    r->pc = pc;
    r->check = check;
    r->expr = xstrdup(expr);
    r->file = xstrdup(diag_file);
    r->line = diag_line;
}
static void w32(FILE *f, uint32_t v)
{
    int i;
    for (i = 24; i >= 0; i -= 8)
        if (fputc((v >> i) & 255, f) == EOF)
            die("object write failed");
}
static uint32_t r32(FILE *f)
{
    uint32_t v = 0;
    int i, c;
    for (i = 0; i < 4; i++) {
        c = fgetc(f);
        if (c == EOF)
            die("truncated object");
        v = v * 256 + (unsigned)c;
    }
    return v;
}
static void ws(FILE *f, const char *s)
{
    size_t n = s ? strlen(s) : 0;
    w32(f, (uint32_t)n);
    if (n && fwrite(s, 1, n, f) != n)
        die("object write failed");
}
static char *rs(FILE *f)
{
    uint32_t n = r32(f);
    char *s;
    if (n > STCC_MAX_INPUT)
        die("invalid object string");
    s = xmalloc(n + 1);
    if (fread(s, 1, n, f) != n)
        die("truncated object");
    s[n] = 0;
    return s;
}
void object_write(Object *o, const char *path)
{
    FILE *f = fopen(path, "wb");
    int i, j;
    if (!f)
        die("cannot write %s", path);
    fwrite("STCO\0\0\0\1", 1, 8, f);
    for (i = 0; i < 3; i++) {
        w32(f, o->seg[i].n);
        if (i < 2)
            for (j = 0; j < (int)o->seg[i].n; j++)
                fputc(o->seg[i].p[j], f);
    }
    w32(f, (uint32_t)o->nsym);
    for (i = 0; i < o->nsym; i++) {
        Symbol *s = &o->sym[i];
        ws(f, s->name);
        w32(f, (uint32_t)(s->sec + 2));
        w32(f, s->off);
        w32(f, (uint32_t)s->global);
        ws(f, s->alias);
    }
    w32(f, (uint32_t)o->nfix);
    for (i = 0; i < o->nfix; i++) {
        Fixup *r = &o->fix[i];
        w32(f, (uint32_t)r->sec);
        w32(f, r->off);
        w32(f, (uint32_t)r->width);
        w32(f, (uint32_t)r->relative);
        w32(f, r->pc);
        w32(f, (uint32_t)r->check);
        ws(f, r->expr);
        ws(f, r->file);
        w32(f, (uint32_t)r->line);
    }
    if (fclose(f))
        die("object close failed");
}
Object *object_read(const char *path)
{
    FILE *f = fopen(path, "rb");
    Object *o = object_new(path);
    char magic[8];
    uint32_t n, j;
    int i;
    if (!f)
        die("cannot read %s", path);
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "STCO\0\0\0\1", 8))
        die("not an STCO v1 object: %s", path);
    for (i = 0; i < 3; i++) {
        n = r32(f);
        if (n > STCC_MAX_INPUT)
            die("object section too large");
        o->seg[i].n = n;
        if (i < 2) {
            o->seg[i].p = xmalloc(n);
            o->seg[i].cap = n;
            if (fread(o->seg[i].p, 1, n, f) != n)
                die("truncated object");
        }
    }
    n = r32(f);
    if (n > 100000)
        die("invalid symbol count");
    for (j = 0; j < n; j++) {
        char *name = rs(f);
        Symbol *s = symbol(o, name, 1);
        s->sec = (int)r32(f) - 2;
        s->off = r32(f);
        s->global = (int)r32(f);
        s->alias = rs(f);
        if (!*s->alias)
            s->alias = NULL;
        if ((s->sec == -2 && !s->alias) || s->sec < -2 || s->sec > 2 ||
            (s->sec >= 0 && s->off > o->seg[s->sec].n))
            die("invalid object symbol");
    }
    n = r32(f);
    if (n > 1000000)
        die("invalid relocation count");
    for (j = 0; j < n; j++) {
        Fixup *r;
        GROW(o->fix, o->nfix, o->cfix);
        r = &o->fix[o->nfix++];
        r->sec = (int)r32(f);
        r->off = r32(f);
        r->width = (int)r32(f);
        r->relative = (int)r32(f);
        r->pc = r32(f);
        r->check = (int)r32(f);
        r->expr = rs(f);
        r->file = rs(f);
        r->line = (int)r32(f);
        if (r->relative < 0 || r->relative > 2 || r->sec < 0 || r->sec > 1 ||
            (r->width != 1 && r->width != 2 && r->width != 4) || r->off > o->seg[r->sec].n ||
            r->width > (int)(o->seg[r->sec].n - r->off))
            die("invalid object relocation");
    }
    if (fgetc(f) != EOF)
        die("trailing object bytes");
    fclose(f);
    return o;
}

typedef struct {
    Object **objs;
    int n;
    Object *cur;
    int depth;
    int nbuiltin;
    struct {
        const char *name;
        Value value;
    } builtin[13];
} Link;
static void builtin(Link *l, const char *name, uint32_t value, int address)
{
    int i;
    for (i = 0; i < l->n; i++) {
        Symbol *s = symbol(l->objs[i], name, 0);
        if (s && s->sec != -1)
            die("reserved linker symbol '%s'", name);
    }
    l->builtin[l->nbuiltin].name = name;
    l->builtin[l->nbuiltin].value.v = value;
    l->builtin[l->nbuiltin++].value.base = address;
}
static Value lookup(void *p, const char *name)
{
    Link *l = p;
    Symbol *s = symbol(l->cur, name, 0);
    Object *owner = l->cur;
    int i;
    Value v = {0, 0};
    for (i = 0; i < l->nbuiltin; i++)
        if (!strcmp(l->builtin[i].name, name))
            return l->builtin[i].value;
    if (!s || s->sec == -1) {
        s = NULL;
        for (i = 0; i < l->n; i++) {
            Symbol *a = symbol(l->objs[i], name, 0);
            if (a && a->global && a->sec != -1) {
                if (s)
                    die("multiple definitions of '%s'", name);
                s = a;
                owner = l->objs[i];
            }
        }
    }
    if (!s)
        die("undefined symbol '%s'", name);
    if (s->sec == -2) {
        Object *saved = l->cur;
        if (++l->depth > 64)
            die("cyclic EQU '%s'", name);
        l->cur = owner;
        v = expression(s->alias, lookup, l);
        l->cur = saved;
        l->depth--;
        return v;
    }
    v.v = owner->start[s->sec] + s->off;
    v.base = 1;
    return v;
}
static int cmp32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}
static void link_image(Object **objs, int n, const char *out, const char *entry, const char *map,
                       const RomOptions *rom, int check_stack)
{
    uint32_t len[3] = {0, 0, 0}, total = 0, *rel = NULL, nrel = 0, crel = 0, pos = 0, prev;
    uint32_t address[3] = {0, 0, 0}, file_offset[2];
    int i, j, k;
    Bytes image = {0}, file = {0};
    Link l = {0};
    FILE *f;
    Value v;
    l.objs = objs;
    l.n = n;
    if (rom) {
        if (!rom->size || ((rom->base | rom->size | rom->ram_base | rom->ram_limit) & 1) ||
            rom->base >= 0x1000000 || rom->size > 0x1000000 - rom->base || rom->ram_base < 0x400 ||
            rom->ram_base >= rom->ram_limit || rom->ram_limit > 0x400000)
            die("invalid ROM layout: use even addresses/sizes, a 24-bit ROM range and "
                "RAM within 0x400..0x400000");
        if (rom->base < rom->ram_limit && rom->base + rom->size > rom->ram_base)
            die("ROM and RAM ranges overlap");
    }
    for (k = 0; k < 3; k++) {
        address[k] = rom ? (k == 0 ? rom->base : rom->ram_base + (k == 2 ? len[1] : 0)) : total;
        for (i = 0; i < n; i++) {
            len[k] = (len[k] + 1) & ~1u;
            objs[i]->start[k] = address[k] + len[k];
            if (len[k] > STCC_MAX_INPUT * 4u - total ||
                objs[i]->seg[k].n > STCC_MAX_INPUT * 4u - total - len[k])
                die("linked image too large");
            len[k] += objs[i]->seg[k].n;
        }
        len[k] = (len[k] + 1) & ~1u;
        if (len[k] > STCC_MAX_INPUT * 4u - total)
            die("linked image too large");
        total += len[k];
    }
    if (!len[0])
        die("empty TEXT: an executable needs an entry instruction");
    if (total > STCC_MAX_INPUT * 4u)
        die("linked image too large");
    file_offset[0] = 0;
    file_offset[1] = len[0];
    if (rom) {
        if (len[0] + len[1] > rom->size)
            die("ROM overflow: TEXT + DATA need %u bytes, capacity is %u", len[0] + len[1],
                rom->size);
        if (len[1] + len[2] > rom->ram_limit - rom->ram_base)
            die("RAM overflow: DATA + BSS need %u bytes, capacity is %u", len[1] + len[2],
                rom->ram_limit - rom->ram_base);
        builtin(&l, "__stcc_rom_base", rom->base, 1);
        builtin(&l, "__stcc_rom_end", rom->base + rom->size, 1);
        builtin(&l, "__stcc_rom_used_end", rom->base + len[0] + len[1], 1);
        builtin(&l, "__stcc_text_end", rom->base + len[0], 1);
        builtin(&l, "__stcc_data_load", rom->base + len[0], 1);
        builtin(&l, "__stcc_data_start", address[1], 1);
        builtin(&l, "__stcc_data_end", address[1] + len[1], 1);
        builtin(&l, "__stcc_data_size", len[1], 0);
        builtin(&l, "__stcc_bss_start", address[2], 1);
        builtin(&l, "__stcc_bss_end", address[2] + len[2], 1);
        builtin(&l, "__stcc_bss_size", len[2], 0);
        builtin(&l, "__stcc_ram_limit", rom->ram_limit, 1);
    }
    builtin(&l, "__stcc_program_end", address[2] + len[2], 1);
    if (check_stack) {
        l.cur = objs[0];
        v = lookup(&l, "__stcc_stack_end");
        if ((v.v & 1) || v.v < (int64_t)address[1] + 2 ||
            v.v > (rom ? rom->ram_limit : address[2] + len[2]) || (!rom && v.base != 1))
            die("startup stack top must be even and within writable program memory "
                "(a PRG stack must relocate with the program)");
    }
    for (i = 0; i < n; i++)
        for (j = 0; j < objs[i]->nsym; j++) {
            Symbol *s = &objs[i]->sym[j];
            if (s->global && s->sec != -1) {
                int a;
                for (a = i + 1; a < n; a++) {
                    Symbol *t = symbol(objs[a], s->name, 0);
                    if (t && t->global && t->sec != -1)
                        die("multiple definitions of '%s'", s->name);
                }
            }
        }
    if (entry) {
        l.cur = objs[0];
        v = lookup(&l, entry);
        if (v.v != address[0] || v.base != 1)
            die("entry '%s' must be the first TEXT byte (use --nostartfiles for assembly)", entry);
    }
    for (pos = 0; pos < len[0] + len[1]; pos++)
        byte(&image, 0);
    for (i = 0; i < n; i++)
        for (k = 0; k < 2; k++)
            if (objs[i]->seg[k].n)
                memcpy(image.p + file_offset[k] + objs[i]->start[k] - address[k], objs[i]->seg[k].p,
                       objs[i]->seg[k].n);
    for (i = 0; i < n; i++) {
        Object *o = objs[i];
        l.cur = o;
        for (j = 0; j < o->nfix; j++) {
            Fixup *r = &o->fix[j];
            uint32_t at = file_offset[r->sec] + o->start[r->sec] - address[r->sec] + r->off;
            int64_t min, max;
            diag_file = r->file;
            diag_line = r->line;
            v = expression(r->expr, lookup, &l);
            if (r->relative == 1 || (r->relative == 2 && v.base)) {
                v.v -= o->start[r->sec] + r->pc;
                v.base--;
            }
            if (v.base < 0 || v.base > 1)
                die("unrepresentable relocation");
            if (v.base && !rom) {
                if (r->width != 4 || r->relative)
                    die("TOS requires a 32-bit absolute relocation");
                if (!at || (at & 1))
                    die("invalid TOS relocation offset %u", at);
                GROW(rel, nrel, crel);
                rel[nrel++] = at;
            }
            min = r->check == 1 ? -(INT64_C(1) << (r->width * 8 - 1))
                                : -(INT64_C(1) << (r->width * 8 - 1));
            max = (r->check == 1 || r->check == 3) ? (INT64_C(1) << (r->width * 8 - 1)) - 1
                                                   : (INT64_C(1) << (r->width * 8)) - 1;
            if (r->check == 2) {
                if (rom && v.base && v.v >= 0x8000 && v.v < 0xff8000)
                    die("ROM symbol address does not fit sign-extended absolute short");
                if (rom && v.v >= 0xff8000 && v.v <= 0xffffff)
                    v.v = (int32_t)((uint32_t)v.v | 0xff000000u);
                if (v.v >= 0xffff8000LL && v.v <= 0xffffffffLL)
                    v.v = (int32_t)(uint32_t)v.v;
                min = -32768;
                max = 65535;
            }
            if (r->check == 4) {
                min = -128;
                max = 255;
            }
            if (v.v < min || v.v > max)
                die("value %lld does not fit %d bits", (long long)v.v, r->width * 8);
            if (r->check == 3 && v.v == 0)
                die("zero short branch displacement is not encodable on 68000");
            for (k = 0; k < r->width; k++)
                image.p[at + (uint32_t)k] =
                    (unsigned char)((uint64_t)v.v >> (8 * (r->width - 1 - k)));
        }
    }
    if (rom) {
        file.p = xmalloc(rom->size);
        file.n = file.cap = rom->size;
        memset(file.p, 0xff, file.n);
        memcpy(file.p, image.p, image.n);
    } else {
        word(&file, 0x601a);
        lng(&file, len[0]);
        lng(&file, len[1]);
        lng(&file, len[2]);
        lng(&file, 0);
        lng(&file, 0);
        lng(&file, 0);
        word(&file, 0);
        for (pos = 0; pos < image.n; pos++)
            byte(&file, image.p[pos]);
        if (nrel)
            qsort(rel, nrel, sizeof *rel, cmp32);
        if (!nrel)
            lng(&file, 0);
        else {
            lng(&file, rel[0]);
            prev = rel[0];
            for (pos = 1; pos < nrel; pos++) {
                uint32_t delta = rel[pos] - prev;
                if (!delta)
                    die("duplicate relocation");
                while (delta > 254) {
                    byte(&file, 1);
                    delta -= 254;
                }
                byte(&file, delta);
                prev = rel[pos];
            }
            byte(&file, 0);
        }
    }
    f = fopen(out, "wb");
    if (!f)
        die("cannot create %s", out);
    if (fwrite(file.p, 1, file.n, f) != file.n || fclose(f))
        die("cannot write %s", out);
    if (map) {
        f = fopen(map, "w");
        if (!f)
            die("cannot create map");
        fprintf(f, "TEXT %u DATA %u BSS %u\n", len[0], len[1], len[2]);
        if (rom) {
            fprintf(f, "ROM %08x SIZE %u USED %u FILL ff\n", rom->base, rom->size, image.n);
            fprintf(f, "RAM %08x LIMIT %08x\n", rom->ram_base, rom->ram_limit);
        }
        for (i = 0; i < l.nbuiltin; i++)
            fprintf(f, "%08x G %s [linker]\n", (uint32_t)l.builtin[i].value.v, l.builtin[i].name);
        for (i = 0; i < n; i++)
            for (j = 0; j < objs[i]->nsym; j++) {
                Symbol *s = &objs[i]->sym[j];
                if (s->sec >= 0)
                    fprintf(f, "%08x %c %s [%s]\n", objs[i]->start[s->sec] + s->off,
                            s->global ? 'G' : 'L', s->name, objs[i]->name);
                else if (s->sec == -2 && s->global) {
                    l.cur = objs[i];
                    v = lookup(&l, s->name);
                    fprintf(f, "%08x G %s [%s]\n", (uint32_t)v.v, s->name, objs[i]->name);
                }
            }
        fclose(f);
    }
}
void link_tos(Object **objs, int n, const char *out, const char *entry, const char *map,
              int check_stack)
{
    link_image(objs, n, out, entry, map, NULL, check_stack);
}
void link_rom(Object **objs, int n, const char *out, const char *entry, const char *map,
              const RomOptions *rom, int check_stack)
{
    link_image(objs, n, out, entry, map, rom, check_stack);
}
