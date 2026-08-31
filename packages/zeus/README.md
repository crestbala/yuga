# Zeus

A component-based GUI library for Yuga, plus the `std:http` pieces you need
to back a component with a real service. One `Node` tree, built once with
signals for reactivity, replayed as native paint calls on desktop/iOS/Android
and as Canvas2D on the web. See [`docs/spec.md`](docs/spec.md) for the full
architecture and per-host build steps; this file is the map + the two
recipes you need to add to it.

## The component model

A **component** is a plain function that takes a `Props` struct and returns
a `Node`:

```yuga
struct ChipProps {
    label: string,
    color: int,
}

fn Chip(p: ChipProps) -> Node {
    let n = zeus.text(p.label)
    return n.styled(|| { /* paint using p.color */ })
}
```

That's the whole contract — no base class, no lifecycle to implement, no
`impl View`. Compose components by nesting `Node`s:

```yuga
import "std:zeus"
import "lib/ui.yuga"

fn main() {
    ui.Card().children(
        ui.CardHeader(CardHeaderProps { title: "Hello", subtitle: "A short subtitle" }),
        ui.Chip(ChipProps { label: "live", color: theme.accent() }),
    ).run()
}
```

Conventions every widget in `lib/ui/` follows:

- **Few arguments → positional; many → a `*Props` struct.** Yuga has no
  overloading and no default fields, so a struct is the only way to carry
  optional or numerous inputs. When a widget needs one or two required values
  it takes them directly, because a two-field struct is pure ceremony:

  ```yuga
  ui.Ghost("Cancel", || { close() })              // positional — two inputs
  ui.Alert(AlertProps { title: …, body: …, color: … })   // struct — several
  ```

  Buttons take `(label, on_press)`. Widgets with richer inputs keep a `*Props`
  struct holding exactly what the caller supplies (labels, callbacks, values)
  — never styling.
- **`Foo(props)`** is the medium-size, default-look constructor. Named
  variants (`Ghost`, `Destructive`, `Soft`, ...) are just different look/size
  arguments to the same painter, so a button and a destructive button share
  one implementation, not two.
- **`.size()` / `.look()`** are chain modifiers on the returned `Node` —
  `ui.sm/md/lg` and `ui.solid/soft/outline/ghost/danger` — so callers can
  restyle a component without a new constructor. `.size()` always means the
  token; raw pixels are `.size_px(px)` or `.w()/.h()`. The two used to share
  the name `size` and resolved by import order, which made `.size(ui.sm())`
  silently produce a 0×0 box.
- **Styling goes through `.styled(|| { ... })`**, a closure that runs once
  per paint and reads props/signals to compute colors, padding, radius. This
  is the only place a component touches `theme/`.

## The chain API

Every `Node` method is a plain function that takes the node first and
returns it, so `n.pad(8)` is exactly `zeus.pad(n, 8)` — there is no `impl`
block, no trait, and nothing to `move` into a closure (captures in a Yuga
`fn() { ... }` literal are copied automatically). That single rule is what
makes the whole API chain:

```yuga
zeus.button("Save")
    .on_click(save)                 // reactivity
    .key("enter", "save.press")     // key binding — see below
    .pad(12).gap(8)                 // spacing
    .flex_row().justify(zeus.flex_center())  // layout
    .bg(theme.accent()).radius(8).font(15)   // style
```

If you've used GPUI (Zed's Rust UI framework), this reads the same way on
purpose — `zeus.container()` is an alias for `zeus.box()` for exactly that reason.
The difference is Yuga has no `impl`/trait to write and nothing to `.into()`
or `move` — a component is `fn(Props) -> Node`, full stop:

```yuga
zeus.container()
    .flex_col().gap(8)
    .bg(0x505050).size_px(500)       // hex int literals work directly —
    .border(1).border_color(0x0000ff) // same packed layout as rgb(r, g, b)
    .center()
    .font(20).fg(0xffffff)
    .child(zeus.text("Hello, World!"))
    .run()
```

**Layout — flex, direction, alignment**

| | |
|---|---|
| `.flex_row()` / `.flex_col()` | stack children left-to-right / top-to-bottom |
| `.flex_row_reverse()` / `.flex_col_reverse()` / `.flex_wrap()` | direction & wrap |
| `.justify(mode)` / `.align(mode)` / `.align_self(mode)` | main-axis / cross-axis alignment |
| `zeus.flex_start()` / `flex_center()` / `flex_end()` / `flex_between()` / `flex_around()` / `flex_evenly()` / `flex_stretch()` | named `mode` constants — no magic numbers |
| `.center()` | sugar for `.justify(flex_center()).align(flex_center())` |
| `.grow(weight)` / `.shrink(weight)` | flex-grow / flex-shrink |
| `zeus.grid(columns, gap)` / `.span(columns)` | grid container / column span |

**Spacing**

`.pad(all)` / `.padX()` `.padY()` `.padTop()` `.padRight()` `.padBottom()` `.padLeft()`,
mirrored by `.margin*`, plus `.gap(both)` / `.gap_row()` / `.gap_col()`.

**Sizing & position**

`.w(px)` / `.h(px)` (`-1` restores the 100%-of-parent default), `.size_px(px)`
(both at once, equal). `.width_pct()` / `.height_pct()`,
`.min_w/.max_w/.min_h/.max_h`, `.aspect(w, h)`. Positioning:
`.position(zeus.pos_relative()|pos_absolute())` + `.top/.right/.bottom/.left(px)`
+ `.z_index()`. Overflow: `.overflow(zeus.overflow_visible()|hidden()|scroll()|auto())`.

**Style**

`.bg(rgb)` / `.fg(rgb)` / `.radius(px)` / `.opacity(pct)` / `.font(px)` /
`.border(width)` / `.border_color(rgb)`, `zeus.rgb(r, g, b)` to build a color.
`.class("card"|"heading"|"muted"|"container")` applies one of the built-in
presets (see `class()` in `std/zeus.yuga` — add your own preset there the same
way, or just chain the individual setters, which is what every widget in
`lib/ui/` does).

**Reactivity**

`zeus.signal(0)` creates a `Signal`; `sig.get()` reads it, `sig.set(v)`
writes it and schedules a repaint. `.bind(sig)` paints a
label from a signal; `.on_click(handler)` runs a closure on click;
`.on_click_inc/.toggle/.set/.add(sig, ...)` are one-call sugar for the common
click-writes-a-signal case. `.show(sig)` / `.show_eq/.ne/.ge/.le(sig, v)` hide
a node from layout without unmounting it; `zeus.when(sig, || node)` is the
combinator form. `.each(items, |i, item| node)`
render a list — closures also accept `(i, item) => node`,
same thing either way (see [`docs/spec.md`](../../docs/spec.md)). There's no
`zeus.range()` — `.each` maps a `[]T` you already have, and a numeric
sequence isn't one, so build the small array yourself first:

```yuga
fn seq(lo: int, hi: int) -> []int {
    let mut xs = []int {}
    for i in lo..hi {
        push(xs, i)
    }
    return xs
}
// ...
grid.each(seq(1, last + 1), (i, n) => { Cell(n) })
```

(`date_picker.yuga` and `pagination.yuga` both keep a local copy of `seq` —
five lines is cheaper than a shared dependency for something this small.)

**Key bindings**

`.key(spec, action)` maps a chord (`"enter"`, `"space"`, `"cmd-s"`, ...) to a
named action **and marks the node focusable** — components never chain
`.focusable()` themselves for this, one call does both:

```yuga
n.key("enter", "button.press").key("space", "button.press").on_key_fn("button.press")
```

The action is a string, not the handler directly, on purpose: it's also the
keymap context (`"button.press"` → context `"button"`), and an app can call
`zeus.map_key(spec, action, ctx)` / `remap_key(...)` to rebind a shortcut
without touching the component. Pick the write that matches what the key
should do: `.on_key_fn(action)` re-fires whatever closure `.on_click(...)`
already set; `.on_key_toggle/.on_key_set/.on_key_add(action, sig, ...)` write a
signal directly, the same way `.on_click_*` does for the mouse.

## Library layout

`lib/ui/` is organized by what a component *does*, not alphabetically:

```
lib/
  theme/            tokens: colors, spacing, scale, font, motion (no components)
  ui.yuga           the public barrel — `import "lib/ui.yuga"` re-exports everything below
  ui/
    typography/      Display, Title, Heading, Body, Overline
    navigation/      Navbar, Toolbar, Breadcrumbs, Header, Tabs, Pagination
    actions/         Button (+ Ghost/Soft/Prominent/Destructive/Outline), ButtonGroup
    display/         Chip, Badge, Avatar, User, Kbd, Link, Icon, Metric
    feedback/        Alert, Toast, Spinner, Skeleton, Progress, Empty, Tooltip, Snippet, Loader*
    overlays/        Dialog, HoverCard
    forms/           Field, TextField, Number, Radio, Select, Checkbox, Switch, Slider, DatePicker
    layout/          Card, Panel, Surface, GroupBox, Divider, Accordion, Scroll
    data/            List, Table, Stats, Activity
    shared/          look.yuga — the styling helper widgets call from `.styled()`, not a component
```

Components can depend on components in other categories (`overlays/dialog.yuga`
imports `actions/button.yuga`, `data/stats.yuga` imports `display/metric.yuga`)
— categories describe intent for whoever is browsing the tree, they are not
a dependency wall.

## Recipe: add a new component

Every widget in `lib/ui/` follows one of two shapes. Use the plainer one
unless you actually need the second.

**Shape A — static look.** Nothing about the styling depends on `.size_of()`,
`.look_of()`, or a signal, so there's nothing to recompute later. Just chain
the setters directly in the constructor — this is most of `layout/` and
`navigation/` (`Divider`, `Panel`, `Toolbar`, ...):

```yuga
import "std:zeus"
import "../../theme/colors.yuga"

struct MyWidgetProps {
    label: string,
}

fn MyWidget(p: MyWidgetProps) -> Node {
    return zeus.text(p.label).fg(colors.text()).font(14)
}
```

**Shape B — depends on `.size()` / `.look()` or a signal.** Anything that
should repaint when a caller does `.with_size(ui.lg)` after the fact, or that
reads a `Signal`, needs to go through `.styled()` — that closure is what
`track.run_node` reruns on the next relevant change. This is the pattern
`avatar.yuga`, `chip.yuga`, `button.yuga`, etc. all use:

```yuga
import "std:zeus"
import "../../theme/colors.yuga"
import "../../theme/scale.yuga"
import "../shared/look.yuga"

struct MyWidgetProps {
    label: string,
    color: int,
}

// Bundle every Node handle .styled() needs to touch, plus any Prop it reads —
// one struct field per thing, not positional args. Suffix `Parts`, distinct
// from the public `Props` struct above it.
struct MyWidgetParts {
    n: Node,
    lab: Node,
    color: int,
}

// Named `style_<widget>`, one word, matching every sibling file — this is
// the one convention worth not improvising on, so a reader who's seen one
// widget can find the paint function in any other by name alone.
fn style_my_widget(p: MyWidgetParts) {
    let size = p.n.size_of()
    let variant = p.n.look_of()
    look.paint(p.n, variant, p.color).font(scale.font_px(size))
    p.lab.fg(colors.text())
}

fn MyWidget(p: MyWidgetProps) -> Node {
    let n = zeus.row().with_size(scale.md()).with_look(scale.solid())
    let lab = n.add(zeus.label(p.label))  // .add() returns the child so you keep a handle to it; .child() returns the parent
    n.styled(|| { style_my_widget(MyWidgetParts { n: n, lab: lab, color: p.color }) })
    return n
}
```

Steps regardless of shape:

1. Pick the category folder it belongs in (or add a new one — update the
   import list in `lib/ui.yuga` either way). Imports from a category folder
   go up two levels to reach `theme/`, one `../` to reach a sibling category,
   none to reach a sibling in the same folder.
2. Re-export it from `lib/ui.yuga`: add the import near its category's other
   imports, and a one-line forwarding `fn` in that category's comment block.
3. Try it from `examples/zeus/kit` (`./run.sh kit`) before wiring it into a
   real app — kit exists to exercise every widget in isolation.

## Recipe: add a backend service (grpc/http)

Zeus apps talk to a backend through `std:http`, with the wire contract living
in Yuga so a typo in a path or field is a compile error on both sides, not a
runtime 404. `examples/zeus/counter` is the reference implementation:

```
examples/zeus/counter/
  api.yuga        shared contract: paths + #[proto] payload shape, imported by both sides
  server.yuga     native backend — http.app(), http.get(...), http.listen(app, PORT)
  screen.yuga     the Zeus UI, imported by every host's app.yuga
  frontend/       wasm host — http.client("") calls same-origin, Vite proxies /api
  macos/ ios/ android/   native hosts — http.client(api.native_addr()) / android_addr()
```

To add a new endpoint:

1. Add a path constant (and any `#[proto]` request/response struct) to
   `api.yuga`. This file has no logic — it's the contract.
2. Handle it in `server.yuga`: `http.get(app, api.my_path(), |req, res| { ... })`.
3. Call it from `screen.yuga` (or a component it renders) via
   `http.client(addr).call(...)` — the same call compiles unchanged on wasm,
   macOS, iOS, and Android because `addr` is the only thing that varies
   per host (see `api.native_addr()` / `api.android_addr()`).
4. `./run.sh zeus/counter` (web) or `./run.sh zeus/counter macos` (native)
   to try it — see [`../../run.sh`](../../run.sh) for every variant.

For the full request/response type story (`#[proto]` structs, JSON vs.
binary wire format) see [`docs/spec.md`](docs/spec.md) and
[`../../docs/boundary.md`](../../docs/boundary.md).
