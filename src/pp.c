#include "stcc.h"

typedef struct Macro {
    char *name, *body, **params;
    int nparams, function, busy;
    struct Macro *next;
} Macro;
typedef struct {
    Macro *macros;
    char **incs;
    int ninc, depth;
} PP;
static int ident(int c)
{
    return isalnum((unsigned char)c) || c == '_';
}
static Macro *find(PP *p, const char *s)
{
    Macro *m;
    for (m = p->macros; m; m = m->next)
        if (!strcmp(m->name, s))
            return m;
    return NULL;
}
static char *expand(PP *, const char *, int);
static char *quoted_end(char *s)
{
    int q = *s++;
    while (*s) {
        if (*s == '\\' && s[1])
            s += 2;
        else if (*s++ == q)
            return s;
    }
    die("unterminated preprocessor literal");
    return s;
}
/* Inline Devpac has its own comments and literal syntax. Keep its entire block
   opaque to C macro expansion (including the clobber list). */
static const char *asm_end(const char *s)
{
    const char *p = s;
    int q = 0, comment = 0, depth = 0, opened = 0, bol = 1;
    for (; *p; p++) {
        if (*p == '\n') {
            bol = 1;
            comment = 0;
            continue;
        }
        if (comment)
            continue;
        if (bol && isspace((unsigned char)*p))
            continue;
        if (bol && *p == '*') {
            comment = 1;
            continue;
        }
        bol = 0;
        if (q) {
            if (*p == q) {
                if (p[1] == q)
                    p++;
                else
                    q = 0;
            }
            continue;
        }
        if (*p == ';') {
            comment = 1;
            continue;
        }
        if (*p == '\'' || *p == '\"') {
            q = *p;
            continue;
        }
        if (*p == '{') {
            depth++;
            opened = 1;
        }
        if (*p == '}' && opened && !--depth)
            return p + 1;
    }
    die("unterminated __asm__ block");
    return p;
}
static char *stringify(const char *s)
{
    Str b = {0};
    int space = 0;
    putc_s(&b, '"');
    while (isspace((unsigned char)*s))
        s++;
    while (*s) {
        if (isspace((unsigned char)*s)) {
            space = 1;
            s++;
            continue;
        }
        if (space && b.n > 1)
            putc_s(&b, ' ');
        space = 0;
        if (*s == '"' || *s == '\\')
            putc_s(&b, '\\');
        putc_s(&b, *s++);
    }
    putc_s(&b, '"');
    return b.s;
}
static int param(Macro *m, const char *s)
{
    int i;
    for (i = 0; i < m->nparams; i++)
        if (!strcmp(m->params[i], s))
            return i;
    return -1;
}
static char *subst(PP *p, Macro *m, char **args, int depth)
{
    Str b = {0};
    const char *s = m->body, *start, *q;
    char *name;
    int i, paste, pending = 0;
    while (*s) {
        if (*s == '#' && s[1] == '#') {
            while (b.n && isspace((unsigned char)b.s[b.n - 1]))
                b.s[--b.n] = 0;
            s += 2;
            while (isspace((unsigned char)*s))
                s++;
            pending = 1;
            continue;
        }
        if (*s == '#') {
            s++;
            while (isspace((unsigned char)*s))
                s++;
            start = s;
            while (ident(*s))
                s++;
            name = slice(start, (size_t)(s - start));
            i = param(m, name);
            if (i < 0)
                die("# must precede a macro parameter");
            puts_s(&b, stringify(args[i]));
            continue;
        }
        if (isalpha((unsigned char)*s) || *s == '_') {
            start = s++;
            while (ident(*s))
                s++;
            name = slice(start, (size_t)(s - start));
            i = param(m, name);
            if (i < 0)
                puts_s(&b, name);
            else {
                q = s;
                while (isspace((unsigned char)*q))
                    q++;
                paste = (q[0] == '#' && q[1] == '#') || pending;
                puts_s(&b, paste ? args[i] : expand(p, args[i], depth + 1));
            }
            pending = 0;
            continue;
        }
        if (*s == '"' || *s == '\'') {
            q = quoted_end((char *)s);
            puts_s(&b, slice(s, (size_t)(q - s)));
            s = q;
        } else
            putc_s(&b, *s++);
    }
    return b.s ? b.s : xstrdup("");
}
static char *expand(PP *p, const char *text, int depth)
{
    Str b = {0};
    const char *s = text, *start, *q;
    char *name, *body, **args;
    Macro *m;
    int n, lev;
    if (depth > 100)
        die("macro expansion nesting limit");
    while (*s) {
        if (isalpha((unsigned char)*s) || *s == '_') {
            start = s++;
            while (ident(*s))
                s++;
            name = slice(start, (size_t)(s - start));
            if (!strcmp(name, "__LINE__")) {
                printf_s(&b, "%d", diag_line);
                continue;
            }
            if (!strcmp(name, "__FILE__")) {
                puts_s(&b, stringify(diag_file));
                continue;
            }
            if (!strcmp(name, "__asm__")) {
                q = asm_end(s);
                puts_s(&b, name);
                puts_s(&b, slice(s, (size_t)(q - s)));
                s = q;
                continue;
            }
            m = find(p, name);
            if (!m || m->busy) {
                puts_s(&b, name);
                continue;
            }
            q = s;
            while (isspace((unsigned char)*q))
                q++;
            args = NULL;
            n = 0;
            if (m->function) {
                if (*q != '(') {
                    puts_s(&b, name);
                    continue;
                }
                s = q + 1;
                start = s;
                lev = 0;
                args = xcalloc((size_t)m->nparams + 2, sizeof *args);
                for (;;) {
                    if (!*s)
                        die("unterminated invocation of %s", name);
                    if (*s == '"' || *s == '\'') {
                        s = quoted_end((char *)s);
                        continue;
                    }
                    if (*s == '(')
                        lev++;
                    if (*s == ')' && !lev) {
                        if (n || m->nparams || *trim(slice(start, (size_t)(s - start)))) {
                            if (n > m->nparams)
                                die("too many macro arguments");
                            args[n++] = slice(start, (size_t)(s - start));
                        }
                        s++;
                        break;
                    }
                    if (*s == ')')
                        lev--;
                    if (*s == ',' && !lev) {
                        if (n >= m->nparams)
                            die("too many macro arguments");
                        args[n++] = slice(start, (size_t)(s - start));
                        start = s + 1;
                    }
                    s++;
                }
                if (n != m->nparams)
                    die("macro %s expects %d arguments, got %d", name, m->nparams, n);
                body = subst(p, m, args, depth);
            } else
                body = m->body;
            m->busy = 1;
            {
                char *expanded = expand(p, body, depth + 1), *name2 = trim(xstrdup(expanded));
                Macro *f = find(p, name2);
                q = s;
                while (isspace((unsigned char)*q))
                    q++;
                if (f && f->function && !f->busy && *q == '(') {
                    const char *end = q + 1;
                    int nesting = 1;
                    while (*end && nesting) {
                        if (*end == '\"' || *end == '\'')
                            end = quoted_end((char *)end);
                        else {
                            if (*end == '(')
                                nesting++;
                            if (*end == ')')
                                nesting--;
                            end++;
                        }
                    }
                    if (nesting)
                        die("unterminated macro invocation");
                    expanded =
                        expand(p, fmt("%s%s", expanded, slice(s, (size_t)(end - s))), depth + 1);
                    s = end;
                }
                puts_s(&b, expanded);
            }
            m->busy = 0;
            continue;
        }
        if (*s == '"' || *s == '\'') {
            q = quoted_end((char *)s);
            puts_s(&b, slice(s, (size_t)(q - s)));
            s = q;
        } else {
            if (*s == '\n')
                diag_line++;
            putc_s(&b, *s++);
        }
    }
    return b.s ? b.s : xstrdup("");
}
static void undef(PP *p, const char *name)
{
    Macro **m = &p->macros;
    while (*m) {
        if (!strcmp((*m)->name, name)) {
            *m = (*m)->next;
            return;
        }
        m = &(*m)->next;
    }
}
static void def(PP *p, char *s)
{
    Macro *m = xcalloc(1, sizeof *m);
    char *b = s, *q;
    int cap = 0;
    if (!isalpha((unsigned char)*s) && *s != '_')
        die("invalid macro name");
    while (ident(*s))
        s++;
    m->name = slice(b, (size_t)(s - b));
    if (*s == '(') {
        m->function = 1;
        s++;
        while (*s && *s != ')') {
            s = trim(s);
            b = s;
            if (!isalpha((unsigned char)*s) && *s != '_')
                die("invalid macro parameter");
            while (ident(*s))
                s++;
            q = slice(b, (size_t)(s - b));
            if (param(m, q) >= 0)
                die("duplicate macro parameter");
            GROW(m->params, m->nparams, cap);
            m->params[m->nparams++] = q;
            while (isspace((unsigned char)*s))
                s++;
            if (*s == ',')
                s++;
            else if (*s != ')')
                die("expected ',' or ')' in macro");
        }
        if (*s++ != ')')
            die("unterminated macro parameter list");
    }
    m->body = xstrdup(trim(s));
    {
        Macro *old = find(p, m->name);
        if (old && (old->function != m->function || old->nparams != m->nparams ||
                    strcmp(old->body, m->body)))
            die("incompatible redefinition of macro '%s'", m->name);
    }
    undef(p, m->name);
    m->next = p->macros;
    p->macros = m;
}
static int condition(PP *p, char *s)
{
    Str b = {0};
    char *name, *e;
    while (*s) {
        if (isalpha((unsigned char)*s) || *s == '_') {
            char *start = s++;
            while (ident(*s))
                s++;
            name = slice(start, (size_t)(s - start));
            if (!strcmp(name, "defined")) {
                int par;
                while (isspace((unsigned char)*s))
                    s++;
                par = *s == '(';
                if (par)
                    s++;
                while (isspace((unsigned char)*s))
                    s++;
                start = s;
                while (ident(*s))
                    s++;
                if (start == s)
                    die("expected macro name after defined");
                name = slice(start, (size_t)(s - start));
                printf_s(&b, "%d", find(p, name) != NULL);
                while (isspace((unsigned char)*s))
                    s++;
                if (par && *s++ != ')')
                    die("expected ')' after defined");
            } else
                puts_s(&b, name);
        } else
            putc_s(&b, *s++);
    }
    e = expand(p, b.s ? b.s : "", 0);
    return c_preprocessor_condition(e);
}
static char *clean_source(const char *s)
{
    Str b = {0}, spliced = {0};
    const char *p;
    int q; /* Translation phase 2. */
    while (*s) {
        if (s[0] == '\\' && s[1] == '\n')
            s += 2;
        else if (s[0] == '\\' && s[1] == '\r' && s[2] == '\n')
            s += 3;
        else
            putc_s(&spliced, *s++);
    }
    s = spliced.s ? spliced.s : "";
    while (*s) {
        if (!strncmp(s, "__asm__", 7) && !ident(s[7]) && (s == spliced.s || !ident(s[-1]))) {
            p = asm_end(s + 7);
            puts_s(&b, slice(s, (size_t)(p - s)));
            s = p;
        } else if (s[0] == '/' && s[1] == '*') {
            putc_s(&b, ' ');
            s += 2;
            while (*s && !(s[0] == '*' && s[1] == '/')) {
                if (*s == '\n')
                    putc_s(&b, '\n');
                s++;
            }
            if (!*s)
                die("unterminated C comment");
            s += 2;
        } else if (s[0] == '/' && s[1] == '/') {
            die("// comments are not C89");
        } else if (*s == '"' || *s == '\'') {
            q = *s;
            putc_s(&b, *s++);
            while (*s && *s != q) {
                if (*s == '\\' && s[1])
                    putc_s(&b, *s++);
                putc_s(&b, *s++);
            }
            if (!*s)
                die("unterminated literal");
            putc_s(&b, *s++);
        } else
            putc_s(&b, *s++);
    }
    return b.s ? b.s : xstrdup("");
}
static char *ppfile(PP *, const char *);
static char *include_file(PP *p, const char *file, char *arg)
{
    char *s = trim(expand(p, arg, 0)), *end, *path;
    int quote = *s == '"', i;
    FILE *f;
    if (!quote && *s != '<')
        die("invalid #include");
    end = strchr(s + 1, quote ? '"' : '>');
    if (!end)
        die("unterminated #include");
    *end = 0;
    s++;
    if (*trim(end + 1))
        die("text after #include");
    if (quote) {
        const char *d = strrchr(file, '/');
        path = d ? fmt("%.*s/%s", (int)(d - file), file, s) : xstrdup(s);
        f = fopen(path, "rb");
        if (f) {
            fclose(f);
            return path;
        }
    }
    for (i = 0; i < p->ninc; i++) {
        path = fmt("%s/%s", p->incs[i], s);
        f = fopen(path, "rb");
        if (f) {
            fclose(f);
            return path;
        }
    }
    die("include file not found: %s", s);
    return NULL;
}
static void flush(PP *p, Str *out, Str *chunk, const char *file, int start)
{
    if (!chunk->n)
        return;
    printf_s(out, "#line %d \"%s\"\n", start, file);
    diag_file = file;
    diag_line = start;
    puts_s(out, expand(p, chunk->s, 0));
    if (out->n && out->s[out->n - 1] != '\n')
        putc_s(out, '\n');
    chunk->n = 0;
    if (chunk->s)
        chunk->s[0] = 0;
}
static char *ppfile(PP *p, const char *file)
{
    char *source, *line, *next, *s, *start, *directive;
    Str out = {0}, chunk = {0};
    struct {
        int parent, taken, active, otherwise;
    } cond[64];
    int nesting = 0, active = 1, lineno = 0, chunkline = 1;
    const char *raw_asm_end = NULL;
    const char *oldfile = diag_file;
    int oldline = diag_line;
    if (++p->depth > 64)
        die("include nesting exceeds 64");
    diag_file = file;
    diag_line = 1;
    source = clean_source(read_text(file));
    line = source;
    while (line && *line) {
        next = strchr(line, '\n');
        if (!raw_asm_end || line >= raw_asm_end) {
            const char *scan = line, *limit = next ? next : line + strlen(line);
            while (scan < limit) {
                if (*scan == '\"' || *scan == '\'') {
                    scan = quoted_end((char *)scan);
                    continue;
                }
                if (!strncmp(scan, "__asm__", 7) && (scan == line || !ident(scan[-1])) &&
                    !ident(scan[7])) {
                    raw_asm_end = asm_end(scan + 7);
                    break;
                }
                scan++;
            }
        }
        if (next)
            *next++ = 0;
        lineno++;
        diag_file = file;
        diag_line = lineno;
        s = trim(line);
        if (*s == '#' && (!raw_asm_end || line >= raw_asm_end)) {
            flush(p, &out, &chunk, file, chunkline);
            s++;
            while (isspace((unsigned char)*s))
                s++;
            start = s;
            while (isalpha((unsigned char)*s) || *s == '_')
                s++;
            directive = slice(start, (size_t)(s - start));
            s = trim(s);
            if (!strcmp(directive, "if") || !strcmp(directive, "ifdef") ||
                !strcmp(directive, "ifndef")) {
                int yes = 0;
                if (nesting == 64)
                    die("conditional nesting exceeds 64");
                if (active) {
                    if (!strcmp(directive, "if"))
                        yes = condition(p, s);
                    else {
                        yes = find(p, s) != NULL;
                        if (!strcmp(directive, "ifndef"))
                            yes = !yes;
                    }
                }
                cond[nesting].parent = active;
                cond[nesting].taken = yes;
                cond[nesting].active = active && yes;
                cond[nesting].otherwise = 0;
                active = cond[nesting++].active;
            } else if (!strcmp(directive, "elif") || !strcmp(directive, "else")) {
                int k = nesting - 1, yes;
                if (k < 0 || cond[k].otherwise)
                    die("misplaced #%s", directive);
                yes = cond[k].parent && !cond[k].taken;
                if (!strcmp(directive, "elif")) {
                    if (yes)
                        yes = condition(p, s);
                } else
                    cond[k].otherwise = 1;
                active = yes;
                cond[k].taken |= yes;
                cond[k].active = yes;
            } else if (!strcmp(directive, "endif")) {
                if (!nesting)
                    die("unmatched #endif");
                active = cond[--nesting].parent;
            } else if (active) {
                if (!strcmp(directive, "define"))
                    def(p, s);
                else if (!strcmp(directive, "undef"))
                    undef(p, s);
                else if (!strcmp(directive, "include")) {
                    char *f = include_file(p, file, s);
                    puts_s(&out, ppfile(p, f));
                } else if (!strcmp(directive, "error"))
                    die("#error %s", s);
                else if (!strcmp(directive, "pragma"))
                    die("unsupported #pragma %s", s);
                else if (!strcmp(directive, "line"))
                    die("user #line is not supported yet");
                else if (*directive)
                    die("unknown preprocessor directive #%s", directive);
            }
        } else if (active) {
            if (!chunk.n)
                chunkline = lineno;
            puts_s(&chunk, line);
            putc_s(&chunk, '\n');
        }
        line = next;
    }
    if (nesting)
        die("unterminated #if");
    flush(p, &out, &chunk, file, chunkline);
    p->depth--;
    diag_file = oldfile;
    diag_line = oldline;
    return out.s ? out.s : xstrdup("");
}
char *preprocess(const char *file, char **incs, int ninc, char **defs, int ndefs)
{
    PP p;
    int i;
    memset(&p, 0, sizeof p);
    p.incs = incs;
    p.ninc = ninc;
    def(&p, xstrdup("__STCC__ 1"));
    def(&p, xstrdup("__STDC__ 1"));
    def(&p, xstrdup("__STDC_HOSTED__ 0"));
    def(&p, xstrdup("__M68000__ 1"));
    for (i = 0; i < ndefs; i++) {
        char *s = xstrdup(defs[i]), *q = strchr(s, '=');
        if (q)
            *q = ' ';
        else
            s = fmt("%s 1", s);
        def(&p, s);
    }
    return ppfile(&p, file);
}
