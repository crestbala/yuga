/* Zeus browser host. Canvas2D only — no WebGPU, no WebGL, no DOM widgets.
   One <canvas>, this file, and a .wasm built by `yugac --target wasm`.
   `window.attachZeus(canvas, opts)` boots from an ArrayBuffer; a canvas with
   `data-wasm` still auto-loads that URL (counter, kit). */
(function (root) {
  function attachZeus(canvas, opts) {
    const ctx = canvas.getContext("2d");
    if (!ctx) throw new Error("Canvas2D unavailable");
    opts = opts || {};

    const dpr = window.devicePixelRatio || 1;
    let layoutW = 1;
    let layoutH = 1;
    let sx = dpr;
    let sy = dpr;
    let mem = null;
    let stopped = false;
    let raf = 0;
    const ac = new AbortController();
    const signal = ac.signal;

  function sizeCanvas() {
    const cssW = canvas.clientWidth || window.innerWidth;
    const cssH = canvas.clientHeight || window.innerHeight;
    layoutW = Math.max(1, Math.round(cssW));
    layoutH = Math.max(1, Math.round(cssH));
    const bw = Math.max(1, Math.round(cssW * dpr));
    const bh = Math.max(1, Math.round(cssH * dpr));
    canvas.width = bw;
    canvas.height = bh;
    sx = bw / layoutW;
    sy = bh / layoutH;
    applyLayoutTransform();
    return { w: layoutW, h: layoutH };
  }

  function applyLayoutTransform() {
    ctx.setTransform(sx, 0, 0, sy, 0, 0);
  }

  /* Map a layout unit onto a device-pixel edge so fills and type stay sharp
     at fractional `devicePixelRatio` (1.25 / 1.5 / 2.25). */
  function snapX(n) {
    return Math.round(n * sx) / sx;
  }
  function snapY(n) {
    return Math.round(n * sy) / sy;
  }
  function snapRect(x, y, w, h) {
    const x0 = Math.round(x * sx);
    const y0 = Math.round(y * sy);
    const x1 = Math.round((x + w) * sx);
    const y1 = Math.round((y + h) * sy);
    return { x: x0 / sx, y: y0 / sy, w: (x1 - x0) / sx, h: (y1 - y0) / sy };
  }
  function layoutPoint(clientX, clientY) {
    const r = canvas.getBoundingClientRect();
    const rw = r.width || 1;
    const rh = r.height || 1;
    return {
      x: Math.round((clientX - r.left) * (layoutW / rw)),
      y: Math.round((clientY - r.top) * (layoutH / rh)),
    };
  }

  function rgb(c) {
    const n = c >>> 0;
    return "rgb(" + ((n >> 16) & 255) + "," + ((n >> 8) & 255) + "," + (n & 255) + ")";
  }
  function rgba(c, a) {
    const n = c >>> 0;
    return "rgba(" + ((n >> 16) & 255) + "," + ((n >> 8) & 255) + "," + (n & 255) + "," + (a / 255) + ")";
  }

  /* Match Cocoa: line box is ascent+descent, paint origin is the top of that
     box. Canvas `textBaseline = "top"` is the em square and `size+4` is taller
     than the glyphs, so labels sit high in buttons, chips, and avatars. */
  const fontFace = "system-ui, sans-serif";
  const fontMetrics = new Map();
  function setFont(px) {
    ctx.font = px + "px " + fontFace;
  }
  function lineBox(px) {
    let m = fontMetrics.get(px);
    if (m) return m;
    setFont(px);
    ctx.textBaseline = "alphabetic";
    const t = ctx.measureText("Hg");
    let ascent = t.fontBoundingBoxAscent;
    let descent = t.fontBoundingBoxDescent;
    if (!Number.isFinite(ascent) || !Number.isFinite(descent)) {
      const inkA = t.actualBoundingBoxAscent;
      const inkD = t.actualBoundingBoxDescent;
      if (Number.isFinite(inkA) && Number.isFinite(inkD)) {
        ascent = inkA;
        descent = inkD;
      } else {
        ascent = px * 0.8;
        descent = px * 0.25;
      }
    }
    ascent = Math.round(ascent);
    descent = Math.round(descent);
    m = { ascent: ascent, height: Math.max(1, ascent + descent) };
    fontMetrics.set(px, m);
    return m;
  }

  function roundRect(x, y, w, h, r) {
    const p = snapRect(x, y, w, h);
    x = p.x;
    y = p.y;
    w = p.w;
    h = p.h;
    r = Math.round(r * sx) / sx;
    if (r <= 0) {
      ctx.fillRect(x, y, w, h);
      return;
    }
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    ctx.beginPath();
    ctx.moveTo(x + r, y);
    ctx.arcTo(x + w, y, x + w, y + h, r);
    ctx.arcTo(x + w, y + h, x, y + h, r);
    ctx.arcTo(x, y + h, x, y, r);
    ctx.arcTo(x, y, x + w, y, r);
    ctx.closePath();
    ctx.fill();
  }

  function svgAttr(tag, name) {
    const m = new RegExp("(?:^|\\s)" + name + "=['\"]([^'\"]+)['\"]", "i").exec(tag || "");
    return m ? m[1] : null;
  }

  function svgPaint(v, cur) {
    if (!v || v === "none" || v === "transparent") return null;
    if (v === "currentColor" || v === "currentcolor") return cur;
    return v;
  }

  /* Path2D rejects some SVG number runs (`m0-6.5`). Trace onto ctx instead. */
  function traceSvgPath(d) {
    let i = 0;
    const n = d.length;
    function skip() {
      while (i < n && (d[i] === " " || d[i] === "\t" || d[i] === "\n" || d[i] === "\r" || d[i] === ",")) i++;
    }
    function num() {
      skip();
      if (i >= n) return null;
      let j = i;
      if (d[j] === "+" || d[j] === "-") j++;
      const s0 = j;
      while (j < n && d[j] >= "0" && d[j] <= "9") j++;
      if (j < n && d[j] === ".") {
        j++;
        while (j < n && d[j] >= "0" && d[j] <= "9") j++;
      }
      if (j < n && (d[j] === "e" || d[j] === "E")) {
        j++;
        if (d[j] === "+" || d[j] === "-") j++;
        while (j < n && d[j] >= "0" && d[j] <= "9") j++;
      }
      if (j === s0 && d[i] !== "." ) return null;
      const v = parseFloat(d.slice(i, j));
      i = j;
      return v;
    }
    ctx.beginPath();
    let cmd = "M";
    let px = 0, py = 0, sx = 0, sy = 0;
    skip();
    while (i < n) {
      skip();
      if (i >= n) break;
      const c = d[i];
      if ((c >= "A" && c <= "Z") || (c >= "a" && c <= "z")) {
        cmd = c;
        i++;
      }
      const rel = cmd >= "a";
      const op = cmd.toUpperCase();
      if (op === "Z") {
        ctx.closePath();
        px = sx;
        py = sy;
        continue;
      }
      if (op === "M" || op === "L") {
        const x = num();
        const y = num();
        if (x == null || y == null) break;
        const nx = rel ? px + x : x;
        const ny = rel ? py + y : y;
        if (op === "M") {
          ctx.moveTo(nx, ny);
          sx = nx;
          sy = ny;
          cmd = rel ? "l" : "L";
        } else {
          ctx.lineTo(nx, ny);
        }
        px = nx;
        py = ny;
        continue;
      }
      if (op === "H") {
        const x = num();
        if (x == null) break;
        px = rel ? px + x : x;
        ctx.lineTo(px, py);
        continue;
      }
      if (op === "V") {
        const y = num();
        if (y == null) break;
        py = rel ? py + y : y;
        ctx.lineTo(px, py);
        continue;
      }
      break;
    }
  }

  function u8() {
    return new Uint8Array(mem.buffer);
  }
  function cstr(ptr) {
    if (!ptr) return "";
    const b = u8();
    let e = ptr;
    while (b[e]) e++;
    return new TextDecoder().decode(b.subarray(ptr, e));
  }
  function bytes(ptr, len) {
    if (len <= 0) return "";
    return new TextDecoder().decode(u8().subarray(ptr, ptr + len));
  }

  const imports = {
    /* clang wasm32 libcalls (`__multi3`, …) land in `env` unless linked in C. */
    env: {
      __multi3: (out, a0, a1, b0, b1) => {
        const a =
          BigInt.asUintN(64, BigInt(a0)) + (BigInt.asUintN(64, BigInt(a1)) << 64n);
        const b =
          BigInt.asUintN(64, BigInt(b0)) + (BigInt.asUintN(64, BigInt(b1)) << 64n);
        const r = a * b;
        const view = new DataView(mem.buffer);
        view.setBigUint64(out, BigInt.asUintN(64, r), true);
        view.setBigUint64(out + 8, BigInt.asUintN(64, r >> 64n), true);
      },
    },
    zeus: {
      write: (p, n) => {
        const t = bytes(p, n);
        if (opts.onWrite) opts.onWrite(t);
        else if (t) console.log(t);
      },
      panic: (p, n) => {
        throw new Error(bytes(p, n) || cstr(p) || "zeus panic");
      },
      fill: (x, y, w, h, color, r) => {
        ctx.fillStyle = rgb(color);
        roundRect(x, y, w, h, r);
      },
      fill_a: (x, y, w, h, color, r, a) => {
        ctx.fillStyle = rgba(color, a);
        roundRect(x, y, w, h, r);
      },
      text: (x, y, ptr, color, font) => {
        const s = cstr(ptr);
        const px = font > 0 ? font : 13;
        const box = lineBox(px);
        ctx.fillStyle = rgb(color);
        setFont(px);
        ctx.textBaseline = "alphabetic";
        ctx.fillText(s, snapX(x), snapY(y + box.ascent));
      },
      measure: (ptr, px, wPtr, hPtr) => {
        const s = cstr(ptr);
        const size = px > 0 ? px : 13;
        const box = lineBox(size);
        setFont(size);
        const m = ctx.measureText(s);
        const view = new DataView(mem.buffer);
        view.setInt32(wPtr, Math.ceil(m.width), true);
        view.setInt32(hPtr, box.height, true);
      },
      save: () => ctx.save(),
      clip: (x, y, w, h) => {
        ctx.beginPath();
        const box = snapRect(x, y, w, h);
        ctx.rect(box.x, box.y, box.w, box.h);
        ctx.clip();
      },
      restore: () => ctx.restore(),
      svg: (x, y, w, h, ptr, color, alpha) => {
        const markup = cstr(ptr);
        if (!markup || w <= 0 || h <= 0) return;
        const svgTag = (/<svg\b([^>]*)>/i.exec(markup) || [])[1] || "";
        const vb = /viewBox=['"]([^'"]+)['"]/i.exec(markup);
        let vx = 0, vy = 0, vw = 24, vh = 24;
        if (vb) {
          const p = vb[1].trim().split(/[\s,]+/).map(Number);
          if (p.length >= 4) {
            vx = p[0];
            vy = p[1];
            vw = p[2] || 24;
            vh = p[3] || 24;
          }
        }
        const p = snapRect(x, y, w, h);
        const isx = p.w / (vw || 1);
        const isy = p.h / (vh || 1);
        ctx.save();
        ctx.translate(p.x, p.y);
        ctx.scale(isx, isy);
        ctx.translate(-vx, -vy);
        ctx.globalAlpha = (alpha >= 0 && alpha < 255) ? alpha / 255 : 1;
        const cur = rgb(color);
        const defFill = svgAttr(svgTag, "fill");
        const defStroke = svgAttr(svgTag, "stroke");
        const defSw = svgAttr(svgTag, "stroke-width");
        const defCap = svgAttr(svgTag, "stroke-linecap");
        const defJoin = svgAttr(svgTag, "stroke-linejoin");
        const re = /<path\b([^>]*)\/?\s*>/gi;
        let m;
        while ((m = re.exec(markup))) {
          const tag = m[1];
          const d = svgAttr(tag, "d");
          if (!d) continue;
          let fillV = svgAttr(tag, "fill");
          if (fillV == null) fillV = defFill;
          let strokeV = svgAttr(tag, "stroke");
          if (strokeV == null) strokeV = defStroke;
          const sw = svgAttr(tag, "stroke-width") || defSw;
          const cap = svgAttr(tag, "stroke-linecap") || defCap;
          const join = svgAttr(tag, "stroke-linejoin") || defJoin;
          let fillPaint = svgPaint(fillV, cur);
          let strokePaint = svgPaint(strokeV, cur);
          if (!fillPaint && !strokePaint) strokePaint = cur;
          traceSvgPath(d);
          if (fillPaint) {
            ctx.fillStyle = fillPaint;
            ctx.fill();
          }
          if (strokePaint) {
            ctx.strokeStyle = strokePaint;
            ctx.lineWidth = sw ? parseFloat(sw) : 1;
            if (cap) ctx.lineCap = cap;
            if (join) ctx.lineJoin = join;
            ctx.stroke();
          }
        }
        ctx.restore();
      },
      fetch_rpc: (pathPtr, pathLen, bodyPtr, bodyLen, outPtr, cap) => {
        try {
          const path = bytes(pathPtr, pathLen) || "/";
          const body =
            bodyLen > 0 ? u8().slice(bodyPtr, bodyPtr + bodyLen) : new Uint8Array(0);
          const xhr = new XMLHttpRequest();
          xhr.open("POST", path, false);
          /* Sync XHR forbids responseType = "arraybuffer". x-user-defined
             keeps each byte as a Latin-1 code unit in responseText. */
          xhr.overrideMimeType("text/plain; charset=x-user-defined");
          xhr.setRequestHeader("Content-Type", "application/grpc-web+proto");
          xhr.setRequestHeader("Accept", "application/grpc-web+proto");
          xhr.send(body);
          if (xhr.status !== 200) return 0;
          const t = xhr.responseText || "";
          const n = Math.min(t.length, Math.max(cap, 0));
          const dst = u8();
          for (let i = 0; i < n; i++) dst[outPtr + i] = t.charCodeAt(i) & 0xff;
          return n;
        } catch (e) {
          console.warn("fetch_rpc", e);
          return 0;
        }
      },
    },
  };

    function stop() {
      stopped = true;
      if (raf) cancelAnimationFrame(raf);
      ac.abort();
    }

    function boot(wasmBuffer) {
      return WebAssembly.instantiate(wasmBuffer, imports).then((r) => {
        if (stopped) return;
        const exp = r.instance.exports;
        mem = exp.memory;
        const sz = sizeCanvas();
        exp.zeus_start();
        if (exp.zeus_resize) exp.zeus_resize(sz.w, sz.h);
        function frame() {
          if (stopped) return;
          ctx.setTransform(1, 0, 0, 1, 0, 0);
          ctx.clearRect(0, 0, canvas.width, canvas.height);
          applyLayoutTransform();
          try {
            exp.zeus_paint();
          } catch (e) {
            console.error("zeus_paint", e);
            return;
          }
          raf = requestAnimationFrame(frame);
        }
        raf = requestAnimationFrame(frame);
        window.addEventListener(
          "resize",
          () => {
            const s = sizeCanvas();
            if (exp.zeus_resize) exp.zeus_resize(s.w, s.h);
          },
          { signal }
        );
        canvas.addEventListener(
          "pointerdown",
          (e) => {
            const p = layoutPoint(e.clientX, e.clientY);
            exp.zeus_pointer_down(p.x, p.y);
          },
          { signal }
        );
        canvas.addEventListener(
          "pointermove",
          (e) => {
            const p = layoutPoint(e.clientX, e.clientY);
            exp.zeus_pointer_move(p.x, p.y);
          },
          { signal }
        );
        canvas.addEventListener("pointerup", () => exp.zeus_pointer_up(), { signal });
        canvas.addEventListener(
          "wheel",
          (e) => {
            e.preventDefault();
            const p = layoutPoint(e.clientX, e.clientY);
            exp.zeus_scroll(p.x, p.y, e.deltaX, e.deltaY);
          },
          { passive: false, signal }
        );
        window.addEventListener(
          "keydown",
          (e) => {
            const tag = (document.activeElement && document.activeElement.tagName) || "";
            if (tag === "TEXTAREA" || tag === "INPUT" || tag === "SELECT") return;
            let mods = 0;
            if (e.shiftKey) mods |= 1;
            if (e.ctrlKey) mods |= 2;
            if (e.altKey) mods |= 4;
            if (e.metaKey) mods |= 8;
            let key = e.key.length === 1 ? e.key.charCodeAt(0) : 0;
            if (e.key === "Enter") key = 13;
            if (e.key === "Tab") key = 9;
            if (e.key === "Backspace") key = 8;
            if (e.key === "Escape") key = 27;
            if (e.key === "ArrowLeft") key = 1000;
            if (e.key === "ArrowRight") key = 1001;
            if (e.key === "ArrowUp") key = 1002;
            if (e.key === "ArrowDown") key = 1003;
            exp.zeus_key(key, mods);
            if (e.key === "Tab") e.preventDefault();
          },
          { signal }
        );
      });
    }

    return { boot: boot, stop: stop, sizeCanvas: sizeCanvas };
  }

  root.attachZeus = attachZeus;

  const canvas = document.getElementById("zeus");
  if (canvas && canvas.getAttribute("data-wasm")) {
    const host = attachZeus(canvas);
    fetch(canvas.getAttribute("data-wasm") || "app.wasm")
      .then((r) => r.arrayBuffer())
      .then((buf) => host.boot(buf))
      .catch((err) => {
        console.error(err);
        document.body.appendChild(document.createTextNode(String(err)));
      });
  }
})(typeof window !== "undefined" ? window : globalThis);
