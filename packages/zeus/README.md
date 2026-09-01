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
    color: string,
}

fn Chip(p: ChipProps) -> Node {
    return Box(background = p.color, padding = 6, radius = 8) {
        Text(p.label, color = "#0a0a0a", font = 11)
    }
}
```

That's the whole contract — no base class, no lifecycle to implement, no
`impl View`. Compose components by nesting trailing blocks:

```yuga
import "std:zeus"
import "../packages/zeus-components/ui.yuga"

fn main() {
    App("Hello", 400, 300, fn() {
        Card() {
            CardHeader(title = "Hello", subtitle = "A short subtitle"),
            Chip(label = "live", color = accent()),
        }
    })
}
```

Conventions every widget follows:

- **Props are named arguments.** `Button("Save", on_click = save)` — the
  named arguments collect into the callee's last `*Props` struct, and
  omitted fields take their declared defaults. One or two required values
  can be positional (`Text("hi")`, `Button("Cancel", ...)`).
- **`Box` is the one container.** `Box(align_direction = DIRECTION.Row |
  DIRECTION.Column, ...)`. There is no `Row` / `Column` widget.
- **Style is props, not chains.** `Box(padding = 12, gap = SPACE.Sm, radius = 8,
  background = accent())`. There is no `n.pad(8).gap(4)`.
- **Names are unqualified.** `import "std:zeus"` exposes `Text`, `Button`,
  `Box`, `signal`, `App`, … directly; `import "../packages/zeus-components/ui.yuga"`
  exposes `Button` (the kit's), `Chip`, `Tabs`, and the theme constants
  (`muted()`, `SPACE.Sm`, …). The old qualified forms (`zeus.Text`,
  `fmt.println`) still work as the disambiguation escape hatch.
- **Events are props with inline `fn(e)` closures.** `on_click`,
  `on_mouse_down`, `on_key_down`, … take `fn()` or `fn(e)`; key codes are
  enum constants: `e.key == Key.K`.
- **`Foo(props)`** is the medium-size, default-look constructor. Named
  variants (`Ghost`, `Destructive`, `Soft`, ...) are just different look
  arguments to the same painter.
- **Size / look are props.** `size = sm() / md() / lg()`, `look = solid() /
  soft() / outline() / ghost() / danger_look()`.

## The plain-function API

Advanced wiring is plain functions taking the node first — never method
chains:

```yuga
let n = Button("Save", on_click = save)
key(n, "enter", "save.press")          // key binding — see below
show_eq(n, sig, 0)                     // hide from layout while sig != 0
```

Style changes belong in props, not node painting. Every style prop is a thunk
— plain values auto-wrap, and a value that reads a signal re-applies just that
prop when the signal changes:

```yuga
Box(
    border = if sel.get() == 1 { 1 } else { 0 },   // if-expression value
    border_color = if sel.get() == 1 { accent() } else { line() }
)
```

The full list of plain functions lives in `packages/compiler/std/zeus.yuga`:
`bind` / `bind_n` (paint a label from a signal), `on_click_set` /
`on_click_toggle` / `on_click_add` (click writes a signal), `show` /
`show_eq` / `show_ne` / `show_ge` / `show_le`, `each` / `when` (lists and
conditionals), `pulse` / `phase` / `enter_fade` (animation), `hover` /
`hover_delay` / `hover_leave`, `key_context` / `focusable` / `capture_text`.

**Layout** is props on `Box`:

| Prop | Meaning |
|---|---|
| `align_direction = DIRECTION.Row | DIRECTION.Column` | stack children left-to-right / top-to-bottom |
| `justify_content` / `align_items` | main-axis / cross-axis alignment (`ALIGN.Start` … `ALIGN.Stretch`) |
| `grow` / `shrink` | flex-grow / flex-shrink |
| `flex_wrap`? → `overflow` + wrapping | `Grid(columns = n, gap = g)` is the 2D layout |
| `width` / `height` / `size` | pixels (`-1` restores the 100%-of-parent default) |
| `padding` / `gap` / `spacing` | spacing (`pad_x`, `pad_y`, `margin`, … for the box model) |
| `background` / `color` / `radius` / `border` / `border_color` | style — hex strings |

**Reactivity**

`signal(0)` creates a `Signal`; `sig.get()` reads it, `sig.set(v)`
writes it and schedules a repaint. `Text("{{sig.get()}}")` re-renders only
that label's effect; `bind(n, sig)` paints a label from an int signal.
`show(n, sig)` hides a node from layout without unmounting it;
`when(sig, || node)` is the combinator form. `each(parent, items, |i, item| node)`
renders a list — closures also accept `(i, item) => node`,
same thing either way (see [`docs/spec.md`](../../docs/spec.md)). There's no
`range()` — `each` maps a `[]T` you already have, and a numeric
sequence isn't one, so loop it directly:

```yuga
for i in 0..n {
    Cell(i)
}
```

**Key bindings**

`key(n, spec, action)` maps a chord (`"enter"`, `"space"`, `"cmd-s"`, ...) to
a named action **and marks the node focusable**:

```yuga
key(n, "enter", "button.press")
key(n, "space", "button.press")
on_key_fn(n, "button.press")
```

The action is a string, not the handler directly, on purpose: it's also the
keymap context (`"button.press"` → context `"button"`), and an app can call
`map_key(spec, action, ctx)` / `remap_key(...)` to rebind a shortcut
without touching the component. `on_key_fn(n, action)` re-fires whatever
closure `on_click(n, ...)` already set; `on_key_toggle` / `on_key_set` /
`on_key_add(n, action, sig, ...)` write a signal directly.

## Library layout

`packages/zeus-components/` is organized by what a component *does*, not
alphabetically:

```
zeus-components/
  theme/            tokens: colors, spacing, scale, font, motion (no components)
  ui.yuga           the public barrel — `import "…/ui.yuga"` re-exports everything below
  ui/
    typography/      Display, Title, Heading, Body, Overline
    navigation/      Navbar, Toolbar, Breadcrumbs, Header, Tabs, Pagination
    actions/         Button (+ Ghost/Soft/Prominent/Destructive/Outline), ButtonGroup
    display/         Chip, Badge, Avatar, User, Kbd, Link, Icon, IconHit, Metric
    feedback/        Alert, Toast, Spinner, Skeleton, Progress, Empty, Tooltip, Snippet, Loader*
    overlays/        Dialog, HoverCard
    forms/           Field, TextField, InputGroup, Number, Radio, Select, Checkbox, Switch, Slider, DatePicker
    layout/          Card, CardHeader/Body/Footer, Panel, Surface, GroupBox, Divider, Accordion, Scroll
    data/            List, ListItem, Table, Stats, Activity
```

The barrel (`ui.yuga`) re-exports every widget as a thin wrapper (each calls
its implementing file qualified), so apps import it and use `Button`, `Chip`,
`muted()`, … unqualified. Because the wrappers are declarations of the
barrel module, they shadow the same-named `std:zeus` widgets — the kit
replaces the raw std widgets for any app that imports it.

Components can depend on components in other categories
(`overlays/dialog.yuga` imports `actions/button.yuga`, `data/stats.yuga`
imports `display/metric.yuga`) — categories describe intent for whoever is
browsing the tree, they are not a dependency wall.

## Recipe: add a new component

Pick the category folder it belongs in (or add a new one). Imports from a
category folder go up two levels to reach `theme.yuga`, one `../` to reach a
sibling category, none to reach a sibling in the same folder. Re-export it
from `packages/zeus-components/ui.yuga`: add the import near its category's
other imports, and a one-line forwarding `fn` in that category's block.

```yuga
import "std:zeus"
import "../../theme.yuga"

struct MyWidgetProps {
    label: string = "",
    color: string = "",
}

fn MyWidget(p: MyWidgetProps) -> Node {
    return Box(align_direction = DIRECTION.Row, gap = SPACE.Sm, background = p.color, radius = button_radius()) {
        Text(p.label, color = text(), font = font_px(1))
    }
}
```

Things that read a `Signal` or repaint later use `Text("{{sig.get()}}")`
(one effect per label) or reactive style props — `Box(border = if sel {
1 } else { 0 })`, `Text(color = if sel { accent() } else { muted() })` —
which re-apply just that prop when the signal changes. See
`ui/forms/date_picker.yuga` for the full pattern (42 cells built once, each
with reactive text + style effects).

Try it from `examples/zeus/kit` (`./run.sh kit`) before wiring it into a
real app — kit exists to exercise every widget in isolation.

## Recipe: add a backend service (grpc/http)

Zeus apps talk to a backend through `std:http`, with the wire contract living
in Yuga so a typo in a path or field is a compile error on both sides, not a
runtime 404. `examples/zeus/counter` is the reference implementation:

```
examples/zeus/counter/
  api.yuga        shared contract: paths + #[proto] payload shape, imported by both sides
  server.yuga     native backend — http.app(), http.rpc(...), http.listen(app, PORT)
  screen.yuga     the Zeus UI, imported by every host's app.yuga
  frontend/       wasm host — http.client("") calls same-origin, Vite proxies /api
  macos/ ios/ android/   native hosts — http.client(api.native_addr()) / android_addr()
```

To add a new endpoint:

1. Add a path constant (and any `#[proto]` request/response struct) to
   `api.yuga`. This file has no logic — it's the contract.
2. Handle it in `server.yuga`: `http.app()`, `app.rpc("Service.Call", |body| { ... })`.
3. Call it from `screen.yuga` (or a component it renders) via
   `http.client(addr).call(...)` — the same call compiles unchanged on wasm,
   macOS, iOS, and Android because `addr` is the only thing that varies
   per host (see `api.native_addr()` / `api.android_addr()`).
4. `./run.sh zeus/counter` (web) or `./run.sh zeus/counter macos` (native)
   to try it — see [`../../run.sh`](../../run.sh) for every variant.

For the full request/response type story (`#[proto]` structs, JSON vs.
binary wire format) see [`docs/spec.md`](docs/spec.md) and
[`../../docs/boundary.md`](../../docs/boundary.md).
