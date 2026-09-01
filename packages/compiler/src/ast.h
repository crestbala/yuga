/**
 * ast.h — Yuga abstract syntax tree.
 *
 * Every node has kind, source location, and after typecheck a Type* and
 * flags. The `as` union is selected by kind. Strings and names stored on
 * nodes are heap-owned unless noted; ast_free recursively releases them.
 *
 * Flags (bitmask on AstNode.flags):
 *   ASTF_INDEX_SAFE — boundscheck proved this index in range (no yuga_idx)
 *   ASTF_NEEDS_DROP — Box binding; codegen frees at scope exit
 *   ASTF_MOVED      — borrowck: this use moved a non-Copy value
 *   ASTF_FN_VAL     — identifier names a function used as a value
 *   ASTF_STATE      — let mut int captured by a closure; lowered to a Signal
 */
#ifndef YUGA_AST_H
#define YUGA_AST_H

#include "diagnostics.h"
#include "lexer.h"
#include "sema/type.h"
#include <stddef.h>
#include <stdint.h>

#define ASTF_INDEX_SAFE 1
#define ASTF_NEEDS_DROP 2
#define ASTF_MOVED      4
#define ASTF_FN_VAL     8
#define ASTF_STATE      16

typedef enum {
    AST_PROGRAM,
    AST_IMPORT,
    AST_FN_DECL,
    AST_STRUCT_DECL,
    AST_ENUM_DECL,
    AST_VAR_DECL,
    AST_BLOCK,
    AST_IF,
    AST_FOR,
    AST_WHILE,
    AST_MATCH,
    AST_MATCH_ARM,
    AST_RETURN,
    AST_BREAK,
    AST_CONTINUE,
    AST_EXPR_STMT,
    AST_ASSIGN,
    AST_BINARY,
    AST_UNARY,
    AST_CAST,
    AST_CALL,
    AST_INDEX,
    AST_FIELD,
    AST_IDENT,
    AST_NUMBER,
    AST_FLOAT,
    AST_STRING,
    AST_BOOL,
    AST_STRUCT_LIT,
    AST_ARRAY_LIT,
    AST_TUPLE,
    AST_DEREF,
    AST_ADDR,
    AST_TYPE,
    AST_CLOSURE,
    AST_INTERP_STRING, /* "a {{x}} b" — alternating literal / expression parts */
} AstKind;

typedef struct AstNode AstNode;

typedef struct {
    const char *name;
    AstNode *type;
    SourceLoc loc;
    AstNode *default_val; /* `x: int = 3`, or NULL */
} Param;

typedef struct {
    const char *name;
    AstNode *type;
    const char *doc; /* `///` above this field, or NULL */
    AstNode *default_val; /* `x: int = 3`, or NULL */
} Field;

typedef struct {
    const char *name;
    AstNode *init;
    int checked; /* 1: a default the typechecker inserted; skip on re-checks */
} FieldInit;

struct AstNode {
    AstKind kind;
    SourceLoc loc;
    Type *ty;       /* filled by typecheck */
    int flags;
    int place_mut;  /* 1 if this place can be assigned */
    const char *doc; /* `///` item docs; heap-owned, or NULL */
    union {
        struct {
            AstNode **imports;
            size_t import_count;
            AstNode **decls;
            size_t decl_count;
            const char *mod_name;
            const char *mod_doc; /* `//!` lines at module scope */
        } program;
        struct {
            const char *alias;
            const char *path;
        } import;
        struct {
            const char *name;
            const char *cname; /* C symbol; set by typecheck */
            Param *params;
            size_t param_count;
            AstNode *ret_type;
            AstNode *body;
            int is_intrinsic; /* empty std hook: no C body emitted */
            int is_proto_codec; /* unused; #[proto] encode/decode are real Yuga now */
            int proto_is_encode;
            AstNode *proto_struct; /* not owned; the #[proto] struct */
            const char **tparams;
            size_t tparam_count;
            const char **caps;     /* captured local names (closures) */
            Type **cap_types;
            size_t cap_count;
            int clos_id;
            int used_as_value;
        } fn;
        struct {
            const char *name;
            Field *fields;
            size_t field_count;
            const char **tparams;
            size_t tparam_count;
            int is_proto; /* inject encode_/decode_ as Yuga calling std:http */
        } strct;
        struct {
            const char *name;
            const char **variants; /* variant names, owned */
            int64_t *values;       /* parallel: int constant per variant */
            size_t variant_count;
        } enum_decl;
        struct {
            const char *name;
            AstNode *type;
            AstNode *init;
            int is_mut;
        } var;
        struct {
            AstNode **stmts;
            size_t stmt_count;
        } block;
        struct {
            AstNode *cond;
            AstNode *then_block;
            AstNode *else_block;
        } if_stmt;
        struct {
            const char *var;
            AstNode *iter;
            AstNode *body;
        } for_stmt;
        struct {
            AstNode *scrut;
            AstNode **arms;
            size_t arm_count;
        } match_stmt;
        struct {
            AstNode **pats;
            size_t pat_count;
            int is_wild;
            AstNode *body;
        } match_arm;
        struct {
            AstNode *expr;
        } ret;
        struct {
            AstNode *expr;
        } expr_stmt;
        struct {
            TokenKind op;
            AstNode *left;
            AstNode *right;
        } assign;
        struct {
            TokenKind op;
            AstNode *left;
            AstNode *right;
        } binary;
        struct {
            TokenKind op;
            AstNode *operand;
        } unary;
        struct {
            AstNode *expr;
            AstNode *type;
        } cast;
        struct {
            AstNode *callee;
            AstNode **args;
            size_t arg_count;
            /* Parallel to `args`: the `name` in `f(name = v)`, else NULL.
               Typecheck reorders args into parameter order and clears this. */
            const char **arg_names;
            int is_println;
            int is_print; /* like is_println but no trailing newline (fmt.print) */
            int is_box_new;
            int is_wrapping_add;
            int is_saturating_add;
            int is_vec_push;
            int is_vec_pop;
            const char *c_builtin; /* emit this C symbol (wrapping_shr, string_from_bytes, …) */
            const char *resolved_cname;
            int is_fn_val;
            int sig_cell; /* 1 = __sig_push, 2 = __sig_load, 3 = __sig_store */
        } call;
        struct {
            AstNode *target;
            AstNode *index;
            const char *field;
            int is_mut;
            int via_colon; /* 1 = `Box::new` path, not field access */
            AstNode *resolved; /* fn this method/module.fn refers to */
        } access;
        struct {
            const char *name;
            AstNode *resolved; /* var/fn decl, if any */
            SourceLoc def_loc;
        } ident;
        struct {
            int64_t value;
            const char *str;
            int b;
            double f;
        } lit;
        struct {
            const char *type_name;
            FieldInit *fields;
            size_t field_count;
        } struct_lit;
        struct {
            AstNode *elem_type;
            int64_t len;
            AstNode **elems;
            size_t count;
        } array_lit;
        struct {
            const char *name;
            int tag; /* 0=&T 1=&mut T 2=named 3=array/vec 4=Box<T> 5=fn(...) */
            AstNode *elem;
            int64_t array_len; /* tag 3: N for [N]T, -1 for []T */
            AstNode **fn_params;
            size_t fn_param_count;
            AstNode **targs; /* named type arguments: Pair<int> */
            size_t targ_count;
        } type;
        struct {
            AstNode **parts; /* AST_STRING literals interleaved with exprs */
            size_t count;
        } interp;
    } as;
};

AstNode *ast_new(AstKind kind, SourceLoc loc);
void ast_free(AstNode *node);

/** Constructors. Names and nested nodes are taken as owned (except loc). */
AstNode *ast_program(AstNode **imps, size_t ni, AstNode **decls, size_t nd, SourceLoc loc);
AstNode *ast_import(const char *alias, const char *path, SourceLoc loc);
AstNode *ast_fn(const char *name, Param *params, size_t pc, AstNode *ret, AstNode *body, SourceLoc loc);
AstNode *ast_struct(const char *name, Field *fields, size_t fc, SourceLoc loc);
AstNode *ast_enum(const char *name, const char **variants, int64_t *values, size_t vc, SourceLoc loc);
AstNode *ast_var(const char *name, AstNode *type, AstNode *init, int is_mut, SourceLoc loc);
AstNode *ast_block(AstNode **stmts, size_t n, SourceLoc loc);
AstNode *ast_if(AstNode *cond, AstNode *thenb, AstNode *elseb, SourceLoc loc);
/** The value a block yields as an if-expression branch: the last statement's
 * expression when it is an expression statement, else NULL. */
AstNode *ast_block_tail(AstNode *block);
AstNode *ast_for(const char *var, AstNode *iter, AstNode *body, SourceLoc loc);
AstNode *ast_while(AstNode *cond, AstNode *body, SourceLoc loc);
AstNode *ast_match(AstNode *scrut, AstNode **arms, size_t n, SourceLoc loc);
AstNode *ast_match_arm(AstNode **pats, size_t np, int wild, AstNode *body, SourceLoc loc);
AstNode *ast_return(AstNode *expr, SourceLoc loc);
AstNode *ast_break(SourceLoc loc);
AstNode *ast_continue(SourceLoc loc);
AstNode *ast_expr_stmt(AstNode *expr, SourceLoc loc);
AstNode *ast_assign(TokenKind op, AstNode *l, AstNode *r, SourceLoc loc);
AstNode *ast_binary(TokenKind op, AstNode *l, AstNode *r, SourceLoc loc);
AstNode *ast_unary(TokenKind op, AstNode *opnd, SourceLoc loc);
AstNode *ast_cast(AstNode *expr, AstNode *type, SourceLoc loc);
AstNode *ast_call(AstNode *callee, AstNode **args, size_t n, SourceLoc loc);
/** As ast_call, but `names[i]` (owned, may be NULL) labels `args[i]`. */
AstNode *ast_call_named(AstNode *callee, AstNode **args, const char **names, size_t n, SourceLoc loc);
/** `"a {{x}} b"` — `parts` alternates string literals and expressions. */
AstNode *ast_interp_string(AstNode **parts, size_t n, SourceLoc loc);

/** Deep-copy a constant expression (number, float, string, bool, or a negated
 * number). Returns NULL for anything else. Used to give each site that omits a
 * defaulted field or parameter its own copy of the default. */
AstNode *ast_clone_const(const AstNode *n);
AstNode *ast_index(AstNode *target, AstNode *idx, SourceLoc loc);
AstNode *ast_field(AstNode *target, const char *field, int via_colon, SourceLoc loc);
AstNode *ast_ident(const char *name, SourceLoc loc);
AstNode *ast_number(int64_t v, SourceLoc loc);
AstNode *ast_float(double v, SourceLoc loc);
AstNode *ast_string(const char *s, SourceLoc loc);
AstNode *ast_bool(int b, SourceLoc loc);
AstNode *ast_struct_lit(const char *tn, FieldInit *fi, size_t n, SourceLoc loc);
AstNode *ast_array_lit(AstNode *et, int64_t len, AstNode **elems, size_t n, SourceLoc loc);
AstNode *ast_tuple(AstNode **elems, size_t n, SourceLoc loc);
AstNode *ast_deref(AstNode *target, SourceLoc loc);
AstNode *ast_addr(AstNode *target, int is_mut, SourceLoc loc);
AstNode *ast_type(const char *name, int tag, AstNode *elem, int64_t alen, SourceLoc loc);

#endif
