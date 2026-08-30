/**
 * compile.h — load a program and its imports, then run all frontend passes.
 *
 * Pipeline: parse each module → typecheck → borrowck → boundscheck.
 * Does not invoke codegen or `cc`; that is driver.c / yuga-lsp.
 */
#ifndef YUGA_COMPILE_H
#define YUGA_COMPILE_H

#include "module.h"
#include "diagnostics.h"

/** All modules of one compile, plus diagnostics from the last check. */
typedef struct {
    YugaModule mods[YUGA_MAX_MODULES];
    int nmods;
    YugaDiag *diags;
    int ndiag;
} YugaSession;

void yuga_session_init(YugaSession *s);
void yuga_session_free(YugaSession *s);

/**
 * Analyze `path` as the main module. If `src` is non-NULL, use it as that
 * file's contents (imports still load from disk). Returns 0 if no errors.
 * Fills s->diags (also printed unless capture is on).
 */
int yuga_session_check(YugaSession *s, const char *path, const char *src);

#endif
