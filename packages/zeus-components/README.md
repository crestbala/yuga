# zeus-components

The Bezel-style UI kit for Zeus, in the declarative Yuga syntax. Widgets are
plain `fn`s, the tree comes from trailing blocks, state is `signal`, and
interpolation is `{{ }}` — the syntax defined in `ZEUS_SPEC.md`. Colors and
spacing are the bezel tokens from `https://github.com/crabtalk/bezel`.

## Layout

| Path | Contents |
| --- | --- |
| `ui.yuga` | The barrel — import this for every widget; it re-exports them unqualified |
| `theme.yuga` | Bezel theme tokens (`canvas()`, `text()`, `SPACE.Md` …) |
| `theme/` | Token sources: colors, spacing, scale, font, motion |
| `ui/*` | Widgets, one file per widget: buttons, chips, forms, tables, loaders, overlays |

## Use

```yuga
import "std:zeus"
import "../packages/zeus-components/ui.yuga"

fn main() {
    let count = signal(0)
    App("Hello", 400, 300, fn() {
        Box(align_direction = DIRECTION.Column, background = canvas(), padding = SPACE.Page, spacing = SPACE.Section) {
            Card() {
                CardHeader(title = "Hello", subtitle = "A short subtitle"),
                Button(label = "Count {{count.get()}}", on_press = fn() => count.set(count.get() + 1))
            }
        }
    })
}
```

Widgets take props as named arguments (`size = sm()`, `look = solid()`);
containers take children in a trailing block with `,` separators. Handlers
ride in as `on_press` / `on_click` props. Every widget file owns its
`*Props` struct and defines its public name directly.

## Conventions

- **No `ui.` / `zeus.` prefixes.** The barrel shadows the same-named
  `std:zeus` widgets, so kit `Button`, `Progress`, `Slider`, `Scroll` … win
  over the raw std ones wherever this barrel is imported.
- **No chaining.** Style is props (`Box(align_direction = DIRECTION.Column, gap =
  8)`); advanced wiring is plain calls (`show_eq(n, sig, v)`,
  `key(n, "enter", "x.toggle")`).
- **`Row` / `Column` do not exist** — one container, `Box`, with
  `align_direction = DIRECTION.Row | DIRECTION.Column`.
- **`danger()` is the danger colour.** The look token is `danger_look()` —
  `look = danger_look()` — so apps can use both unqualified.
