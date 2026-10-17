#include "stcc.h"

typedef struct AMacro {
    char *name, *body;
    struct AMacro *next;
} AMacro;
typedef struct {
    Object *o;
    int sec, serial;
    char *scope, *here;
    AMacro *macros;
} Asm;
typedef struct {
    int bits, width, pc, check, index;
    char *expr;
} EA;
static Bytes *seg(Asm *a)
{
    return &a->o->seg[a->sec];
}
static void emit(Asm *a, unsigned w)
{
    if (a->sec == 2)
        die("instruction/data in BSS");
    word(seg(a), w);
}
static void zeros(Asm *a, uint32_t n)
{
    if (n > STCC_MAX_INPUT || seg(a)->n > STCC_MAX_INPUT - n)
        die("section too large");
    if (a->sec == 2)
        seg(a)->n += n;
    else
        while (n--)
            byte(seg(a), 0);
}
static char *canon(Asm *a, const char *s)
{
    Str b = {0};
    int expect = 1;
    while (*s) {
        if (*s == '\'' || *s == '\"') {
            int q = *s;
            putc_s(&b, *s++);
            while (*s && *s != q)
                putc_s(&b, *s++);
            if (*s)
                putc_s(&b, *s++);
            expect = 0;
        } else if (isalpha((unsigned char)*s) || *s == '_' || *s == '.' || *s == '@') {
            const char *p = s++;
            while (isalnum((unsigned char)*s) || (*s && strchr("_.@", *s)))
                s++;
            if (*p == '.') {
                if (!a->scope)
                    die("local label without enclosing global label");
                puts_s(&b, a->scope);
            }
            puts_s(&b, slice(p, (size_t)(s - p)));
            expect = 0;
        } else if (*s == '*' && expect) {
            puts_s(&b, a->here);
            s++;
            expect = 0;
        } else {
            int c = *s++;
            putc_s(&b, c);
            if (isdigit(c) || c == ')')
                expect = 0;
            else if (!isspace(c))
                expect = 1;
        }
    }
    return b.s ? b.s : xstrdup("");
}
static Value early(void *p, const char *n)
{
    Asm *a = p;
    Symbol *s = symbol(a->o, n, 0);
    Value v = {0, 0};
    static int depth;
    if (!s || s->sec == -1)
        die("constant must already be defined: '%s'", n);
    if (s->sec == -2) {
        if (++depth > 64)
            die("cyclic constant");
        v = expression(s->alias, early, a);
        depth--;
        return v;
    }
    if (s->sec != a->sec)
        die("layout-dependent assembly-time constant: '%s'", n);
    v.v = s->off;
    v.base = 1;
    return v;
}
static int64_t number(Asm *a, const char *s)
{
    Value v = expression(canon(a, s), early, a);
    if (v.base)
        die("absolute value required");
    return v.v;
}
static void fixval(Asm *a, const char *expr, int width, int relative, uint32_t pc, int check)
{
    uint32_t at = seg(a)->n;
    if (a->sec == 2)
        die("initialized BSS");
    add_fix(a->o, a->sec, at, width, relative, pc, check, canon(a, expr));
    zeros(a, (uint32_t)width);
}
/* Commas inside effective-address parentheses or quoted strings are not separators. */
static int split(char *s, char **out, int max)
{
    int n = 0, level = 0, q = 0;
    char *p = s, *start = s;
    if (!*trim(s))
        return 0;
    for (;; p++) {
        int c = *p;
        if (q) {
            if (c == q) {
                if (p[1] == q)
                    p++;
                else
                    q = 0;
            }
        } else if (c == '\'' || c == '\"')
            q = c;
        else if (c == '(')
            level++;
        else if (c == ')')
            level--;
        if (!c || (c == ',' && !q && !level)) {
            if (n == max)
                die("too many operands");
            if (c)
                *p = 0;
            out[n++] = trim(start);
            start = p + 1;
            if (!c)
                break;
        }
    }
    if (level || q)
        die("unbalanced assembler operand");
    return n;
}
static int reg(const char *s)
{
    if (samei(s, "sp"))
        return 15;
    if (strlen(s) == 2 && s[1] >= '0' && s[1] <= '7') {
        if (tolower((unsigned char)s[0]) == 'd')
            return s[1] - '0';
        if (tolower((unsigned char)s[0]) == 'a')
            return 8 + s[1] - '0';
    }
    return -1;
}
static EA ea(char *text, int sz)
{
    EA e;
    char *s = trim(text), *p, *end, *v[3];
    int r, n, base, idx;
    memset(&e, 0, sizeof e);
    e.check = 0;
    r = reg(s);
    if (r >= 0) {
        e.bits = r;
        return e;
    }
    if (*s == '#') {
        e.bits = 60;
        e.width = sz == 4 ? 4 : 2;
        e.expr = s + 1;
        e.check = sz == 1 ? 4 : 0;
        return e;
    }
    n = (int)strlen(s);
    if (n > 3 && s[0] == '-' && s[1] == '(' && s[n - 1] == ')') {
        s[n - 1] = 0;
        r = reg(s + 2);
        if (r < 8)
            die("invalid predecrement");
        e.bits = 32 + r - 8;
        return e;
    }
    if (n > 3 && s[0] == '(' && s[n - 2] == ')' && s[n - 1] == '+') {
        s[n - 2] = 0;
        r = reg(s + 1);
        if (r < 8)
            die("invalid postincrement");
        e.bits = 24 + r - 8;
        return e;
    }
    if (n > 2 && s[n - 2] == '.' &&
        (tolower((unsigned char)s[n - 1]) == 'w' || tolower((unsigned char)s[n - 1]) == 'l')) {
        int w = tolower((unsigned char)s[n - 1]) == 'w';
        s[n - 2] = 0;
        e.bits = w ? 56 : 57;
        e.width = w ? 2 : 4;
        e.check = w ? 2 : 0;
        e.expr = s;
        return e;
    }
    p = strchr(s, '(');
    end = strrchr(s, ')');
    if (p && end && end[1] == 0) {
        *p = 0;
        *end = 0;
        n = split(p + 1, v, 3);
        if (!n)
            die("empty effective address");
        base = reg(v[0]);
        if (p == s && n == 1 && base < 0 && !samei(v[0], "pc")) {
            *p = '(';
            *end = ')';
            e.bits = 57;
            e.width = 4;
            e.expr = s;
            return e;
        }
        if (n == 1 && base >= 8) {
            if (!*trim(s)) {
                e.bits = 16 + base - 8;
                return e;
            }
            e.bits = 40 + base - 8;
            e.width = 2;
            e.expr = trim(s);
            e.check = 1;
            return e;
        }
        if (n == 1 && samei(v[0], "pc")) {
            e.bits = 58;
            e.width = 2;
            e.pc = 2;
            e.expr = *trim(s) ? trim(s) : "0";
            e.check = 1;
            return e;
        }
        if (base < 8 && !samei(v[0], "pc")) { /* alternate (disp,An[,Xn]) */
            if (n < 2)
                die("invalid effective address");
            e.expr = v[0];
            base = reg(v[1]);
            if (samei(v[1], "pc"))
                base = 16;
            idx = 2;
        } else {
            e.expr = *trim(s) ? trim(s) : "0";
            if (samei(v[0], "pc"))
                base = 16;
            idx = 1;
        }
        if (base < 8 || base > 16)
            die("invalid address register");
        if (idx == n) {
            e.bits = base == 16 ? 58 : 40 + base - 8;
            e.width = 2;
            e.pc = base == 16 ? 2 : 0;
            e.check = 1;
            return e;
        }
        if (idx + 1 != n)
            die("invalid index addressing");
        s = v[idx];
        p = strchr(s, '.');
        r = 0;
        if (p) {
            *p++ = 0;
            if (samei(p, "l"))
                r = 0x800;
            else if (!samei(p, "w"))
                die("68000 index must be .w or .l (no scale)");
        }
        idx = reg(s);
        if (idx < 0)
            die("invalid index register");
        e.bits = base == 16 ? 59 : 48 + base - 8;
        e.width = 2;
        e.index = 0x10000 | (idx << 12) | r;
        e.pc = base == 16 ? 2 : 0;
        e.check = 1;
        return e;
    }
    e.bits = 57;
    e.width = 4;
    e.expr = s;
    return e;
}
static void ext(Asm *a, EA e)
{
    uint32_t pc = seg(a)->n;
    if (!e.width)
        return;
    if (e.index) {
        emit(a, (unsigned)e.index);
        add_fix(a->o, a->sec, pc + 1, 1, e.pc, pc, 1, canon(a, e.expr));
    } else
        fixval(a, e.expr, e.width, e.pc, pc, e.check);
}
static int alter(EA e)
{
    return e.bits < 8 || (e.bits >= 16 && e.bits <= 57);
}
static int memalter(EA e)
{
    return e.bits >= 16 && e.bits <= 57;
}
static int control(EA e)
{
    return (e.bits >= 16 && e.bits < 24) || (e.bits >= 40 && e.bits <= 59);
}
static void data(EA e)
{
    if (e.bits >= 8 && e.bits < 16)
        die("data addressing mode required");
}
static void writable(EA e)
{
    if (!alter(e))
        die("destination is not alterable");
}
static int sc(int sz)
{
    return sz == 1 ? 0 : sz == 2 ? 1 : 2;
}
static int cc(const char *s)
{
    static const char *names[] = {"t",  "f",  "hi", "ls", "cc", "cs", "ne", "eq",
                                  "vc", "vs", "pl", "mi", "ge", "lt", "gt", "le"};
    int i;
    if (samei(s, "hs"))
        return 4;
    if (samei(s, "lo"))
        return 5;
    if (samei(s, "ra"))
        return 0;
    if (samei(s, "sr"))
        return 1;
    for (i = 0; i < 16; i++)
        if (samei(s, names[i]))
            return i;
    return -1;
}
static unsigned reglist(char *s)
{
    char *list[32];
    int n = split(s, list, 32), i, r, t;
    unsigned mask = 0;
    for (i = 0; i < n; i++) {
        char *p = list[i], *q; /* slash and ranges */
        while (p && *p) {
            q = strchr(p, '/');
            if (q)
                *q++ = 0;
            {
                char *dash = strchr(p, '-');
                if (dash)
                    *dash++ = 0;
                r = reg(trim(p));
                t = dash ? reg(trim(dash)) : r;
                if (r < 0 || t < r)
                    die("invalid MOVEM register list");
                for (; r <= t; r++)
                    mask |= 1u << r;
            }
            p = q;
        }
    }
    return mask;
}
static int islist(const char *s)
{
    return reg(s) >= 0 || strchr(s, '/') ||
           ((*s == 'd' || *s == 'a' || *s == 'D' || *s == 'A') && strchr(s, '-'));
}
static void instruction(Asm *a, char *op, char **v, int n, int sz, int explicit_size)
{
    EA x, y;
    int r, t, c = -1, dir, base;
    int64_t imm;
    uint32_t pc = seg(a)->n;
    unsigned mask, rev;
    char *s;
#define N(k)                                                                                       \
    do {                                                                                           \
        if (n != (k))                                                                              \
            die("%s expects %d operand(s)", op, (k));                                              \
    } while (0)
#define SZ(w)                                                                                      \
    do {                                                                                           \
        if (explicit_size && sz != (w))                                                            \
            die("invalid size for %s", op);                                                        \
    } while (0)
    if (samei(op, "nop") || samei(op, "rts") || samei(op, "rte") || samei(op, "rtr") ||
        samei(op, "reset") || samei(op, "trapv") || samei(op, "illegal")) {
        static const char *nm[] = {"nop", "rts", "rte", "rtr", "reset", "trapv", "illegal"};
        static const int code[] = {0x4e71, 0x4e75, 0x4e73, 0x4e77, 0x4e70, 0x4e76, 0x4afc};
        N(0);
        if (explicit_size)
            die("unsized instruction");
        for (r = 0; r < 7; r++)
            if (samei(op, nm[r]))
                emit(a, (unsigned)code[r]);
        return;
    }
    if (samei(op, "trap")) {
        N(1);
        if (*v[0] != '#')
            die("TRAP requires #vector");
        imm = number(a, v[0] + 1);
        if (imm < 0 || imm > 15)
            die("TRAP vector 0..15");
        emit(a, 0x4e40u + (unsigned)imm);
        return;
    }
    if (samei(op, "stop")) {
        N(1);
        if (*v[0] != '#')
            die("STOP requires immediate SR");
        emit(a, 0x4e72);
        fixval(a, v[0] + 1, 2, 0, 0, 0);
        return;
    }
    if (samei(op, "link")) {
        N(2);
        SZ(2);
        r = reg(v[0]);
        if (r < 8 || *v[1] != '#')
            die("LINK An,#displacement");
        emit(a, 0x4e50u + (unsigned)r - 8);
        fixval(a, v[1] + 1, 2, 0, 0, 1);
        return;
    }
    if (samei(op, "unlk")) {
        N(1);
        r = reg(v[0]);
        if (r < 8)
            die("UNLK requires An");
        emit(a, 0x4e58u + (unsigned)r - 8);
        return;
    }
    if (samei(op, "moveq")) {
        N(2);
        SZ(4);
        r = reg(v[1]);
        if (r < 0 || r > 7 || *v[0] != '#')
            die("MOVEQ #imm,Dn");
        imm = number(a, v[0] + 1);
        if (imm < -128 || imm > 127)
            die("MOVEQ immediate out of range");
        emit(a, 0x7000u + ((unsigned)r << 9) + ((unsigned)imm & 255));
        return;
    }
    if (samei(op, "move") || samei(op, "movea")) {
        N(2);
        r = reg(v[0]);
        t = reg(v[1]);
        if (samei(v[0], "usp") || samei(v[1], "usp")) {
            SZ(4);
            if (samei(v[0], "usp")) {
                if (t < 8)
                    die("MOVE USP,An");
                emit(a, 0x4e68u + (unsigned)t - 8);
            } else {
                if (r < 8)
                    die("MOVE An,USP");
                emit(a, 0x4e60u + (unsigned)r - 8);
            }
            return;
        }
        if (samei(v[0], "sr") || samei(v[1], "sr") || samei(v[1], "ccr")) {
            SZ(2);
            if (samei(v[0], "sr")) {
                y = ea(v[1], 2);
                writable(y);
                emit(a, 0x40c0u + (unsigned)y.bits);
                ext(a, y);
            } else {
                x = ea(v[0], 2);
                data(x);
                emit(a, (samei(v[1], "sr") ? 0x46c0u : 0x44c0u) + (unsigned)x.bits);
                ext(a, x);
            }
            return;
        }
        x = ea(v[0], sz);
        y = ea(v[1], sz);
        if (y.bits >= 8 && y.bits < 16) {
            if (sz == 1)
                die("byte MOVEA does not exist");
        } else
            writable(y);
        if (sz == 1)
            data(x);
        if (samei(op, "movea") && (y.bits < 8 || y.bits >= 16))
            die("MOVEA destination must be An");
        base = sz == 1 ? 0x1000 : sz == 2 ? 0x3000 : 0x2000;
        emit(a, (unsigned)base + ((unsigned)(y.bits & 7) << 9) + ((unsigned)(y.bits >> 3) << 6) +
                    (unsigned)x.bits);
        ext(a, x);
        ext(a, y);
        return;
    }
    if (samei(op, "movem")) {
        N(2);
        if (sz == 1)
            die("MOVEM requires .w or .l");
        dir = !islist(v[0]);
        s = xstrdup(dir ? v[1] : v[0]);
        mask = reglist(s);
        x = ea(dir ? v[0] : v[1], sz);
        if (dir) {
            if (!(control(x) || (x.bits >= 24 && x.bits < 32)))
                die("invalid MOVEM source");
        } else {
            if (!(memalter(x) && !(x.bits >= 24 && x.bits < 32)))
                die("invalid MOVEM destination");
            if (x.bits >= 32 && x.bits < 40) {
                rev = 0;
                for (r = 0; r < 16; r++)
                    if (mask & (1u << r))
                        rev |= 1u << (15 - r);
                mask = rev;
            }
        }
        emit(a, 0x4880u + (dir ? 0x400u : 0) + (sz == 4 ? 0x40u : 0) + (unsigned)x.bits);
        emit(a, mask);
        ext(a, x);
        return;
    }
    if (samei(op, "movep")) {
        N(2);
        if (sz == 1)
            die("MOVEP .w/.l only");
        r = reg(v[0]);
        dir = r >= 0 && r < 8;
        if (dir)
            x = ea(v[1], sz);
        else {
            x = ea(v[0], sz);
            r = reg(v[1]);
        }
        if (r < 0 || r > 7 || x.bits < 40 || x.bits >= 48)
            die("MOVEP requires d16(An),Dn or reverse");
        emit(a, 0x0108u + ((unsigned)r << 9) + (dir ? 0x80u : 0) + (sz == 4 ? 0x40u : 0) +
                    (unsigned)(x.bits & 7));
        ext(a, x);
        return;
    }
    if (samei(op, "lea") || samei(op, "pea") || samei(op, "jmp") || samei(op, "jsr")) {
        N(samei(op, "lea") ? 2 : 1);
        x = ea(v[0], 4);
        if (!control(x))
            die("control addressing mode required for %s", op);
        base = samei(op, "lea")   ? 0x41c0
               : samei(op, "pea") ? 0x4840
               : samei(op, "jmp") ? 0x4ec0
                                  : 0x4e80;
        if (samei(op, "lea")) {
            r = reg(v[1]);
            if (r < 8)
                die("LEA destination must be An");
            base += (r - 8) << 9;
        }
        emit(a, (unsigned)base + (unsigned)x.bits);
        ext(a, x);
        return;
    }
    if (samei(op, "swap") || samei(op, "ext")) {
        N(1);
        r = reg(v[0]);
        if (r < 0 || r > 7)
            die("data register required");
        if (samei(op, "ext")) {
            if (sz == 1)
                die("EXT .w/.l only");
            base = sz == 4 ? 0x48c0 : 0x4880;
        } else
            base = 0x4840;
        emit(a, (unsigned)base + (unsigned)r);
        return;
    }
    if (samei(op, "exg")) {
        N(2);
        r = reg(v[0]);
        t = reg(v[1]);
        if (r < 0 || t < 0)
            die("EXG requires registers");
        if (r < 8 && t < 8)
            base = 0xc140;
        else if (r >= 8 && t >= 8) {
            base = 0xc148;
            r -= 8;
            t -= 8;
        } else {
            base = 0xc188;
            if (r >= 8) {
                c = r;
                r = t;
                t = c;
            }
            t -= 8;
        }
        emit(a, (unsigned)base + ((unsigned)r << 9) + (unsigned)t);
        return;
    }
    if ((tolower((unsigned char)op[0]) == 'b' && (c = cc(op + 1)) >= 0) || samei(op, "bra") ||
        samei(op, "bsr")) {
        N(1);
        c = samei(op, "bra") ? 0 : samei(op, "bsr") ? 1 : c;
        if (sz == 4)
            die("68000 has no .l branch");
        if (sz == 1) {
            emit(a, 0x6000u + ((unsigned)c << 8));
            add_fix(a->o, a->sec, pc + 1, 1, 1, pc + 2, 3, canon(a, v[0]));
        } else {
            emit(a, 0x6000u + ((unsigned)c << 8));
            fixval(a, v[0], 2, 1, pc + 2, 1);
        }
        return;
    }
    if (tolower((unsigned char)op[0]) == 'd' && tolower((unsigned char)op[1]) == 'b' &&
        ((c = cc(op + 2)) >= 0 || samei(op, "dbra"))) {
        N(2);
        c = samei(op, "dbra") ? 1 : c;
        r = reg(v[0]);
        if (r < 0 || r > 7)
            die("DBcc requires Dn");
        emit(a, 0x50c8u + ((unsigned)c << 8) + (unsigned)r);
        fixval(a, v[1], 2, 1, pc + 2, 1);
        return;
    }
    if (tolower((unsigned char)op[0]) == 's' && (c = cc(op + 1)) >= 0) {
        N(1);
        if (explicit_size && sz != 1)
            die("Scc is byte-sized");
        x = ea(v[0], 1);
        writable(x);
        emit(a, 0x50c0u + ((unsigned)c << 8) + (unsigned)x.bits);
        ext(a, x);
        return;
    }
    if (samei(op, "clr") || samei(op, "neg") || samei(op, "negx") || samei(op, "not") ||
        samei(op, "tst") || samei(op, "tas") || samei(op, "nbcd")) {
        N(1);
        x = ea(v[0], sz);
        writable(x);
        base = samei(op, "clr")    ? 0x4200
               : samei(op, "neg")  ? 0x4400
               : samei(op, "negx") ? 0x4000
               : samei(op, "not")  ? 0x4600
               : samei(op, "tst")  ? 0x4a00
               : samei(op, "tas")  ? 0x4ac0
                                   : 0x4800;
        if (samei(op, "tas") || samei(op, "nbcd")) {
            if (explicit_size && sz != 1)
                die("byte size required");
        } else
            base += sc(sz) << 6;
        emit(a, (unsigned)base + (unsigned)x.bits);
        ext(a, x);
        return;
    }
    if (samei(op, "addq") || samei(op, "subq")) {
        N(2);
        if (*v[0] != '#')
            die("quick immediate required");
        imm = number(a, v[0] + 1);
        if (imm < 1 || imm > 8)
            die("quick immediate must be 1..8");
        y = ea(v[1], sz);
        if (y.bits >= 8 && y.bits < 16) {
            if (sz == 1)
                die("byte quick to address register");
        } else
            writable(y);
        emit(a, 0x5000u + ((unsigned)(imm & 7) << 9) + (samei(op, "subq") ? 0x100u : 0) +
                    ((unsigned)sc(sz) << 6) + (unsigned)y.bits);
        ext(a, y);
        return;
    }
    if (samei(op, "btst") || samei(op, "bchg") || samei(op, "bclr") || samei(op, "bset")) {
        N(2);
        base = samei(op, "btst") ? 0 : samei(op, "bchg") ? 1 : samei(op, "bclr") ? 2 : 3;
        r = reg(v[0]);
        y = ea(v[1], 1);
        data(y);
        if (y.bits == 60)
            die("bit destination cannot be immediate");
        if (base)
            writable(y);
        if (r >= 0 && r < 8) {
            emit(a, 0x0100u + ((unsigned)r << 9) + ((unsigned)base << 6) + (unsigned)y.bits);
        } else {
            if (*v[0] != '#')
                die("bit number must be #constant or Dn");
            emit(a, 0x0800u + ((unsigned)base << 6) + (unsigned)y.bits);
            fixval(a, v[0] + 1, 2, 0, 0, 0);
        }
        ext(a, y);
        return;
    }
    if (samei(op, "asl") || samei(op, "asr") || samei(op, "lsl") || samei(op, "lsr") ||
        samei(op, "rol") || samei(op, "ror") || samei(op, "roxl") || samei(op, "roxr")) {
        dir = tolower((unsigned char)op[strlen(op) - 1]) == 'l';
        base = tolower((unsigned char)op[0]) == 'a'   ? 0
               : tolower((unsigned char)op[0]) == 'l' ? 1
               : strlen(op) == 4                      ? 2
                                                      : 3;
        if (n == 1) {
            SZ(2);
            x = ea(v[0], 2);
            if (!memalter(x))
                die("memory shift requires alterable memory");
            emit(a, 0xe0c0u + ((unsigned)base << 9) + (dir ? 0x100u : 0) + (unsigned)x.bits);
            ext(a, x);
        } else {
            N(2);
            r = reg(v[1]);
            if (r < 0 || r > 7)
                die("shift destination must be Dn");
            t = reg(v[0]);
            c = 0;
            if (t < 0) {
                if (*v[0] != '#')
                    die("shift count must be #1..8 or Dn");
                imm = number(a, v[0] + 1);
                if (imm < 1 || imm > 8)
                    die("shift count must be 1..8");
                t = (int)imm & 7;
            } else {
                if (t > 7)
                    die("data register count required");
                c = 0x20;
            }
            emit(a, 0xe000u + ((unsigned)t << 9) + (dir ? 0x100u : 0) + ((unsigned)sc(sz) << 6) +
                        (unsigned)c + ((unsigned)base << 3) + (unsigned)r);
        }
        return;
    }
    if (samei(op, "mulu") || samei(op, "muls") || samei(op, "divu") || samei(op, "divs") ||
        samei(op, "chk")) {
        N(2);
        SZ(2);
        x = ea(v[0], 2);
        data(x);
        r = reg(v[1]);
        if (r < 0 || r > 7)
            die("destination must be Dn");
        base = samei(op, "mulu")   ? 0xc0c0
               : samei(op, "muls") ? 0xc1c0
               : samei(op, "divu") ? 0x80c0
               : samei(op, "divs") ? 0x81c0
                                   : 0x4180;
        emit(a, (unsigned)base + ((unsigned)r << 9) + (unsigned)x.bits);
        ext(a, x);
        return;
    }
    if (samei(op, "addx") || samei(op, "subx") || samei(op, "abcd") || samei(op, "sbcd") ||
        samei(op, "cmpm")) {
        N(2);
        x = ea(v[0], sz);
        y = ea(v[1], sz);
        base = samei(op, "addx")   ? 0xd100
               : samei(op, "subx") ? 0x9100
               : samei(op, "abcd") ? 0xc100
               : samei(op, "sbcd") ? 0x8100
                                   : 0xb108;
        if (samei(op, "cmpm")) {
            if (x.bits < 24 || x.bits >= 32 || y.bits < 24 || y.bits >= 32)
                die("CMPM requires (An)+,(An)+");
        } else if (x.bits < 8 && y.bits < 8) {
        } else if (x.bits >= 32 && x.bits < 40 && y.bits >= 32 && y.bits < 40)
            base += 8;
        else
            die("expected Dn,Dn or -(An),-(An)");
        if (samei(op, "abcd") || samei(op, "sbcd")) {
            if (explicit_size && sz != 1)
                die("BCD operations are byte-sized");
        } else
            base += sc(sz) << 6;
        emit(a, (unsigned)base + ((unsigned)(y.bits & 7) << 9) + (unsigned)(x.bits & 7));
        return;
    }
    /* Arithmetic families share their effective-address encoding. */
    {
        int kind = -1, immediate = 0, address = 0;
        size_t len = strlen(op);
        char name[32];
        if (len >= sizeof name)
            die("unknown instruction '%s'", op);
        strcpy(name, op);
        if (len && tolower((unsigned char)name[len - 1]) == 'i') {
            name[len - 1] = 0;
            immediate = 1;
        } else if (len && tolower((unsigned char)name[len - 1]) == 'a') {
            name[len - 1] = 0;
            address = 1;
        }
        if (samei(name, "add"))
            kind = 0;
        if (samei(name, "sub"))
            kind = 1;
        if (samei(name, "cmp"))
            kind = 2;
        if (samei(name, "and"))
            kind = 3;
        if (samei(name, "or"))
            kind = 4;
        if (samei(name, "eor"))
            kind = 5;
        if (kind >= 0) {
            N(2);
            if ((samei(v[1], "ccr") || samei(v[1], "sr")) &&
                (kind == 3 || kind == 4 || kind == 5)) {
                if (*v[0] != '#')
                    die("immediate required");
                base = kind == 3 ? 0x0200 : kind == 4 ? 0 : 0x0a00;
                emit(a, (unsigned)base + (samei(v[1], "sr") ? 0x7cu : 0x3cu));
                fixval(a, v[0] + 1, 2, 0, 0, 0);
                return;
            }
            x = ea(v[0], sz);
            y = ea(v[1], sz);
            if (y.bits >= 8 && y.bits < 16 && kind <= 2)
                address = 1;
            if (address) {
                if (kind > 2 || y.bits < 8 || y.bits >= 16 || sz == 1)
                    die("invalid address arithmetic");
                base = kind == 0 ? 0xd0c0 : kind == 1 ? 0x90c0 : 0xb0c0;
                emit(a, (unsigned)base + ((unsigned)(y.bits - 8) << 9) + (sz == 4 ? 0x100u : 0) +
                            (unsigned)x.bits);
                ext(a, x);
                return;
            }
            if (x.bits == 60)
                immediate = 1;
            if (immediate) {
                if (x.bits != 60)
                    die("immediate operand required");
                writable(y);
                base = kind == 0   ? 0x0600
                       : kind == 1 ? 0x0400
                       : kind == 2 ? 0x0c00
                       : kind == 3 ? 0x0200
                       : kind == 4 ? 0
                                   : 0x0a00;
                emit(a, (unsigned)base + ((unsigned)sc(sz) << 6) + (unsigned)y.bits);
                ext(a, x);
                ext(a, y);
                return;
            }
            if (kind == 5) {
                if (x.bits >= 8)
                    die("EOR source must be Dn");
                writable(y);
                emit(a, 0xb100u + ((unsigned)x.bits << 9) + ((unsigned)sc(sz) << 6) +
                            (unsigned)y.bits);
                ext(a, y);
                return;
            }
            if (y.bits < 8) {
                if (kind >= 3)
                    data(x);
                if (sz == 1)
                    data(x);
                base = kind == 0   ? 0xd000
                       : kind == 1 ? 0x9000
                       : kind == 2 ? 0xb000
                       : kind == 3 ? 0xc000
                                   : 0x8000;
                emit(a, (unsigned)base + ((unsigned)y.bits << 9) + ((unsigned)sc(sz) << 6) +
                            (unsigned)x.bits);
                ext(a, x);
                return;
            }
            if (kind == 2 || x.bits >= 8 || !memalter(y))
                die("invalid arithmetic operand combination");
            base = kind == 0 ? 0xd100 : kind == 1 ? 0x9100 : kind == 3 ? 0xc100 : 0x8100;
            emit(a, (unsigned)base + ((unsigned)x.bits << 9) + ((unsigned)sc(sz) << 6) +
                        (unsigned)y.bits);
            ext(a, y);
            return;
        }
    }
    die("unknown or unsupported 68000 instruction '%s'", op);
#undef N
#undef SZ
}

static AMacro *macro_find(Asm *a, const char *name)
{
    AMacro *m;
    for (m = a->macros; m; m = m->next)
        if (samei(m->name, name))
            return m;
    return NULL;
}
static char *collect_block(char **cursor, int *line, const char *open, const char *close)
{
    Str body = {0};
    int nested = 0;
    char *s = *cursor;
    while (s && *s) {
        char *end = strchr(s, '\n'), *copy, *op, *p;
        size_t n = end ? (size_t)(end - s) : strlen(s);
        copy = slice(s, n);
        op = trim(copy);
        p = op;
        while (*p && !isspace((unsigned char)*p))
            p++;
        *p = 0;
        (*line)++;
        if (samei(op, open))
            nested++;
        if (samei(op, close)) {
            if (!nested) {
                *cursor = end ? end + 1 : NULL;
                return body.s ? body.s : xstrdup("");
            }
            nested--;
        }
        puts_s(&body, slice(s, n));
        putc_s(&body, '\n');
        s = end ? end + 1 : NULL;
    }
    die("missing %s", close);
}
static char *macro_expand(Asm *a, AMacro *m, char *args, int sz, int sized)
{
    char *v[10];
    int n = split(args, v, 10), id = a->serial++;
    const char *s = m->body;
    Str b = {0};
    while (*s) {
        if (*s == '\\') {
            s++;
            if (*s >= '1' && *s <= '9') {
                int i = *s++ - '1';
                if (i >= n)
                    die("missing macro argument %d", i + 1);
                puts_s(&b, v[i]);
            } else if (*s == '@') {
                s++;
                printf_s(&b, "%d", id);
            } else if (*s == '0') {
                s++;
                if (sized)
                    putc_s(&b, sz == 1 ? 'b' : sz == 2 ? 'w' : 'l');
            } else if (*s == '\\') {
                putc_s(&b, *s++);
            } else
                die("unsupported macro substitution");
        } else
            putc_s(&b, *s++);
    }
    return b.s ? b.s : xstrdup("");
}
static void assemble_lines(Asm *, const char *, const char *, int);
static char *relative_file(const char *file, const char *arg)
{
    const char *p = strrchr(file, '/');
    if (*arg == '/')
        return xstrdup(arg);
    return p ? fmt("%.*s/%s", (int)(p - file), file, arg) : xstrdup(arg);
}
static char *unquote(char *s)
{
    size_t n = strlen(s);
    if (n < 2 || !strchr("\"'", *s) || s[n - 1] != *s)
        die("quoted filename required");
    s[n - 1] = 0;
    return s + 1;
}
static void assemble_lines(Asm *a, const char *source, const char *filename, int depth)
{
    char *buf = xstrdup(source), *line = buf, *next, *s, *label, *op, *p, *v[256];
    int lineno = 0, n, sz, explicit_size, indented, i;
    int64_t count, val;
    char *name;
    const char *savedfile = diag_file;
    int savedline = diag_line;
    if (depth > 32)
        die("assembler include depth exceeded");
    while (line && *line) {
        next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        lineno++;
        diag_file = filename;
        diag_line = lineno;
        indented = isspace((unsigned char)*line);
        s = trim(line);
        line = next;
        if (!*s || *s == ';' || *s == '*')
            continue;
        /* Semicolon comments, ignoring quoted data. */ {
            int q = 0;
            for (p = s; *p; p++) {
                if (q) {
                    if (*p == q) {
                        if (p[1] == q)
                            p++;
                        else
                            q = 0;
                    }
                } else if (*p == '\'' || *p == '\"')
                    q = *p;
                else if (*p == ';') {
                    *p = 0;
                    break;
                }
            }
        }
        label = NULL;
        op = NULL;
        p = s;
        while (*p && !isspace((unsigned char)*p) && *p != ':')
            p++;
        if (*p == ':') {
            *p++ = 0;
            label = s;
            s = trim(p);
        } else if (!indented) {
            if (*p)
                *p++ = 0;
            label = s;
            s = trim(p);
        }
        if (*s) {
            op = s;
            while (*s && !isspace((unsigned char)*s))
                s++;
            if (*s)
                *s++ = 0;
            s = trim(s);
        }
        sz = 2;
        explicit_size = 0;
        /* Devpac's first unquoted operand whitespace starts the comment field. */
        {
            int quote = 0;
            for (p = s; *p; p++) {
                if (quote) {
                    if (*p == quote) {
                        if (p[1] == quote)
                            p++;
                        else
                            quote = 0;
                    }
                } else if (*p == '\'' || *p == '\"')
                    quote = *p;
                else if (isspace((unsigned char)*p)) {
                    *p = 0;
                    break;
                }
            }
        }

        if (op) {
            p = strrchr(op, '.');
            if (p && p[1] && !p[2]) {
                int c = tolower((unsigned char)p[1]);
                sz = c == 'b' || c == 's' ? 1 : c == 'w' ? 2 : c == 'l' ? 4 : 0;
                if (!sz)
                    die("unsupported instruction size '.%c'", c);
                *p = 0;
                explicit_size = 1;
            }
        }
        if (op && samei(op, "macro")) {
            AMacro *m;
            if (!label)
                die("MACRO requires a name in the label field");
            if (macro_find(a, label))
                die("duplicate macro '%s'", label);
            m = xcalloc(1, sizeof *m);
            m->name = xstrdup(label);
            m->body = collect_block(&line, &lineno, "macro", "endm");
            m->next = a->macros;
            a->macros = m;
            continue;
        }
        if (op && samei(op, "rept")) {
            char *body = collect_block(&line, &lineno, "rept", "endr");
            count = number(a, s);
            if (count < 0 || count > 65536)
                die("REPT count outside 0..65536");
            while (count--)
                assemble_lines(a, body, filename, depth + 1);
            continue;
        }
        if (op && samei(op, "machine")) {
            if (number(a, s) != 68000)
                die("only MACHINE 68000 is supported");
            continue;
        }
        if (op && samei(op, "equ")) {
            a->here = fmt("__stcc_here_%d", a->serial++);
            define(a->o, a->here, a->sec, seg(a)->n, NULL);
            if (!label)
                die("EQU needs a label");
            name = canon(a, label);
            define(a->o, name, -2, 0, canon(a, s));
            continue;
        }
        if (op && samei(op, "="))
            die("use EQU (mutable SET is not supported)");
        if (op && !(samei(op, "dc") || samei(op, "ds") || samei(op, "dcb"))) {
            if (samei(op, "section")) {
                n = split(s, v, 256);
                if (n < 1)
                    die("SECTION requires a name");
                p = n >= 2 ? v[1] : v[0];
                if (samei(p, "text") || samei(p, "code") || !strcmp(p, "0"))
                    a->sec = 0;
                else if (samei(p, "data") || !strcmp(p, "1"))
                    a->sec = 1;
                else if (samei(p, "bss") || !strcmp(p, "2"))
                    a->sec = 2;
                else
                    die("SECTION must be TEXT, DATA or BSS");
                continue;
            }
            if (samei(op, "text") || samei(op, "data") || samei(op, "bss")) {
                a->sec = samei(op, "text") ? 0 : samei(op, "data") ? 1 : 2;
                continue;
            }
        }
        if (op && (samei(op, "dc") || samei(op, "ds") || samei(op, "dcb"))) {
            if (sz > 1 && seg(a)->n & 1)
                zeros(a, 1);
        } else if (op && !samei(op, "even") && !samei(op, "cnop") && !samei(op, "xdef") &&
                   !samei(op, "xref") && !samei(op, "include") && !samei(op, "incbin") &&
                   !samei(op, "end")) {
            if (seg(a)->n & 1)
                zeros(a, 1);
        }
        if (label) {
            if (*label != '.')
                a->scope = xstrdup(label);
            define(a->o, canon(a, label), a->sec, seg(a)->n, NULL);
        }
        a->here = fmt("__stcc_here_%d", a->serial++);
        define(a->o, a->here, a->sec, seg(a)->n, NULL);
        if (!op)
            continue;
        if (samei(op, "end"))
            break;
        if (samei(op, "xdef") || samei(op, "xref")) {
            n = split(s, v, 256);
            if (!n)
                die("empty symbol list");
            for (i = 0; i < n; i++) {
                name = canon(a, v[i]);
                symbol(a->o, name, 1)->global = 1;
            }
            continue;
        }
        if (samei(op, "even")) {
            if (seg(a)->n & 1)
                zeros(a, 1);
            continue;
        }
        if (samei(op, "cnop")) {
            n = split(s, v, 256);
            if (n != 2)
                die("CNOP offset,alignment");
            count = number(a, v[1]);
            val = number(a, v[0]);
            if (count < 1 || count > 65536 || val < 0 || val >= count)
                die("invalid CNOP");
            while (seg(a)->n % (uint32_t)count != (uint32_t)val)
                zeros(a, 1);
            continue;
        }
        if (samei(op, "include")) {
            name = relative_file(filename, unquote(s));
            assemble_lines(a, read_text(name), name, depth + 1);
            continue;
        }
        if (samei(op, "incbin")) {
            FILE *f;
            int c;
            name = relative_file(filename, unquote(s));
            f = fopen(name, "rb");
            if (!f)
                die("cannot open %s", name);
            if (a->sec == 2)
                die("INCBIN in BSS");
            while ((c = fgetc(f)) != EOF) {
                if (seg(a)->n >= STCC_MAX_INPUT)
                    die("INCBIN too large");
                byte(seg(a), (unsigned)c);
            }
            fclose(f);
            continue;
        }
        if (samei(op, "ds")) {
            count = number(a, s);
            if (count < 0 || count > STCC_MAX_INPUT / sz)
                die("invalid DS count");
            zeros(a, (uint32_t)count * (unsigned)sz);
            continue;
        }
        if (samei(op, "dcb")) {
            n = split(s, v, 256);
            if (n < 1 || n > 2)
                die("DCB count[,value]");
            count = number(a, v[0]);
            if (count < 0 || count > STCC_MAX_INPUT / sz)
                die("invalid DCB count");
            while (count--)
                fixval(a, n == 2 ? v[1] : "0", sz, 0, 0, 0);
            continue;
        }
        if (samei(op, "dc")) {
            n = split(s, v, 256);
            for (i = 0; i < n; i++) {
                p = v[i];
                if (sz == 1 && (*p == '\'' || *p == '\"')) {
                    int q = *p++;
                    while (*p) {
                        if (*p == q) {
                            if (p[1] == q)
                                p++;
                            else {
                                p++;
                                break;
                            }
                        }
                        if (a->sec == 2)
                            die("DC in BSS");
                        byte(seg(a), (unsigned char)*p++);
                    }
                    if (*trim(p))
                        die("text after DC.B string");
                } else
                    fixval(a, p, sz, 0, 0, 0);
            }
            continue;
        }
        {
            AMacro *m = macro_find(a, op);
            if (m) {
                char *body = macro_expand(a, m, s, sz, explicit_size);
                assemble_lines(a, body, filename, depth + 1);
                continue;
            }
        }
        n = split(s, v, 256);
        instruction(a, op, v, n, sz, explicit_size);
    }
    diag_file = savedfile;
    diag_line = savedline;
    free(buf);
}
Object *assemble(const char *s, const char *file)
{
    Asm a;
    memset(&a, 0, sizeof a);
    a.o = object_new(file);
    assemble_lines(&a, s, file, 0);
    return a.o;
}
