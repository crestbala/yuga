# The C / Yuga boundary

There is exactly one C runtime in the tree: **`yuga_rt`** (`packages/compiler/runtime/yuga_rt.h`,
included into generated C). No library gets a `*_rt.c` of its own as a place
to hide protocol or widget logic. Zeus layout/paint/hit-test and http
encode/dispatch live in Yuga. Hosts (Cocoa / UIKit / Canvas / JNI) own the
event loop and call back into `engine_*` trampolines.

## Convention

A **boundary module** (today: `packages/compiler/std/zeuscore/platform.yuga`, empty `fn`s in
`packages/compiler/std/fmt.yuga` / `packages/compiler/std/net.yuga` / `packages/compiler/std/sys.yuga`) declares plain, **bodyless** functions:

```yuga
fn plat_set_window(title: string, width: int, height: int) {}
```

The compiler assigns `yuga_<module>_<name>` (e.g. `yuga_platform_plat_set_window`)
and emits a declaration only (`is_intrinsic`). The matching C symbol is linked
from `yuga_rt` or from a **host** that drives the event loop — not from a
library-specific runtime file that contains retries, parsers, or widget code.

No `extern` keyword. No `impl`. UFCS means a plain `fn w(node: Node, px: int)` is
callable as `node.w(32)` — which is why the zeus libraries ship plain
functions instead of a chain API. `Node` is `{ id: int }`, not
`Node<Backend>`. Generics are allowed in `zeus.yuga`
where they help (`each<T>`); they are not a substitute for a
backend type parameter.

The other direction: C calls compiled Yuga as `yuga_zeus_engine_paint`,
`yuga_http_dispatch`, and so on.

## What belongs where

| Layer | C | Yuga |
|---|---|---|
| Language (`yuga_rt`) | Allocator, panic/trap, overflow, wrapping bit ops, `string_from_bytes`, `yuga_fn` `{fn, env, env_size}`, `env_set` / `exit` | — |
| net (`packages/compiler/std/net`) | TCP connect/read/write/close, listen/accept/peek, wasm `fetch_rpc` | — |
| Zeus | Host event loop + replay of the draw list; `plat_intern_fn` copies a `yuga_fn` env (and drops the moved-in value) so click / text / style thunks outlive the interned value | Tree, layout, signals, kit, `platform.yuga` signatures |
| Maya | Host event loop + present (Cocoa blit / 2D discs) | Scene, orbits, camera, CPU tracer, 2D map layout, `sin`/`cos` |
| http | — | unary gRPC: `#[proto]`, dispatch, path/frame, HTTP/1.1 parse, h2c, `listen`, client `rt_call` / `h2_call`, loopback `h1_read_unary` / `h2_read_unary`, HEADLESS selftest |

A new `*_rt.c` outside `yuga_rt`, or a `yuga_rt` stub that knows Zeus widgets
or protobuf field numbers, is the old per-library runtime sneaking back.

Bodyless `fn`s must stay bodyless: no branches, retries, or transforms in the
boundary declaration. That logic goes in the ordinary Yuga function that calls
them.

## Review

- New library-specific C that is not a host event loop or a generic ABI trampoline — reject.
- Boundary `fn` with a real body — reject (unless you are *moving* logic *out* of C into Yuga, which is the goal).
- “This library needs its own runtime” — it needs bodyless signatures; extend `yuga_rt` only for a reusable ABI.
