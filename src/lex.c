#include "c.h"
void at(Token *t, const char *f, ...)
{
    va_list a;
    diag_file = t ? t->file : "stcc";
    diag_line = t ? t->line : 0;
    fprintf(stderr, "%s:%d: error: ", diag_file, diag_line);
    va_start(a, f);
    vfprintf(stderr, f, a);
    va_end(a);
    fputc('\n', stderr);
    exit(1);
}
static int escape(const char **p)
{
    int c = (unsigned char)*(*p)++, n = 0, i;
    if (c != '\\')
        return c;
    c = (unsigned char)*(*p)++;
    switch (c) {
    case 'a':
        return 7;
    case 'b':
        return 8;
    case 'f':
        return 12;
    case 'n':
        return 10;
    case 'r':
        return 13;
    case 't':
        return 9;
    case 'v':
        return 11;
    case '\\':
    case '\'':
    case '"':
    case '?':
        return c;
    case 'x':
        if (!isxdigit((unsigned char)**p))
            die("empty hex escape");
        while (isxdigit((unsigned char)**p)) {
            c = (unsigned char)*(*p)++;
            n = (n << 4) + (isdigit(c) ? c - '0' : tolower(c) - 'a' + 10);
        }
        return n & 255;
    default:
        if (c >= '0' && c <= '7') {
            n = c - '0';
            for (i = 0; i < 2 && **p >= '0' && **p <= '7'; i++)
                n = n * 8 + *(*p)++ - '0';
            return n & 255;
        }
        die("invalid C escape");
    }
    return 0;
}
Token *lex_c(const char *s, const char *filename)
{
    Token head = {0}, *tail = &head, *t;
    int line = 1, bol = 1;
    char *file = xstrdup(filename);
    const char *start, *p;
    int q, c;
    while (*s) {
        if (*s == '\n') {
            s++;
            line++;
            bol = 1;
            continue;
        }
        if (isspace((unsigned char)*s)) {
            s++;
            continue;
        }
        if (bol && *s == '#') {
            int n;
            char path[4096];
            if (sscanf(s, "#line %d \"%4095[^\"]\"", &n, path) != 2)
                die("invalid line marker");
            file = xstrdup(path);
            line = n - 1;
            while (*s && *s != '\n')
                s++;
            continue;
        }
        bol = 0;
        t = xcalloc(1, sizeof *t);
        t->line = line;
        t->file = file;
        tail->next = t;
        tail = t;
        diag_file = file;
        diag_line = line;
        start = s;
        if (isalpha((unsigned char)*s) || *s == '_') {
            s++;
            while (isalnum((unsigned char)*s) || *s == '_')
                s++;
            t->kind = TK_IDENT;
            t->text = slice(start, (size_t)(s - start));
            if (!strcmp(t->text, "__asm__")) {
                int depth = 0, opened = 0, comment = 0, quote = 0, asm_bol = 1;
                t->kind = TK_ASM;
                start = s;
                for (; *s; s++) {
                    if (*s == '\n') {
                        line++;
                        comment = 0;
                        asm_bol = 1;
                        continue;
                    }
                    if (comment)
                        continue;
                    if (asm_bol && isspace((unsigned char)*s))
                        continue;
                    if (asm_bol && *s == '*') {
                        comment = 1;
                        continue;
                    }
                    asm_bol = 0;
                    if (quote) {
                        if (*s == quote) {
                            if (s[1] == quote)
                                s++;
                            else
                                quote = 0;
                        }
                        continue;
                    }
                    if (*s == ';') {
                        comment = 1;
                        continue;
                    }
                    if (*s == '\'' || *s == '"') {
                        quote = *s;
                        continue;
                    }
                    if (*s == '{') {
                        depth++;
                        opened = 1;
                    }
                    if (*s == '}' && opened && !--depth) {
                        s++;
                        break;
                    }
                }
                if (!opened || depth)
                    at(t, "unterminated __asm__");
                t->text = slice(start, (size_t)(s - start));
            }
            continue;
        }
        if (isdigit((unsigned char)*s) || (*s == '.' && isdigit((unsigned char)s[1]))) {
            char *end;
            uint64_t v;
            int u = 0, l = 0, hex = s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
            if (*s == '.')
                at(t, "floating point is not implemented in this release");
            errno = 0;
            v = strtoull(s, &end, 0);
            if (end == s || errno || v > UINT32_MAX)
                at(t, "integer constant exceeds 32 bits");
            s = end;
            if (*s == '.' || (*s && strchr("pPeE", *s) && !hex))
                at(t, "floating point is not implemented in this release");
            while (*s && strchr("uUlL", *s)) {
                if (tolower((unsigned char)*s) == 'u') {
                    if (u++)
                        at(t, "invalid integer suffix");
                } else if (l++)
                    at(t, "long long is not C89");
                s++;
            }
            if (isalnum((unsigned char)*s) || *s == '_')
                at(t, "invalid integer literal");
            t->kind = TK_NUMBER;
            t->val = (uint32_t)v;
            if (l) {
                t->longlit = 1;
                t->uns = u || v > INT32_MAX;
            } else if (u) {
                t->uns = 1;
                t->longlit = v > 65535;
            } else if (v <= 32767) {
            } else if ((*start == '0') && v <= 65535)
                t->uns = 1;
            else {
                t->longlit = 1;
                t->uns = v > INT32_MAX;
            }
            t->text = slice(start, (size_t)(s - start));
            continue;
        }
        if (*s == '"' || *s == '\'') {
            Str b = {0};
            q = *s++;
            while (*s && *s != q) {
                if (*s == '\n')
                    at(t, "newline in literal");
                c = escape(&s);
                putc_s(&b, c);
            }
            if (*s++ != q)
                at(t, "unterminated literal");
            t->text = slice(start, (size_t)(s - start));
            if (q == '\'') {
                if (!b.n || b.n > 4)
                    at(t, "invalid character constant");
                t->kind = TK_NUMBER;
                for (p = b.s; p < b.s + b.n; p++)
                    t->val = (t->val << 8) | (unsigned char)*p;
                t->val = convert_const(t->val, type_new(TY_INT, 0));
            } else {
                t->kind = TK_STRING;
                t->len = (int)b.n + 1;
                t->bytes = b.s ? b.s : xstrdup("");
            }
            continue;
        }
        {
            static const char *punct[] = {">>=", "<<=", "...", "->", "++", "--", "<<", ">>",
                                          "<=",  ">=",  "==",  "!=", "&&", "||", "+=", "-=",
                                          "*=",  "/=",  "%=",  "&=", "^=", "|=", NULL};
            int i;
            for (i = 0; punct[i]; i++)
                if (!strncmp(s, punct[i], strlen(punct[i])))
                    break;
            if (punct[i])
                s += strlen(punct[i]);
            else {
                if (!strchr("{}[]();,:?~!+-*/%&|^<>=.", *s))
                    at(t, "invalid C character 0x%02x", (unsigned char)*s);
                s++;
            }
            t->kind = TK_PUNCT;
            t->text = slice(start, (size_t)(s - start));
        }
    }
    t = xcalloc(1, sizeof *t);
    t->kind = TK_EOF;
    t->text = xstrdup("");
    t->file = file;
    t->line = line;
    tail->next = t;
    return head.next;
}
