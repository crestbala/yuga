# Yuga

Yuga is a memory-safe systems language: Odin-like syntax, Rust-like ownership.
The compiler (`yugac`) is C11. It typechecks a program, lowers it to IR, emits
C99, and invokes `cc`. C is the platform binding target, not the language's
semantics.

```yuga
import "std:fmt"

fn main() {
    fmt.println("hello, yuga")
}
```

```
make
./bin/yugac hello.yuga -o hello
./hello
```

Language rules: [docs/spec.md](docs/spec.md). Architecture and how-to:
[docs/yuga.md](docs/yuga.md). C vs Yuga: [docs/boundary.md](docs/boundary.md).

## Requirements

- A C11 compiler (`cc`) and `make`
- macOS for native desktop GUI (Cocoa) and Maya present
- [Xcode](https://developer.apple.com/xcode/) for the iOS Simulator target
- A `wasm32` clang (Homebrew LLVM, not Apple `/usr/bin/clang`) for web builds;
  set `YUGA_WASM_CC` if it is not on `PATH`
- Android SDK, NDK, Gradle 8.2+, and `adb` for `--target=android`

The compiler itself is C11 + libc. CLI programs link with the host `cc`. GUI
and 3D hosts are listed under [Platforms](#platforms).

## Build

From the repo root:

```
make
```

That produces `bin/yugac` and `bin/yuga-lsp`. Then:

```
./bin/yugac app.yuga -o app          # native binary
./bin/yugac app.yuga --run           # compile and run
./bin/yugac app.yuga --emit-c -o a.c # C99
./bin/yugac app.yuga --emit-ir -o a.ir
./bin/yugac --target wasm app.yuga -o app.wasm
./bin/yugac --target=ios --run examples/zeus/dashboard/dashboard.yuga
./bin/yugac --target=android examples/zeus/counter/android/app.yuga
```

`make test` compiles and runs the language tests, golden programs, and
examples (GUI and Maya in headless mode). Set `YUGA_TIME=1` to print
check / codegen / cc timings.

## Libraries

Quoted imports only. `import "std:foo"` loads `packages/compiler/std/foo.yuga`.
Call imported items as `foo.bar(...)`.

| Import | What it is |
|---|---|
| `std:fmt` | Stdout. `fmt.println` is compile-time lowering to length-based writes, not `printf`. |
| `std:zeus` | UI toolkit. One `Node` tree, signals, chain API. Same source on every host. |
| `std:http` | Unary RPC over gRPC-Web (HTTP/1.1) and h2c. `#[proto]` structs, no REST routes. |
| `std:maya` | Tiny 3D/2D engine. Scene and tracer in Yuga; C is the event loop and present. |
| `std:net` | TCP connect / listen / read / write. Used by `http`; not an app-level import. |
| `std:sys` | `env_set` / `exit`. Language-level seam into `yuga_rt`. |

Document items with `///` (and `//!` at the file top). Hover in the editor
shows those comments plus the type.

### Zeus

Zeus is Yuga's UI library. A component is a function that takes a `Props`
struct and returns a `Node` — no `View` trait, no `impl`. Backend is a
`--target` flag, not an import.

```yuga
import "std:zeus"

fn main() {
    let n = zeus.signal(0)
    zeus.col().pad(16).gap(8).child(
        zeus.text("Count").font(22),
        zeus.label("").bind(n).font(28),
        zeus.row().gap(8).child(
            zeus.button("-").on(zeus.click(), || { zeus.inc(n, -1) }),
            zeus.button("+").on(zeus.click(), || { zeus.inc(n, 1) }),
        ),
    ).run()
}
```

Kit widgets, theme tokens, and the public barrel live in `packages/zeus/lib/`
(`import "lib/ui.yuga"` from an app). Zeus paints its own theme on every host;
Cocoa / UIKit / Android widgets are not used. Full map and recipes:
[packages/zeus/README.md](packages/zeus/README.md). Architecture:
[docs/zeus.md](docs/zeus.md), [packages/zeus/docs/spec.md](packages/zeus/docs/spec.md).

## Platforms

| Target | Flag | Host | Notes |
|---|---|---|---|
| Native desktop | `--target=native` (default) | macOS Cocoa | Zeus paints; AppKit is the window, not the widgets. |
| Web | `--target=wasm` / `wasm32` | Canvas2D | No WebGPU / WebGL. Needs a `wasm32` clang. |
| iOS | `--target=ios` | UIKit Simulator | Window and touch only. Needs Xcode. Device signing is out of scope. |
| Android | `--target=android` | JNI Canvas | Writes a Gradle project. Layout is density-independent pixels. |

CLI and `std:http` servers are ordinary native binaries. Maya present is
Cocoa 2D on macOS (`MAYA_HEADLESS=1` updates once and exits).

The Android emulator reaches a Mac backend at `10.0.2.2:8080`, not
`127.0.0.1`. The Simulator and Cocoa apps share the Mac loopback.

## Examples

`./run.sh` with no arguments lists everything. It builds `yugac` if needed.
GUI examples open a window and servers block until Ctrl-C. For one frame then
exit (what `make test` does), set `ZEUS_HEADLESS=1` or `MAYA_HEADLESS=1`.

```
./run.sh                      # list
./run.sh language/counter     # language demo
./run.sh solar                # Maya 3D (macOS window)
./run.sh dashboard            # Zeus app, Cocoa
./run.sh dashboard wasm32     # same app, browser
./run.sh dashboard ios        # same app, Simulator
./run.sh zeus/counter         # full-stack: API :8080 + web UI :5173
./run.sh zeus/counter macos   # same UI as a Cocoa client
```

`counter` is both a language example and the full-stack Zeus app, so a bare
`./run.sh counter` is rejected — use `language/counter` or `zeus/counter`.

### Language (`examples/language/`)

| Name | What it does |
|---|---|
| `minimal` | Smallest `fn main()`. |
| `counter` | A struct and a `let mut` binding. |
| `http_server` | `std:http` unary RPC on `:8080`. |
| `solar` | Maya solar-system scene (orbit camera). |
| `studio` | Maya 3D toy with orbiting bodies. |
| `oob` | Out-of-bounds index; expected to trap. |

```
./run.sh http_server          # then Ctrl-C to stop
./run.sh oob                  # compile, run, confirm the trap
```

Equivalent without `run.sh`:

```
./bin/yugac --run examples/language/http_server.yuga
```

Golden programs under `packages/compiler/tests/golden/` (hello, fib, fizzbuzz,
…) are compiled by `make test`. They are fixtures, not demos.

### Zeus (`examples/zeus/`)

| App | What it is |
|---|---|
| `kit` | Every kit widget in isolation. Start here to see the component library. |
| `dashboard` | A small dashboard: stats, activity, dialog, signals. |
| `counter` | Full-stack: shared `#[proto]` contract, Yuga backend, Zeus UI on web / macOS / iOS / Android. |

```
./run.sh kit
./run.sh kit wasm32
./run.sh dashboard ios
```

Full-stack counter (backend on `:8080`, then a client):

```
./run.sh zeus/counter              # web — UI http://127.0.0.1:5173
./run.sh zeus/counter macos        # Cocoa
./run.sh zeus/counter ios          # Simulator (Xcode)
./run.sh zeus/counter android      # emulator (see below)
./run.sh zeus/counter backend      # API only
```

Android, from the repo root, on macOS with Homebrew:

```
./examples/zeus/counter/android/install.sh   # once; several GB, SDK licenses
./examples/zeus/counter/android/emu.sh       # leave this terminal open
./examples/zeus/counter/android/run.sh       # second terminal
```

Step-by-step: [examples/zeus/counter/android/guide.md](examples/zeus/counter/android/guide.md).

## Repository

```
packages/compiler/     yugac, yuga-lsp, std/, runtime/, tests
packages/zeus/         UI kit + Cocoa / iOS / Android / Canvas2D hosts
packages/tree-sitter-yuga/
packages/editors/      Zed extension
examples/language/     standalone .yuga programs
examples/zeus/         kit, dashboard, full-stack counter
docs/                  language, Zeus, C boundary
bin/yugac              compiler
bin/yuga-lsp           diagnostics, hover, go-to-def
```

Zed: [packages/editors/zed/README.md](packages/editors/zed/README.md).
