#include "c.h"
Type *type_new(int k, int u)
{
    Type *t = xcalloc(1, sizeof *t);
    t->kind = k;
    t->bit_width = -1;
    t->identity = t;
    t->uns = u;
    t->align = k == TY_CHAR ? 1 : 2;
    t->size = k == TY_VOID ? 0 : k == TY_CHAR ? 1 : (k == TY_LONG || k == TY_PTR) ? 4 : 2;
    if (k == TY_STRUCT || k == TY_UNION)
        t->size = -1;
    return t;
}
Type *ptr(Type *b)
{
    Type *t = type_new(TY_PTR, 1);
    t->base = b;
    return t;
}
Type *array(Type *b, int n)
{
    Type *t = type_new(TY_ARRAY, 0);
    if (b->kind == TY_VOID || b->kind == TY_FUNC || b->size < 0)
        die("array element type is incomplete");
    if (n > 0 && b->size > INT32_MAX / n)
        die("array too large");
    t->base = b;
    t->count = n;
    t->size = n < 0 ? -1 : b->size * n;
    t->align = b->align;
    return t;
}
Type *function_type(Type *b)
{
    Type *t = type_new(TY_FUNC, 0);
    if (b->kind == TY_FUNC || b->kind == TY_ARRAY)
        die("invalid function return type");
    t->base = b;
    t->size = 0;
    return t;
}
Type *qualified(Type *b, int q)
{
    if (b->kind == TY_ARRAY)
        return array(qualified(b->base, q), b->count);
    Type *t = xmalloc(sizeof *t);
    *t = *b;
    t->qual |= q;
    return t;
}
int integer(Type *t)
{
    return t->kind == TY_CHAR || t->kind == TY_SHORT || t->kind == TY_INT || t->kind == TY_LONG ||
           t->kind == TY_ENUM;
}
int scalar(Type *t)
{
    return integer(t) || t->kind == TY_PTR;
}
int aggregate(Type *t)
{
    return t->kind == TY_STRUCT || t->kind == TY_UNION;
}
Type *decay(Type *t)
{
    return t->kind == TY_ARRAY ? ptr(t->base) : t->kind == TY_FUNC ? ptr(t) : t;
}
Type *promote(Type *t)
{
    if (t->bit_width >= 0)
        return type_new(TY_INT, t->uns && t->bit_width == 16);
    if (t->kind == TY_CHAR || t->kind == TY_SHORT || t->kind == TY_ENUM)
        return type_new(TY_INT, t->size == 2 && t->uns);
    return t;
}
Type *usual(Type *a, Type *b)
{
    int u;
    a = promote(a);
    b = promote(b);
    if (!integer(a) || !integer(b))
        die("integer operands required");
    if (a->size == b->size)
        u = a->uns || b->uns;
    else {
        Type *larger = a->size > b->size ? a : b;
        u = larger->uns;
    }
    return type_new(a->size == 4 || b->size == 4 ? TY_LONG : TY_INT, u);
}
static int compatible_impl(Type *a, Type *b, int ignore_top_qual)
{
    int i;
    if (a == b)
        return 1;
    if (!ignore_top_qual && a->qual != b->qual)
        return 0;
    if (a->kind != b->kind) {
        if ((a->kind == TY_INT || a->kind == TY_ENUM) && (b->kind == TY_INT || b->kind == TY_ENUM))
            return a->uns == b->uns;
        return 0;
    }
    if (a->uns != b->uns)
        return 0;
    switch (a->kind) {
    case TY_PTR:
        return compatible_impl(a->base, b->base, 0);
    case TY_ARRAY:
        return (a->count < 0 || b->count < 0 || a->count == b->count) &&
               compatible_impl(a->base, b->base, 0);
    case TY_FUNC:
        if (a->func_flags != b->func_flags)
            return 0;
        if (!compatible_impl(a->base, b->base, 0))
            return 0;
        if (!a->prototype || !b->prototype)
            return 1;
        if (a->nparams != b->nparams || a->variadic != b->variadic)
            return 0;
        for (i = 0; i < a->nparams; i++)
            if (!compatible_impl(a->params[i], b->params[i], 1))
                return 0;
        return 1;
    case TY_STRUCT:
    case TY_UNION:
        return a->identity == b->identity;
    default:
        return 1;
    }
}
int compatible(Type *a, Type *b)
{
    return compatible_impl(a, b, 1);
}
uint32_t convert_const(uint32_t v, Type *t)
{
    if (t->bit_width > 0) {
        uint32_t mask = (1u << t->bit_width) - 1;
        v &= mask;
        if (!t->uns && (v & (1u << (t->bit_width - 1))))
            v |= ~mask;
        return v;
    }
    if (t->size == 1)
        return t->uns ? v & 255 : (uint32_t)(int32_t)(int8_t)v;
    if (t->size == 2)
        return t->uns ? v & 65535 : (uint32_t)(int32_t)(int16_t)v;
    return v;
}
