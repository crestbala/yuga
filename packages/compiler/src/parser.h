/**
 * parser.h — recursive-descent parse of a Yuga module into an AST.
 *
 * One token of lookahead. `allow_struct_lit` is cleared in condition
 * position so `if x {` is not parsed as a struct literal. `///` / `//!`
 * tokens are attached to items / the module.
 */
#ifndef YUGA_PARSER_H
#define YUGA_PARSER_H

#include "lexer.h"
#include "ast.h"

typedef struct {
    Lexer *lex;
    Token current;
    Token peek;
    Token previous;
    int had_error;
    int allow_struct_lit;
    /* Alias the file imported `std:zeus` under, or NULL. A trailing UI block
       (`Column(...) { ... }`) desugars to `<alias>.__ui_scope(...)`. */
    const char *zeus_alias;
    /* 1 while parsing a trailing UI block's body: a comma after a widget
       call is a list separator (`Column { Text("a"), Text("b") }`), the
       same list syntax the chain API's `.children(a, b)` used. */
    int in_ui_block;
} Parser;

/** Bind `p` to lexer `l` and prime current/peek. */
void parser_init(Parser *p, Lexer *l);

/**
 * Parse a full module: imports, then fn/struct items.
 * Returns a program node even on error; check `p->had_error`.
 */
AstNode *parser_parse(Parser *p);

#endif
