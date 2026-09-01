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
`--target` flag: same `std:zeus` + `packages/compiler/std/zeuscore` Yuga, different C
`plat_*` (Cocoa vs Canvas2D). Do not add `View`, `impl View`, or a
second tree type.

| Item | State |
|---|---|
| `import "std:zeus"` only | done |
| `packages/compiler/std/zeuscore/*.yuga` tree / layout / hit-test | done |
| Declarative widgets (`Text`, `Button`, `Box`, `Input`, …) + trailing blocks | done |
| PascalCase composers (`Wrap`, `Note`, `Orb`, `Card`, …) | done |
| Buttons take `fn()`, not `Signal` | done |
| `yugac build --target=native` | done |
| `yugac build --target=wasm32` + Zeus Kit compile | done |
| Unqualified names (`Button`, `signal`, `println` — no `zeus.`/`fmt.` prefix) | done |
| `Box(align_direction = DIRECTION.Row | "column")` (no `Row`/`Column`) | done |
| No chaining — style is props, wiring is plain calls | done |
| `enum Key { … }` (`e.key == Key.K`) | done |
| Duplicate-definition errors | done |
| `each` / `when` combinators (plain fns) | done |
| Reactive style props (`color = if sel { a } else { b }`) — no `styled` closures, no paint-modifier fns | done |
| If-expressions (`if c { a } else { b }` as a value) | done |
| Per-file `*Props` + interned `fn()` clicks | done |
| `Node(B: Backend)` generics | skipped (no traits) |

---

## 1. Confirmed target syntax

```yuga
import "std:zeus"
import "../packages/zeus-components/ui.yuga"

fn Wrap() -> Node {
    return Box(align_direction = DIRECTION.Row, gap = SPACE.Sm, align_items = ALIGN.Center, justify_content = ALIGN.Start, overflow = overflow_auto())
}

fn Note(s: string) -> Node {
    return Text(s, color = muted(), font = 11)
}

fn Orb(n: Node, s: string) -> Node {
    return Box(align_direction = DIRECTION.Column, gap = SPACE.Sm, align_items = ALIGN.Center, shrink = 0) {
        n,
        Note(s)
    }
}

fn main() {
    let ticks = signal(0)
    let load = signal(42)

    App("Zeus Kit", 960, 2400, fn() {
        Scroll(background = canvas(), padding = SPACE.Page, spacing = SPACE.Section) {
            Navbar(brand = "Zeus", a = "Foundations", b = "Components", c = "Patterns"),
            Breadcrumbs(a = "Zeus", b = "Examples", c = "Kit"),
            Overline(text = "Library"),
            Display(text = "Build interfaces with Zeus"),
            Body(text = "Every widget ships medium..."),
            Wrap() {
                Chip(label = "Stable", color = success()),
                Chip(label = "Headless tests", color = accent()),
                Badge(label = "v1", color = purple()),
                Kbd(key = "Esc")
            },
            Card() {
                CardHeader(title = "Project card", subtitle = "..."),
                CardFooter() {
                    Ghost(label = "Cancel", on_press = fn() => ticks.set(ticks.get() + 1)),
                    Prominent(label = "Save", on_press = fn() => ticks.set(ticks.get() + 1)),
                    Spacer(),
                    Text("{{ticks.get()}}", color = muted(), font = 12)
                }
            }
            // ... every remaining section as nested trailing blocks ...
        }
    })
}
```

Rules this locks in:

1. **One import, always `std:zeus`.** Same source compiles to native or
   wasm32 based on the build target flag alone.
2. **The tree comes from trailing blocks** — `Box(...) { ... }` attaches
   every widget evaluated inside to that container. `App(title, w, h, fn) {
   ... }` opens the window, builds, and runs.
3. **Every function that composes other `Node`s and gets called like a
   widget is PascalCase** — `Wrap`, `Note`, `Orb`, `Card`, `Button`, all of
   it. Lowercase is reserved for `signal`, `hex`, the colour tokens
   (`muted()`, `accent()`), and the plain wiring functions (`on_click`,
   `show`, `key`, `each`, …). Layout constants are enum values:
   `DIRECTION.Row`, `ALIGN.Center`, `SPACE.Sm`.
4. **`Node` is the only type.** No `View` trait, no `impl View`.
   Component functions are `fn Name(props...) -> Node`.
5. **Buttons take `fn()`, not `Signal`.** Signals are Copy handles:
   `on_click = fn() => count.set(count.get() + 1)`. Intern copies the
   closure env, so the handler still runs after the constructor returns.
6. **Style is props, never chains.** `Box(align_direction = DIRECTION.Row, gap =
   8, shrink = 0)`. There is no `.pad(8).gap(4)`; `Row`/`Column` do not
   exist — one container, `Box`, with `align_direction`. Advanced wiring
   (`key`, `show_eq`, `pulse`, …) is plain functions taking the node
   first. Conditional style is an if-expression prop: `Box(border = if
   sel { 1 } else { 0 })` re-applies just that prop when `sel` changes.
7. **Constructors return a `Node`.** Size and look are props (`size =
   sm()`, `look = solid()`).
8. **`each` maps `(index, item)`** — `each(parent, jobs, |i, j| {
   ScrollRow(title = j.title, meta = j.meta) })`. An integer sequence
   isn't an item list — loop it directly.
9. **One `*Props` struct per widget file.** Constructors take that struct;
   named arguments build it. Names are global (`find_struct` is
   name-only), so do not reuse `Props`. Apps write `ChipProps { … }` —
   `ui.ChipProps { }` does not parse.

## 2. How the single API stays static across two targets

`Node` must stay a concrete, non-dynamic type, so "one API, two render
targets" is done via **target selection**, never a trait object. Yuga
has no traits: do not introduce `Node(B: Backend)` until the language
can express it. Today `--target=native` links Cocoa and `--target=wasm32`
links Canvas2D; both fill the same `plat_*` declarations in
`packages/compiler/std/zeuscore/platform.yuga`. The generic sketch below is the shape to
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

The phases below record how the current API was reached. Phase 2–8 names
are historical: the builder chain, `.size()`/`.look()` modifiers, and
`.mount()`/`.run()` terminals were all replaced by the declarative API in
section 1 (props, trailing blocks, plain wiring calls, `App`).

**Phase 1 — Extract `zeuscore` — done.**
Tree, layout, scene, input, and geometry live in `packages/compiler/std/zeuscore/*.yuga`.
C is only `plat_*` FFI (`packages/compiler/runtime/zeus_plat.c`, `packages/zeus/desktop/mac.m`, `packages/zeus/web/wasm.c`).
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
`parent.each(items, |i, item| { ... })` maps a list into children (keyed by
index; lists are built once, matching the retained-tree contract). An
integer sequence (pagination, calendar cells) is a native `for i in lo..hi`
loop calling `.child(...)` directly — not a list, so not `.each`.
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
    App("Zeus", 480, 320, fn() {
        CounterPanel(initial)
    })
}
```
`.mount(title, w, h)` attaches window/canvas target info to the built
tree (a no-op or native-window-sizing call under `--target=native`, a
canvas-sizing call under `--target=wasm32` — same call, backend-specific
effect), `.run()` stays the terminal chain method on every target.

The counter example serves its wasm page with Vite in
`examples/zeus/counter` (`npm run dev` on `:5173`).

**Phase 7 — Delete deprecated parent-mutation API** once every call site
is converted, and delete any leftover `std:zeus` references in the
codebase entirely.

**Phase 8 — Fine-grained reactivity — done.**
Tracking scopes in `packages/compiler/std/zeuscore/track.yuga`: a `get(sig)`
inside a text thunk or a style-prop thunk records a dependency; `set(sig)`
re-runs only those effects. Catalog widgets build children once. Theme and
`.size()` / `.look()` update paint in place. `on_restyle` / public
`drop_children` are gone.

**Phase 9 — Interned click / text / style `fn()` — done.**
`plat_intern_fn` copies the closure env (`yuga_fn.env_size`). `.on()` / text
thunks / style-prop thunks keep working after a `*Props` struct (or other
`fn` field) is dropped. Clicks write signals; the next paint reads
`.bind` / `.bound`.

**Phase 10 — Reactive style props — done.**
Style props are thunks; `bind_style_*` registers each passed prop as a
per-node effect whose dispatcher re-applies the setter when a signal the
thunk read changes. `styled` closures and the paint-modifier functions are
gone — style changes ride in props, `Box(border = if sel { 1 } else { 0 })`.

## 5. Reactivity contract

Component functions run once, at construction. `bind` / style props /
`on_click` wire the specific `Node` they are called on — they do not
re-run the enclosing component. `each` / `show` only rebuild inserted,
removed, or toggled children. A style-prop thunk must not call `child`.

| Hook | When it runs | What updates |
|---|---|---|
| `bind(n, sig)` / `bind_n(n, v)` | Every layout and paint | Label / metric reads `sig` now |
| Style prop (`Box(color = …)`) | Construction, then each `set` of a `get`'d signal | Re-applies that prop's setter (`set_fg`, `set_border`, …) on its node |
| `bind_text(n, body)` | Construction, then each `set` of a `get`'d signal | Repaints that one label |
| `on_click(n, handler)` | Pointer hit after intern | `set` / `inc` (or captured `let mut`) |

`App` keeps the tree (kit, counter). `view` rebuilds every layout and
resets interned handlers, then interns again from the new tree. Default
is retained: intern once, paint reads live `Signal`s.

A `fn()` in `ButtonProps.on_press` is interned by value. Intern copies
the env so the click still sees captured `Signal` ids after the widget
returns and drops its props. Without that copy, `+1` would write a freed
env and the Count label would stay put. The moved-in value is dropped by
intern — ownership follows the call.

## 6. Acceptance criteria

- `import "std:zeus"` is the only UI import an app needs; kit apps add
  `import "../packages/zeus-components/ui.yuga"`.
- `main()` ends with `App(title, w, h, fn() { ... })`.
- Every composing function is PascalCase; every token/wiring call
  (`muted()`, `SPACE.Sm`, `on_click`, `show_eq`) is lowercase.
- No `View` trait, no `impl View`, anywhere — `Node` is the only
  tree/builder type.
- The `Zeus Kit` example compiles and renders identically under
  `--target=native` and `--target=wasm32` from the same source.
- Signal-bound widgets update without re-invoking their enclosing
  component function. Interned `on_click` / text / style thunks outlive the
  `fn` value that was interned.
- `zeuscore`'s tree/layout/diff/signal logic is Yuga code; C exists only
  inside `zeus_plat.c` (and each target's thin FFI calls into it) — no
  tree/diff/layout logic implemented in C and merely exposed through
  `zeuscore`.

## 7. Non-goals

- No change to the native rasterizer, layout algorithm, or hit-testing
  logic beyond what's needed to sit behind `Backend`.
- No change to `theme.yuga` token values.