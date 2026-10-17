#ifndef STCC_C_H
#define STCC_C_H
#include "stcc.h"

enum { TK_EOF = 256, TK_IDENT, TK_NUMBER, TK_STRING, TK_ASM, TK_PUNCT };
enum {
    TY_VOID,
    TY_CHAR,
    TY_SHORT,
    TY_INT,
    TY_LONG,
    TY_PTR,
    TY_ARRAY,
    TY_FUNC,
    TY_STRUCT,
    TY_UNION,
    TY_ENUM
};
typedef struct Token Token;
typedef struct Type Type;
typedef struct Var Var;
typedef struct Node Node;
typedef struct Member Member;
struct Token {
    int kind, line, len;
    char *text, *file, *bytes;
    uint32_t val;
    int uns, longlit;
    Token *next;
};
struct Member {
    char *name;
    Type *type;
    int offset, bit_shift;
    Member *next;
};
struct Type {
    int kind, size, align, uns, qual, count, nparams, variadic, prototype;
    int bit_width, func_flags;
    Type *base, **params, *identity;
    char **pnames;
    Member *members;
};
struct Var {
    char *name, *label;
    Type *type;
    int offset, storage, defined, is_enum;
    uint32_t val;
    int depth;
    Var *next, *allnext;
    Node *init;
};
enum { SC_NONE, SC_EXTERN, SC_STATIC, SC_TYPEDEF, SC_AUTO, SC_REGISTER };
enum { FN_INTERRUPT = 1, FN_NAKED = 2 };
enum {
    ND_NUM,
    ND_VAR,
    ND_BIN,
    ND_UNARY,
    ND_ASSIGN,
    ND_ADDR,
    ND_DEREF,
    ND_CAST,
    ND_CALL,
    ND_COND,
    ND_COMMA,
    ND_MEMBER,
    ND_INC,
    ND_BLOCK,
    ND_EXPR,
    ND_IF,
    ND_FOR,
    ND_DO,
    ND_RETURN,
    ND_BREAK,
    ND_CONTINUE,
    ND_SWITCH,
    ND_CASE,
    ND_GOTO,
    ND_LABEL,
    ND_ASM,
    ND_ZERO
};
struct Node {
    int kind;
    Token *tok;
    Type *type;
    char *op, *text;
    uint32_t val;
    Var *var;
    int offset, id, post, bit_shift;
    Node *lhs, *rhs, *cond, *init, *inc, *body, *els, *next, *cases, *casenext;
    Node **args;
    int nargs;
};
typedef struct Function {
    Var *var;
    Var **params;
    int nparams, frame;
    Node *body;
    struct Function *next;
} Function;
typedef struct {
    Var *globals;
    Function *functions;
} Program;
Token *lex_c(const char *, const char *);
NORETURN void at(Token *, const char *, ...) PRINTF_FMT(2, 3);
Type *type_new(int, int);
Type *ptr(Type *);
Type *array(Type *, int);
Type *function_type(Type *);
Type *qualified(Type *, int);
Type *promote(Type *);
Type *usual(Type *, Type *);
int compatible(Type *, Type *);
int integer(Type *);
int scalar(Type *);
int aggregate(Type *);
Type *decay(Type *);
uint32_t convert_const(uint32_t, Type *);
char *generate(Program *);
#endif
