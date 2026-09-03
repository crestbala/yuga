/* wasm.c — Canvas2D glue for Zeus (zeus/web). Not WebGPU.
 *
 * One paint path: Yuga `scene.paint` (`yuga_zeus_engine_paint`). This file
 * binds Canvas2D as the plat_* backend, registers a run loop that returns
 * (the browser owns rAF), and exports pointer/key entry points.
 */
#include "zeus_rt.h"
#include <stdint.h>
#include <string.h>

int main(void);

__attribute__((import_module("zeus"), import_name("fill")))
void zeus_js_fill(int32_t x, int32_t y, int32_t w, int32_t h, int32_t rgb, int32_t radius);

__attribute__((import_module("zeus"), import_name("fill_a")))
void zeus_js_fill_a(int32_t x, int32_t y, int32_t w, int32_t h, int32_t rgb, int32_t radius,
                    int32_t alpha);

__attribute__((import_module("zeus"), import_name("text")))
void zeus_js_text(int32_t x, int32_t y, const char *s, int32_t rgb, int32_t font);

__attribute__((import_module("zeus"), import_name("text_rot")))
void zeus_js_text_rot(int32_t x, int32_t y, const char *s, int32_t rgb, int32_t font,
                      int32_t deg);

__attribute__((import_module("zeus"), import_name("measure")))
void zeus_js_measure(const char *s, int32_t px, int32_t *w, int32_t *h);

__attribute__((import_module("zeus"), import_name("save")))
void zeus_js_save(void);

__attribute__((import_module("zeus"), import_name("clip")))
void zeus_js_clip(int32_t x, int32_t y, int32_t w, int32_t h);

__attribute__((import_module("zeus"), import_name("restore")))
void zeus_js_restore(void);

__attribute__((import_module("zeus"), import_name("svg")))
void zeus_js_svg(int32_t x, int32_t y, int32_t w, int32_t h, const char *markup, int32_t rgb,
                 int32_t alpha);

static void draw_fill(void *ctx, int64_t x, int64_t y, int64_t w, int64_t h, int64_t rgb,
                      int64_t radius) {
    (void)ctx;
    zeus_js_fill((int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, (int32_t)(rgb & 0xFFFFFF),
                 (int32_t)radius);
}

static void draw_fill_a(void *ctx, int64_t x, int64_t y, int64_t w, int64_t h, int64_t rgb,
                        int64_t radius, int64_t alpha) {
    (void)ctx;
    zeus_js_fill_a((int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, (int32_t)(rgb & 0xFFFFFF),
                   (int32_t)radius, (int32_t)alpha);
}

static void draw_text(void *ctx, int64_t x, int64_t y, const char *s, int64_t rgb, int64_t font) {
    (void)ctx;
    zeus_js_text((int32_t)x, (int32_t)y, s ? s : "", (int32_t)(rgb & 0xFFFFFF), (int32_t)font);
}

static void draw_text_rot(void *ctx, int64_t x, int64_t y, const char *s, int64_t rgb,
                          int64_t font, int64_t deg) {
    (void)ctx;
    zeus_js_text_rot((int32_t)x, (int32_t)y, s ? s : "", (int32_t)(rgb & 0xFFFFFF),
                     (int32_t)font, (int32_t)deg);
}

static void draw_save(void *ctx) {
    (void)ctx;
    zeus_js_save();
}

static void draw_clip(void *ctx, int64_t x, int64_t y, int64_t w, int64_t h) {
    (void)ctx;
    zeus_js_clip((int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h);
}

static void draw_restore(void *ctx) {
    (void)ctx;
    zeus_js_restore();
}

static void draw_svg(void *ctx, int64_t x, int64_t y, int64_t w, int64_t h, const char *markup,
                     int64_t rgb, int64_t alpha) {
    (void)ctx;
    zeus_js_svg((int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, markup ? markup : "",
                (int32_t)(rgb & 0xFFFFFF), (int32_t)alpha);
}

static void wasm_measure(const char *s, int64_t px, int64_t *w, int64_t *h) {
    int32_t tw = 0, th = 0;
    zeus_js_measure(s ? s : "", (int32_t)px, &tw, &th);
    if (w) *w = tw;
    if (h) *h = th;
}

static void wasm_run(void) {
    zeus_layout(zeus_window_width(), zeus_window_height());
}

static void wasm_redraw(void) {}

static void bind_canvas(void) {
    ZeusDraw d;
    memset(&d, 0, sizeof d);
    d.fill = draw_fill;
    d.fill_a = draw_fill_a;
    d.text = draw_text;
    d.text_rot = draw_text_rot;
    d.save = draw_save;
    d.clip = draw_clip;
    d.restore = draw_restore;
    d.svg = draw_svg;
    zeus_bind_draw(d);
}

__attribute__((export_name("zeus_start")))
void zeus_start(void) {
    zeus_set_platform(wasm_run, wasm_measure, wasm_redraw);
    bind_canvas();
    main();
}

__attribute__((export_name("zeus_resize")))
void zeus_resize(int32_t w, int32_t h) {
    zeus_set_window_size(w, h);
    zeus_layout(w, h);
}

__attribute__((export_name("zeus_paint")))
void zeus_wasm_paint(void) {
    zeus_layout(zeus_window_width(), zeus_window_height());
    zeus_step(1.f / 60.f);
    yuga_zeus_engine_paint();
}

__attribute__((export_name("zeus_pointer_down")))
void zeus_pointer_down(int32_t x, int32_t y) {
    zeus_handle_click(x, y);
}

__attribute__((export_name("zeus_pointer_move")))
void zeus_pointer_move(int32_t x, int32_t y) {
    zeus_handle_hover(x, y);
    zeus_handle_drag(x, y);
}

__attribute__((export_name("zeus_pointer_up")))
void zeus_pointer_up(void) {
    zeus_handle_mouseup();
}

static char wasm_cursor[64];

/* Copy the engine's CSS cursor name ("", "pointer", "text", …) into a
   static buffer the loader can read and hand to canvas.style.cursor. */
__attribute__((export_name("zeus_cursor_sync")))
const char *zeus_cursor_sync(void) {
    const char *name = zeus_cursor();
    size_t n = name ? strlen(name) : 0;
    if (n >= sizeof wasm_cursor) n = sizeof wasm_cursor - 1;
    memcpy(wasm_cursor, name ? name : "", n);
    wasm_cursor[n] = '\0';
    return wasm_cursor;
}

__attribute__((export_name("zeus_scroll")))
void zeus_scroll(int32_t x, int32_t y, int32_t dx, int32_t dy) {
    zeus_handle_scroll(x, y, dx, dy);
}

__attribute__((export_name("zeus_key_up")))
void zeus_key_up(int32_t key, int32_t mods) {
    zeus_handle_key_up((int)key, (int)mods);
}

__attribute__((export_name("zeus_key")))
void zeus_key(int32_t key, int32_t mods) {
    zeus_handle_key_ev((int)key, (int)mods);
}
