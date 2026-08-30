/**
 * parser.c — recursive-descent parser (one token lookahead).
 *
 * Pratt parser for binary ops. Struct literals are disabled in `if`/`for`
 * conditions so `{` starts a block. Closures are `|x| { ... }` / `|| { ... }`.
 * Semicolons are optional. `///` attaches to the next item; `//!` to the module.
 */
#include "parser.h"
#include "diagnostics.h"
#include <stdlib.h>
#include <string.h>

static void advance(Parser *p) {
    p->previous = p->current;
    p->current = p->peek;
    p->peek = lexer_next(p->lex);
}

static int check(Parser *p, TokenKind k) { return p->current.kind == k; }

/** Consume current if it matches `k`. */
static int match(Parser *p, TokenKind k) {
    if (check(p, k)) {
        advance(p);
        return 1;
    }
    return 0;
}

static void error(Parser *p, const char *m) {
    if (p->had_error) return;
    yuga_error(p->current.loc, "%s (got %s)", m, token_kind_name(p->current.kind));
    p->had_error = 1;
}

static void consume(Parser *p, TokenKind k, const char *m) {
    if (match(p, k)) return;
    error(p, m);
}

static void optional_semi(Parser *p) { match(p, TOK_SEMICOLON); }

/** Text after `///` / `//!`, dropping one leading space. */
static char *doc_line_text(Token t) {
    int skip = 3;
    if (t.len < skip) return yuga_dup("");
    const char *s = t.start + skip;
    int n = t.len - skip;
    if (n > 0 && s[0] == ' ') {
        s++;
        n--;
    }
    return yuga_dupn(s, n > 0 ? (size_t)n : 0);
}

static char *doc_append(char *acc, const char *line) {
    if (!line) line = "";
    if (!acc) return yuga_dup(line);
    size_t a = strlen(acc), b = strlen(line);
    char *n = (char *)malloc(a + b + 2);
    if (!n) yuga_fatal("out of memory");
    memcpy(n, acc, a);
    n[a] = '\n';
    memcpy(n + a + 1, line, b + 1);
    free(acc);
    return n;
}

/** Consecutive `///` lines. */
static char *take_docs(Parser *p) {
    char *acc = NULL;
    while (check(p, TOK_DOC)) {
        char *line = doc_line_text(p->current);
        acc = doc_append(acc, line);
        free(line);
        advance(p);
    }
    return acc;
}

/** Consecutive `//!` lines. */
static char *take_mod_docs(Parser *p) {
    char *acc = NULL;
    while (check(p, TOK_MOD_DOC)) {
        char *line = doc_line_text(p->current);
        acc = doc_append(acc, line);
        free(line);
        advance(p);
    }
    return acc;
}

static char *doc_join(char *a, char *b) {
    if (!a) return b;
    if (!b) return a;
    char *n = doc_append(a, b);
    free(b);
    return n;
}

static char *tok_text(Token t) { return yuga_dupn(t.start, (size_t)t.len); }

/** Decode a quoted token into a heap string (`\n` `\t` `\r` `\"` `\\`). */
static char *unescape_string(Token t) {
    /* t includes quotes */
    int len = t.len;
    const char *s = t.start;
    char *out = (char *)malloc((size_t)len);
    if (!out) yuga_fatal("out of memory");
    int j = 0;
    for (int i = 1; i < len - 1; i++) {
        if (s[i] == '\\' && i + 1 < len - 1) {
            char n = s[++i];
            if (n == 'n') out[j++] = '\n';
            else if (n == 't') out[j++] = '\t';
            else if (n == 'r') out[j++] = '\r';
            else if (n == '"') out[j++] = '"';
            else if (n == '\\') out[j++] = '\\';
            else out[j++] = n;
        } else {
            out[j++] = s[i];
        }
    }
    out[j] = '\0';
    return out;
}

static char *file_stem(const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    size_t n = strlen(base);
    if (n > 5 && strcmp(base + n - 5, ".yuga") == 0) n -= 5;
    return yuga_dupn(base, n);
}

/** Prime current and peek from the lexer. */
void parser_init(Parser *p, Lexer *l) {
    p->lex = l;
    p->had_error = 0;
    p->allow_struct_lit = 1;
    p->current = lexer_next(l);
    p->peek = lexer_next(l);
}

static AstNode *parse_type(Parser *p);
static AstNode *parse_expr(Parser *p);
static AstNode *parse_block(Parser *p);
static AstNode *parse_stmt(Parser *p);
static Param *parse_params(Parser *p, size_t *out);

/** Parse a type: `&`/`&mut`, `Box<T>`, `[N]T`, `[]T`, `fn(...)`, or a name. */
static AstNode *parse_type(Parser *p) {
    SourceLoc loc = p->current.loc;
    if (match(p, TOK_AMP)) {
        int mut = match(p, TOK_MUT);
        AstNode *e = parse_type(p);
        return ast_type(NULL, mut ? 1 : 0, e, 0, loc);
    }
    if (match(p, TOK_LBRACKET)) {
        if (match(p, TOK_RBRACKET)) {
            AstNode *e = parse_type(p);
            return ast_type(NULL, 3, e, -1, loc);
        }
        if (!check(p, TOK_NUMBER)) {
            error(p, "expected array length or []T");
            return NULL;
        }
        int64_t n = strtoll(p->current.start, NULL, 10);
        advance(p);
        consume(p, TOK_RBRACKET, "expected ]");
        AstNode *e = parse_type(p);
        return ast_type(NULL, 3, e, n, loc);
    }
    if (match(p, TOK_FN)) {
        consume(p, TOK_LPAREN, "expected ( after fn");
        AstNode **ps = NULL;
        size_t n = 0;
        if (!check(p, TOK_RPAREN)) {
            do {
                AstNode *t = parse_type(p);
                ps = (AstNode **)realloc(ps, (n + 1) * sizeof(AstNode *));
                ps[n++] = t;
            } while (match(p, TOK_COMMA));
        }
        consume(p, TOK_RPAREN, "expected )");
        AstNode *ret = NULL;
        if (match(p, TOK_ARROW)) ret = parse_type(p);
        AstNode *t = ast_type(NULL, 5, ret, 0, loc);
        t->as.type.fn_params = ps;
        t->as.type.fn_param_count = n;
        return t;
    }
    if (!match(p, TOK_IDENT)) {
        error(p, "expected type");
        return NULL;
    }
    char *nm = tok_text(p->previous);
    if (strcmp(nm, "Box") == 0 && match(p, TOK_LT)) {
        AstNode *e = parse_type(p);
        consume(p, TOK_GT, "expected >");
        free(nm);
        return ast_type(NULL, 4, e, 0, loc);
    }
    AstNode *t = ast_type(nm, 2, NULL, 0, loc);
    if (match(p, TOK_LT)) {
        AstNode **targs = NULL;
        size_t nt = 0;
        if (!check(p, TOK_GT)) {
            do {
                AstNode *a = parse_type(p);
                targs = (AstNode **)realloc(targs, (nt + 1) * sizeof(AstNode *));
                targs[nt++] = a;
            } while (match(p, TOK_COMMA));
        }
        consume(p, TOK_GT, "expected >");
        t->as.type.targs = targs;
        t->as.type.targ_count = nt;
    }
    return t;
}

/** `|x, y|` parameter list. Types are optional (`|x: int|`). */
static Param *parse_closure_params(Parser *p, size_t *out) {
    Param *params = NULL;
    size_t count = 0;
    if (check(p, TOK_PIPE)) {
        *out = 0;
        return NULL;
    }
    do {
        if (!match(p, TOK_IDENT)) {
            error(p, "expected closure parameter name");
            break;
        }
        char *name = tok_text(p->previous);
        SourceLoc nloc = p->previous.loc;
        AstNode *type = NULL;
        if (match(p, TOK_COLON)) type = parse_type(p);
        params = (Param *)realloc(params, (count + 1) * sizeof(Param));
        params[count].name = name;
        params[count].type = type;
        params[count].loc = nloc;
        count++;
    } while (match(p, TOK_COMMA));
    *out = count;
    return params;
}

/** `|x| { ... }` or `|| { ... }`. Opening `|` / `||` already consumed. */
static AstNode *parse_closure(Parser *p, SourceLoc loc, int zero_arg) {
    Param *ps = NULL;
    size_t pc = 0;
    if (!zero_arg) {
        ps = parse_closure_params(p, &pc);
        consume(p, TOK_PIPE, "expected | after closure parameters");
    }
    AstNode *ret = NULL;
    if (match(p, TOK_ARROW)) ret = parse_type(p);
    if (!check(p, TOK_LBRACE)) {
        error(p, "expected { after closure — bodies use braces, e.g. |x| { x + 1 }");
        return NULL;
    }
    AstNode *body = parse_block(p);
    AstNode *n = ast_fn(NULL, ps, pc, ret, body, loc);
    n->kind = AST_CLOSURE;
    return n;
}

/** Literals, ident, parenthesized expr, array lit, or `|x| { }` closure. */
static AstNode *parse_primary(Parser *p) {
    SourceLoc loc = p->current.loc;
    if (match(p, TOK_IDENT)) {
        char *nm = tok_text(p->previous);
        if (p->allow_struct_lit && match(p, TOK_LBRACE)) {
            FieldInit *fi = NULL;
            size_t fc = 0;
            while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
                if (!match(p, TOK_IDENT)) {
                    error(p, "expected field name");
                    break;
                }
                char *fn = tok_text(p->previous);
                consume(p, TOK_COLON, "expected : in struct literal");
                AstNode *v = parse_expr(p);
                fi = (FieldInit *)realloc(fi, (fc + 1) * sizeof(FieldInit));
                fi[fc].name = fn;
                fi[fc].init = v;
                fc++;
                if (!match(p, TOK_COMMA)) break;
            }
            consume(p, TOK_RBRACE, "expected }");
            return ast_struct_lit(nm, fi, fc, loc);
        }
        return ast_ident(nm, loc);
    }
    if (match(p, TOK_FLOAT)) {
        double v = strtod(p->previous.start, NULL);
        return ast_float(v, loc);
    }
    if (match(p, TOK_NUMBER)) {
        int64_t v = strtoll(p->previous.start, NULL, 10);
        return ast_number(v, loc);
    }
    if (match(p, TOK_STRING)) return ast_string(unescape_string(p->previous), loc);
    if (match(p, TOK_TRUE)) return ast_bool(1, loc);
    if (match(p, TOK_FALSE)) return ast_bool(0, loc);
    /* `|x| { ... }` / `|| { ... }`. `a || b` stays boolean or (infix). */
    if (match(p, TOK_PIPE_PIPE)) return parse_closure(p, loc, 1);
    if (match(p, TOK_PIPE)) return parse_closure(p, loc, 0);
    if (match(p, TOK_LPAREN)) {
        if (match(p, TOK_RPAREN)) {
            error(p, "empty tuple");
            return NULL;
        }
        AstNode *e = parse_expr(p);
        if (!match(p, TOK_COMMA)) {
            consume(p, TOK_RPAREN, "expected )");
            return e;
        }
        AstNode **elems = (AstNode **)malloc(sizeof(AstNode *));
        elems[0] = e;
        size_t n = 1;
        if (!check(p, TOK_RPAREN)) {
            do {
                AstNode *x = parse_expr(p);
                elems = (AstNode **)realloc(elems, (n + 1) * sizeof(AstNode *));
                elems[n++] = x;
            } while (match(p, TOK_COMMA) && !check(p, TOK_RPAREN));
        }
        consume(p, TOK_RPAREN, "expected )");
        return ast_tuple(elems, n, loc);
    }
    if (match(p, TOK_LBRACKET)) {
        int64_t n = -1;
        if (match(p, TOK_RBRACKET)) {
            n = -1;
        } else {
            if (!check(p, TOK_NUMBER)) {
                error(p, "expected array literal [N]T{...} or []T{...}");
                return NULL;
            }
            n = strtoll(p->current.start, NULL, 10);
            advance(p);
            consume(p, TOK_RBRACKET, "expected ]");
        }
        AstNode *et = parse_type(p);
        consume(p, TOK_LBRACE, "expected {");
        AstNode **elems = NULL;
        size_t ec = 0;
        if (!check(p, TOK_RBRACE)) {
            do {
                AstNode *e = parse_expr(p);
                elems = (AstNode **)realloc(elems, (ec + 1) * sizeof(AstNode *));
                elems[ec++] = e;
            } while (match(p, TOK_COMMA) && !check(p, TOK_RBRACE));
        }
        consume(p, TOK_RBRACE, "expected }");
        return ast_array_lit(et, n, elems, ec, loc);
    }
    error(p, "expected expression");
    return NULL;
}

/** Calls, `.field`, `::path`, and `[index]` after a primary. */
static AstNode *parse_postfix(Parser *p, AstNode *left) {
    for (;;) {
        SourceLoc loc = p->current.loc;
        if (match(p, TOK_LPAREN)) {
            AstNode **args = NULL;
            size_t ac = 0;
            if (!check(p, TOK_RPAREN)) {
                do {
                    AstNode *a = parse_expr(p);
                    args = (AstNode **)realloc(args, (ac + 1) * sizeof(AstNode *));
                    args[ac++] = a;
                } while (match(p, TOK_COMMA) && !check(p, TOK_RPAREN));
            }
            consume(p, TOK_RPAREN, "expected )");
            loc.end_line = p->previous.loc.end_line;
            loc.end_col = p->previous.loc.end_col;
            if (left && left->loc.line > 0) {
                loc.line = left->loc.line;
                loc.col = left->loc.col;
                loc.file = left->loc.file;
            }
            left = ast_call(left, args, ac, loc);
            continue;
        }
        if (match(p, TOK_DOT)) {
            if (!match(p, TOK_IDENT)) {
                /* While typing `recv.` the next token is often `}` / EOF. */
                if (yuga_diag_capturing() &&
                    (check(p, TOK_EOF) || check(p, TOK_RBRACE) || check(p, TOK_RPAREN) ||
                     check(p, TOK_COMMA) || check(p, TOK_RBRACKET) || check(p, TOK_SEMICOLON)))
                    break;
                error(p, "expected field name");
                break;
            }
            left = ast_field(left, tok_text(p->previous), 0, p->previous.loc);
            continue;
        }
        if (match(p, TOK_COLON_COLON)) {
            if (!match(p, TOK_IDENT)) {
                if (yuga_diag_capturing() &&
                    (check(p, TOK_EOF) || check(p, TOK_RBRACE) || check(p, TOK_RPAREN) ||
                     check(p, TOK_COMMA) || check(p, TOK_RBRACKET) || check(p, TOK_SEMICOLON)))
                    break;
                error(p, "expected identifier after ::");
                break;
            }
            left = ast_field(left, tok_text(p->previous), 1, p->previous.loc);
            continue;
        }
        if (match(p, TOK_LBRACKET)) {
            AstNode *idx = parse_expr(p);
            consume(p, TOK_RBRACKET, "expected ]");
            left = ast_index(left, idx, loc);
            continue;
        }
        if (match(p, TOK_AS)) {
            AstNode *ty = parse_type(p);
            left = ast_cast(left, ty, loc);
            continue;
        }
        break;
    }
    return left;
}

/** Unary `&`, `&mut`, `*`, `!`, `-`. */
static AstNode *parse_unary(Parser *p) {
    SourceLoc loc = p->current.loc;
    if (match(p, TOK_AMP)) {
        int mut = match(p, TOK_MUT);
        return ast_addr(parse_unary(p), mut, loc);
    }
    if (match(p, TOK_STAR)) return ast_deref(parse_unary(p), loc);
    if (match(p, TOK_BANG) || match(p, TOK_MINUS)) {
        TokenKind op = p->previous.kind;
        return ast_unary(op, parse_unary(p), loc);
    }
    return parse_postfix(p, parse_primary(p));
}

/** Binding power for Pratt parsing of binary operators. */
static int precedence(TokenKind k) {
    if (k == TOK_DOT_DOT) return 1;
    if (k == TOK_PIPE_PIPE) return 2;
    if (k == TOK_AMP_AMP) return 3;
    if (k == TOK_EQ_EQ || k == TOK_BANG_EQ || k == TOK_LT || k == TOK_GT ||
        k == TOK_LT_EQ || k == TOK_GT_EQ)
        return 4;
    if (k == TOK_PLUS || k == TOK_MINUS) return 5;
    if (k == TOK_STAR || k == TOK_SLASH || k == TOK_PERCENT) return 6;
    return 0;
}

/** Binary expression with precedence >= min_prec. */
static AstNode *parse_binary(Parser *p, int min_prec) {
    AstNode *left = parse_unary(p);
    for (;;) {
        TokenKind op = p->current.kind;
        int pr = precedence(op);
        if (pr < min_prec || pr == 0) break;
        /* do not treat a line-leading operator as continuing the previous expr
           (`let x = Box::new(1)` then `*x += 1` is two statements) */
        if (p->current.loc.line > p->previous.loc.line) break;
        advance(p);
        AstNode *right = parse_binary(p, pr + 1);
        left = ast_binary(op, left, right, left ? left->loc : p->previous.loc);
    }
    return left;
}

static AstNode *parse_expr(Parser *p) { return parse_binary(p, 1); }

/** Expression used as `if`/`for` condition: no struct-lit at this `{`. */
static AstNode *parse_cond_expr(Parser *p) {
    int saved = p->allow_struct_lit;
    p->allow_struct_lit = 0;
    AstNode *e = parse_expr(p);
    p->allow_struct_lit = saved;
    return e;
}

/** `let` / `let mut` binding. */
static AstNode *parse_let(Parser *p) {
    SourceLoc loc = p->previous.loc;
    int is_mut = match(p, TOK_MUT);
    if (!match(p, TOK_IDENT)) {
        error(p, "expected identifier after let");
        return NULL;
    }
    char *name = tok_text(p->previous);
    loc.end_line = p->previous.loc.end_line;
    loc.end_col = p->previous.loc.end_col;
    AstNode *type = NULL;
    if (match(p, TOK_COLON)) type = parse_type(p);
    consume(p, TOK_EQ, "expected =");
    AstNode *init = parse_expr(p);
    optional_semi(p);
    return ast_var(name, type, init, is_mut, loc);
}

static int is_assign_op(TokenKind k) {
    return k == TOK_EQ || k == TOK_PLUS_EQ || k == TOK_MINUS_EQ ||
           k == TOK_STAR_EQ || k == TOK_SLASH_EQ;
}

/** One match pattern: literal or `_`. */
static AstNode *parse_match_pat(Parser *p, int *wild) {
    SourceLoc loc = p->current.loc;
    *wild = 0;
    if (match(p, TOK_IDENT)) {
        char *nm = tok_text(p->previous);
        if (strcmp(nm, "_") == 0) {
            free(nm);
            *wild = 1;
            return NULL;
        }
        free(nm);
        error(p, "match patterns are literals or `_`");
        return NULL;
    }
    if (match(p, TOK_NUMBER)) {
        int64_t v = strtoll(p->previous.start, NULL, 10);
        return ast_number(v, loc);
    }
    if (match(p, TOK_FLOAT)) {
        double v = strtod(p->previous.start, NULL);
        return ast_float(v, loc);
    }
    if (match(p, TOK_TRUE)) return ast_bool(1, loc);
    if (match(p, TOK_FALSE)) return ast_bool(0, loc);
    if (match(p, TOK_STRING)) return ast_string(unescape_string(p->previous), loc);
    error(p, "expected match pattern");
    return NULL;
}

static AstNode *parse_match_arm(Parser *p) {
    SourceLoc loc = p->current.loc;
    int wild = 0;
    AstNode **pats = NULL;
    size_t np = 0;
    int w0 = 0;
    AstNode *first = parse_match_pat(p, &w0);
    if (w0) {
        wild = 1;
        if (check(p, TOK_PIPE)) {
            error(p, "wildcard `_` cannot be in an or-pattern");
            return NULL;
        }
    } else if (first) {
        pats = (AstNode **)malloc(sizeof(AstNode *));
        pats[0] = first;
        np = 1;
        while (match(p, TOK_PIPE)) {
            int w = 0;
            AstNode *q = parse_match_pat(p, &w);
            if (w) {
                error(p, "wildcard `_` cannot be in an or-pattern");
                break;
            }
            if (q) {
                pats = (AstNode **)realloc(pats, (np + 1) * sizeof(AstNode *));
                pats[np++] = q;
            }
        }
    }
    consume(p, TOK_FAT_ARROW, "expected => after match pattern");
    AstNode *body = parse_block(p);
    return ast_match_arm(pats, np, wild, body, loc);
}

/** One statement: if, for, while, match, return, break, continue, let, assign, or expression. */
static AstNode *parse_stmt(Parser *p) {
    char *doc = take_docs(p);
    SourceLoc loc = p->current.loc;
    if (match(p, TOK_IF)) {
        free(doc);
        AstNode *cond = parse_cond_expr(p);
        AstNode *thenb = parse_block(p);
        AstNode *elseb = NULL;
        if (match(p, TOK_ELSE)) {
            if (check(p, TOK_IF)) elseb = parse_stmt(p);
            else elseb = parse_block(p);
        }
        return ast_if(cond, thenb, elseb, loc);
    }
    if (match(p, TOK_FOR)) {
        free(doc);
        if (!match(p, TOK_IDENT)) {
            error(p, "expected loop variable");
            return NULL;
        }
        char *var = tok_text(p->previous);
        consume(p, TOK_IN, "expected in");
        AstNode *iter = parse_cond_expr(p);
        AstNode *body = parse_block(p);
        return ast_for(var, iter, body, loc);
    }
    if (match(p, TOK_WHILE)) {
        free(doc);
        AstNode *cond = parse_cond_expr(p);
        AstNode *body = parse_block(p);
        return ast_while(cond, body, loc);
    }
    if (match(p, TOK_MATCH)) {
        free(doc);
        AstNode *scrut = parse_cond_expr(p);
        consume(p, TOK_LBRACE, "expected { after match");
        AstNode **arms = NULL;
        size_t na = 0;
        while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
            AstNode *arm = parse_match_arm(p);
            if (arm) {
                arms = (AstNode **)realloc(arms, (na + 1) * sizeof(AstNode *));
                arms[na++] = arm;
            }
            if (p->had_error) break;
        }
        consume(p, TOK_RBRACE, "expected }");
        return ast_match(scrut, arms, na, loc);
    }
    if (match(p, TOK_BREAK)) {
        free(doc);
        optional_semi(p);
        return ast_break(loc);
    }
    if (match(p, TOK_CONTINUE)) {
        free(doc);
        optional_semi(p);
        return ast_continue(loc);
    }
    if (match(p, TOK_RETURN)) {
        free(doc);
        AstNode *expr = NULL;
        if (!check(p, TOK_SEMICOLON) && !check(p, TOK_RBRACE)) expr = parse_expr(p);
        optional_semi(p);
        return ast_return(expr, loc);
    }
    if (match(p, TOK_LET)) {
        AstNode *v = parse_let(p);
        if (v) v->doc = doc;
        else free(doc);
        return v;
    }
    free(doc);

    AstNode *e = parse_expr(p);
    if (is_assign_op(p->current.kind)) {
        TokenKind op = p->current.kind;
        advance(p);
        AstNode *r = parse_expr(p);
        optional_semi(p);
        return ast_assign(op, e, r, loc);
    }
    optional_semi(p);
    return ast_expr_stmt(e, loc);
}

/** `{` statements `}`. */
static AstNode *parse_block(Parser *p) {
    SourceLoc loc = p->current.loc;
    consume(p, TOK_LBRACE, "expected {");
    AstNode **stmts = NULL;
    size_t count = 0;
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        AstNode *s = parse_stmt(p);
        if (s) {
            stmts = (AstNode **)realloc(stmts, (count + 1) * sizeof(AstNode *));
            stmts[count++] = s;
        }
        if (p->had_error) break;
    }
    consume(p, TOK_RBRACE, "expected }");
    return ast_block(stmts, count, loc);
}

static Param *parse_params(Parser *p, size_t *out) {
    Param *params = NULL;
    size_t count = 0;
    if (!check(p, TOK_RPAREN)) {
        do {
            if (!match(p, TOK_IDENT)) {
                error(p, "expected parameter name");
                break;
            }
            char *name = tok_text(p->previous);
            SourceLoc nloc = p->previous.loc;
            consume(p, TOK_COLON, "expected :");
            AstNode *type = parse_type(p);
            params = (Param *)realloc(params, (count + 1) * sizeof(Param));
            params[count].name = name;
            params[count].type = type;
            params[count].loc = nloc;
            count++;
        } while (match(p, TOK_COMMA));
    }
    *out = count;
    return params;
}

/** `fn` item: optional type params, params, return type, body. */
static AstNode *parse_fn(Parser *p) {
    SourceLoc loc = p->previous.loc;
    if (!match(p, TOK_IDENT)) {
        error(p, "expected function name");
        return NULL;
    }
    char *name = tok_text(p->previous);
    loc.end_line = p->previous.loc.end_line;
    loc.end_col = p->previous.loc.end_col;
    const char **tparams = NULL;
    size_t nt = 0;
    if (match(p, TOK_LT)) {
        do {
            if (!match(p, TOK_IDENT)) {
                error(p, "expected type parameter name");
                break;
            }
            tparams = (const char **)realloc(tparams, (nt + 1) * sizeof(char *));
            tparams[nt++] = tok_text(p->previous);
        } while (match(p, TOK_COMMA));
        consume(p, TOK_GT, "expected >");
    }
    consume(p, TOK_LPAREN, "expected (");
    size_t pc = 0;
    Param *ps = parse_params(p, &pc);
    consume(p, TOK_RPAREN, "expected )");
    AstNode *ret = NULL;
    if (match(p, TOK_ARROW)) ret = parse_type(p);
    AstNode *body = parse_block(p);
    AstNode *n = ast_fn(name, ps, pc, ret, body, loc);
    n->as.fn.tparams = tparams;
    n->as.fn.tparam_count = nt;
    return n;
}

/** `struct Name { field: Type, ... }` or `struct Name<T> { ... }`. */
static AstNode *parse_struct(Parser *p) {
    SourceLoc loc = p->previous.loc;
    if (!match(p, TOK_IDENT)) {
        error(p, "expected struct name");
        return NULL;
    }
    char *name = tok_text(p->previous);
    loc.end_line = p->previous.loc.end_line;
    loc.end_col = p->previous.loc.end_col;
    const char **tparams = NULL;
    size_t nt = 0;
    if (match(p, TOK_LT)) {
        do {
            if (!match(p, TOK_IDENT)) {
                error(p, "expected type parameter name");
                break;
            }
            tparams = (const char **)realloc(tparams, (nt + 1) * sizeof(char *));
            tparams[nt++] = tok_text(p->previous);
        } while (match(p, TOK_COMMA));
        consume(p, TOK_GT, "expected >");
    }
    consume(p, TOK_LBRACE, "expected {");
    Field *fields = NULL;
    size_t fc = 0;
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        char *fdoc = take_docs(p);
        if (!match(p, TOK_IDENT)) {
            free(fdoc);
            error(p, "expected field name");
            break;
        }
        char *fn = tok_text(p->previous);
        consume(p, TOK_COLON, "expected :");
        AstNode *ty = parse_type(p);
        fields = (Field *)realloc(fields, (fc + 1) * sizeof(Field));
        fields[fc].name = fn;
        fields[fc].type = ty;
        fields[fc].doc = fdoc;
        fc++;
        match(p, TOK_COMMA);
        match(p, TOK_SEMICOLON);
    }
    consume(p, TOK_RBRACE, "expected }");
    AstNode *st = ast_struct(name, fields, fc, loc);
    st->as.strct.tparams = tparams;
    st->as.strct.tparam_count = nt;
    return st;
}

/** `import "std:foo"` or `import "rel/path.yuga"` only (quoted). */
static AstNode *parse_import(Parser *p) {
    SourceLoc loc = p->previous.loc;
    if (match(p, TOK_IDENT)) {
        error(p, "import requires a quoted path, e.g. import \"std:zeus\" or import \"foo.yuga\"");
        return NULL;
    }
    if (!match(p, TOK_STRING)) {
        error(p, "expected import path string after import");
        return NULL;
    }
    loc.end_line = p->previous.loc.end_line;
    loc.end_col = p->previous.loc.end_col;
    char *path = unescape_string(p->previous);
    char *alias;
    if (strncmp(path, "std:", 4) == 0) {
        const char *name = path + 4;
        if (!name[0] || strchr(name, '/') || strchr(name, '\\') || strchr(name, ':')) {
            error(p, "std import must be import \"std:name\"");
            free(path);
            return NULL;
        }
        alias = yuga_dup(name);
    } else {
        alias = file_stem(path);
        if (!alias || !alias[0]) {
            error(p, "import path has no module name");
            free(path);
            free(alias);
            return NULL;
        }
    }
    return ast_import(alias, path, loc);
}

/** Module: zero or more imports, then fn/struct/let items, until EOF. */
AstNode *parser_parse(Parser *p) {
    SourceLoc loc = p->current.loc;
    AstNode **imps = NULL;
    size_t ni = 0;
    AstNode **decls = NULL;
    size_t nd = 0;
    char *mod_doc = take_mod_docs(p);

    for (;;) {
        char *more = take_mod_docs(p);
        if (more) mod_doc = doc_join(mod_doc, more);
        if (check(p, TOK_EOF) || p->had_error) break;
        char *doc = take_docs(p);
        if (match(p, TOK_IMPORT)) {
            if (nd) {
                error(p, "import must appear at the top of the file");
                free(doc);
                break;
            }
            AstNode *im = parse_import(p);
            optional_semi(p);
            if (im) {
                im->doc = doc;
                imps = (AstNode **)realloc(imps, (ni + 1) * sizeof(AstNode *));
                imps[ni++] = im;
            } else {
                free(doc);
            }
            continue;
        }
        if (match(p, TOK_FN)) {
            AstNode *fn = parse_fn(p);
            if (fn) {
                fn->doc = doc;
                decls = (AstNode **)realloc(decls, (nd + 1) * sizeof(AstNode *));
                decls[nd++] = fn;
            } else {
                free(doc);
            }
            continue;
        }
        int is_proto = 0;
        if (match(p, TOK_HASH)) {
            consume(p, TOK_LBRACKET, "expected [ after #");
            if (!match(p, TOK_IDENT)) {
                error(p, "expected attribute name");
                free(doc);
                break;
            }
            char *attr = tok_text(p->previous);
            if (strcmp(attr, "proto") == 0) {
                is_proto = 1;
            } else {
                error(p, "unknown attribute");
                free(attr);
                free(doc);
                break;
            }
            free(attr);
            consume(p, TOK_RBRACKET, "expected ] after attribute");
            if (!check(p, TOK_STRUCT)) {
                error(p, "#[proto] can only be applied to a struct");
                free(doc);
                break;
            }
        }
        if (match(p, TOK_STRUCT)) {
            AstNode *st = parse_struct(p);
            if (st) {
                st->doc = doc;
                st->as.strct.is_proto = is_proto;
                decls = (AstNode **)realloc(decls, (nd + 1) * sizeof(AstNode *));
                decls[nd++] = st;
            } else {
                free(doc);
            }
            continue;
        }
        if (is_proto) {
            error(p, "#[proto] can only be applied to a struct");
            free(doc);
            break;
        }
        if (match(p, TOK_LET)) {
            AstNode *v = parse_let(p);
            if (v) {
                v->doc = doc;
                decls = (AstNode **)realloc(decls, (nd + 1) * sizeof(AstNode *));
                decls[nd++] = v;
            } else {
                free(doc);
            }
            continue;
        }
        free(doc);
        error(p, "expected fn, struct, or let");
        break;
    }

    AstNode *prog = ast_program(imps, ni, decls, nd, loc);
    prog->as.program.mod_doc = mod_doc;
    return prog;
}
