# Prompt for Cursor: Zeus — Unified GUI + Full-Stack Library

Yuga's UI library is **Zeus**. Every app, native desktop or WASM/canvas,
imports `std:zeus`. `Node` is the only builder/tree type: no `View` trait,
no `impl View` anywhere.

**Backend selection is a build flag, not an import choice.** The native
Cocoa backend and the Canvas2D/WASM backend are two compile-time
instantiations of the same generic core, picked by target:

```
yugac build --target=native   app.yuga   # Cocoa backend
yugac build --target=wasm32   app.yuga   # Canvas2D backend
```

App source is identical either way — the only thing that changes is the
build command. `http.yuga` is available under Zeus regardless of target;
native apps calling HTTP APIs is just as valid as a WASM app doing it, so
it isn't reserved for "the web one" anymore.

## Status

Yuga has **no traits**, so `Node(B: Backend)` / `impl(B: Backend)` from
§2 is the long-term sketch, not what we compile today. Backend is the
`--target` flag: same `std:zeus` + `std/zeuscore` Yuga, different C
`plat_*` (Cocoa vs Canvas2D). Do not add `View`, `impl View`, or a
second tree type.

| Item | State |
|---|---|
| `import "std:zeus"` only | done |
| `std/zeuscore/*.yuga` tree / layout / hit-test | done |
| `.child()` / `.run()` / `.mount()` chain | done |
| PascalCase composers (`Wrap`, `Note`, `Orb`, `Card`, …) | done |
| Buttons take `fn()`, not `Signal` | done |
| `yugac build --target=native` | done |
| `yugac build --target=wasm32` + Zeus Kit compile | done |
| PascalCase widgets + `.size()` / `.look()` | done |
| `each` / `when` combinators | done |
| `.styled()` / tracking scopes | done |
| Per-file `*Props` + interned `fn()` clicks | done |
| `Node(B: Backend)` generics | skipped (no traits) |

---

## 1. Confirmed target syntax

```yuga
import "std:zeus"
import "../../lib/theme.yuga"
import "../../lib/ui.yuga"

fn Wrap() -> Node {
    return zeus.row().gap(theme.space_sm()).align(zeus.flex_center()).justify(zeus.flex_start()).flex_wrap()
}

fn Note(s: string) -> Node {
    return zeus.label(s).fg(theme.muted()).font(11)
}

fn Orb(n: Node, s: string) -> Node {
    return zeus.col().gap(theme.space_sm()).align(zeus.flex_center()).shrink(0).child((
        n,
        Note(s),
    ))
}

fn main() {
    let mut ticks = 0
    let load = zeus.signal(42)
    // ... remaining signal declarations unchanged ...

    theme.Page("Zeus Kit", 960, 2400).child((
        ui.Navbar(NavbarProps { brand: "Zeus", a: "Foundations", b: "Components", c: "Patterns", appearance: "Dark" }),
        ui.Breadcrumbs(BreadcrumbProps { a: "Zeus", b: "Examples", c: "Kit" }),
        ui.Overline(TypeProps { text: "Library" }),
        ui.Display(TypeProps { text: "Build interfaces with Zeus" }),
        ui.Body(TypeProps { text: "Every widget ships medium..." }),
        Wrap().child((
            ui.Chip(ChipProps { label: "Stable", color: theme.success() }),
            ui.Chip(ChipProps { label: "Headless tests", color: theme.accent() }),
            ui.Badge(BadgeProps { label: "v1", color: theme.purple() }),
            ui.Kbd(KbdProps { key: "Esc" }),
        )),
        ui.Card().child((
            ui.CardHeader(CardHeaderProps { title: "Project card", subtitle: "..." }),
            ui.CardFooter().child((
                ui.Ghost(ButtonProps { label: "Cancel", on_press: || { ticks = ticks + 1 } }),
                ui.Prominent(ButtonProps { label: "Save", on_press: || { ticks = ticks + 1 } }),
                zeus.spacer(),
            )).child(zeus.label("0").bind_n(ticks).fg(theme.muted()).font(12)),
        )),
        // ... every remaining section as one nested chain ...
    )).run()
}
```

Rules this locks in:

1. **One import, always `std:zeus`.** Never `std:zeus` — that library name
   is retired. Same source compiles to native or wasm32 based on the
   build target flag alone.
2. **The whole tree is one expression, ending with `.run()`** as the final
   chained method on the built tree — never `run(tree)` as a wrapping
   function, never a root stored in a `let` and run as a separate
   statement.
3. **Every function that composes other `Node`s and gets called like a
   widget is PascalCase** — `Wrap`, `Note`, `Orb`, `CounterPanel`, `Card`,
   `Button`, all of it, no exceptions. Lowercase is reserved for the
   handful of `zeus` primitives (`row`, `col`, `text`, `signal`, `label`,
   `spacer`) and chain/modifier methods (`.gap()`, `.child()`, `.bound()`,
   `.align()`, `.size()`, `.look()`, `.each()`, `.styled()`, `.bind()`, `.run()`).
4. **`Node` is the only type.** No `View` trait, no `impl View`.
   Component functions are `fn Name(props...) -> Node`, body is a single
   chained expression — never built across multiple intermediate `let`
   bindings for the tree itself. (Signal `let` bindings above the chain
   are fine — only the *tree construction* must stay one chain.)
5. **Buttons take `fn()`, not `Signal`.** Captured `let mut int` is
   component state; passing that value into a child is an `int` prop.
   Write `ui.Prominent(ButtonProps { label: "Save", on_press: || { ticks = ticks + 1 } })`,
   never a signal as the click payload. Intern copies the closure env, so
   the handler still runs after the constructor returns.
6. **`.size()` / `.look()` before `.child()`.** Containers that take
   children (`Card`) must receive size/look first:
   `ui.Card().size(ui.sm()).look(ui.outline()).child(...)`.
7. **Constructors return a `Node`.** Size and look are tokens on that node
   (`with_size` / `with_look`). `.size()` / `.look()` update the tokens and
   re-run that node's `.styled()` closure. Never a parallel arg bag — no `keep`, `tag`,
   or dummy `""` / `0` slots.
8. **`.each` maps items, not indexes.** `parent.each(jobs, |j| { ui.ScrollRow(j.title, j.meta) })`.
   Integer sequences use `zeus.range(lo, hi)`: `row.each(zeus.range(1, last + 1), |i| { Page(i) })`.
9. **One `*Props` struct per widget file.** Constructors take that struct
   (`ChipProps`, `ButtonProps`). Size/look stay chained. Names are global
   (`find_struct` is name-only), so do not reuse `Props`. Apps write
   `ChipProps { … }` — `ui.ChipProps { }` does not parse.

## 2. How the single API stays static across two targets

`Node` must stay a concrete, non-dynamic type, so "one API, two render
targets" is done via **target selection**, never a trait object. Yuga
has no traits: do not introduce `Node(B: Backend)` until the language
can express it. Today `--target=native` links Cocoa and `--target=wasm32`
links Canvas2D; both fill the same `plat_*` declarations in
`std/zeuscore/platform.yuga`. The generic sketch below is the shape to
aim at later, not current source.

**`zeuscore` is real implementation, not a wrapper.** This is
important enough to state explicitly: `zeuscore` must contain the actual
tree-building, layout, diffing, and signal-subscription logic written in
Yuga itself — it is not a thin pass-through that just forwards calls into
a C library and reformats the result. C is reserved strictly for the
**runtime** (`zeus_plat` — the same role `yuga_rt` already plays for the
base language): the allocator, the Cocoa/Canvas2D FFI bindings, the OS
event loop, raw syscalls — the minimal, unavoidable layer Yuga code has
to cross to talk to the operating system or browser. Everything above
that line — what a `Node` is, how `.child()`/`.gap()`/`.bind()` behave,
how layout is computed, how a signal update finds the right dirty
`Node` — is Yuga code compiled by the Yuga compiler, same as any other
`.yuga` module. If a phase below ends up implementing tree/diff/layout
logic in C "for speed" and calling it from a thin `zeuscore` shim, that's
a violation of this rule, not an optimization — flag it and move the
logic back into Yuga.

```yuga
// zeuscore — internal, not imported by apps directly.
// Chain methods are UFCS (`n.pad(8)` is `zeus.pad(n, 8)`). No `impl` blocks,
// no `move` on closures, no `*` deref — Node and Signal are `{id: int}` handles.
struct Node {
    id: int,
}

fn child(parent: Node, node: Node) -> Node { ... }
fn bind(label: Node, sig: Signal) -> Node { ... }
fn styled(node: Node, handler: fn()) -> Node { ... }
fn run(node: Node) { ... }
```

`Backend` itself is the seam where C shows up, and only there: each
`Backend` implementation (`CocoaBackend`, `Canvas2DBackend`) is a thin
Yuga struct whose methods call into `zeus_plat.c` FFI bindings for the
handful of operations that genuinely require the OS/browser (create a
window, draw a rect via CoreGraphics, draw a rect via a Canvas2D import,
measure text via the platform's shaper). Everything upstream of that —
the tree, the diffing, the layout math, the event dispatch logic that
decides *which* `Node` a raw click hit — stays in Yuga.

- `std:zeus` resolves `B` to `CocoaBackend` when compiling with
  `--target=native`, and to `Canvas2DBackend` when compiling with
  `--target=wasm32` — a compile-time constant picked from the build flag,
  the same mechanism used for any target-specific codegen. There is no
  runtime branch on backend; the compiler emits one concrete
  instantiation per build.
- The `ui.yuga` widget kit (`Card`, `Button`, `Tabs`, etc.) is written
  generically over `B: Backend`, so the same source compiles against
  either target — still zero dynamic dispatch, just a different compiled
  artifact per build.
- App code never mentions `Backend`, `B`, or `zeuscore` — it only ever
  sees a concrete `Node` through `std:zeus`.

This is the reason merging the two libraries is safe rather than just
convenient: there was never a second implementation to keep in sync in
the first place, only a redundant second import name pointing at the same
generic core. Removing it removes a whole class of future drift, not just
a naming inconsistency.

## 3. Practical effect: no more "does this component work on canvas" question

A component built and tested under `--target=native` — anything in the
`ui.yuga` catalog — compiles for `--target=wasm32` unmodified, because
it's the same source recompiled against a different `B`, not a port.
**Rule:** every `ui.yuga` component must be written against the generic
`zeuscore` primitives, never hardcoding a backend-specific call — that's
what guarantees the retarget always works.

**Acceptance check:** build the `Zeus Kit` example with both
`--target=native` and `--target=wasm32` and confirm every catalog entry
renders correctly on both, with zero source changes between builds. Any
component that fails this is a bug — it means something backend-specific
leaked out of `zeuscore`.

## 4. Migration plan

**Phase 1 — Extract `zeuscore` — done.**
Tree, layout, scene, input, and geometry live in `std/zeuscore/*.yuga`.
C is only `plat_*` FFI (`runtime/zeus_plat.c`, `zeus/desktop/mac.m`, `zeus/web/wasm.c`).
No `Backend` trait: Yuga cannot express `Node(B: Backend)` yet.

**Phase 2 — Builder chain over `zeuscore` — done.**
`.child()`, `.gap()`, `.bind()` / `.bind_n()`, `.on()`, `.run()`,
`.mount()` are defined once on concrete `Node`. Apps end with `.run()`.

**Phase 3 — `--target=wasm32` + Zeus Kit — done.**
`--target=native` (default) and `--target=wasm32` (`wasm` still works).
`yugac build` is accepted. Same `kit.yuga` compiles to Cocoa or Canvas2D
`.wasm`; C symbols stay `yuga_zeus_*` so the existing plat files keep
linking. Browser pixel-check of the wasm kit is still a manual pass.

**Phase 4 — Widget kit `.size()` / `.look()` — done.**
Catalog entries are PascalCase constructors with no parent `Node` argument.
Each widget file owns a uniquely named `*Props` struct. Size and look are
chain modifiers on the returned `Node`:
`ui.Prominent(ButtonProps { label: "Save", on_press: || { ticks = ticks + 1 } }).size(ui.lg())`.
Named looks (`Ghost`, `Prominent`, …) stay medium + that look; chain
`.size()` if needed. No `B: Backend` — same concrete `Node` as the rest
of Zeus. Zeus Kit compiles native and wasm32 from this source.

**Phase 5 — `each` / `when` combinators — done.**
`parent.each(items, |item| { ... })` maps a list into children (keyed by
index; lists are built once, matching the retained-tree contract).
`zeus.range(lo, hi)` is the integer sequence for pagination and calendars.
`zeus.when(sig, || { ... })` is the show combinator: it builds the child
once and uses `n.show(sig)` to hide it from layout while the signal is 0.
(`show` on `Node` stays the modifier — Yuga has no overloading.)
Scroll / table / list in the kit, pagination buttons, and the
date-picker grid use `each`; accordion bodies use `when`.

**Phase 6 — `http.yuga` prop flow (both targets) — done.**
```yuga
import "std:zeus"
import "std:http"
import "api.yuga"

fn main() {
    let body = http.client_get(api.hello_path())
    let initial = http.json_get_int(body, api.hello_count_key())
    CounterPanel(initial).mount("Zeus", 480, 320).run()
}
```
`.mount(title, w, h)` attaches window/canvas target info to the built
tree (a no-op or native-window-sizing call under `--target=native`, a
canvas-sizing call under `--target=wasm32` — same call, backend-specific
effect), `.run()` stays the terminal chain method on every target.

The counter example serves its wasm page with Vite in
`zeus/examples/counter` (`npm run dev` on `:5173`).

**Phase 7 — Delete deprecated parent-mutation API** once every call site
is converted, and delete any leftover `std:zeus` references in the
codebase entirely.

**Phase 8 — Fine-grained `.styled()` — done.**
Tracking scopes in `std/zeuscore/track.yuga`: `get(sig)` inside `.styled`
records a dependency; `set(sig)` re-runs only those closures. Catalog
widgets build children once. Theme and `.size()` / `.look()` update paint
in place. `on_restyle` / public `drop_children` are gone.

**Phase 9 — Interned click / styled `fn()` — done.**
`plat_intern_fn` copies the closure env (`yuga_fn.env_size`). `.on()` and
`.styled()` keep working after a `*Props` struct (or other `fn` field)
is dropped. Clicks write signals; the next paint reads `.bind` / `.bound`.

## 5. Reactivity contract

Component functions run once, at construction. `.bind()` / `.styled()` /
`.on()` wire the specific `Node` they are called on — they do not re-run
the enclosing component. `each` / `show` only rebuild inserted, removed,
or toggled children. A `.styled` closure must not call `.child()`.

| Hook | When it runs | What updates |
|---|---|---|
| `.bind(sig)` / `.bound(sig, …)` | Every layout and paint | Label / metric reads `sig` now |
| `.styled` | Construction, then each `set` of a `get`'d signal | Paint tokens on existing nodes |
| `.on(click, handler)` | Pointer hit after intern | `set` / `inc` (or captured `let mut`) |

`.run()` keeps the tree (kit, counter). `zeus.view` / `zeus.app` rebuild
every layout and reset interned handlers, then intern again from the new
tree. Default is retained: intern once, paint reads live `Signal`s.

A `fn()` in `ButtonProps.on_press` is interned by value. Intern copies
the env so the click still sees captured `Signal` ids after `paint(p)`
returns and drops `p`. Without that copy, `+1` would write a freed env
and the Count label would stay put.

## 6. Acceptance criteria

- Only `std:zeus` exists as an import; `std:zeus` is gone from the
  codebase entirely.
- `main()` (or any root) ends with `.run()` as a chained terminal method
  on the built tree.
- Every composing function is PascalCase; every primitive/modifier call
  is lowercase.
- No `View` trait, no `impl View`, anywhere — `Node` is the only
  tree/builder type, parameterized by `Backend` only inside
  `zeuscore.yuga`.
- The `Zeus Kit` example compiles and renders identically under
  `--target=native` and `--target=wasm32` from the same source.
- Signal-bound widgets update without re-invoking their enclosing
  component function. Interned `.on` / `.styled` handlers outlive the
  `fn` value that was interned.
- `zeuscore`'s tree/layout/diff/signal logic is Yuga code; C exists only
  inside `zeus_plat.c` (and each `Backend`'s thin FFI calls into it) — no
  tree/diff/layout logic implemented in C and merely exposed through
  `zeuscore`.

## 7. Non-goals

- No change to the native rasterizer, layout algorithm, or hit-testing
  logic beyond what's needed to sit behind `Backend`.
- No change to `theme.yuga` token values.