#include "c.h"

/* Recursive-descent parser and typed AST. No host-sized C arithmetic is used
   to decide the target's promotions or object layout. */
typedef struct Tag {
    char *name;
    Type *type;
    int depth;
    struct Tag *next;
} Tag;
typedef struct Dcl {
    int kind, count, qual;
    char *name;
    struct Dcl *child;
    Type *ft;
} Dcl;
typedef struct {
    char *label;
    int64_t off;
    int absolute;
} Constant;
static Token *tok;
static Program program;
static Var *scope;
static Tag *tags;
static int depth, unique, frame, breakable, continuable, current_function_flags;
static Type *return_type;
static Node *current_switch;
static Function *last_function;
static int equal(const char *s)
{
    return !strcmp(tok->text, s);
}
static int eat(const char *s)
{
    if (equal(s)) {
        tok = tok->next;
        return 1;
    }
    return 0;
}
static Token *need(const char *s)
{
    Token *t = tok;
    if (!eat(s))
        at(tok, "expected '%s', found '%s'", s, tok->text);
    return t;
}
static char *identifier(void)
{
    char *s;
    if (tok->kind != TK_IDENT)
        at(tok, "expected identifier");
    s = tok->text;
    tok = tok->next;
    return s;
}
static Node *node(int kind, Token *t)
{
    Node *n = xcalloc(1, sizeof *n);
    n->kind = kind;
    n->tok = t;
    return n;
}
static Node *num(uint32_t v, Type *type, Token *t)
{
    Node *n = node(ND_NUM, t);
    n->val = v;
    n->type = type;
    return n;
}
static Node *varnode(Var *v, Token *t)
{
    Node *n = node(ND_VAR, t);
    n->var = v;
    n->type = v->type;
    return n;
}
static Node *cast(Node *n, Type *type)
{
    Node *c;
    if (aggregate(type) || type->kind == TY_ARRAY || type->kind == TY_FUNC)
        at(n->tok, "invalid scalar conversion");
    if (type->kind != TY_VOID && !scalar(decay(n->type)))
        at(n->tok, "scalar value required");
    c = node(ND_CAST, n->tok);
    c->type = type;
    c->lhs = n;
    return c;
}
static Var *lookup(const char *name)
{
    Var *v;
    for (v = scope; v; v = v->next)
        if (!strcmp(v->name, name))
            return v;
    for (v = program.globals; v; v = v->allnext)
        if (v->depth == 0 && !strcmp(v->name, name))
            return v;
    return NULL;
}
static Var *global(const char *name)
{
    Var *v;
    for (v = program.globals; v; v = v->allnext)
        if (!strcmp(v->name, name) && v->depth == 0)
            return v;
    return NULL;
}
static Var *bind(const char *name, Type *type, int storage)
{
    Var *v = xcalloc(1, sizeof *v);
    v->name = xstrdup(name);
    v->type = type;
    v->storage = storage;
    v->depth = depth;
    v->next = scope;
    scope = v;
    return v;
}
static Var *add_global(const char *name, Type *type, int storage)
{
    Var *v = global(name);
    if (v) {
        if (!compatible(v->type, type) || v->type->qual != type->qual)
            at(tok, "conflicting declaration of '%s'", name);
        if ((v->storage == SC_STATIC) != (storage == SC_STATIC) && storage != SC_EXTERN &&
            !(type->kind == TY_FUNC && storage == SC_NONE))
            at(tok, "conflicting linkage for '%s'", name);
        if (type->kind == TY_FUNC && type->prototype) {
            v->type = type;
        }
        if (v->type->kind == TY_ARRAY && v->type->count < 0 && type->count >= 0)
            v->type = type;
        return v;
    }
    v = bind(name, type, storage);
    v->depth = 0;
    v->label = fmt("_%s", name);
    v->allnext = program.globals;
    program.globals = v;
    return v;
}
static Var *anonymous(Type *type, const char *prefix)
{
    Var *v = xcalloc(1, sizeof *v);
    v->name = v->label = fmt("__stcc_%s_%d", prefix, unique++);
    v->type = type;
    v->storage = SC_STATIC;
    v->defined = 1;
    v->depth = 1;
    v->allnext = program.globals;
    program.globals = v;
    return v;
}
static Node *expression_c(void);
static Node *assign(void);
static Node *conditional(void);
static Type *spec(int *, int *);
static Type *declarator(Type *, char **, int);
static Type *with_function_flags(Type *t, int flags)
{
    if (!flags)
        return t;
    t = qualified(t, 0);
    if (t->kind == TY_PTR) {
        t->base = with_function_flags(t->base, flags);
        return t;
    }
    if (t->kind != TY_FUNC)
        at(tok, "__interrupt__/__naked__ require a function or function pointer");
    t->func_flags |= flags;
    if (t->base->kind != TY_VOID || !t->prototype || t->nparams || t->variadic)
        at(tok, "__interrupt__/__naked__ require void (void)");
    return t;
}
static Constant constant(Node *);
static int64_t iconst(Node *n)
{
    Constant c = constant(n);
    if (c.label)
        at(n->tok, "integer constant expression required");
    return c.off;
}
static int typename_start(void)
{
    Var *v;
    if (tok->kind != TK_IDENT)
        return 0;
    {
        const char *s = tok->text;
        static const char *names[] = {
            "void",     "char",          "short",     "int",    "long",     "signed",
            "unsigned", "const",         "volatile",  "struct", "union",    "enum",
            "typedef",  "extern",        "static",    "auto",   "register", "float",
            "double",   "__interrupt__", "__naked__", NULL};
        int i;
        for (i = 0; names[i]; i++)
            if (!strcmp(s, names[i]))
                return 1;
    }
    v = lookup(tok->text);
    return v && v->storage == SC_TYPEDEF;
}
static Tag *find_tag(const char *name)
{
    Tag *t;
    for (t = tags; t; t = t->next)
        if (!strcmp(t->name, name))
            return t;
    return NULL;
}
static Type *record(int kind)
{
    char *name = NULL;
    Tag *tag;
    Type *t;
    Member **tail;
    int off = 0, max = 0, bit_used = 16, bit_off = 0;
    if (tok->kind == TK_IDENT)
        name = identifier();
    tag = name ? find_tag(name) : NULL;
    if (!equal("{")) {
        if (!name)
            at(tok, "record needs a tag or a definition");
        if (tag) {
            if (tag->type->kind != kind)
                at(tok, "tag kind mismatch");
            return tag->type;
        }
        t = type_new(kind, 0);
        tag = xcalloc(1, sizeof *tag);
        tag->name = name;
        tag->type = t;
        tag->depth = depth;
        tag->next = tags;
        tags = tag;
        return t;
    }
    if (tag && tag->depth == depth) {
        t = tag->type;
        if (t->kind != kind || t->size >= 0)
            at(tok, "tag redefinition");
    } else {
        t = type_new(kind, 0);
        if (name) {
            tag = xcalloc(1, sizeof *tag);
            tag->name = name;
            tag->type = t;
            tag->depth = depth;
            tag->next = tags;
            tags = tag;
        }
    }
    need("{");
    tail = &t->members;
    while (!eat("}")) {
        int storage, flags;
        Type *base = spec(&storage, &flags);
        if (storage)
            at(tok, "storage class on member");
        do {
            char *mn = NULL;
            Type *mt = equal(":") ? base : declarator(base, &mn, 0);
            Member *m, *z;
            mt = with_function_flags(mt, flags);
            if (mt->size < 0 || mt->kind == TY_FUNC || mt->kind == TY_VOID)
                at(tok, "incomplete/invalid member type");
            for (z = t->members; z; z = z->next)
                if (mn && z->name && !strcmp(z->name, mn))
                    at(tok, "duplicate member '%s'", mn);
            m = xcalloc(1, sizeof *m);
            m->name = mn;
            if (eat(":")) {
                int64_t width = iconst(conditional());
                if (mt->kind != TY_INT)
                    at(tok, "bit fields require int, signed int or unsigned int");
                if (width < 0 || width > 16 || (!width && mn))
                    at(tok, "invalid bit field width (0..16; zero must be unnamed)");
                mt = qualified(mt, 0);
                mt->bit_width = (int)width;
                if (kind == TY_UNION) {
                    m->bit_shift = 16 - (int)width;
                    if (width && max < 2)
                        max = 2;
                } else if (!width) {
                    off = (off + 1) & ~1;
                    bit_used = 16;
                    m->offset = off;
                } else {
                    if (bit_used + width > 16) {
                        bit_off = (off + 1) & ~1;
                        off = bit_off + 2;
                        bit_used = 0;
                    }
                    bit_used += (int)width;
                    m->offset = bit_off;
                    m->bit_shift = 16 - bit_used;
                }
            } else {
                bit_used = 16;
                off = (off + mt->align - 1) / mt->align * mt->align;
                m->offset = kind == TY_UNION ? 0 : off;
                off += mt->size;
                if (mt->size > max)
                    max = mt->size;
            }
            m->type = mt;
            *tail = m;
            tail = &m->next;
        } while (eat(","));
        need(";");
    }
    t->size = ((kind == TY_UNION ? max : off) + 1) & ~1;
    t->align = 2;
    if (!t->members || !t->size)
        at(tok, "empty structure is not C89");
    return t;
}
static Type *enumeration(void)
{
    Type *t = type_new(TY_ENUM, 0);
    char *name = NULL;
    Tag *tag;
    int64_t value = 0;
    if (tok->kind == TK_IDENT)
        name = identifier();
    if (!equal("{")) {
        tag = name ? find_tag(name) : NULL;
        if (!tag || tag->type->kind != TY_ENUM)
            at(tok, "unknown enum tag");
        return tag->type;
    }
    if (name) {
        tag = find_tag(name);
        if (tag && tag->depth == depth)
            at(tok, "enum tag redefinition");
        tag = xcalloc(1, sizeof *tag);
        tag->name = name;
        tag->type = t;
        tag->depth = depth;
        tag->next = tags;
        tags = tag;
    }
    need("{");
    do {
        Var *v;
        char *en = identifier();
        if (eat("="))
            value = iconst(conditional());
        if (value < -32768 || value > 32767)
            at(tok, "enum value does not fit target int");
        v = lookup(en);
        if (v && v->depth == depth)
            at(tok, "duplicate enum constant");
        v = bind(en, t, SC_NONE);
        v->is_enum = 1;
        v->val = (uint32_t)value++;
        if (equal("}"))
            break;
        need(",");
        if (equal("}"))
            at(tok, "trailing enum comma is not C89");
    } while (1);
    need("}");
    return t;
}
static Type *spec(int *storage, int *flags)
{
    Type *type = NULL;
    int u = 0, sign = 0, shorts = 0, longs = 0, ints = 0, chars = 0, voids = 0, q = 0, seen = 0;
    *storage = 0;
    *flags = 0;
    for (;;) {
        int sc = 0;
        Var *v;
        if (equal("__interrupt__") || equal("__naked__")) {
            *flags |= equal("__interrupt__") ? FN_INTERRUPT : FN_NAKED;
            tok = tok->next;
            seen = 1;
            continue;
        }
        if (eat("typedef"))
            sc = SC_TYPEDEF;
        else if (eat("extern"))
            sc = SC_EXTERN;
        else if (eat("static"))
            sc = SC_STATIC;
        else if (eat("auto"))
            sc = SC_AUTO;
        else if (eat("register"))
            sc = SC_REGISTER;
        if (sc) {
            if (*storage)
                at(tok, "multiple storage classes");
            *storage = sc;
            seen = 1;
            continue;
        }
        if (eat("const")) {
            q |= 1;
            seen = 1;
            continue;
        }
        if (eat("volatile")) {
            q |= 2;
            seen = 1;
            continue;
        }
        if (eat("unsigned")) {
            if (u++ || sign)
                at(tok, "invalid signedness");
            seen = 1;
            continue;
        }
        if (eat("signed")) {
            if (sign++ || u)
                at(tok, "invalid signedness");
            seen = 1;
            continue;
        }
        if (eat("short")) {
            shorts++;
            seen = 1;
            continue;
        }
        if (eat("long")) {
            longs++;
            seen = 1;
            continue;
        }
        if (eat("int")) {
            ints++;
            seen = 1;
            continue;
        }
        if (eat("char")) {
            chars++;
            seen = 1;
            continue;
        }
        if (eat("void")) {
            voids++;
            seen = 1;
            continue;
        }
        if (equal("float") || equal("double"))
            at(tok, "float/double software arithmetic is not implemented");
        if (equal("struct") || equal("union") || equal("enum")) {
            int k = equal("struct") ? TY_STRUCT : equal("union") ? TY_UNION : TY_ENUM;
            if (type || u || sign || shorts || longs || ints || chars || voids)
                at(tok, "invalid type specifier combination");
            tok = tok->next;
            type = k == TY_ENUM ? enumeration() : record(k);
            seen = 1;
            continue;
        }
        v = lookup(tok->text);
        if (!type && !u && !sign && !shorts && !longs && !ints && !chars && !voids && v &&
            v->storage == SC_TYPEDEF) {
            type = v->type;
            tok = tok->next;
            seen = 1;
            continue;
        }
        break;
    }
    if (!seen)
        at(tok, "expected declaration specifiers");
    if (shorts > 1 || longs > 1 || ints > 1 || chars > 1 || voids > 1 || (shorts && longs) ||
        ((chars || voids) && (shorts || longs || ints)) || (voids && (u || sign)) ||
        (type && (u || sign || shorts || longs || ints || chars || voids)))
        at(tok, "invalid C89 type specifiers");
    if (!type)
        type = type_new(voids    ? TY_VOID
                        : chars  ? TY_CHAR
                        : longs  ? TY_LONG
                        : shorts ? TY_SHORT
                                 : TY_INT,
                        u);
    return q ? qualified(type, q) : type;
}
static Dcl *dnew(int k, Dcl *child)
{
    Dcl *d = xcalloc(1, sizeof *d);
    d->kind = k;
    d->child = child;
    return d;
}
static Dcl *decltree(int abstract)
{
    int stars = 0, quals[64];
    Dcl *d;
    while (eat("*")) {
        int q = 0;
        if (stars == 64)
            at(tok, "declarator nesting exceeds 64");
        while (equal("const") || equal("volatile")) {
            q |= equal("const") ? 1 : 2;
            tok = tok->next;
        }
        quals[stars++] = q;
    }
    if (eat("(")) {
        d = decltree(abstract);
        need(")");
    } else {
        d = dnew(0, NULL);
        if (tok->kind == TK_IDENT)
            d->name = identifier();
        else if (!abstract)
            at(tok, "declarator requires a name");
    }
    for (;;) {
        if (eat("[")) {
            int count = -1;
            Dcl *a;
            if (!equal("]")) {
                int64_t v = iconst(conditional());
                if (v <= 0 || v > INT_MAX / 4)
                    at(tok, "invalid array bound");
                count = (int)v;
            }
            need("]");
            a = dnew(2, d);
            a->count = count;
            d = a;
        } else if (eat("(")) {
            Dcl *f = dnew(3, d);
            Type *ft = function_type(type_new(TY_INT, 0));
            int cap = 0;
            f->ft = ft;
            if (eat(")")) {
                ft->prototype = 0;
                d = f;
                continue;
            }
            ft->prototype = 1;
            if (equal("void") && !strcmp(tok->next->text, ")")) {
                tok = tok->next;
                need(")");
                d = f;
                continue;
            }
            for (;;) {
                int storage, flags;
                char *pn = NULL;
                Type *pt;
                if (eat("...")) {
                    if (!ft->nparams)
                        at(tok, "ellipsis needs a named parameter");
                    ft->variadic = 1;
                    need(")");
                    break;
                }
                pt = spec(&storage, &flags);
                if (storage && storage != SC_REGISTER)
                    at(tok, "invalid parameter storage");
                pt = declarator(pt, &pn, 1);
                pt = with_function_flags(pt, flags);
                if (pt->kind == TY_VOID)
                    at(tok, "void parameter");
                pt = decay(pt);
                if (aggregate(pt))
                    at(tok, "aggregate parameters are not implemented");
                if (ft->nparams == cap) {
                    cap = cap ? cap * 2 : 8;
                    ft->params = xrealloc(ft->params, (size_t)cap * sizeof *ft->params);
                    ft->pnames = xrealloc(ft->pnames, (size_t)cap * sizeof *ft->pnames);
                }
                ft->params[ft->nparams] = pt;
                ft->pnames[ft->nparams++] = pn;
                if (eat(")"))
                    break;
                need(",");
            }
            d = f;
        } else
            break;
    }
    while (stars--) {
        d = dnew(1, d);
        d->qual = quals[stars];
    }
    return d;
}
static Type *apply_decl(Dcl *d, Type *base, char **name)
{
    if (!d->kind) {
        *name = d->name;
        return base;
    }
    if (d->kind == 1) {
        Type *t = ptr(base);
        t->qual = d->qual;
        return apply_decl(d->child, t, name);
    }
    if (d->kind == 2)
        return apply_decl(d->child, array(base, d->count), name);
    {
        Type *t = d->ft;
        t->base = base;
        if (base->kind == TY_ARRAY || base->kind == TY_FUNC)
            at(tok, "invalid function return type");
        return apply_decl(d->child, t, name);
    }
}
static Type *declarator(Type *base, char **name, int abstract)
{
    *name = NULL;
    return apply_decl(decltree(abstract), base, name);
}
static Type *type_name(void)
{
    int storage, flags;
    char *name;
    Type *t = spec(&storage, &flags);
    if (storage)
        at(tok, "storage class in type name");
    if (!equal(")"))
        t = declarator(t, &name, 1);
    return with_function_flags(t, flags);
}
static int lvalue(Node *n)
{
    return (n->kind == ND_VAR && n->type->kind != TY_FUNC) || n->kind == ND_DEREF ||
           (n->kind == ND_MEMBER && lvalue(n->lhs));
}
static void modifiable(Node *n)
{
    if (!lvalue(n) || n->type->kind == TY_ARRAY || n->type->kind == TY_FUNC || n->type->qual & 1)
        at(n->tok, "modifiable lvalue required");
}
static int pure_integer(Node *n)
{
    if (!integer(n->type))
        return 0;
    switch (n->kind) {
    case ND_NUM:
        return 1;
    case ND_CAST:
    case ND_UNARY:
        return pure_integer(n->lhs);
    case ND_BIN:
        return pure_integer(n->lhs) && pure_integer(n->rhs);
    case ND_COND:
        return pure_integer(n->cond) && pure_integer(n->lhs) && pure_integer(n->rhs);
    default:
        return 0;
    }
}
static int null_pointer(Node *n)
{
    return pure_integer(n) && iconst(n) == 0;
}
static int pointer_ok(Type *a, Type *b)
{
    if (a->kind != TY_PTR || b->kind != TY_PTR)
        return 0;
    if (a->base->kind == TY_VOID)
        return b->base->kind != TY_FUNC;
    if (b->base->kind == TY_VOID)
        return a->base->kind != TY_FUNC;
    return compatible(a->base, b->base);
}

static Node *assignment_conversion(Node *n, Type *to)
{
    Type *from = decay(n->type);
    if (aggregate(to)) {
        if (!compatible(to, from))
            at(n->tok, "incompatible aggregate assignment");
        return n;
    }
    if (to->kind == TY_PTR) {
        if (pointer_ok(to, from) && (from->base->qual & ~to->base->qual))
            at(n->tok, "pointer assignment discards qualifiers");
        if (!(pointer_ok(to, from) || null_pointer(n)))
            at(n->tok, "incompatible pointer assignment (use an explicit cast)");
    } else if (!integer(to) || !integer(from))
        at(n->tok, "incompatible assignment");
    return cast(n, to);
}
static Node *binary(char *op, Node *a, Node *b, Token *t)
{
    Node *n = node(ND_BIN, t);
    Type *x = decay(a->type), *y = decay(b->type), *common;
    n->op = op;
    n->lhs = a;
    n->rhs = b;
    if (!strcmp(op, "&&") || !strcmp(op, "||")) {
        if (!scalar(x) || !scalar(y))
            at(t, "logical operator needs scalar operands");
        n->type = type_new(TY_INT, 0);
        return n;
    }
    if (!strcmp(op, "+") || !strcmp(op, "-")) {
        if (x->kind == TY_PTR && integer(y)) {
            if (x->base->size <= 0)
                at(t, "pointer arithmetic on incomplete/function type");
            n->rhs = cast(b, promote(y));
            n->type = x;
            return n;
        }
        if (!strcmp(op, "+") && integer(x) && y->kind == TY_PTR)
            return binary(op, b, a, t);
        if (!strcmp(op, "-") && x->kind == TY_PTR && y->kind == TY_PTR) {
            if (!pointer_ok(x, y) || x->base->size <= 0)
                at(t, "incompatible pointer subtraction");
            n->type = type_new(TY_LONG, 0);
            return n;
        }
    }
    if (!strcmp(op, "==") || !strcmp(op, "!=") || !strcmp(op, "<") || !strcmp(op, ">") ||
        !strcmp(op, "<=") || !strcmp(op, ">=")) {
        if (x->kind == TY_PTR || y->kind == TY_PTR) {
            if (null_pointer(a) && y->kind == TY_PTR)
                n->lhs = cast(a, y);
            else if (null_pointer(b) && x->kind == TY_PTR)
                n->rhs = cast(b, x);
            else if (!pointer_ok(x, y))
                at(t, "incompatible pointer comparison");
            n->type = type_new(TY_INT, 0);
            return n;
        }
        if (!integer(x) || !integer(y))
            at(t, "invalid comparison");
        common = usual(x, y);
        n->lhs = cast(a, common);
        n->rhs = cast(b, common);
        n->type = type_new(TY_INT, 0);
        return n;
    }
    if (!integer(x) || !integer(y))
        at(t, "operator '%s' requires integer operands", op);
    if (!strcmp(op, "<<") || !strcmp(op, ">>")) {
        common = promote(x);
        n->lhs = cast(a, common);
        n->rhs = cast(b, promote(y));
        n->type = common;
        return n;
    }
    common = usual(x, y);
    n->lhs = cast(a, common);
    n->rhs = cast(b, common);
    n->type = common;
    return n;
}
static Node *primary(void)
{
    Token *t = tok;
    Node *n;
    Var *v;
    if (eat("(")) {
        n = expression_c();
        need(")");
        return n;
    }
    if (t->kind == TK_NUMBER) {
        tok = tok->next;
        return num(t->val, type_new(t->longlit ? TY_LONG : TY_INT, t->uns), t);
    }
    if (t->kind == TK_STRING) {
        Str bytes = {0};
        Type *ty;
        int i;
        while (tok->kind == TK_STRING) {
            for (i = 0; i < tok->len - 1; i++)
                putc_s(&bytes, (unsigned char)tok->bytes[i]);
            tok = tok->next;
        }
        putc_s(&bytes, 0);
        ty = array(type_new(TY_CHAR, 0), (int)bytes.n);
        v = anonymous(ty, "string");
        {
            Node head = {0}, *tail = &head;
            for (i = 0; i < (int)bytes.n; i++) {
                Node *z = node(ND_ASSIGN, t);
                z->offset = i;
                z->lhs = num((unsigned char)bytes.s[i], ty->base, t);
                z->type = ty->base;
                tail->next = z;
                tail = z;
            }
            v->init = head.next;
        }
        return varnode(v, t);
    }
    if (t->kind == TK_IDENT) {
        char *name = identifier();
        v = lookup(name);
        if (!v) {
            if (equal("(")) {
                v = add_global(name, function_type(type_new(TY_INT, 0)), SC_EXTERN);
            } else
                at(t, "undeclared identifier '%s'", name);
        }
        if (v->storage == SC_TYPEDEF)
            at(t, "typedef name used as expression");
        if (v->is_enum)
            return num(v->val, v->type, t);
        return varnode(v, t);
    }
    at(tok, "expected expression, found '%s'", tok->text);
    return NULL;
}
static Node *postfix(void)
{
    Node *n = primary(), *c;
    Token *t;
    for (;;) {
        t = tok;
        if (eat("[")) {
            Node *idx = expression_c(), *sum;
            need("]");
            sum = binary("+", n, idx, t);
            if (sum->type->kind != TY_PTR)
                at(t, "subscript needs a pointer/array");
            c = node(ND_DEREF, t);
            c->lhs = sum;
            c->type = sum->type->base;
            n = c;
        } else if (eat("(")) {
            Type *ft = decay(n->type);
            int cap = 0, i;
            if (ft->kind != TY_PTR || ft->base->kind != TY_FUNC)
                at(t, "called expression is not a function");
            ft = ft->base;
            if (ft->func_flags & FN_INTERRUPT)
                at(t, "an __interrupt__ handler cannot be called as a C function");
            if (aggregate(ft->base))
                at(t, "aggregate return ABI is not implemented");
            c = node(ND_CALL, t);
            c->lhs = n;
            c->type = ft->base;
            if (!eat(")"))
                for (;;) {
                    Node *arg = assign();
                    if (aggregate(arg->type))
                        at(arg->tok, "aggregate argument ABI is not implemented");
                    if (ft->prototype && c->nargs < ft->nparams)
                        arg = assignment_conversion(arg, ft->params[c->nargs]);
                    else
                        arg = cast(arg, promote(decay(arg->type)));
                    GROW(c->args, c->nargs, cap);
                    c->args[c->nargs++] = arg;
                    if (eat(")"))
                        break;
                    need(",");
                }
            i = ft->nparams;
            if (ft->prototype && (c->nargs < i || (!ft->variadic && c->nargs != i)))
                at(t, "wrong number of arguments");
            n = c;
        } else if (equal(".") || equal("->")) {
            int arrow = eat("->");
            Member *m;
            char *name;
            if (!arrow)
                need(".");
            if (arrow) {
                Type *ty = decay(n->type);
                if (ty->kind != TY_PTR)
                    at(t, "-> needs a pointer");
                c = node(ND_DEREF, t);
                c->lhs = n;
                c->type = ty->base;
                n = c;
            }
            if (!aggregate(n->type))
                at(t, "member access needs a structure/union");
            name = identifier();
            for (m = n->type->members; m; m = m->next)
                if (m->name && !strcmp(m->name, name))
                    break;
            if (!m)
                at(t, "unknown member '%s'", name);
            c = node(ND_MEMBER, t);
            c->lhs = n;
            c->offset = m->offset;
            c->bit_shift = m->bit_shift;
            c->type = n->type->qual ? qualified(m->type, n->type->qual) : m->type;
            n = c;
        } else if (equal("++") || equal("--")) {
            char *op = tok->text;
            tok = tok->next;
            modifiable(n);
            if (!scalar(decay(n->type)))
                at(t, "increment needs scalar lvalue");
            c = node(ND_INC, t);
            c->lhs = n;
            c->op = op;
            c->post = 1;
            c->type = n->type;
            n = c;
        } else
            return n;
    }
}
static Node *unary(void)
{
    Token *t = tok;
    Node *n, *c;
    Type *ty;
    if (eat("sizeof")) {
        n = NULL;
        if (equal("(") && typename_start() == 0) { /* inspect after '(' below */
        }
        if (equal("(")) {
            Token *save = tok;
            tok = tok->next;
            if (typename_start()) {
                ty = type_name();
                need(")");
            } else {
                tok = save;
                n = unary();
                ty = n->type;
            }
        } else {
            n = unary();
            ty = n->type;
        }
        if (n && n->kind == ND_MEMBER && ty->bit_width >= 0)
            at(t, "sizeof a bit field is not permitted");
        if (ty->size < 0 || ty->kind == TY_VOID || ty->kind == TY_FUNC)
            at(t, "sizeof incomplete/invalid type");
        return num((uint32_t)ty->size, type_new(TY_LONG, 1), t);
    }
    if (equal("(")) {
        Token *save = tok;
        tok = tok->next;
        if (typename_start()) {
            ty = type_name();
            need(")");
            n = unary();
            if (ty->kind != TY_VOID && !scalar(ty))
                at(t, "cast target must be scalar or void");
            return cast(n, ty);
        }
        tok = save;
    }
    if (eat("&")) {
        n = unary();
        if (n->kind == ND_MEMBER && n->type->bit_width >= 0)
            at(t, "cannot take the address of a bit field");
        if (!lvalue(n) && n->type->kind != TY_FUNC)
            at(t, "address requires an lvalue");
        if (n->kind == ND_VAR && n->var->storage == SC_REGISTER)
            at(t, "address of register variable");
        c = node(ND_ADDR, t);
        c->lhs = n;
        c->type = ptr(n->type);
        return c;
    }
    if (eat("*")) {
        n = unary();
        ty = decay(n->type);
        if (ty->kind != TY_PTR || ty->base->kind == TY_VOID)
            at(t, "dereference requires pointer to object/function");
        c = node(ND_DEREF, t);
        c->lhs = n;
        c->type = ty->base;
        return c;
    }
    if (equal("++") || equal("--")) {
        char *op = tok->text;
        tok = tok->next;
        n = unary();
        modifiable(n);
        if (!scalar(decay(n->type)))
            at(t, "invalid increment");
        c = node(ND_INC, t);
        c->op = op;
        c->lhs = n;
        c->type = n->type;
        return c;
    }
    if (equal("+") || equal("-") || equal("!") || equal("~")) {
        char *op = tok->text;
        tok = tok->next;
        n = unary();
        ty = decay(n->type);
        if (!strcmp(op, "!")) {
            if (!scalar(ty))
                at(t, "! requires scalar");
            ty = type_new(TY_INT, 0);
        } else {
            if (!integer(ty))
                at(t, "integer unary operand required");
            ty = promote(ty);
            n = cast(n, ty);
        }
        c = node(ND_UNARY, t);
        c->op = op;
        c->lhs = n;
        c->type = ty;
        return c;
    }
    return postfix();
}
static Node *precedence(int level)
{
    static const char *levels[] = {"||",    "&&",        "|",     "^",   "&",
                                   "== !=", "< > <= >=", "<< >>", "+ -", "* / %"};
    Node *n;
    if (level == 10)
        return unary();
    n = precedence(level + 1);
    for (;;) {
        char *ops = xstrdup(levels[level]), *p = ops, *end;
        int found = 0;
        Token *t = tok;
        while (*p) {
            end = strchr(p, ' ');
            if (end)
                *end = 0;
            if (equal(p)) {
                char *op = tok->text;
                tok = tok->next;
                n = binary(op, n, precedence(level + 1), t);
                found = 1;
                break;
            }
            if (!end)
                break;
            p = end + 1;
        }
        free(ops);
        if (!found)
            return n;
    }
}
static Node *conditional(void)
{
    Node *n = precedence(0), *c;
    Type *a, *b;
    Token *t = tok;
    if (!eat("?"))
        return n;
    if (!scalar(decay(n->type)))
        at(t, "conditional test must be scalar");
    c = node(ND_COND, t);
    c->cond = n;
    c->lhs = expression_c();
    need(":");
    c->rhs = conditional();
    a = decay(c->lhs->type);
    b = decay(c->rhs->type);
    if (integer(a) && integer(b)) {
        c->type = usual(a, b);
        c->lhs = cast(c->lhs, c->type);
        c->rhs = cast(c->rhs, c->type);
    } else if (pointer_ok(a, b)) {
        Type *base = a->base->kind == TY_VOID   ? a->base
                     : b->base->kind == TY_VOID ? b->base
                                                : a->base;
        c->type = ptr(qualified(base, a->base->qual | b->base->qual));
    } else if (a->kind == TY_PTR && null_pointer(c->rhs)) {
        c->type = a;
        c->rhs = cast(c->rhs, a);
    } else if (b->kind == TY_PTR && null_pointer(c->lhs)) {
        c->type = b;
        c->lhs = cast(c->lhs, b);
    } else if (a->kind == TY_VOID && b->kind == TY_VOID)
        c->type = a;
    else if (compatible(a, b) && aggregate(a))
        c->type = a;
    else
        at(t, "incompatible conditional operands");
    return c;
}
static Node *assign(void)
{
    Node *n = conditional(), *a;
    Token *t = tok;
    char *op = tok->text;
    if (!strcmp(op, "=") || !strcmp(op, "+=") || !strcmp(op, "-=") || !strcmp(op, "*=") ||
        !strcmp(op, "/=") || !strcmp(op, "%=") || !strcmp(op, "<<=") || !strcmp(op, ">>=") ||
        !strcmp(op, "&=") || !strcmp(op, "^=") || !strcmp(op, "|=")) {
        tok = tok->next;
        modifiable(n);
        a = node(ND_ASSIGN, t);
        a->lhs = n;
        a->type = n->type;
        a->rhs = assign();
        a->op = op;
        if (!strcmp(op, "="))
            a->rhs = assignment_conversion(a->rhs, a->type);
        else {
            char *bin = slice(op, strlen(op) - 1);
            Node *check = binary(bin, n, a->rhs, t);
            a->text = bin; /* codegen evaluates the lvalue address once */
            a->init = check;
        }
        return a;
    }
    return n;
}
static Node *expression_c(void)
{
    Node *n = assign();
    while (eat(",")) {
        Node *c = node(ND_COMMA, n->tok);
        c->lhs = n;
        c->rhs = assign();
        c->type = c->rhs->type;
        n = c;
    }
    return n;
}

/* Constant folding is separate from machine-code emission. A static address
   carries exactly one symbolic base plus a signed byte offset. */
static Constant constant(Node *n)
{
    Constant a = {NULL, 0, 1}, b;
    Type *t = n->type;
    uint32_t x, y, v = 0;
    int u;
    const char *op = n->op;
    switch (n->kind) {
    case ND_NUM:
        a.off = t->uns ? (int64_t)n->val : (int64_t)(int32_t)n->val;
        return a;
    case ND_CAST:
        a = constant(n->lhs);
        if (a.label) {
            if (t->size != 4)
                at(n->tok, "narrowing address constant");
            return a;
        }
        v = convert_const((uint32_t)a.off, t);
        a.off = t->uns ? (int64_t)v : (int64_t)(int32_t)v;
        return a;
    case ND_ADDR:
        if (n->lhs->kind == ND_VAR) {
            Var *z = n->lhs->var;
            if (!z->label)
                at(n->tok, "automatic address is not a static constant");
            a.label = z->label;
            return a;
        }
        if (n->lhs->kind == ND_DEREF)
            return constant(n->lhs->lhs);
        if (n->lhs->kind == ND_MEMBER) {
            Node temp = *n;
            temp.lhs = n->lhs->lhs;
            a = constant(&temp);
            a.off += n->lhs->offset;
            return a;
        }
        break;
    case ND_VAR:
        if (n->type->kind == TY_ARRAY || n->type->kind == TY_FUNC) {
            if (!n->var->label)
                break;
            a.label = n->var->label;
            return a;
        }
        break;
    case ND_UNARY:
        a = constant(n->lhs);
        if (a.label)
            break;
        x = (uint32_t)a.off;
        if (!strcmp(op, "-"))
            v = 0u - x;
        else if (!strcmp(op, "~"))
            v = ~x;
        else if (!strcmp(op, "!"))
            v = !x;
        else
            v = x;
        v = convert_const(v, t);
        a.off = t->uns ? (int64_t)v : (int64_t)(int32_t)v;
        return a;
    case ND_COND:
        a = constant(n->cond);
        if (a.label)
            break;
        return constant(a.off ? n->lhs : n->rhs);
    case ND_BIN:
        a = constant(n->lhs);
        if (!strcmp(op, "&&") && !a.label && !a.off)
            return a;
        if (!strcmp(op, "||") && !a.label && a.off) {
            a.off = 1;
            return a;
        }
        b = constant(n->rhs);
        if (!strcmp(op, "+") || !strcmp(op, "-")) {
            int subtract = !strcmp(op, "-");
            Type *l = decay(n->lhs->type), *r = decay(n->rhs->type);
            if (l->kind == TY_PTR && integer(r))
                b.off *= l->base->size;
            if (a.label && b.label) {
                if (!subtract || strcmp(a.label, b.label))
                    at(n->tok, "unsupported symbolic constant expression");
                a.label = NULL;
            } else if (b.label) {
                if (subtract || a.label)
                    at(n->tok, "unsupported relocation");
                a.label = b.label;
            }
            a.off += subtract ? -b.off : b.off;
            if (l->kind == TY_PTR && r->kind == TY_PTR)
                a.off /= l->base->size;
            if (a.label)
                return a;
            v = convert_const((uint32_t)a.off, t);
            a.off = t->uns ? (int64_t)v : (int64_t)(int32_t)v;
            return a;
        }
        if (a.label || b.label)
            break;
        x = (uint32_t)a.off;
        y = (uint32_t)b.off;
        u = decay(n->lhs->type)->uns;
        if (!strcmp(op, "*"))
            v = x * y;
        else if (!strcmp(op, "/") || !strcmp(op, "%")) {
            if (!y)
                at(n->tok, "division by zero in constant expression");
            if (u)
                v = !strcmp(op, "/") ? x / y : x % y;
            else {
                int64_t xx = (int32_t)x, yy = (int32_t)y;
                v = (uint32_t)(!strcmp(op, "/") ? xx / yy : xx % yy);
            }
        } else if (!strcmp(op, "<<") || !strcmp(op, ">>")) {
            if (y >= (unsigned)decay(n->lhs->type)->size * 8)
                at(n->tok, "invalid constant shift count");
            v = !strcmp(op, "<<") ? x << y : u ? x >> y : (uint32_t)((int32_t)x >> y);
        } else if (!strcmp(op, "&"))
            v = x & y;
        else if (!strcmp(op, "|"))
            v = x | y;
        else if (!strcmp(op, "^"))
            v = x ^ y;
        else if (!strcmp(op, "=="))
            v = x == y;
        else if (!strcmp(op, "!="))
            v = x != y;
        else if (!strcmp(op, "<"))
            v = u ? x < y : (int32_t)x < (int32_t)y;
        else if (!strcmp(op, ">"))
            v = u ? x > y : (int32_t)x > (int32_t)y;
        else if (!strcmp(op, "<="))
            v = u ? x <= y : (int32_t)x <= (int32_t)y;
        else if (!strcmp(op, ">="))
            v = u ? x >= y : (int32_t)x >= (int32_t)y;
        else if (!strcmp(op, "&&"))
            v = x && y;
        else if (!strcmp(op, "||"))
            v = x || y;
        else
            break;
        v = convert_const(v, t);
        a.off = t->uns ? (int64_t)v : (int64_t)(int32_t)v;
        return a;
    }
    at(n->tok, "expression is not a supported static constant");
    return a;
}
/* Initializers are flattened into typed writes. Both static and automatic
   aggregates use the same layout; omitted members become zero. */
static void init_write(Node **head, Node **tail, Node *value, Type *type, int off, int bit_shift)
{
    Node *n = node(ND_ASSIGN, value->tok);
    n->lhs = assignment_conversion(value, type);
    n->type = type;
    n->offset = off;
    n->bit_shift = bit_shift;
    if (*tail)
        (*tail)->next = n;
    else
        *head = n;
    *tail = n;
}
static void initializer(Type *ty, int off, int bit_shift, Node **head, Node **tail)
{
    int braces = eat("{"), i = 0;
    Token *t = tok;
    if (ty->kind == TY_ARRAY && ty->base->kind == TY_CHAR && tok->kind == TK_STRING) {
        Str bytes = {0};
        int j;
        while (tok->kind == TK_STRING) {
            for (j = 0; j < tok->len - 1; j++)
                putc_s(&bytes, (unsigned char)tok->bytes[j]);
            tok = tok->next;
        }
        putc_s(&bytes, 0);
        if (ty->count < 0) {
            ty->count = (int)bytes.n;
            ty->size = ty->count;
        }
        if (ty->count < (int)bytes.n - 1)
            at(t, "string initializer too long");
        for (j = 0; j < ty->count && j < (int)bytes.n; j++)
            init_write(head, tail, num((unsigned char)bytes.s[j], ty->base, t), ty->base, off + j,
                       0);
        if (braces) {
            eat(",");
            need("}");
        }
        return;
    }
    if (aggregate(ty) && !braces) {
        init_write(head, tail, assign(), ty, off, 0);
        return;
    }
    if (ty->kind == TY_ARRAY || aggregate(ty)) {
        Member *m = ty->members;
        if (!braces)
            at(tok, "aggregate initializer requires braces (brace elision is not implemented)");
        while (!eat("}")) {
            if (ty->kind == TY_ARRAY) {
                if (ty->count >= 0 && i >= ty->count)
                    at(tok, "excess array initializer");
                initializer(ty->base, off + i * ty->base->size, 0, head, tail);
            } else {
                while (m && !m->name)
                    m = m->next;
                if (!m || (ty->kind == TY_UNION && i))
                    at(tok, "excess record initializer");
                initializer(m->type, off + m->offset, m->bit_shift, head, tail);
                m = m->next;
            }
            i++;
            if (eat("}"))
                break;
            need(",");
        }
        if (!i)
            at(t, "empty initializer is not C89");
        if (ty->kind == TY_ARRAY && ty->count < 0) {
            ty->count = i;
            ty->size = i * ty->base->size;
        }
        return;
    }
    init_write(head, tail, assign(), ty, off, bit_shift);
    if (braces) {
        eat(",");
        need("}");
    }
}
static void append(Node **head, Node **tail, Node *n)
{
    if (!n)
        return;
    if (*tail)
        (*tail)->next = n;
    else
        *head = n;
    while (n->next)
        n = n->next;
    *tail = n;
}
static Node *local_declaration(void)
{
    int storage, flags;
    Type *base;
    if (current_function_flags & FN_NAKED)
        at(tok, "__naked__ bodies only accept inline assembly, without declarations");
    base = spec(&storage, &flags);
    Node *head = NULL, *tail = NULL;
    Token *start = tok;
    if (eat(";"))
        return NULL;
    do {
        char *name;
        Type *ty = declarator(base, &name, 0);
        ty = with_function_flags(ty, flags);
        Var *v, *old = lookup(name);
        Node *ih = NULL, *it = NULL;
        if (old && old->depth == depth)
            at(start, "redeclaration of '%s'", name);
        if (storage == SC_TYPEDEF) {
            bind(name, ty, storage);
            if (equal("="))
                at(tok, "typedef initializer");
            continue;
        }
        if (ty->kind == TY_VOID)
            at(tok, "variable has void type");
        if (storage == SC_EXTERN || ty->kind == TY_FUNC) {
            Var *g = global(name);
            if (!g)
                g = add_global(name, ty, SC_EXTERN);
            else if (!compatible(g->type, ty))
                at(tok, "conflicting extern declaration");
            v = bind(name, ty, SC_EXTERN);
            v->label = g->label;
            if (equal("="))
                at(tok, "block extern initializer");
            continue;
        }
        v = bind(name, ty, storage);
        if (storage == SC_STATIC) {
            Var *g = anonymous(ty, "static");
            v->label = g->label;
            if (eat("=")) {
                initializer(ty, 0, 0, &ih, &it);
                g->init = ih;
                for (it = ih; it; it = it->next)
                    (void)constant(it->lhs);
            }
        } else {
            if (eat("="))
                initializer(ty, 0, 0, &ih, &it);
            if (ty->size < 0)
                at(tok, "incomplete local type");
            frame = (frame + ty->align - 1) / ty->align * ty->align + ty->size;
            frame = (frame + 1) & ~1;
            if (frame > 32760)
                at(tok, "local frame exceeds 68000 LINK displacement");
            v->offset = -frame;
            if (ih) {
                if (ty->kind == TY_ARRAY || aggregate(ty)) {
                    Node *z = node(ND_ZERO, start);
                    z->var = v;
                    z->type = ty;
                    append(&head, &tail, z);
                }
                for (it = ih; it; it = it->next) {
                    Node *write = node(ND_ASSIGN, it->tok), *lhs = varnode(v, it->tok),
                         *st = node(ND_EXPR, it->tok);
                    if (ty->kind == TY_ARRAY || aggregate(ty)) {
                        Node *m = node(ND_MEMBER, it->tok);
                        m->lhs = lhs;
                        m->type = it->type;
                        m->offset = it->offset;
                        m->bit_shift = it->bit_shift;
                        lhs = m;
                    }
                    write->lhs = lhs;
                    write->rhs = it->lhs;
                    write->op = "=";
                    write->type = it->type;
                    st->lhs = write;
                    append(&head, &tail, st);
                }
            }
        }
    } while (eat(","));
    need(";");
    return head;
}
static int asmreg(const char *s)
{
    return (strlen(s) == 2 && strchr("dDaA", s[0]) && s[1] >= '0' && s[1] <= '7') ||
           samei(s, "sp") || samei(s, "pc") || samei(s, "sr") || samei(s, "ccr") || samei(s, "usp");
}
static char *inline_asm(Token *t)
{
    char *s = xstrdup(t->text), *p = trim(s), *body, *end, *line, *next;
    Str out = {0};
    int id = unique++;
    if (*p == '(') {
        if (current_function_flags & FN_NAKED)
            at(t, "__naked__ assembly has no automatic clobber preservation; omit the list");
        p++;
        for (;;) {
            char *start;
            while (isspace((unsigned char)*p))
                p++;
            if (*p == ')') {
                p++;
                break;
            }
            start = p;
            while (isalnum((unsigned char)*p) || *p == '_')
                p++;
            if (p == start)
                at(t, "invalid inline clobber list");
            {
                char *r = slice(start, (size_t)(p - start));
                if (samei(r, "sp") || samei(r, "a7") || samei(r, "a6"))
                    at(t, "sp/a7/a6 cannot be clobbered");
                if (!(strlen(r) == 2 && strchr("dDaA", r[0]) && r[1] >= '0' && r[1] <= '7') &&
                    !samei(r, "cc") && !samei(r, "memory"))
                    at(t, "unknown clobber '%s'", r);
            }
            while (isspace((unsigned char)*p))
                p++;
            if (*p == ')') {
                p++;
                break;
            }
            if (*p++ != ',')
                at(t, "expected ',' in clobber list");
        }
    }
    p = trim(p);
    if (*p++ != '{')
        at(t, "expected inline assembly block");
    body = p;
    end = strrchr(p, '}');
    if (!end)
        at(t, "unterminated inline assembly");
    *end = 0;
    printf_s(&out, "__stcc_inline_%d:\n", id);
    line = body;
    while (line) {
        int operand = 0, q = 0;
        next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        p = line;
        while (isspace((unsigned char)*p))
            p++;
        if (*p && *p != ';' && *p != '*') { /* Every assembly label must be local; this also
                                               isolates repeated .labels. */
            char *z = p;
            while (*z && !isspace((unsigned char)*z) && *z != ':')
                z++;
            if (*z == ':' && *p != '.')
                at(t, "inline assembly labels must start with '.'");
            if (*z == ':') {
                puts_s(&out, slice(p, (size_t)(z + 1 - p)));
                putc_s(&out, ' ');
                p = z + 1;
                while (isspace((unsigned char)*p))
                    p++;
            }
            putc_s(&out, ' '); /* Copy mnemonic, then resolve identifiers only in operands. */
            while (*p && !isspace((unsigned char)*p))
                putc_s(&out, *p++);
            operand = 1;
        }
        for (; *p;) {
            if (*p == ';' || (!operand && *p == '*')) {
                puts_s(&out, p);
                break;
            }
            if (*p == '\'' || *p == '"') {
                q = *p;
                putc_s(&out, *p++);
                while (*p) {
                    int c = *p++;
                    putc_s(&out, c);
                    if (c == q) {
                        if (*p == q)
                            putc_s(&out, *p++);
                        else
                            break;
                    }
                }
                continue;
            }
            if (operand && (*p == '$' || *p == '%' || isdigit((unsigned char)*p))) {
                putc_s(&out, *p++);
                while (isalnum((unsigned char)*p))
                    putc_s(&out, *p++);
                continue;
            }
            if (operand && (isalpha((unsigned char)*p) || *p == '_')) {
                char *start = p++, *name;
                Var *v;
                while (isalnum((unsigned char)*p) || *p == '_')
                    p++;
                name = slice(start, (size_t)(p - start));
                v = lookup(name);
                if (v && v->storage != SC_TYPEDEF && !v->is_enum && !asmreg(name) &&
                    (start == line || start[-1] != '.') &&
                    !(start > line && isalnum((unsigned char)start[-1]))) {
                    if (v->label)
                        puts_s(&out, v->label);
                    else
                        printf_s(&out, "%d(a6)", v->offset);
                } else
                    puts_s(&out, name);
            } else
                putc_s(&out, *p++);
        }
        putc_s(&out, '\n');
        line = next;
    }
    return out.s;
}
typedef struct UserLabel {
    char *name, *label;
    int defined, used;
    struct UserLabel *next;
} UserLabel;
static UserLabel *labels;
static UserLabel *get_label(const char *name)
{
    UserLabel *l;
    for (l = labels; l; l = l->next)
        if (!strcmp(l->name, name))
            return l;
    l = xcalloc(1, sizeof *l);
    l->name = xstrdup(name);
    l->label = fmt("__stcc_goto_%d", unique++);
    l->next = labels;
    labels = l;
    return l;
}
static Node *statement(void);
static Node *block(void)
{
    Node *n = node(ND_BLOCK, tok), *head = NULL, *tail = NULL;
    Var *saved = scope;
    Tag *tsaved = tags;
    int seen_statement = 0;
    need("{");
    depth++;
    while (!eat("}")) {
        if (tok->kind == TK_EOF)
            at(tok, "unterminated compound statement");
        if (typename_start()) {
            if (seen_statement)
                at(tok, "C89 declarations must precede statements");
            append(&head, &tail, local_declaration());
        } else {
            seen_statement = 1;
            append(&head, &tail, statement());
        }
    }
    depth--;
    scope = saved;
    tags = tsaved;
    n->body = head;
    return n;
}
static void scalar_condition(Node *n)
{
    if (!scalar(decay(n->type)))
        at(n->tok, "scalar condition required");
}
static Node *statement(void)
{
    Token *t = tok;
    Node *n;
    if (equal("{"))
        return block();
    if (eat(";"))
        return node(ND_BLOCK, t);
    if (tok->kind == TK_ASM) {
        n = node(ND_ASM, t);
        n->text = inline_asm(tok);
        tok = tok->next;
        eat(";");
        return n;
    }
    if (current_function_flags & FN_NAKED)
        at(t, "__naked__ bodies only accept inline assembly");
    if (eat("return")) {
        n = node(ND_RETURN, t);
        if (!eat(";")) {
            n->lhs = expression_c();
            if (return_type->kind == TY_VOID)
                at(t, "value returned by void function");
            n->lhs = assignment_conversion(n->lhs, return_type);
            need(";");
        } else if (return_type->kind != TY_VOID)
            at(t, "return without a value");
        return n;
    }
    if (eat("if")) {
        n = node(ND_IF, t);
        need("(");
        n->cond = expression_c();
        scalar_condition(n->cond);
        need(")");
        n->body = statement();
        if (eat("else"))
            n->els = statement();
        return n;
    }
    if (eat("while")) {
        n = node(ND_FOR, t);
        need("(");
        n->cond = expression_c();
        scalar_condition(n->cond);
        need(")");
        breakable++;
        continuable++;
        n->body = statement();
        breakable--;
        continuable--;
        return n;
    }
    if (eat("for")) {
        n = node(ND_FOR, t);
        need("(");
        if (!eat(";")) {
            if (typename_start())
                at(tok, "for-loop declarations are not C89");
            n->init = expression_c();
            need(";");
        }
        if (!eat(";")) {
            n->cond = expression_c();
            scalar_condition(n->cond);
            need(";");
        }
        if (!eat(")")) {
            n->inc = expression_c();
            need(")");
        }
        breakable++;
        continuable++;
        n->body = statement();
        breakable--;
        continuable--;
        return n;
    }
    if (eat("do")) {
        n = node(ND_DO, t);
        breakable++;
        continuable++;
        n->body = statement();
        breakable--;
        continuable--;
        need("while");
        need("(");
        n->cond = expression_c();
        scalar_condition(n->cond);
        need(")");
        need(";");
        return n;
    }
    if (eat("break")) {
        if (!breakable)
            at(t, "break outside loop/switch");
        n = node(ND_BREAK, t);
        need(";");
        return n;
    }
    if (eat("continue")) {
        if (!continuable)
            at(t, "continue outside loop");
        n = node(ND_CONTINUE, t);
        need(";");
        return n;
    }
    if (eat("switch")) {
        Node *save = current_switch;
        n = node(ND_SWITCH, t);
        need("(");
        n->cond = expression_c();
        if (!integer(n->cond->type))
            at(t, "switch needs an integer expression");
        n->cond = cast(n->cond, promote(n->cond->type));
        need(")");
        current_switch = n;
        breakable++;
        n->body = statement();
        breakable--;
        current_switch = save;
        return n;
    }
    if (equal("case") || equal("default")) {
        Node *c;
        int def = eat("default");
        if (!def)
            need("case");
        if (!current_switch)
            at(t, "case/default outside switch");
        n = node(ND_CASE, t);
        n->post = def;
        n->id = unique++;
        if (!def)
            n->val = convert_const((uint32_t)iconst(conditional()), current_switch->cond->type);
        for (c = current_switch->cases; c; c = c->casenext)
            if ((def && c->post) || (!def && !c->post && n->val == c->val))
                at(t, "duplicate case/default");
        n->casenext = current_switch->cases;
        current_switch->cases = n;
        need(":");
        if (typename_start())
            at(tok, "a label must precede a statement");
        n->body = statement();
        return n;
    }
    if (eat("goto")) {
        UserLabel *l = get_label(identifier());
        n = node(ND_GOTO, t);
        n->text = l->label;
        l->used = 1;
        need(";");
        return n;
    }
    if (tok->kind == TK_IDENT && !strcmp(tok->next->text, ":")) {
        UserLabel *l = get_label(identifier());
        need(":");
        if (l->defined)
            at(t, "duplicate label '%s'", l->name);
        l->defined = 1;
        n = node(ND_LABEL, t);
        n->text = l->label;
        n->body = statement();
        return n;
    }
    n = node(ND_EXPR, t);
    n->lhs = expression_c();
    need(";");
    return n;
}
char *compile_c(const char *s, const char *file)
{
    memset(&program, 0, sizeof program);
    scope = NULL;
    tags = NULL;
    depth = unique = current_function_flags = 0;
    last_function = NULL;
    tok = lex_c(s, file);
    while (tok->kind != TK_EOF) {
        int storage = 0, flags = 0;
        Type *base;
        if (tok->kind == TK_IDENT && !typename_start() && !strcmp(tok->next->text, "("))
            base = type_new(TY_INT, 0);
        else
            base = spec(&storage, &flags);
        if (storage == SC_AUTO || storage == SC_REGISTER)
            at(tok, "invalid file-scope storage class");
        if (eat(";"))
            continue;
        do {
            char *name;
            Type *ty = declarator(base, &name, 0);
            ty = with_function_flags(ty, flags);
            Var *v;
            Token *where = tok;
            if (storage == SC_TYPEDEF) {
                Var *old = lookup(name);
                if (old && old->depth == 0)
                    at(tok, "typedef redeclaration");
                bind(name, ty, storage);
                if (equal("="))
                    at(tok, "typedef initializer");
                continue;
            }
            if (ty->kind == TY_VOID)
                at(tok, "object has void type");
            v = add_global(name, ty, storage);
            if (!strcmp(name, "main") && ty->kind == TY_FUNC && ty->func_flags)
                at(tok, "main cannot use __interrupt__ or __naked__");
            if (ty->kind == TY_FUNC && equal("{")) {
                Function *f;
                Var *saved = scope;
                UserLabel *l;
                Tag *tsaved = tags;
                int i, argoff = 8;
                if (v->defined)
                    at(tok, "function redefinition '%s'", name);
                if (aggregate(ty->base))
                    at(tok, "aggregate return ABI is not implemented");
                if (!strcmp(name, "main") && ty->nparams)
                    at(tok, "startup currently supports main(void) only");
                v->defined = 1;
                f = xcalloc(1, sizeof *f);
                f->var = v;
                f->nparams = ty->nparams;
                f->params = xcalloc((size_t)f->nparams, sizeof *f->params);
                if (last_function)
                    last_function->next = f;
                else
                    program.functions = f;
                last_function = f;
                depth = 1;
                frame = 0;
                labels = NULL;
                return_type = ty->base;
                current_function_flags = ty->func_flags;
                breakable = continuable = 0;
                current_switch = NULL;
                for (i = 0; i < ty->nparams; i++) {
                    Var *pv;
                    int slot = ty->params[i]->size == 4 ? 4 : 2;
                    if (!ty->pnames[i])
                        at(where, "function definition needs parameter names");
                    if (lookup(ty->pnames[i]) && lookup(ty->pnames[i])->depth == 1)
                        at(where, "duplicate parameter");
                    pv = bind(ty->pnames[i], ty->params[i], SC_AUTO);
                    pv->offset = argoff + (ty->params[i]->size == 1 ? 1 : 0);
                    f->params[i] = pv;
                    argoff += slot;
                }
                /* Parameters and the outermost block share one C scope. */ depth = 0;
                f->body = block();
                current_function_flags = 0;
                f->frame = frame;
                scope = saved;
                tags = tsaved;
                depth = 0;
                for (l = labels; l; l = l->next)
                    if (l->used && !l->defined)
                        at(where, "undefined label '%s'", l->name);
                goto next_declaration;
            }
            if (ty->kind == TY_FUNC) {
                if (equal("="))
                    at(tok, "function initializer");
                continue;
            }
            if (eat("=")) {
                Node *head = NULL, *tail = NULL;
                if (v->init || v->defined == 2)
                    at(tok, "multiple initializers for '%s'", name);
                initializer(v->type, 0, 0, &head, &tail);
                v->init = head;
                v->defined = 2;
                for (tail = head; tail; tail = tail->next) {
                    Constant c = constant(tail->lhs);
                    tail->rhs = num((uint32_t)c.off, tail->type, tail->tok);
                    tail->text = c.label;
                }
            } else if (storage != SC_EXTERN && !v->defined)
                v->defined = 1;
        } while (eat(","));
        need(";");
    next_declaration:;
    }
    {
        Var *v;
        for (v = program.globals; v; v = v->allnext)
            if (v->type->kind != TY_FUNC && v->defined) {
                if (v->type->kind == TY_ARRAY && v->type->count < 0) {
                    v->type->count = 1;
                    v->type->size = v->type->base->size;
                }
                if (v->type->size < 0)
                    at(tok, "incomplete object '%s'", v->name);
                if (v->init) {
                    Node *it;
                    for (it = v->init; it; it = it->next)
                        if (!it->rhs) {
                            Constant c = constant(it->lhs);
                            it->rhs = num((uint32_t)c.off, it->type, it->tok);
                            it->text = c.label;
                        }
                }
            }
    }
    return generate(&program);
}

/* #if evaluates integer tokens as target long / unsigned long, as C89
   requires. Reuse the typed constant folder, including short circuit. */
int c_preprocessor_condition(const char *text)
{
    Token *saved = tok, *p;
    Var *oldscope = scope;
    Program oldprogram = program;
    Node *n;
    int result;
    tok = lex_c(text, diag_file);
    scope = NULL;
    memset(&program, 0, sizeof program);
    for (p = tok; p->kind != TK_EOF; p = p->next) {
        p->line = diag_line;
        if (p->kind == TK_IDENT) {
            p->kind = TK_NUMBER;
            p->val = 0;
            p->text = "0";
        }
        if (p->kind == TK_NUMBER)
            p->longlit = 1;
    }
    n = conditional();
    if (tok->kind != TK_EOF)
        at(tok, "unexpected token in #if: '%s'", tok->text);
    result = iconst(n) != 0;
    tok = saved;
    scope = oldscope;
    program = oldprogram;
    return result;
}
