#!/usr/bin/env python3
# pyright: basic
# Smoke-test yuga-lsp: diagnostics, hover, go-to-definition, completion.
from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path
from typing import Any, Optional

ROOT = Path(__file__).resolve().parent.parent
REPO = ROOT.parent.parent
LSP = REPO / "bin" / "yuga-lsp"
Json = dict[str, Any]


def rpc(obj: Json) -> bytes:
    body = json.dumps(obj).encode()
    return b"Content-Length: %d\r\n\r\n" % len(body) + body


def read_message(buf: bytes) -> tuple[Optional[Json], bytes]:
    while True:
        sep = buf.find(b"\r\n\r\n")
        if sep < 0:
            return None, buf
        headers = buf[:sep].decode("utf-8", "replace")
        length = None
        for line in headers.split("\r\n"):
            if line.lower().startswith("content-length:"):
                length = int(line.split(":", 1)[1].strip())
        if length is None:
            return None, buf[sep + 4 :]
        start = sep + 4
        if len(buf) < start + length:
            return None, buf
        raw = buf[start : start + length]
        msg = json.loads(raw.decode())
        if not isinstance(msg, dict):
            return None, buf[start + length :]
        return msg, buf[start + length :]


def collect(out: bytes) -> list[Json]:
    messages: list[Json] = []
    rest = out
    while True:
        msg, rest = read_message(rest)
        if msg is None:
            break
        messages.append(msg)
    return messages


def fail(msg: str, extra: object = None) -> int:
    print("FAIL lsp:", msg)
    if extra is not None:
        print(extra)
    return 1


def hover_text(result: object) -> str:
    if not isinstance(result, dict):
        return ""
    contents = result.get("contents")
    if isinstance(contents, dict):
        value = contents.get("value", "")
        return value if isinstance(value, str) else str(value)
    if isinstance(contents, str):
        return contents
    return str(contents) if contents is not None else ""


def main() -> int:
    if not LSP.is_file():
        return fail("missing " + str(LSP))

    yuga_path = ROOT / "tests" / "tmp" / "lsp_smoke.yuga"
    uri = yuga_path.resolve().as_uri()
    bad = "fn main() {\n    let x = y\n}\n"
    good = (
        "/// Adds two integers.\n"
        "fn add(a: int, b: int) -> int {\n"
        "    return a + b\n"
        "}\n"
        "fn main() {\n"
        "    let x = add(1, 2)\n"
        "}\n"
    )

    rich = (
        'import "std:fmt"\n'
        "fn add(a: int, b: int) -> int {\n"
        "    return a + b\n"
        "}\n"
        "fn main() {\n"
        "    let x = add(1, 2)\n"
        "    fmt.println(x)\n"
        "    let f = || { 1 }\n"
        "    fmt.\n"
        "}\n"
    )
    fmt_uri = (ROOT / "std" / "fmt.yuga").resolve().as_uri()

    payload = (
        rpc({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"capabilities": {}}})
        + rpc({"jsonrpc": "2.0", "method": "initialized", "params": {}})
        + rpc(
            {
                "jsonrpc": "2.0",
                "method": "textDocument/didOpen",
                "params": {
                    "textDocument": {
                        "uri": uri,
                        "languageId": "yuga",
                        "version": 1,
                        "text": bad,
                    }
                },
            }
        )
        + rpc(
            {
                "jsonrpc": "2.0",
                "id": 2,
                "method": "textDocument/hover",
                "params": {"textDocument": {"uri": uri}, "position": {"line": 1, "character": 12}},
            }
        )
        + rpc(
            {
                "jsonrpc": "2.0",
                "method": "textDocument/didChange",
                "params": {
                    "textDocument": {"uri": uri, "version": 2},
                    "contentChanges": [{"text": good}],
                },
            }
        )
        + rpc(
            {
                "jsonrpc": "2.0",
                "id": 3,
                "method": "textDocument/hover",
                "params": {"textDocument": {"uri": uri}, "position": {"line": 5, "character": 12}},
            }
        )
        + rpc(
            {
                "jsonrpc": "2.0",
                "id": 4,
                "method": "textDocument/definition",
                "params": {"textDocument": {"uri": uri}, "position": {"line": 5, "character": 12}},
            }
        )
        + rpc(
            {
                "jsonrpc": "2.0",
                "id": 5,
                "method": "textDocument/hover",
                "params": {"textDocument": {"uri": uri}, "position": {"line": 2, "character": 11}},
            }
        )
        + rpc(
            {
                "jsonrpc": "2.0",
                "id": 7,
                "method": "textDocument/hover",
                "params": {"textDocument": {"uri": uri}, "position": {"line": 1, "character": 7}},
            }
        )
        + rpc(
            {
                "jsonrpc": "2.0",
                "method": "textDocument/didChange",
                "params": {
                    "textDocument": {"uri": uri, "version": 3},
                    "contentChanges": [{"text": rich}],
                },
            }
        )
        + rpc(
            {
                "jsonrpc": "2.0",
                "id": 8,
                "method": "textDocument/hover",
                "params": {"textDocument": {"uri": uri}, "position": {"line": 0, "character": 12}},
            }
        )
        + rpc(
            {
                "jsonrpc": "2.0",
                "id": 9,
                "method": "textDocument/hover",
                "params": {"textDocument": {"uri": uri}, "position": {"line": 6, "character": 4}},
            }
        )
        + rpc(
            {
                "jsonrpc": "2.0",
                "id": 10,
                "method": "textDocument/definition",
                "params": {"textDocument": {"uri": uri}, "position": {"line": 6, "character": 4}},
            }
        )
        + rpc(
            {
                "jsonrpc": "2.0",
                "id": 11,
                "method": "textDocument/hover",
                "params": {"textDocument": {"uri": uri}, "position": {"line": 7, "character": 12}},
            }
        )
        + rpc(
            {
                "jsonrpc": "2.0",
                "id": 12,
                "method": "textDocument/completion",
                "params": {"textDocument": {"uri": uri}, "position": {"line": 8, "character": 8}},
            }
        )
        + rpc(
            {
                "jsonrpc": "2.0",
                "id": 13,
                "method": "textDocument/hover",
                "params": {"textDocument": {"uri": fmt_uri}, "position": {"line": 23, "character": 3}},
            }
        )
        + rpc({"jsonrpc": "2.0", "id": 6, "method": "shutdown", "params": None})
        + rpc({"jsonrpc": "2.0", "method": "exit"})
    )
    proc = subprocess.Popen(
        [str(LSP)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    out, err = proc.communicate(payload, timeout=10)
    messages = collect(out)

    inits = [m for m in messages if m.get("id") == 1 and "result" in m]
    if not inits:
        return fail("no initialize result", (out[:500], err[:500]))
    caps = inits[0]["result"]
    if not isinstance(caps, dict):
        return fail("initialize result is not an object", caps)
    caps = caps.get("capabilities", {})
    if not isinstance(caps, dict):
        return fail("unexpected capabilities", caps)
    if caps.get("textDocumentSync") != 1:
        return fail("unexpected capabilities", caps)
    if caps.get("hoverProvider") is not True:
        return fail("missing hoverProvider", caps)
    if caps.get("definitionProvider") is not True:
        return fail("missing definitionProvider", caps)
    comp = caps.get("completionProvider")
    if not isinstance(comp, dict) or "." not in (comp.get("triggerCharacters") or []):
        return fail("missing completionProvider triggerCharacters", caps)

    diags_msgs = [m for m in messages if m.get("method") == "textDocument/publishDiagnostics"]
    if not diags_msgs:
        return fail("no publishDiagnostics", messages)
    params = diags_msgs[0].get("params", {})
    items = params.get("diagnostics", []) if isinstance(params, dict) else []
    if not isinstance(items, list) or not items:
        return fail("empty diagnostics for invalid source")
    first = items[0]
    if not isinstance(first, dict):
        return fail("diagnostic is not an object", first)
    rng = first.get("range", {})
    if not isinstance(rng, dict):
        return fail("missing range", rng)
    start = rng.get("start", {})
    end = rng.get("end", {})
    if not isinstance(start, dict) or not isinstance(end, dict):
        return fail("bad range", rng)
    if start.get("line") != 1 or start.get("character") != 12:
        return fail("unknown-ident start range", rng)
    if end.get("line") != 1 or end.get("character") != 13:
        return fail("unknown-ident end range (expected token span)", rng)

    hovers = {m.get("id"): m for m in messages if m.get("id") in (2, 3, 5) and "result" in m}
    if 3 not in hovers:
        return fail("no hover result for add()", messages)
    value = hover_text(hovers[3].get("result"))
    if "add" not in value or "fn(" not in value:
        return fail("hover on add() should show fn type", value)
    if "Adds two integers" not in value:
        return fail("hover on add() should include /// doc comment", value)

    if 5 not in hovers:
        return fail("no hover result for param a", messages)
    value_a = hover_text(hovers[5].get("result"))
    if "int" not in value_a:
        return fail("hover on param use should show int", value_a)

    defs = [m for m in messages if m.get("id") == 4 and "result" in m]
    if not defs:
        return fail("no definition result", messages)
    loc = defs[0].get("result")
    if not isinstance(loc, dict) or loc.get("uri") != uri:
        return fail("definition uri", loc)
    loc_range = loc.get("range") if isinstance(loc, dict) else None
    loc_start = loc_range.get("start") if isinstance(loc_range, dict) else None
    if not isinstance(loc_start, dict) or loc_start.get("line") != 1:
        return fail("definition should jump to fn add", loc)

    hovers.update({m.get("id"): m for m in messages if m.get("id") in (7, 8, 9, 11, 13) and "result" in m})
    if 7 not in hovers:
        return fail("no hover result for signature param a", messages)
    value_sig = hover_text(hovers[7].get("result"))
    if "a:" not in value_sig or "int" not in value_sig:
        return fail("hover on fn param name should show a: int", value_sig)

    if 8 not in hovers:
        return fail("no hover result for import", messages)
    value_imp = hover_text(hovers[8].get("result"))
    if "module" not in value_imp or "fmt" not in value_imp:
        return fail("hover on import should show module fmt", value_imp)

    if 9 not in hovers:
        return fail("no hover result for module prefix fmt", messages)
    value_fmt = hover_text(hovers[9].get("result"))
    if "module fmt" not in value_fmt:
        return fail("hover on fmt. should show module fmt", value_fmt)

    defs_fmt = [m for m in messages if m.get("id") == 10 and "result" in m]
    if not defs_fmt:
        return fail("no definition result for module fmt", messages)
    floc = defs_fmt[0].get("result")
    if not isinstance(floc, dict) or "fmt.yuga" not in str(floc.get("uri", "")):
        return fail("definition of fmt should jump to std/fmt.yuga", floc)

    if 11 not in hovers:
        return fail("no hover result for closure", messages)
    value_cl = hover_text(hovers[11].get("result"))
    if "fn(" not in value_cl:
        return fail("hover on || closure should show fn type", value_cl)

    comps = [m for m in messages if m.get("id") == 12 and "result" in m]
    if not comps:
        return fail("no completion result", messages)
    cres = comps[0].get("result")
    items_c = cres.get("items") if isinstance(cres, dict) else cres
    if not isinstance(items_c, list) or not items_c:
        return fail("empty completion after fmt.", cres)
    labels = [it.get("label") for it in items_c if isinstance(it, dict)]
    if "println" not in labels and "writeln" not in labels:
        return fail("completion after fmt. should list fmt functions", labels)

    if 13 not in hovers:
        return fail("no hover result in imported std/fmt.yuga", messages)
    value_w = hover_text(hovers[13].get("result"))
    if "write" not in value_w:
        return fail("hover in imported module should find fn write", value_w)

    print("ok   yuga-lsp (%d diagnostic(s), hover, definition, completion)" % len(items))
    return 0


if __name__ == "__main__":
    sys.exit(main())
