# Zeus

A component-based GUI library for Yuga, plus the `std:http` pieces you need
to back a component with a real service. One `Node` tree, built once with
signals for reactivity, replayed as native paint calls on desktop/iOS/Android
and as Canvas2D on the web. See [`docs/spec.md`](docs/spec.md) for backends;
this file is the public API map.

## The component model

A **component** is a plain function. Hierarchy is a trailing block. Props are
named arguments. There is no base class, no `impl View`, and no chain builder.

```yuga
import "std:zeus"

fn Chip(label: string) {
    zeus.Box(padding_x = 12, padding_y = 4, radius = 8, border = 1) {
        zeus.Text(label, font = 12)
    }
}

fn main() {
    let n = zeus.signal(0)
    zeus.App("Count", 320, 200, fn() {
        zeus.Box(align_direction = DIRECTION.Column, padding = 16, spacing = 8) {
            zeus.Text("Count", font = 22)
            zeus.Text("{{n.get()}}", font = 28)
            zeus.Box(align_direction = DIRECTION.Row, spacing = 8) {
                zeus.Button("-", on_click = fn() => n.set(n.get() - 1))
                zeus.Button("+", on_click = fn() => n.set(n.get() + 1))
            }
        }
    })
}
```

Public widgets in `std:zeus`: `App`, `Box`, `Text`, `Button`, `Input`, `Grid`,
`Overlay`, `Scroll`, `Svg`, `If`, `Each`, `For`. Kit look is
[`import "std:kit"`](../../packages/compiler/std/kit.yuga). Growing lists:
`zeus.signal([]string {})` + `zeus.For` + `zeus.push_item`.
SVG path strings live in [`lib/ui/display/icons.yuga`](lib/ui/display/icons.yuga).
Theme tokens live in [`lib/theme.yuga`](lib/theme.yuga) and `std:kit`.
Web is **Canvas2D wasm** — not HTML/DOM.

`zeus.App` builds the tree **once**. A signal write re-runs only the props that
read it. `zeus.view` / `zeus.app` rebuild every layout if you need that.

Visibility: `zeus.show(node, sig)` / `show_eq` hide a node from layout without
unmounting it.

## Recipe: add a backend service (grpc/http)

Zeus apps talk to a backend through `std:http`, with the wire contract living
in Yuga so a typo in a path or field is a compile error on both sides, not a
runtime 404. `examples/zeus/counter` is the reference implementation:

```
examples/zeus/counter/
  backend/api.yuga  shared contract: #[proto] payloads, imported by both sides
  backend/server    native backend
  screen.yuga       the Zeus UI, imported by every host's app.yuga
  frontend/         wasm host — http.client("") calls same-origin, Vite proxies
  macos/ ios/ android/   native hosts
```

To add a new endpoint:

1. Add a `#[proto]` request/response struct to `api.yuga`.
2. Handle it on the server with `app.rpc(...)`.
3. Call it from `screen.yuga` via `http.client(addr).call(...)` — the same call
   compiles unchanged on wasm, macOS, iOS, and Android because `addr` is the
   only thing that varies per host.
4. `./run.sh zeus/counter` (web) or `./run.sh zeus/counter macos` (native).

For the full request/response type story see [`docs/spec.md`](docs/spec.md) and
[`../../docs/boundary.md`](../../docs/boundary.md).
