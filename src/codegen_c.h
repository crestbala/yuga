/**
 * codegen_c.h — emit gnu99 C for a typechecked Yuga program.
 *
 * Inlines `rt_path` (yuga_rt.h). Empty std fns are not emitted (intrinsics
 * in the runtime). Capturing closures become stack envs; Box uses malloc.
 */
#ifndef YUGA_CODEGEN_C_H
#define YUGA_CODEGEN_C_H

#include <stdio.h>
#include "module.h"

/**
 * Write a complete C translation unit to `out`.
 * `mods[0]` is the main module. `rt_path` is copied into the file.
 */
void codegen_emit_c(FILE *out, YugaModule *mods, int nmods, const char *rt_path);

#endif
