/* Editable Yuga playground: textarea + gRPC-Web Playground.Run (protobuf). */
const KW = new Set([
  "fn", "let", "mut", "struct", "import", "if", "else", "for", "while", "in",
  "return", "break", "continue", "match", "as", "true", "false",
]);
const SAMPLES = {
  ui: `import "std:zeus"
import "../../../../../packages/zeus-components/ui.yuga"
import "../../../../../packages/zeus-components/theme.yuga"

fn main() {
    let count = zeus.signal(5)
    let load = zeus.signal(42)
    zeus.App("Zeus", 560, 520, fn() {
        zeus.Column(background = theme.app(), padding = theme.page(), spacing = theme.section()) {
            zeus.Row(spacing = theme.space_sm(), align_items = zeus.align_center()) {
                zeus.Text("Counter", color = theme.text(), font = theme.type_heading()),
                ui.Chip(label = "Canvas2D", color = theme.accent()),
                ui.Badge(label = "live", color = theme.success())
            },
            ui.Alert(title = "Zeus", body = "Buttons write a signal. This is the same tree on web, macOS, iOS, and Android.", color = theme.accent()),
            ui.Card() {
                ui.CardHeader(title = "Count", subtitle = "Ghost and Prominent buttons; the bar is Progress."),
                zeus.Row(spacing = theme.space_sm(), align_items = zeus.align_center()) {
                    ui.Ghost(label = "-1", on_press = fn() => count.set(count.get() - 1)),
                    zeus.Text("{{count.get()}}", color = theme.text(), font = 16),
                    ui.Prominent(label = "+1", on_press = fn() => count.set(count.get() + 1))
                },
                ui.Progress(sig = count),
                ui.Divider(),
                zeus.Row(spacing = theme.space_md(), align_items = zeus.align_center()) {
                    zeus.Text("Load", color = theme.label(), font = 13),
                    ui.Progress(sig = load)
                }
            }
        }
    })
}
`,
  hello: `import "std:fmt"

fn main() {
    fmt.println("hello, yuga")
}
`,
  fib: `import "std:fmt"

fn fib(n: int) -> int {
    if n < 2 {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}

fn main() {
    fmt.println("fib(10) =", fib(10))
}
`,
  fizz: `import "std:fmt"

fn main() {
    for i in 1..31 {
        if i % 15 == 0 {
            fmt.println("fizzbuzz")
        } else if i % 3 == 0 {
            fmt.println("fizz")
        } else if i % 5 == 0 {
            fmt.println("buzz")
        } else {
            fmt.println(i)
        }
    }
}
`,
  struct: `import "std:fmt"

struct Counter {
    name: string,
    count: int,
}

fn increment(c: &mut Counter) {
    c.count += 1
}

fn main() {
    let mut c = Counter { name: "tick", count: 0 }
    increment(&mut c)
    increment(c)
    fmt.println(c.name, c.count)
}
`,
  signal: `import "std:zeus"

fn main() {
    let n = zeus.signal(0)
    zeus.col().pad(16).gap(8).child(
        zeus.text("Count").font(22),
        zeus.label("").bind(n).font(28),
        zeus.row().gap(8).child(
            zeus.button("-").on_click(|| { n.set(n.get() - 1) }),
            zeus.button("+").on_click(|| { n.set(n.get() + 1) }),
        ),
    ).run()
}
`,
};

const src = document.getElementById("src");
const hl = document.getElementById("hl");
const gutter = document.getElementById("gutter");
const logEl = document.getElementById("log");
const runBtn = document.getElementById("run");
const sample = document.getElementById("sample");
const preview = document.getElementById("preview");
const canvas = document.getElementById("zeus");
let zeusHost = null;

for (const k of Object.keys(SAMPLES)) {
  const o = document.createElement("option");
  o.value = k;
  o.textContent = k;
  sample.appendChild(o);
}
src.value = SAMPLES.ui;
sample.value = "ui";

function esc(s) {
  return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

function highlight(text) {
  let out = "";
  let i = 0;
  const n = text.length;
  function take(kind, end) {
    const raw = text.slice(i, end);
    out += kind ? '<span class="tok-' + kind + '">' + esc(raw) + "</span>" : esc(raw);
    i = end;
  }
  while (i < n) {
    const c = text[i];
    if (c === "/" && text[i + 1] === "/") {
      let j = i + 2;
      while (j < n && text[j] !== "\n") j++;
      take("cm", j);
      continue;
    }
    if (c === "/" && text[i + 1] === "*") {
      let j = i + 2;
      while (j + 1 < n && !(text[j] === "*" && text[j + 1] === "/")) j++;
      take("cm", Math.min(n, j + 2));
      continue;
    }
    if (c === '"') {
      let j = i + 1;
      while (j < n && text[j] !== '"') {
        if (text[j] === "\\") j++;
        j++;
      }
      take("str", Math.min(n, j + 1));
      continue;
    }
    if (/[0-9]/.test(c)) {
      let j = i;
      while (j < n && /[0-9a-fA-FxX.]/.test(text[j])) j++;
      take("num", j);
      continue;
    }
    if (/[A-Za-z_]/.test(c)) {
      let j = i + 1;
      while (j < n && /[A-Za-z0-9_]/.test(text[j])) j++;
      const word = text.slice(i, j);
      const next = text.slice(j).match(/^\s*\(/);
      let kind = "";
      if (KW.has(word)) kind = "kw";
      else if (next) kind = "fn";
      take(kind, j);
      continue;
    }
    take("", i + 1);
  }
  return out || " ";
}

function render() {
  const t = src.value;
  const st = src.scrollTop;
  const sl = src.scrollLeft;
  hl.textContent = "";
  hl.innerHTML = highlight(t);
  const lines = t.split("\n").length;
  let g = "";
  for (let i = 1; i <= lines; i++) g += i + "\n";
  gutter.textContent = g;
  src.scrollTop = st;
  src.scrollLeft = sl;
  syncScroll();
}

function syncScroll() {
  hl.scrollTop = src.scrollTop;
  hl.scrollLeft = src.scrollLeft;
  gutter.scrollTop = src.scrollTop;
}
src.addEventListener("scroll", syncScroll);
src.addEventListener("input", render);
window.addEventListener("resize", syncScroll);
sample.addEventListener("change", () => {
  src.value = SAMPLES[sample.value] || src.value;
  render();
  src.focus();
});

function concatBytes(parts) {
  let n = 0;
  for (const p of parts) n += p.length;
  const out = new Uint8Array(n);
  let o = 0;
  for (const p of parts) {
    out.set(p, o);
    o += p.length;
  }
  return out;
}

function varint(v) {
  const out = [];
  let u = v >>> 0;
  while (u > 127) {
    out.push((u & 127) | 128);
    u >>>= 7;
  }
  out.push(u & 127);
  return Uint8Array.from(out);
}

function encodeStringField(tag, s) {
  const bytes = new TextEncoder().encode(s);
  if (bytes.length === 0) return new Uint8Array(0);
  return concatBytes([varint(tag * 8 + 2), varint(bytes.length), bytes]);
}

function grpcFrame(proto) {
  const out = new Uint8Array(5 + proto.length);
  out[0] = 0;
  const n = proto.length;
  out[1] = (n >>> 24) & 255;
  out[2] = (n >>> 16) & 255;
  out[3] = (n >>> 8) & 255;
  out[4] = n & 255;
  out.set(proto, 5);
  return out;
}

function unframe(buf) {
  let i = 0;
  let proto = new Uint8Array(0);
  while (i + 5 <= buf.length) {
    const flags = buf[i];
    const len = ((buf[i + 1] << 24) | (buf[i + 2] << 16) | (buf[i + 3] << 8) | buf[i + 4]) >>> 0;
    i += 5;
    if (i + len > buf.length) break;
    const chunk = buf.subarray(i, i + len);
    i += len;
    if (flags & 0x80) break;
    if (flags & 1) continue;
    proto = chunk;
  }
  return proto;
}

function readVarint(buf, i) {
  let acc = 0;
  let shift = 0;
  while (i < buf.length) {
    const b = buf[i++];
    acc += (b & 127) * 2 ** shift;
    if (b < 128) return { val: acc, next: i };
    shift += 7;
  }
  return { val: acc, next: i };
}

function decodeRunResp(buf) {
  let i = 0;
  let ok = 0;
  let log = "";
  let kind = 0;
  let wasm = new Uint8Array(0);
  const dec = new TextDecoder();
  while (i < buf.length) {
    const t = readVarint(buf, i);
    i = t.next;
    const field = Math.floor(t.val / 8);
    const wt = t.val % 8;
    if (wt === 0) {
      const v = readVarint(buf, i);
      i = v.next;
      if (field === 1) ok = v.val;
      if (field === 3) kind = v.val;
    } else if (wt === 2) {
      const ln = readVarint(buf, i);
      i = ln.next;
      const slice = buf.subarray(i, i + ln.val);
      i += ln.val;
      if (field === 2) log = dec.decode(slice);
      if (field === 4) wasm = Uint8Array.from(slice);
    } else {
      break;
    }
  }
  return { ok, log, kind, wasm };
}

async function playgroundRun(source) {
  const proto = encodeStringField(1, source);
  const r = await fetch("/Playground/Run", {
    method: "POST",
    headers: {
      "Content-Type": "application/grpc-web+proto",
      Accept: "application/grpc-web+proto",
    },
    body: grpcFrame(proto),
  });
  if (!r.ok) {
    return { ok: 0, log: "HTTP " + r.status };
  }
  const raw = new Uint8Array(await r.arrayBuffer());
  return decodeRunResp(unframe(raw));
}

async function run() {
  runBtn.disabled = true;
  logEl.textContent = "compiling…\n";
  logEl.className = "";
  try {
    const res = await playgroundRun(src.value);
    if (zeusHost) {
      zeusHost.stop();
      zeusHost = null;
    }
    preview.classList.remove("on");
    if (res.ok && res.kind === 1 && res.wasm && res.wasm.length) {
      if (typeof window.attachZeus !== "function") {
        logEl.textContent = "missing attachZeus — /loader.js failed to load";
        logEl.className = "fail";
        return;
      }
      preview.classList.add("on");
      let out = res.log ? res.log + "\n" : "";
      zeusHost = window.attachZeus(canvas, {
        onWrite: (t) => {
          out += t;
          logEl.textContent = out;
        },
      });
      await zeusHost.boot(res.wasm);
      zeusHost.sizeCanvas();
      logEl.textContent = out || "Zeus UI running on the canvas.\n";
      logEl.className = "ok";
      return;
    }
    logEl.textContent = res.log || (res.ok ? "(no output)\n" : "compile failed");
    logEl.className = res.ok ? "ok" : "fail";
  } catch (e) {
    logEl.textContent = String(e && e.message ? e.message : e);
    logEl.className = "fail";
  } finally {
    runBtn.disabled = false;
  }
}

runBtn.addEventListener("click", run);
src.addEventListener("keydown", (e) => {
  if ((e.metaKey || e.ctrlKey) && e.key === "Enter") {
    e.preventDefault();
    run();
    return;
  }
  if (e.key === "Tab") {
    e.preventDefault();
    const s = src.selectionStart;
    src.value = src.value.slice(0, s) + "    " + src.value.slice(src.selectionEnd);
    src.selectionStart = src.selectionEnd = s + 4;
    render();
  }
});

render();
src.focus();
