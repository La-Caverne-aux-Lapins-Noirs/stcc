#include "c.h"

/* Stack-machine code generator: d0 is the current value; nested operands live
   on the stack. No C value is cached across an inline-assembly barrier. */
static Str output;
static int serial, return_label, break_label, continue_label, function_flags;
#define EMIT(...) printf_s(&output, __VA_ARGS__)
static void expr(Node *);
static void stmt(Node *);
static void movem(unsigned mask, int save)
{
    Str registers = {0};
    int i;
    if (!mask)
        return;
    if (mask == 0x3cfcu)
        puts_s(&registers, "d2-d7/a2-a5");
    else
        for (i = 1; i <= 16; i++)
            if (mask & (1u << (i - 1))) {
                if (registers.n)
                    putc_s(&registers, '/');
                puts_s(&registers, c_register_name(i));
            }
    if (save)
        EMIT("        movem.l %s,-(sp)\n", registers.s);
    else
        EMIT("        movem.l (sp)+,%s\n", registers.s);
}
static void shift_const(const char *op, int count)
{
    while (count > 0) {
        int part = count > 8 ? 8 : count;
        EMIT("        %s.l #%d,d0\n", op, part);
        count -= part;
    }
}
static void normalize(Type *t)
{
    t = decay(t);
    if (!integer(t))
        return;
    if (t->bit_width > 0) {
        if (t->uns)
            EMIT("        andi.l #%u,d0\n", (1u << t->bit_width) - 1);
        else {
            shift_const("lsl", 32 - t->bit_width);
            shift_const("asr", 32 - t->bit_width);
        }
        return;
    }
    if (t->size == 1) {
        if (t->uns)
            EMIT("        andi.l #255,d0\n");
        else
            EMIT("        ext.w d0\n        ext.l d0\n");
    } else if (t->size == 2) {
        if (t->uns)
            EMIT("        andi.l #65535,d0\n");
        else
            EMIT("        ext.l d0\n");
    }
}
static char size_suffix(Type *t)
{
    return t->size == 1 ? 'b' : t->size == 2 ? 'w' : 'l';
}
static void label(int n)
{
    EMIT("__stcc_gen_%d:\n", n);
}
static void jump(const char *op, int n)
{
    EMIT("        %s.w __stcc_gen_%d\n", op, n);
}
static void address(Node *n)
{
    switch (n->kind) {
    case ND_VAR:
        if (n->var->reg)
            at(n->tok, "register variable has no address");
        if (n->var->label)
            EMIT("        move.l #%s,d0\n", n->var->label);
        else
            EMIT("        lea %d(a6),a0\n        move.l a0,d0\n", n->var->offset);
        return;
    case ND_DEREF:
        expr(n->lhs);
        return;
    case ND_MEMBER:
        expr(n->lhs);
        if (n->offset)
            EMIT("        addi.l #%d,d0\n", n->offset);
        return;
    case ND_COMMA:
        expr(n->lhs);
        address(n->rhs);
        return;
    default:
        at(n->tok, "cannot form address of expression");
    }
}
static void load(Type *t, int bit_shift)
{
    if (t->kind == TY_ARRAY || t->kind == TY_FUNC || aggregate(t))
        return;
    EMIT("        move.l d0,a0\n        move.%c (a0),d0\n", size_suffix(t));
    if (t->bit_width > 0) {
        EMIT("        andi.l #65535,d0\n");
        shift_const("lsr", bit_shift);
    }
    normalize(t);
}
static void store(Type *t, int bit_shift)
{
    if (t->bit_width > 0) {
        unsigned mask = (1u << t->bit_width) - 1;
        /* Keep d0 as the assignment value and d1 as the postfix result. */
        EMIT("        move.l d1,-(sp)\n        move.l d0,-(sp)\n        andi.l #%u,d0\n", mask);
        shift_const("lsl", bit_shift);
        EMIT("        moveq #0,d1\n        move.w (a0),d1\n        andi.l #%u,d1\n"
             "        or.l d0,d1\n        move.w d1,(a0)\n"
             "        move.l (sp)+,d0\n        move.l (sp)+,d1\n",
             65535u ^ (mask << bit_shift));
        return;
    }
    if (aggregate(t)) {
        int loop = serial++;
        EMIT("        move.l d0,a1\n        move.l a0,d0\n        move.l #%d,d1\n", t->size);
        label(loop);
        EMIT("        move.b (a1)+,(a0)+\n        subq.l #1,d1\n");
        jump("bne", loop);
    } else
        EMIT("        move.%c d0,(a0)\n", size_suffix(t));
}
static void compare(const char *op, int uns)
{
    const char *cc = !strcmp(op, "==")   ? "eq"
                     : !strcmp(op, "!=") ? "ne"
                     : !strcmp(op, "<")  ? (uns ? "cs" : "lt")
                     : !strcmp(op, ">")  ? (uns ? "hi" : "gt")
                     : !strcmp(op, "<=") ? (uns ? "ls" : "le")
                                         : (uns ? "cc" : "ge");
    EMIT("        cmp.l d1,d0\n        s%s d0\n        andi.l #1,d0\n", cc);
}
static void binary_value(Node *n)
{
    const char *op = n->op;
    Type *l = decay(n->lhs->type), *r = decay(n->rhs->type);
    int ptrleft = l->kind == TY_PTR;
    if (ptrleft && integer(r) && (!strcmp(op, "+") || !strcmp(op, "-")) && l->base->size != 1) {
        EMIT("        move.l d0,-(sp)\n        move.l d1,d0\n        move.l #%d,d1\n        jsr "
             "__stcc_mul32\n        move.l d0,d1\n        move.l (sp)+,d0\n",
             l->base->size);
    }
    if (!strcmp(op, "+"))
        EMIT("        add.l d1,d0\n");
    else if (!strcmp(op, "-")) {
        EMIT("        sub.l d1,d0\n");
        if (l->kind == TY_PTR && r->kind == TY_PTR && l->base->size != 1)
            EMIT("        move.l #%d,d1\n        jsr __stcc_sdiv32\n", l->base->size);
    } else if (!strcmp(op, "*"))
        EMIT("        jsr __stcc_mul32\n");
    else if (!strcmp(op, "/") || !strcmp(op, "%")) {
        EMIT("        jsr __stcc_%sdiv32\n", l->uns ? "u" : "s");
        if (!strcmp(op, "%"))
            EMIT("        move.l d1,d0\n");
    } else if (!strcmp(op, "&"))
        EMIT("        and.l d1,d0\n");
    else if (!strcmp(op, "|"))
        EMIT("        or.l d1,d0\n");
    else if (!strcmp(op, "^"))
        EMIT("        eor.l d1,d0\n");
    else if (!strcmp(op, "<<"))
        EMIT("        lsl.l d1,d0\n");
    else if (!strcmp(op, ">>"))
        EMIT("        %s.l d1,d0\n", l->uns ? "lsr" : "asr");
    else
        compare(op, l->uns || ptrleft);
    normalize(n->type);
}
static void expr(Node *n)
{
    int a, b, i, bytes;
    Type *t;
    if (!n)
        return;
    switch (n->kind) {
    case ND_NUM:
        EMIT("        move.l #$%08x,d0\n", n->val);
        normalize(n->type);
        return;
    case ND_VAR:
        if (n->var->reg) {
            EMIT("        move.l %s,d0\n", c_register_name(n->var->reg));
            normalize(n->type);
            return;
        }
        address(n);
        load(n->type, n->bit_shift);
        return;
    case ND_ADDR:
        address(n->lhs);
        return;
    case ND_DEREF:
        expr(n->lhs);
        load(n->type, n->bit_shift);
        return;
    case ND_MEMBER:
        address(n);
        load(n->type, n->bit_shift);
        return;
    case ND_CAST:
        expr(n->lhs);
        normalize(n->type);
        return;
    case ND_COMMA:
        expr(n->lhs);
        expr(n->rhs);
        return;
    case ND_UNARY:
        expr(n->lhs);
        if (!strcmp(n->op, "-"))
            EMIT("        neg.l d0\n");
        else if (!strcmp(n->op, "~"))
            EMIT("        not.l d0\n");
        else if (!strcmp(n->op, "!"))
            EMIT("        tst.l d0\n        seq d0\n        andi.l #1,d0\n");
        normalize(n->type);
        return;
    case ND_BIN:
        if (!strcmp(n->op, "&&") || !strcmp(n->op, "||")) {
            int isand = !strcmp(n->op, "&&");
            a = serial++;
            b = serial++;
            expr(n->lhs);
            EMIT("        tst.l d0\n");
            jump(isand ? "beq" : "bne", a);
            expr(n->rhs);
            EMIT("        tst.l d0\n        sne d0\n        andi.l #1,d0\n");
            jump("bra", b);
            label(a);
            EMIT("        moveq #%d,d0\n", !isand);
            label(b);
            return;
        }
        expr(n->lhs);
        EMIT("        move.l d0,-(sp)\n");
        expr(n->rhs);
        EMIT("        move.l d0,d1\n        move.l (sp)+,d0\n");
        binary_value(n);
        return;
    case ND_COND:
        a = serial++;
        b = serial++;
        expr(n->cond);
        EMIT("        tst.l d0\n");
        jump("beq", a);
        expr(n->lhs);
        jump("bra", b);
        label(a);
        expr(n->rhs);
        label(b);
        return;
    case ND_ASSIGN:
        if (n->lhs->kind == ND_VAR && n->lhs->var->reg) {
            if (n->text) {
                expr(n->lhs);
                normalize(n->init->lhs->type);
                EMIT("        move.l d0,-(sp)\n");
                expr(n->init->rhs);
                EMIT("        move.l d0,d1\n        move.l (sp)+,d0\n");
                binary_value(n->init);
            } else
                expr(n->rhs);
            normalize(n->type);
            EMIT("        move.l d0,%s\n", c_register_name(n->lhs->var->reg));
            return;
        }
        address(n->lhs);
        EMIT("        move.l d0,-(sp)\n");
        if (n->text) {
            load(n->lhs->type, n->lhs->bit_shift);
            normalize(n->init->lhs->type);
            EMIT("        move.l d0,-(sp)\n");
            expr(n->init->rhs);
            EMIT("        move.l d0,d1\n        move.l (sp)+,d0\n");
            binary_value(n->init);
            normalize(n->type);
        } else
            expr(n->rhs);
        EMIT("        move.l (sp)+,a0\n");
        store(n->type, n->lhs->bit_shift);
        return;
    case ND_INC:
        t = decay(n->type);
        if (t->kind == TY_PTR && t->base->size <= 0)
            at(n->tok, "increment on incomplete/function pointer");
        if (n->lhs->kind == ND_VAR && n->lhs->var->reg) {
            expr(n->lhs);
            if (n->post)
                EMIT("        move.l d0,-(sp)\n");
            EMIT("        %si.l #%d,d0\n", !strcmp(n->op, "++") ? "add" : "sub",
                 t->kind == TY_PTR ? t->base->size : 1);
            normalize(n->type);
            EMIT("        move.l d0,%s\n", c_register_name(n->lhs->var->reg));
            if (n->post)
                EMIT("        move.l (sp)+,d0\n");
            return;
        }
        address(n->lhs);
        EMIT("        move.l d0,-(sp)\n");
        load(n->type, n->lhs->bit_shift);
        if (n->post)
            EMIT("        move.l d0,-(sp)\n");
        EMIT("        %si.l #%d,d0\n", !strcmp(n->op, "++") ? "add" : "sub",
             t->kind == TY_PTR ? t->base->size : 1);
        normalize(n->type);
        if (n->post)
            EMIT("        move.l (sp)+,d1\n");
        EMIT("        move.l (sp)+,a0\n");
        store(n->type, n->lhs->bit_shift);
        if (n->post)
            EMIT("        move.l d1,d0\n");
        return;
    case ND_CALL:
        bytes = 0;
        for (i = n->nargs - 1; i >= 0; i--) {
            expr(n->args[i]);
            t = decay(n->args[i]->type);
            a = t->size == 4 ? 4 : 2;
            EMIT("        move.%c d0,-(sp)\n", a == 4 ? 'l' : 'w');
            bytes += a;
        }
        if (n->lhs->kind == ND_VAR && n->lhs->type->kind == TY_FUNC)
            EMIT("        jsr %s\n", n->lhs->var->label);
        else {
            expr(n->lhs);
            EMIT("        move.l d0,a0\n        jsr (a0)\n");
        }
        if (bytes) {
            if (bytes > 32767)
                at(n->tok, "argument block exceeds 32767 bytes");
            EMIT("        lea %d(sp),sp\n", bytes);
        }
        normalize(n->type);
        return;
    default:
        at(n->tok, "internal error: expression node %d", n->kind);
    }
}
static void stmt(Node *n)
{
    Node *p;
    int a, b, c, oldbreak, oldcontinue;
    if (!n)
        return;
    switch (n->kind) {
    case ND_BLOCK:
        for (p = n->body; p; p = p->next)
            stmt(p);
        return;
    case ND_EXPR:
        expr(n->lhs);
        return;
    case ND_RETURN:
        expr(n->lhs);
        jump("bra", return_label);
        return;
    case ND_IF:
        a = serial++;
        b = serial++;
        expr(n->cond);
        EMIT("        tst.l d0\n");
        jump("beq", a);
        stmt(n->body);
        if (n->els) {
            jump("bra", b);
            label(a);
            stmt(n->els);
            label(b);
        } else
            label(a);
        return;
    case ND_FOR:
        oldbreak = break_label;
        oldcontinue = continue_label;
        a = serial++;
        b = serial++;
        c = serial++;
        break_label = c;
        continue_label = b;
        expr(n->init);
        label(a);
        if (n->cond) {
            expr(n->cond);
            EMIT("        tst.l d0\n");
            jump("beq", c);
        }
        stmt(n->body);
        label(b);
        expr(n->inc);
        jump("bra", a);
        label(c);
        break_label = oldbreak;
        continue_label = oldcontinue;
        return;
    case ND_DO:
        oldbreak = break_label;
        oldcontinue = continue_label;
        a = serial++;
        b = serial++;
        c = serial++;
        break_label = c;
        continue_label = b;
        label(a);
        stmt(n->body);
        label(b);
        expr(n->cond);
        EMIT("        tst.l d0\n");
        jump("bne", a);
        label(c);
        break_label = oldbreak;
        continue_label = oldcontinue;
        return;
    case ND_BREAK:
        jump("bra", break_label);
        return;
    case ND_CONTINUE:
        jump("bra", continue_label);
        return;
    case ND_SWITCH:
        oldbreak = break_label;
        break_label = serial++;
        expr(n->cond);
        a = -1;
        for (p = n->cases; p; p = p->casenext) {
            if (p->post) {
                a = p->id;
                continue;
            }
            EMIT("        cmpi.l #$%08x,d0\n        beq.w __stcc_case_%d\n", p->val, p->id);
        }
        if (a >= 0)
            EMIT("        bra.w __stcc_case_%d\n", a);
        else
            jump("bra", break_label);
        stmt(n->body);
        label(break_label);
        break_label = oldbreak;
        return;
    case ND_CASE:
        EMIT("__stcc_case_%d:\n", n->id);
        stmt(n->body);
        return;
    case ND_GOTO:
        EMIT("        bra.w %s\n", n->text);
        return;
    case ND_LABEL:
        EMIT("%s:\n", n->text);
        stmt(n->body);
        return;
    case ND_ASM:
        if (function_flags & FN_NAKED)
            EMIT("%s", n->text);
        else {
            movem(0x3cfcu & ~n->regmask, 1);
            EMIT("%s", n->text);
            movem(0x3cfcu & ~n->regmask, 0);
        }
        return;
    case ND_ZERO:
        a = serial++;
        EMIT("        lea %d(a6),a0\n        move.l #%d,d1\n        moveq #0,d0\n", n->var->offset,
             n->type->size);
        label(a);
        EMIT("        move.b d0,(a0)+\n        subq.l #1,d1\n");
        jump("bne", a);
        return;
    default:
        at(n->tok, "internal error: statement node %d", n->kind);
    }
}
char *generate(Program *program)
{
    Var *v;
    Function *f;
    memset(&output, 0, sizeof output);
    serial = 0;
    EMIT("; stcc " VERSION
         " / original Motorola 68000 / int16 long32 ptr32\n        section text\n");
    for (f = program->functions; f; f = f->next) {
        function_flags = f->var->type->func_flags;
        return_label = serial++;
        if (f->var->storage != SC_STATIC)
            EMIT("        xdef %s\n", f->var->label);
        EMIT("%s:\n", f->var->label);
        if (function_flags & FN_NAKED) {
            stmt(f->body);
            continue;
        }
        if (function_flags & FN_INTERRUPT)
            EMIT("        movem.l d0-d7/a0-a6,-(sp)\n");
        EMIT("        link a6,#-%d\n", f->frame);
        if (!(function_flags & FN_INTERRUPT))
            movem(f->regmask, 1);
        {
            int i;
            for (i = 0; i < f->nparams; i++) {
                Var *parameter = f->params[i];
                if (parameter->reg) {
                    EMIT("        move.%c %d(a6),d0\n", size_suffix(parameter->type),
                         parameter->offset);
                    normalize(parameter->type);
                    EMIT("        move.l d0,%s\n", c_register_name(parameter->reg));
                }
            }
        }
        stmt(f->body);
        EMIT("        moveq #0,d0\n");
        label(return_label);
        if (!(function_flags & FN_INTERRUPT))
            movem(f->regmask, 0);
        EMIT("        unlk a6\n");
        if (function_flags & FN_INTERRUPT)
            EMIT("        movem.l (sp)+,d0-d7/a0-a6\n        rte\n");
        else
            EMIT("        rts\n");
    }
    for (v = program->globals; v; v = v->allnext) {
        Node *i;
        int pos = 0;
        if (!v->defined || v->type->kind == TY_FUNC)
            continue;
        EMIT("        section %s\n        even\n", v->init ? "data" : "bss");
        if (v->storage != SC_STATIC)
            EMIT("        xdef %s\n", v->label);
        EMIT("%s:\n", v->label);
        if (!v->init) {
            EMIT("        ds.b %d\n", v->type->size);
            continue;
        }
        for (i = v->init; i; i = i->next) {
            if (i->offset < pos)
                at(i->tok, "overlapping initializer");
            if (i->offset > pos)
                EMIT("        ds.b %d\n", i->offset - pos);
            if (i->type->bit_width > 0) {
                uint32_t word = 0;
                int offset = i->offset;
                for (;;) {
                    uint32_t mask = (1u << i->type->bit_width) - 1;
                    word |= (i->rhs->val & mask) << i->bit_shift;
                    if (!i->next || i->next->offset != offset || i->next->type->bit_width < 1)
                        break;
                    i = i->next;
                }
                EMIT("        dc.w $%x\n", word);
            } else if (i->text)
                EMIT("        dc.l %s%+d\n", i->text, (int32_t)i->rhs->val);
            else {
                uint32_t val = i->rhs->val;
                if (i->type->size == 1)
                    val &= 255;
                if (i->type->size == 2)
                    val &= 65535;
                EMIT("        dc.%c $%x\n", size_suffix(i->type), val);
            }
            pos = i->offset + i->type->size;
        }
        if (v->type->size > pos)
            EMIT("        ds.b %d\n", v->type->size - pos);
    }
    return output.s ? output.s : xstrdup("");
}
