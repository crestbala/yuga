# Zeus GUI Architecture & Specification for Yuga Language Compiler

This document defines the architecture, syntax, API structure, and implementation requirements for **Zeus**, the native reactive GUI framework for the **Yuga** programming language. 

The primary goal of Zeus is to deliver an ultra-clean, modern developer experience. It eliminates legacy object-oriented boilerplate, manual tree construction, imperative UI mutators, and virtual DOM diffing overhead in favor of a declarative, fine-grained reactive model.

---

## 1. Architectural Principles & Banned Legacy Patterns

To maintain a fast and developer-friendly design, Zeus explicitly bans legacy GUI architectures:

1. **NO Manual Node Instantiation or Parenting:**
   - ❌ BANNED: `Node.new()`, `parent.add_child(child)`, `node.set_parent()`.
   - ✅ REQUIRED: UI hierarchies are built implicitly using native block structure.

2. **NO String-Based Event Plumbing:**
   - ❌ BANNED: `widget.on("click", handler)`, `widget.add_event_listener(...)`.
   - ✅ REQUIRED: Events are first-class function parameters and inline `fn(...)` closures.

3. **NO Imperative UI Mutators:**
   - ❌ BANNED: `text_node.set_text(...)`, `button.set_disabled(true)`.
   - ✅ REQUIRED: UI elements dynamically reflect reactive state via fine-grained signals (`signal`).

4. **NO Absolute Coordinate Layouts as Default:**
   - ❌ BANNED: Mandatory pixel positioning (`x: 100, y: 200`) for structural layouts.
   - ✅ REQUIRED: Automated flow engines (`Row`, `Column`, `Box`, `Grid`) backed by a native layout solver.

5. **NO Full-Tree Virtual DOM Diffing:**
   - ❌ BANNED: React-style component re-render loops and Virtual DOM tree comparison.
   - ✅ REQUIRED: Direct signal-to-node dependency tracking. Mutating a signal updates only the specific leaf node bound to that value.

---

## 2. Core Language Syntax Rules for Yuga & Zeus

### A. Functions for UI Components (`fn`)
Components do not use `class`, `component`, `@Composable`, or OOP structures. Every component is declared as a native Yuga function (`fn`):

```yuga
fn Header(title: String) {
    Box(background = "#1e1e2e", padding = 16) {
        Text(title, color = "#cdd6f4")
    }
}
```

### B. Double Curly Braces (`{{ ... }}`) for String Interpolation
Evaluated variables and expressions inside string literals MUST use `{{expression}}` syntax:

```yuga
Text("Current Count: {{count}}")
Text("Total Price: {{price * quantity}}")
```

### C. Structs for Multi-Value Properties & Configs
Complex properties, layouts, and event payloads MUST be passed using struct instances:

```yuga
struct MouseEvent {
    x: Int,
    y: Int,
    button: Int
}

struct KeyEvent {
    key: Int,
    ctrl: Bool,
    shift: Bool,
    alt: Bool
}

struct BoxStyle {
    width: Int = -1,        // -1 represents auto/wrap
    height: Int = -1,
    background: String = "",
    padding: Int = 0,
    flex: Int = 0
}

struct GridConfig {
    columns: Int,
    gap: Int,
    padding: Int
}
```

### D. Trailing Blocks for Implicit UI Hierarchy
Containers (`Column`, `Row`, `Box`, `Grid`) accept a trailing block `{ ... }`. Evaluating expressions inside a trailing block automatically registers those elements as child nodes of the parent container frame via an internal compiler builder stack:

```yuga
Column(spacing = 10) {
    Text("Title")
    Text("Subtitle")
}
```

### E. Native Closures (`fn(...)`) for Events
Event handlers are passed directly as inline anonymous functions:

```yuga
Button("Save", on_click = fn() => save_data())
```

### F. Fine-Grained Signals (`signal` & `bind`)
State management relies on reactive primitives:

- **Create state:** `let count = signal(0)`
- **Mutate state:** `count += 1`
- **Bind inputs (two-way binding):** `Input(bind = username)`

---

## 3. Reference Syntax Implementations

### A. Counter Component
```yuga
fn Counter() {
    let count = signal(0)

    Column(spacing = 8) {
        Text("Count: {{count}}")
        Button("Increment", on_click = fn() => count += 1)
    }
}
```

### B. Form Validation with Struct Configuration
```yuga
struct FormConfig {
    placeholder: String,
    min_length: Int
}

fn ProfileForm(config: FormConfig) {
    let username = signal("")

    Column(spacing = 12) {
        Input(
            bind = username,
            placeholder = config.placeholder
        )

        if username.len < config.min_length {
            Text("Username must be at least {{config.min_length}} characters!", color = "#f38ba8")
        }

        Button(
            "Submit",
            enabled = username.len >= config.min_length,
            on_click = fn() => print("Submitted: {{username}}")
        )
    }
}
```

### C. Mouse & Keyboard Event Handling
```yuga
fn CanvasWidget() {
    let status = signal("Idle")

    Box(
        style = BoxStyle(width = 300, height = 200, background = "#1e1e2e"),
        on_mouse_down = fn(e: MouseEvent) => status = "Pressed button {{e.button}}",
        on_mouse_up   = fn(e: MouseEvent) => status = "Released mouse"
    ) {
        Column(padding = 16) {
            Text(status)
        }
    }
}

fn CommandPalette() {
    let input_text = signal("")

    Input(
        bind = input_text,
        on_key_down = fn(e: KeyEvent) {
            if e.ctrl && e.key == Key.K {
                print("Shortcut triggered!")
            }
        }
    )
}
```

Key events mirror the mouse pair plus a press cycle: `on_key_down` fires
when a key goes down, `on_key_up` when it is released, and `on_key_press`
on the matching down→up cycle (the keyboard analog of `on_click`). They are
available on any container (`Box`, `Row`, `Column`, …) and on `Input`, and
deliver a `KeyEvent { key, ctrl, shift, alt }` to the focused node:

```yuga
Box(on_key_down = fn(e: KeyEvent) => status = "pressed {{e.key}}",
    on_key_up   = fn(e: KeyEvent) => status = "released",
    on_key_press = fn(e: KeyEvent) => shortcut(e.key))
```

### D. Responsive Flexbox & Grid Layouts
```yuga
fn DashboardLayout() {
    // 1D Single-Direction Flexbox Layout (Row)
    Box(align_direction = DIRECTION.Row, spacing = 16, align_items = ALIGN.Center) {
        Box(flex = 1, background = "#313244", padding = 12) {
            Text("Flexible Left Panel")
        }
        Box(width = 150, background = "#45475a", padding = 12) {
            Text("Fixed Right Panel (150px)")
        }
    }

    // 2D Matrix Grid Layout using Struct Configuration
    Grid(config = GridConfig(columns = 3, gap = 12, padding = 16)) {
        Box(background = "#89b4fa", height = 100) { Text("Card 1") }
        Box(background = "#a6e3a1", height = 100) { Text("Card 2") }
        Box(background = "#f9e2af", height = 100) { Text("Card 3") }
    }
}
```

---

## 4. Implementation Checklist for Compiler & Engine

### 1. Lexer & Parser Layer
- [x] Add lexer rules to tokenise double curly braces `{{ ... }}` inside string literals as interpolation expressions.
- [x] Parse `fn` block declarations and anonymous `fn(...)` closure parameters.
- [x] Support trailing block parsing following identifiers/calls (`Identifier(...) { ... }`) to construct UI trees implicitly.
- [x] Parse `struct` declarations and struct instantiation expressions.

### 2. Reactive Signal Runtime
- [x] Build a dynamic dependency graph for `Signal<T>`.
- [x] Automatically subscribe active UI leaf nodes when evaluating signals inside components.
- [x] On signal mutation (`signal.set(val)`), bypass parent containers and re-render only target subscriber leaf nodes.

### 3. Layout & Platform Event Bridge
- [x] Bridge container calls (`Row`, `Column`, `Box`, `Grid`) to a native Flexbox/Grid calculation engine.
- [x] Capture OS-level input events, construct Yuga `MouseEvent` and `KeyEvent` structs, and execute associated `fn(e)` closures.

The signal graph (`std/zeuscore/track.yuga`) and the flex/grid solver
(`std/zeuscore/layout.yuga`) already existed; they are Yuga, not Taffy/Yoga.
The work was the declarative syntax layer, the props/event model, and wiring
payload-carrying events through to them.

---

## 5. As Implemented

Sections 1–3 above are the design intent, written before implementation. This
section is the delivered API. Where the two differ, this section is correct.

### Where things live

| Area | File |
| --- | --- |
| `{{ }}`, `fn(...)` closures, trailing blocks, named args | `packages/compiler/src/parser.c` |
| Props gathering, defaults, thunk coercion, capture chain | `packages/compiler/src/sema/typecheck.c` |
| String-concat runtime for interpolation | `packages/compiler/runtime/yuga_rt.h` |
| Declarative widgets, props structs, event dispatch | `packages/compiler/std/zeus.yuga` |
| Handler env snapshots | `packages/compiler/runtime/zeus_plat.c` |
| Tests | `packages/compiler/tests/compile_pass/zeus_spec_*.yuga` |

### The Counter, as it actually compiles

```yuga
import "std:zeus"

fn Counter() {
    let count = signal(0)

    Box(align_direction = DIRECTION.Column, spacing = 8, padding = 16) {
        Text("Count: {{count.get()}}")
        Button("Increment", on_click = fn() => count.set(count.get() + 1))
    }
}

fn main() {
    App("Counter", 320, 200, fn() { Counter() })
}
```

### Deviations from sections 2–3

These are consequences of Yuga's existing semantics, not open work:

- **Types are Yuga's**: `int`, `string`, `bool` — not `Int`, `String`, `Bool`.
- **Names are unqualified** (roadmap): `import "std:zeus"` exposes `Text`,
  `Button`, `Box`, `signal`, `App`, … directly — no `zeus.` prefix.
  `import "std:fmt"` exposes `println` / `print` directly. Qualified calls
  (`fmt.println`, `zeus.Text`) still work as the disambiguation escape hatch.
- **No `Row` / `Column` widgets** (roadmap): one container, `Box`, with
  `align_direction = DIRECTION.Row | DIRECTION.Column`. A row box centres on
  the cross axis, a column box stretches, unless `align_items` is given.
- **Theme values are constants, not functions** (roadmap): alignment modes,
  directions, and the spacing scale are enum constants — `ALIGN.Center`,
  `DIRECTION.Column`, `SPACE.Md` — never `align_center()` / `space_md()`
  calls. Enums lower to `int` literals, so the prop thunks still auto-wrap.
- **No chaining** (roadmap): `n.pad(8).gap(4)` does not exist. Style is
  props; advanced wiring (`key`, `show`, `each`, …) is plain functions taking
  the node first. `sig.get()` / `sig.set(v)` stay method calls — that is the
  language's UFCS, not a chain API.
- **Enums** (roadmap): `enum Key { Enter = 13, K = 107, … }`; `e.key == Key.K`
  is the spell. `zeus.key_k()` is kept as a legacy name.
- **Signals are explicit**: `count.set(count.get() + 1)`, and `{{count.get()}}`
  in interpolation. `count += 1` and bare `{{count}}` would require a signal to
  be transparently deref'd, which the type system does not do.
- **Alignment and direction are enum constants**: `ALIGN.Center`,
  `DIRECTION.Row`, `SPACE.Md` — the `Align.Center` of the spec is spelled
  `ALIGN.Center`; `align_items = ALIGN.Center`.
- **Colors are strings, parsed once**: `background = "#1e1e2e"`. `hex`
  accepts `#rrggbb` and `#rgb`, and returns `-1` for empty/invalid, which every
  style setter reads as "leave unset".
- **`username.len` on a signal is not supported**: use `username.get().len`.

### Duplicate definitions (roadmap)

A name defined twice is an error at the second definition, with the first
site named: two `fn`s, a `fn` and a `let`, two structs, two `let x` in one
scope, a parameter shadowed by a `let` in the same function. Imported names
shadow by depth (a local definition beats an import; a later import beats an
earlier one), which is how the kit barrel layers over std:zeus.

### How the syntax lowers

- **Trailing block** — `C(...) { ... }` becomes
  `zeus.__ui_scope(C(...), || { ... })`. `__ui_scope` pushes the node as the
  current parent, runs the block, and pops; each widget ends with `ui_emit`,
  which attaches it to that parent. Hierarchy therefore comes from block
  structure, with no parenting calls. The `{` must open on the same line as the
  closing `)`, so a following standalone block is still its own statement.
- **Named arguments** — collected into one struct literal for the callee's last
  parameter, which must be a struct; omitted fields take their declared
  defaults. `Text("hi")` with no props synthesises an empty literal. A props
  struct may declare `foo__set: bool = false`; the compiler sets it when `foo`
  is passed, which is how an optional handler is distinguished from its default
  (function values cannot be compared).
- **Struct construction** — `BoxStyle(width = 300)` is a struct literal, not a
  call. Works module-qualified too: `zeus.GridConfig(columns = 3)`.
- **Unqualified names** — a call whose name is not a local or a definition of
  the current module is looked up in the imports, by depth (own declarations
  first, then imports' declarations, last import wins). The kit barrel
  (`ui.yuga`) re-exports its widgets by importing them.
- **Enums** — `enum Key { Enter = 13, K = 107 }`; a `Key.K` expression is
  lowered to its `int` constant in typecheck, so neither backend sees it.
- **Field moves** — a non-Copy field read that is consumed (`on_click = p.h`)
  records the moved path on the binding; the struct's drop skips it and a
  later read errors. No runtime zeroing: ownership is a static fact.
- **Interpolation** — folded into `yuga_str_concat` / `yuga_str_of_*` builtin
  calls during typecheck, so neither backend knows the node kind existed.
  Accepts `string`, `int`, `float`, `bool`.
- **Reactive text** — a value passed where `fn() -> T` is expected is wrapped in
  a thunk. `Text` takes `fn() -> string`, so the signal read happens *inside*
  the thunk and is recorded against that label's own effect. Setting the signal
  re-runs that one effect and repaints that one label. (A call returning a
  function must be bound to a local first, or it will be wrapped.)
- **Reactive style props** — every style prop of the std widgets is a thunk
  (`fn() -> int`, `fn() -> string`, or `fn() -> BoxStyle`); plain values
  auto-wrap, and a value that reads a signal re-applies just that prop when the
  signal changes. The widget registers each passed prop as a tracked effect on
  its node; the effect's thunk returns the value and the dispatcher re-applies
  the setter (`set_fg`, `set_border`, `set_w`, …). This is the declarative
  replacement for the old `styled(node, fn() { fg(node, …) })` closures and the
  paint-modifier functions — no node mutation after build. `border` applies
  unconditionally so `border = if sel { 1 } else { 0 }` can clear it; the other
  props keep their sentinel checks (`> 0` / `>= 0`).
- **If-expressions** — `if cond { a } else { b }` is a value in expression
  position; both branches must end with an expression of the same type, and
  `else` is required. `Box(align_direction = if a { DIRECTION.Row } else {
  DIRECTION.Column })` and `Text(color = if sel { accent() } else { muted() })`
  are the canonical spells. Statement-position `if` is unchanged and needs no
  `else`. In a `-> T` body, a trailing `if` whose branches end in expressions
  is the return value: `fn f(n: int) -> string { if n > 0 { "pos" } else {
  "neg" } }` means `return if n > 0 { "pos" } else { "neg" }`. A branch
  ending in `return` stays a statement.
- **Defaults** are constant expressions only — literals, negated numbers,
  identifiers, and struct literals — and are resolved in the module that
  declares the struct, not the caller's.

### Events

`MouseEvent` / `KeyEvent` are built in Yuga and delivered to `fn(e)` closures.
The engine invokes argument-less interned functions, so the payload is staged in
module globals and read by `current_mouse()` / `current_key()`.

A node carrying only a pointer handler is now hit-testable; because children are
tried first, that doubles as event bubbling. `Input(bind = sig)` writes the edit
buffer back into a `Signal<string>` on each edit, and the store notifies only on
a real change.

Handlers arrive as props-struct fields or parameters, both of which Yuga drops on
return — freeing the closure's captured env. `zeus_plat.c` snapshots the env when
a handler is stored (the same snapshot `plat_intern_fn` already took, for the
same reason) and takes ownership of the moved-in value. For the same reason, a
stored handler is called through its vector directly: binding it to a local
would make Yuga drop and free it.

### Compiler fixes this required

- **Unqualified resolution by import depth.** A barrel like `ui.yuga` re-exports
  its widgets by importing them; the app gets `Button`, `Chip`, `muted()`, …
  without prefixes. Own declarations beat imports; later imports beat earlier
  ones; imports of imports are only consulted after every direct import.
- **Duplicate-definition errors.** Same module (fn / struct / enum / let) and
  same scope (params, `let`s) collisions are errors at the second definition.
- **Enum constants.** `Key.K` lowers to an int literal at the use site.
- **Static field moves.** A consumed non-Copy field records its path; the drop
  skips it, later reads error, and no runtime zeroing is involved.
- **Nested closures now capture transitively.** A name used inside a nested
  closure is captured by every enclosing closure between it and its definition;
  previously only the innermost one recorded it, so the inner closure read a
  name its env never received. This is what the Counter needs — the handler
  inside a trailing block closes over a local of the component.
- **Module globals are no longer captured.** They have static storage; copying
  them into an env produced a member reference on a `void` env.
- **Field defaults resolve in the declaring module**, so a default naming a
  function in `std:zeus` works from any caller.

### Known limitations

- Interpolated strings allocate and are never freed, matching the existing
  ownership of `yuga_string_from_bytes`. Text rebuilt every frame from a signal
  will grow memory; the language has no string ownership story to hook into yet.
- Struct names are global across modules. The declarative `ButtonProps` is named
  `BtnProps` to avoid colliding with the UI kit's.
- `compile_fail/return_cap_clos.yuga` and `cap_clos_escape_call.yuga` were
  removed. They asserted that `fn(...)` closures are rejected — which this spec
  requires — and never tested escape analysis: `compile_pass/escape_clos.yuga`
  does the same thing with `|x|` syntax and is expected to pass, because
  capturing closures copy Copy locals into a heap env and may escape by design.
  They are now `compile_pass/fn_closure_{escape,arg}.yuga`.
