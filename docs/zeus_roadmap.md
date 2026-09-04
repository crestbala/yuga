# Zeus / Yuga — production roadmap

Phase-by-phase plan for turning `std/zeus.yuga` + `std/kit.yuga` (+ `std/net`,
`std/http`, the C runtime) into a stack you can build real products on —
ecommerce tools, team chat (Slack/Teams-shaped), social apps.

Related: this file is the living plan; the sharp-edge catalog it grew from
(`zeus_kit_downsides.md`) and the API/spec docs were removed with the rest
of `docs/` — the roadmap below is written to stand alone, and each phase
re-states its own rationale. Status: tick phases off as they land, with
their exit criteria as the definition of done.

---

## 1. Where the stack stands today

| Layer | Today | Gap for real apps |
|---|---|---|
| UI core | Retained tree, per-prop thunks, arena recycling, headless layout/paint tests, DRAW goldens | list virtualization, scroll physics, IME/multiline, images |
| Reactivity | `Signal<int>` scalars, `For` list rebuilds, reactive props (`width`, `position`, `grow`, `show`, …) | floats/dates in signals; appends rebuild whole lists |
| Data plane | `#[proto]` codecs + unary gRPC-Web / h2c client **and** server, `Result<T>`, async `call_async` client, SSE + WebSocket streams (Phase 1b), bearer-token auth (`set_token` + `app.before`) | no bidirectional streaming, no TLS |
| Networking | `std/net`: blocking POSIX sockets + Phase 1 async transport (timers, non-blocking connect/poll/send, `http.call_async`/`sse_open`/`ws_open` steppers); wasm: `fetch_rpc` + async fetch slots | TLS, streaming bodies on wasm |
| Storage | `sys.read_file` / `write_file` (whole-file), env | KV/table store, structured persistence, fs tree, app-data paths |
| Media | inline SVG + raster `Image` (PNG / JPEG / WebP / GIF, host decode) | gradients |
| Text | one weight per host; kind 10 multiline + caret/selection; mac IME; wasm paste | rich text spans, emoji metrics |
| Server | single-threaded Yuga accept loop (HTTP/1.1 + h2c + one SSE/ws client per connection) | concurrency model for chat-class servers |
| Kit | ~50 shadcn widgets, palette tokens, goldens | monolith namespace, no DCE, fixed-pixel tuning |

## 2. Already fixed (do not re-plan)

Landmarks that are done and tested — the roadmap starts past them:

- Arena recycling: `For`/`refit` rebuilds free nodes, signals, handlers, effects (`zeus_for_recycle.yuga`).
- Lazy DatePicker: one 7×6 signal-driven grid, no rebuild on month/year change.
- Row shrink-before-wrap; `grow = 1` is elastic by default (CSS `flex: 1`), `shrink` prop overrides.
- Single following chart tooltip + cursor + band driven by one `act` signal.
- Palette tokens; no bare hex left in kit chrome.
- Headless layout tests + byte-exact DRAW goldens wired into `make test`.
- Declarative props: `position`/`left`/`top`/`right`/`bottom`/`z_index`, `width`/`height` (all widgets incl. `Svg`), `grow`/`shrink`, `show` (bool thunk; signal shows win).
- Language: default/optional trailing parameters (`press: fn() = __noop`), incl. the declaring-module binding fix.
- Clean-exit tests (no trap-style failures in `compile_pass`).

## 3. The data plane is gRPC — no REST/CRUD layer needed

`#[proto]` structs give you schema + codecs; `http.app().rpc(...)` /
`http.rpc_call` give unary calls over gRPC-Web (HTTP/1.1) and h2c, in pure
Yuga, client and server, native and wasm. That covers catalog, carts,
messages, presence payloads, sync — whatever the app is. Build on it:

1. **RPC is the only data API, and gRPC/proto the only wire format.** New
   endpoints = new proto structs + `app.rpc` / client calls. No JSON, no
   form-urlencoded, no ad-hoc route bodies, no other wire format — not even
   for interop. A `#[proto]` struct is the one way to describe data; a new
   endpoint is never a new format.
2. **No CRUD layer, ever.** No REST resources, no generic collection routes
   (`/users`, `/items/{id}`, PUT/DELETE conventions), no ORM-ish tables.
   State changes are named RPCs (`Cart.AddItem`, `Message.Send`), because
   they carry intent, versioning, and auth at the method level.
3. **What's missing is transport:**
   - **SSE** (server → client push; trivial on the existing accept loop) for
     notifications and coarse presence.
   - **WebSocket** (client + server, native and wasm) for chat/social realtime.
   - **Auth/session** convention at the RPC layer (token header, middleware fn).
4. **Streaming later, not now.** Unary RPC + SSE covers v1 products; true
   bidirectional streams can ride the WebSocket once it exists.

## 4. Remaining downsides, ranked

Severity is *for the target apps* (ecommerce / team chat / social), not for
demos. Phase column points into §6.

| # | Downside | Blocks | Effort | Phase |
|---|---|---|---|---|
| 1 | ~~No raster image primitive (draw op + host decode)~~ **Phase 2** | `zeus.Image`; hosts decode PNG/JPEG/WebP/GIF | — | 2 |
| 2 | ~~Single-line text, no IME on canvas hosts~~ **Phase 3** | chat compose exists; wasm IME is paste+composition, not a hidden field | — | 3 |
| 3 | `For` rebuilds the whole list per push (recycling yes, O(n) still) | message logs, activity feeds, 10k-row tables | medium (windowed rows) | 5 |
| 4 | No scroll physics / overscroll / nested scrollers | feed feel, long pages | medium | 5b |
| 5 | `Signal<int>` only (no float/date/bool scalar store) | smooth animation, timestamps, charts | small-med | 6b |
| 6 | Kit monolith, no dead-code elimination | web bundle size, componentization | medium | 7 |
| 7 | a11y = labels only; no focus ring, thin keyboard model | enterprise + compliance | ongoing | 7b |
| 8 | Fixed-pixel tuning; no density/font scaling | devices, accessibility | small | 7c |
| 9 | No TLS | anything beyond localhost | med | 6 |
| 10 | ~~No KV/persistence beyond whole-file read/write~~ **Phase 4** | `std:kv` file-backed; wasm is memory-only | — | 4 |
| 11 | Gallery wasm first paint ~3 min (counter wasm is instant) | kit catalog in the browser | med | follow-up |

Resolved by the landed phases: blocking sockets/async runtime (Phase 1), and no realtime transport (Phase 1b) — ws/SSE + the auth/session convention exist; what remains for chat-class servers is the §5.3 reactor (concurrent connections) and wasm ws via the browser's WebSocket.

## 5. Architecture decisions (the reasoning to preserve)

### 5.1 UI thread: single, forever
`arena.nodes`, `sigs`, `track`, focus, edit buffers are global and painting is
stateful; threading zeus would be a rewrite with no user-visible gain. The
industry answer (Android/UIKit/Swing) is: background I/O, completion **on the
one UI thread**. Keep it.

### 5.2 Async = one UI thread + Yuga queues (no language threads)
Yuga fns cannot run on arbitrary threads (globals, arenas). Shape:

```yuga
let c = http.client("127.0.0.1:8080")
c.call_async("Identity.Me", "", fn(resp) {   // gRPC I/O runs off the UI thread
    profile.set(decode_Me(resp))                // callback runs ON the UI thread
})
// Text("{{profile.get().name}}") updates. Nothing else moves.
```

The wire format is always proto over gRPC-Web — never JSON or an ad-hoc
format, so the callback hands back `#[proto]` bytes `decode_Me` already
understands.

- C side: **Phase 1 shipped queues in Yuga with a minimal C seam** — monotonic
  clock + blocking sleep (`yuga_rt.h`), non-blocking connect/poll/send
  (`net.c`), wasm async-fetch slots (JS XHR → per-frame drain). Every host
  frame calls `engine_layout`, which ticks the async world first (steppers,
  spawns, due timers) and lays out over whatever signals changed. Hosts
  sleep while idle and wake at the next deadline (`engine_next_ms`). A C
  task pool stays an option for truly blocking syscalls (DNS, TLS) when
  those appear.
- Yuga side: `spawn(fn)` + completion callbacks run everything; the
  `async fn` / `await` sugar (Phase 1c) re-enters the same pump — sequential
  code reads like JS, still on the one thread.
- Timers (`sleep`, `after(ms, fn)`, interval) ship with the same queue.

### 5.3 Server: reactor, not threads
One thread, non-blocking `net` (poll/select in C), resuming Yuga handlers on
readiness. Thousands of concurrent connections, zero locks. This is the chat
server story. Thread-per-connection in C *under* the API is a later option.

### 5.4 Concurrency primitives
- v1: **message passing only** (`channel<T>` between tasks and the UI loop) —
  composes with signals, keeps the no-shared-mutable-state property.
- No shared-memory threads in the language until a workload demands them
  (they need a Send-like discipline; the runtime is not thread-safe by design).

### 5.5 New primitives ride the existing pipeline
Any draw primitive (image, gradient, rounded-text) travels
`scene.yuga → platform.yuga → zeus_plat.c → zeus_rt.h → mac/iOS/wasm/android
hosts` + both canvas loaders. Budget that blast radius per primitive; add
decode (PNG/JPEG) in the hosts, never in Yuga.

## 6. Phases

Definition of done per phase = exit criteria, all verified by `make test`
(headless runs) unless noted. Tick boxes as phases land.

### Phase 1 — Async runtime, timers, non-blocking net  (foundation)
- [x] Async on the one UI thread, queues in Yuga: `std/async.yuga` (spawn / after / interval / cancel / per-frame steps), a minimal C seam (clock + sleep in `yuga_rt.h`), hosts drain per frame via `engine_layout`'s tick (mac + wasm; mac sleeps idle and wakes at `engine_next_ms`).
- [x] `after(ms, fn)` / `interval(ms, fn)` / `spawn` / `cancel` in std (feed signals; `async_timers.yuga`).
- [x] Non-blocking transport: `net.tcp_nb_connect` / `tcp_poll` / `tcp_send` / `tcp_so_error` — the UI never blocks on sockets.
- [x] First async-to-UI proof: `compile_pass` fake async op (`spawn`) completes, sets a signal, and a prop repaints through the headless pump (`zeus.pump`, no manual `engine_layout`).
- [x] `http.call_async(c, name, body, fn(resp))` gRPC-Web callback client (native sockets via per-frame steppers; wasm async fetch slots) — same `#[proto]` contract as `call`, no JSON layer.
- **Exit:** an app completes a gRPC round-trip while animating; headless test asserts completion → repaint without a manual `engine_layout`. **Green** (`zeus_async_rpc.yuga` runs a real child-process `Echo.Ping` server; wasm fetch path compiles, needs a browser to run).

### Phase 1b — Realtime transport
- [x] SSE on the existing HTTP/1.1 server (server → client push): `read_head` / `sse_start` / `sse_send` per connection, `http.sse_open` streaming client (events per `data:` line, UI-thread callback).
- [x] WebSocket client (native **and** wasm) and server: pure-Yuga RFC 6455 codec in `httpcore/ws.yuga` (base64, SHA-1, accept key, masked/unmasked frames), `http.ws_upgrade` / `ws_send_text` server push, `http.ws_open` streaming client — native TCP steppers, wasm via the browser's WebSocket (JS loader bridge, per-frame drain). Live demo: `examples/zeus/ws` (native tick server + wasm page).
- [x] Auth/session convention: bearer token header (`http.set_token`), parsed server-side into `req_token()`, `app.before(fn)` middleware hook (alias of `use`) that can `reject()` → handler skipped, grpc-status 16 (`http_auth.yuga`).
- **Exit:** a headless loop test streams N events over SSE/ws and the UI shows each (golden or probe assertions). **Green** (`zeus_stream.yuga`: 5 SSE events + 5 ws messages → signal-fed label `events: 10`, repaint probes, child-process servers).

### Phase 1c — JS-shaped `async fn` / `await` (language sugar)
Callback APIs (`call_async`) are the runtime; this phase adds the syntax the
apps read and write. `await` never blocks and never threads — it re-enters
the one-thread async pump until the awaited mailbox fills (§5.2 model), so
sequential code reads like JS and still never touches a socket on the UI
thread.

- [x] Grammar + sema: `async fn name(...)`, `await expr` (contextual keywords — parser desugars `await e` to `async.await_value(e)`; typecheck rejects `await` outside an `async fn` body or inside a closure, like JS).
- [x] Awaitable surface: `Future<T>` mailboxes in `std:async` (`future` / `future_str` / `resolve` / `await_value` — Copy `T`; cells in `yuga_rt.h`); `http.async_call(c, name, body)` is the awaitable gRPC-Web call (`Future<string>`); awaiting pumps timers/spawns/socket steppers until `resolve`, then returns the value.
- [x] Demo + tests: `examples/zeus/counter/macos/app.yuga` is `async fn main` with two awaited RPCs (`let raw = await c.async_call(...)`) feeding the App; `compile_pass/async_await.yuga` (sequential awaits, sync call of an async fn, pre-resolved future, `Future<int>` / `bool` / `float` / Copy struct / `[]int`); `compile_pass/async_await_stress.yuga` (for/while/if/match/continue/break around awaits); `compile_fail/async_await_ctx.yuga`, `compile_fail/async_future_not_copy.yuga`.
- **Exit:** sequential awaited values flow in order through the pump and repaint; awaits outside `async fn` are compile errors. **Green** — caveat: awaiting re-enters the pump on the UI thread, so an await inside an event handler pauses host repaint until it returns; init/startup awaits (the counter App) and headless flows are the sweet spot, callbacks stay the reactive path.
- [x] Follow-up: `async`/`await` highlighting landed in the tree-sitter grammar + Zed/VSCode grammars (violet scopes in VSCode).
- [x] Follow-ups: widen `Future<T>` beyond string payloads; loop-and-branch-heavy bodies are already fine (no CPS split in this design) but want a stress test.

### Phase 2 — Image primitive
- [x] New draw op kind (raster) through scene → platform → C trampoline → hosts + canvas loaders.
- [x] Host decode of PNG / JPEG / WebP / GIF (ImageIO, Canvas `Image`, BitmapFactory); `zeus.Image(...)` widget + `SvgProps`-style props (`width` / `height` / `radius` clip).
- [x] Network image + cache by `src` (path, `data:` URI, or `http(s):` URL; http loads off the UI thread, next frame paints).
- **Exit:** a golden fixture with an image draw op; avatars/product shots render in gallery. **Green** (`draw_golden/golden_image`; gallery People + Catalog).
- [ ] Follow-up (not Vite): `examples/zeus/gallery` wasm takes ~3 minutes to fully paint the UI in the browser. `app.wasm` (~428K) and `loader.js` arrive in milliseconds on Vite and on a plain static host; counter wasm loads immediately. The stall is `zeus_start` / first layout of the kit catalog (all three tabs via `show_eq`, SVG-heavy widgets). Cocoa gallery is fine. Check later: construct only the visible tab without losing `kit.Page` section gaps; idle rAF like mac (`engine_next_ms`); skip layout of `show_eq`-hidden subtrees on wasm.

### Phase 3 — Multiline text + IME
- [x] Multiline text input (kind 10 `multiline` / `wrap`): line breaks, caret, click-to-caret, arrow/home/end, shift-select.
- [x] IME composition on mac (`NSTextInputClient`); canvas/wasm ASCII + paste + compositionend.
- [x] Kit: `TextArea` widget (shadcn-style chrome reusing `Input` tokens).
- **Exit:** a chat-compose headless test (type, edit, bind signal) + golden. **Green** (`zeus_textarea.yuga`, `draw_golden/golden_textarea`).

### Phase 4 — KV persistence
- [x] `std/kv.yuga` over the `yuga_sys_*` seam: `get/set/delete/list`, file-backed, atomic-ish (`write` + `rename`).
- [x] App-data path helper (`kv.data_dir`, `~/Library/Application Support/<app>`; wasm: empty → in-memory).
- **Exit:** a compile_pass test that round-trips rows across two "processes"
  (reopen), plus gallery draft/autosave demo. **Green** (`kv_roundtrip.yuga`; gallery Forms draft).

### Phase 5 — Virtualized lists + Virtualized table + scroll
- [ ] Windowed `For` (render visible rows ± margin; recycle beyond the window).
- [ ] Append/insert row ops that do not rebuild the whole list.
- [ ] Scroll physics: momentum, overscroll clamp, `scroll_to` (signal-driven).
- **Exit:** headless test: 10k-row feed stays under a node/effect budget while
  scrolling (`arena_nodes()` bounded), items paint on demand.

### Phase 6 — TLS + float/date signals
- [ ] TLS on `net` (SecureTransport/OpenSSL; wasm keeps browser TLS).
- [ ] `Signal<float>` / timestamp scalar store (sigs backend extends beyond `[]int`).
- **Exit:** `https` client test (skipped when offline), float-driven animation prop test.

### Phase 7 — Kit & a11y growth discipline
- [ ] Kit namespace pass or per-widget opt-in (DCE) before the monolith doubles.
- [ ] Focus ring painting + visible tab order on native hosts.
- [ ] Density/font-scale tokens (kit reads a scale signal, not constants).
- **Exit:** gallery unchanged visually at scale 1.0 (golden); a11y probe test asserts roles/focus order.

### Phase 8 — Revisit only if demanded
- [ ] Language threads + Send discipline + channels (CPU-bound workloads).
- [ ] Rich text spans (bold/italic/inline color) once text is measured in-tree.

---

## 7. Cross-cutting notes

- **Validation is the moat.** Every phase ships a headless test; the DRAW
  goldens make visual regressions cheap to catch. Keep adding probe-style
  tests (`engine_find_text`, `node_x/y`) per phase.
- **Wasm parity** per phase where possible (net: `fetch`; ws exists in
  browsers; images decode via canvas) — note deviations in the phase notes.
- **Don't gold-plate.** Unary RPC + SSE + KV + images + multiline input
  already clears "useful ecommerce tool" and most social-app shapes; ws +
  virtualized lists clear chat. Ship those before streaming/bidi/rich text.
- **Tracking:** tick a phase only when its exit criteria run green in
  `make test` on this branch.
