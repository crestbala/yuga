/* zeus_plat.c — Cocoa/headless seam for the Yuga zeus library.
 *
 * std/zeus.yuga owns the node arena, layout, paint, and hit-test. This file
 * is the other side of that seam: window title/size, text measure, present,
 * and the zeus_* entry points the Cocoa/iOS/web hosts already call. Keyboard chords live
 * in zeus_key.c; focus chain and Tab walk the Yuga node tree.
 */
#include "zeus_rt.h"
#include "zeus_key.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern yuga_vec yuga_arena_sigs;
/* Rebuild-scope ownership (arena.yuga): while `scope_node` is non-zero, a
   signal allocation is recorded against it so teardown can recycle the id. */
extern int64_t yuga_arena_scope_node;
extern yuga_vec yuga_arena_rec_sid;
extern yuga_vec yuga_arena_rec_owner;
void yuga_arena_ensure(void);
void yuga_arena_store_sig(int64_t id, int64_t value);
void yuga_track_note_read(int64_t sid);
void yuga_track_notify(int64_t sid);

typedef struct {
    void *p;
    size_t n;
    int64_t gen;
} ZeusCell;

static ZeusCell *g_cells;
static size_t g_ncells;
static size_t g_ccells;

static void cell_grow(int64_t id) {
    size_t need;
    if (id < 0) return;
    need = (size_t)id + 1;
    if (need <= g_ncells) return;
    if (need > g_ccells) {
        size_t nc = g_ccells ? g_ccells : 16;
        ZeusCell *next;
        while (nc < need) nc *= 2;
        next = (ZeusCell *)realloc(g_cells, nc * sizeof(ZeusCell));
        if (!next) {
            fprintf(stderr, "zeus: out of memory\n");
            abort();
        }
        memset(next + g_ccells, 0, (nc - g_ccells) * sizeof(ZeusCell));
        g_cells = next;
        g_ccells = nc;
    }
    g_ncells = need;
}

void yuga_zeus_sig_bind(int64_t id, const void *src, int64_t n) {
    ZeusCell *c;
    size_t nn;
    if (id < 0 || n < 0) return;
    cell_grow(id);
    c = &g_cells[id];
    nn = (size_t)n;
    if (c->n != nn) {
        free(c->p);
        c->p = nn ? malloc(nn) : NULL;
        if (nn && !c->p) {
            fprintf(stderr, "zeus: out of memory\n");
            abort();
        }
        c->n = nn;
    }
    if (nn && src && c->p) memcpy(c->p, src, nn);
    c->gen++;
}

void yuga_zeus_sig_load(int64_t id, void *dst, int64_t n) {
    size_t nn, k;
    if (!dst || n <= 0) return;
    nn = (size_t)n;
    memset(dst, 0, nn);
    if (id < 0 || (size_t)id >= g_ncells) return;
    if (!g_cells[id].p) return;
    k = g_cells[id].n < nn ? g_cells[id].n : nn;
    memcpy(dst, g_cells[id].p, k);
}

int64_t yuga_zeus_sig_changed(int64_t id, const void *src, int64_t n) {
    if (id < 0 || !src || n <= 0) return 1;
    if ((size_t)id >= g_ncells || !g_cells[id].p || g_cells[id].n != (size_t)n)
        return 1;
    return memcmp(g_cells[id].p, src, (size_t)n) != 0;
}

int64_t yuga_zeus_sig_gen(int64_t id) {
    if (id < 0 || (size_t)id >= g_ncells) return 0;
    return g_cells[id].gen;
}

Signal yuga_zeus_signal(int64_t value) {
    Signal s;
    yuga_arena_ensure();
    yuga_vec_push(&yuga_arena_sigs, &value, sizeof(value), __FILE__, __LINE__);
    s.id = yuga_arena_sigs.len - 1;
    yuga_zeus_sig_bind(s.id, &value, (int64_t)sizeof(value));
    return s;
}

int64_t yuga_zeus_get(Signal sig) {
    int64_t v = 0;
    yuga_arena_ensure();
    yuga_track_note_read(sig.id);
    yuga_zeus_sig_load(sig.id, &v, (int64_t)sizeof(v));
    return v;
}

void yuga_zeus_set(Signal sig, int64_t value) {
    yuga_arena_ensure();
    yuga_arena_store_sig(sig.id, value);
}

void yuga_platform_plat_sig_bind_int(int64_t id, int64_t value) {
    yuga_zeus_sig_bind(id, &value, (int64_t)sizeof(value));
}

int64_t yuga_platform_plat_sig_gen(int64_t id) {
    return yuga_zeus_sig_gen(id);
}

/* Freed signal slots, recycled by the allocators below. An id is freed only
   when nothing reachable from the live tree can reference it (subtree
   teardown), so recycling never aliases a live signal. */
static int64_t *g_free_sigs;
static size_t g_nfree_sigs;
static size_t g_cfree_sigs;

static void free_sig_push(int64_t id) {
    if (g_nfree_sigs == g_cfree_sigs) {
        size_t nc = g_cfree_sigs ? g_cfree_sigs * 2 : 64;
        int64_t *next = (int64_t *)realloc(g_free_sigs, nc * sizeof(int64_t));
        if (!next) {
            fprintf(stderr, "zeus: out of memory\n");
            abort();
        }
        g_free_sigs = next;
        g_cfree_sigs = nc;
    }
    g_free_sigs[g_nfree_sigs++] = id;
}

static int64_t free_sig_pop(void) {
    if (g_nfree_sigs <= 0) return -1;
    return g_free_sigs[--g_nfree_sigs];
}

/* Reserve a slot: recycle a freed id or append a fresh one (mirror 0).
   Allocations made while a rebuild scope is open are recorded against the
   scope node so the yuga teardown can free them. */
static int64_t sig_slot_zero(void) {
    int64_t id, zero = 0;
    yuga_arena_ensure();
    id = free_sig_pop();
    if (id >= 0) {
        ((int64_t *)yuga_arena_sigs.ptr)[id] = 0;
    } else {
        yuga_vec_push(&yuga_arena_sigs, &zero, sizeof(int64_t), __FILE__, __LINE__);
        id = yuga_arena_sigs.len - 1;
    }
    if (yuga_arena_scope_node > 0) {
        yuga_vec_push(&yuga_arena_rec_sid, &id, sizeof(int64_t), __FILE__, __LINE__);
        yuga_vec_push(&yuga_arena_rec_owner, &yuga_arena_scope_node, sizeof(int64_t),
                      __FILE__, __LINE__);
    }
    return id;
}

int64_t yuga_zeus_sig_alloc_int(int64_t value) {
    int64_t id = sig_slot_zero();
    ((int64_t *)yuga_arena_sigs.ptr)[id] = value;
    yuga_zeus_sig_bind(id, &value, sizeof(value));
    return id;
}

int64_t yuga_zeus_sig_alloc_zero(void) {
    return sig_slot_zero();
}

void yuga_zeus_sig_free(int64_t id) {
    if (id <= 0 || (size_t)id >= g_ncells) return;
    if (!g_cells[id].p && g_cells[id].n == 0 && g_cells[id].gen == 0) return;
    if (id < yuga_arena_sigs.len) ((int64_t *)yuga_arena_sigs.ptr)[id] = 0;
    free(g_cells[id].p);
    g_cells[id].p = NULL;
    g_cells[id].n = 0;
    g_cells[id].gen = 0;
    free_sig_push(id);
}

void yuga_platform_plat_sig_free(int64_t id) {
    yuga_zeus_sig_free(id);
}

static void (*plat_run)(void);
static void (*plat_measure)(const char *s, int64_t px, int64_t *w, int64_t *h);
static void (*plat_redraw)(void);

static char *win_title;
static int64_t win_w = 640, win_h = 480;

static void *paint_ctx;
static ZeusDraw paint_draw;
static int have_draw;

static char *dup_ys(yuga_str s) {
    size_t n = s.len < 0 ? 0 : (size_t)s.len;
    char *p = (char *)malloc(n + 1);
    if (!p) {
        fprintf(stderr, "zeus: out of memory\n");
        abort();
    }
    if (s.ptr && n) memcpy(p, s.ptr, n);
    p[n] = '\0';
    return p;
}

static void measure_default(const char *s, int64_t px, int64_t *w, int64_t *h) {
    size_t n = s ? strlen(s) : 0;
    if (px < 8) px = 8;
    *w = (int64_t)n * (px * 6 / 10);
    *h = px + 4;
}

void zeus_set_platform(void (*run)(void),
                      void (*measure)(const char *s, int64_t px, int64_t *w, int64_t *h),
                      void (*redraw)(void)) {
    plat_run = run;
    plat_measure = measure;
    plat_redraw = redraw;
}

void yuga_zeus_plat_run(void) {
    if (plat_run) plat_run();
}

int64_t yuga_zeus_plat_headless(void) {
    if (getenv("ZEUS_HEADLESS") || !plat_run) return 1;
    return 0;
}

void yuga_zeus_plat_fill(int64_t x, int64_t y, int64_t w, int64_t h, int64_t rgb,
                        int64_t radius) {
    if (!have_draw || !paint_draw.fill) return;
    paint_draw.fill(paint_ctx, x, y, w, h, rgb & 0xFFFFFF, radius);
}

void yuga_zeus_plat_fill_a(int64_t x, int64_t y, int64_t w, int64_t h, int64_t rgb,
                          int64_t radius, int64_t alpha) {
    if (!have_draw) return;
    if (paint_draw.fill_a)
        paint_draw.fill_a(paint_ctx, x, y, w, h, rgb & 0xFFFFFF, radius, alpha);
    else if (paint_draw.fill)
        paint_draw.fill(paint_ctx, x, y, w, h, rgb & 0xFFFFFF, radius);
}

void yuga_zeus_plat_text(int64_t x, int64_t y, yuga_str s, int64_t rgb, int64_t font) {
    char *p;
    if (!have_draw || !paint_draw.text) return;
    p = dup_ys(s);
    paint_draw.text(paint_ctx, x, y, p, rgb & 0xFFFFFF, font);
    free(p);
}

void yuga_zeus_plat_text_rot(int64_t x, int64_t y, yuga_str s, int64_t rgb, int64_t font,
                             int64_t deg) {
    char *p;
    if (!have_draw) return;
    if (!paint_draw.text_rot) {
        if (paint_draw.text) yuga_zeus_plat_text(x, y, s, rgb, font);
        return;
    }
    p = dup_ys(s);
    paint_draw.text_rot(paint_ctx, x, y, p, rgb & 0xFFFFFF, font, deg);
    free(p);
}

void yuga_zeus_plat_text_int(int64_t x, int64_t y, int64_t v, int64_t rgb, int64_t font) {
    char buf[32];
    if (!have_draw || !paint_draw.text) return;
    snprintf(buf, sizeof buf, "%lld", (long long)v);
    paint_draw.text(paint_ctx, x, y, buf, rgb & 0xFFFFFF, font);
}

void yuga_zeus_plat_svg(int64_t x, int64_t y, int64_t w, int64_t h, yuga_str markup,
                       int64_t rgb, int64_t alpha) {
    char *p;
    if (!have_draw || !paint_draw.svg) return;
    p = dup_ys(markup);
    paint_draw.svg(paint_ctx, x, y, w, h, p, rgb & 0xFFFFFF, alpha);
    free(p);
}

void yuga_zeus_plat_save(void) {
    if (have_draw && paint_draw.save) paint_draw.save(paint_ctx);
}

void yuga_zeus_plat_clip(int64_t x, int64_t y, int64_t w, int64_t h) {
    if (have_draw && paint_draw.clip) paint_draw.clip(paint_ctx, x, y, w, h);
}

void yuga_zeus_plat_restore(void) {
    if (have_draw && paint_draw.restore) paint_draw.restore(paint_ctx);
}

void yuga_zeus_plat_measure(yuga_str s, int64_t px, int64_t *w, int64_t *h) {
    char *p = dup_ys(s);
    int64_t tw = 0, th = 0;
    if (plat_measure) plat_measure(p, px, &tw, &th);
    else measure_default(p, px, &tw, &th);
    if (w) *w = tw;
    if (h) *h = th;
    free(p);
}

void yuga_zeus_plat_measure_int(int64_t v, int64_t px, int64_t *w, int64_t *h) {
    char buf[32];
    snprintf(buf, sizeof buf, "%lld", (long long)v);
    if (plat_measure) plat_measure(buf, px, w, h);
    else measure_default(buf, px, w, h);
}

static void measure_span(const char *s, int64_t n, int64_t px, int64_t *w, int64_t *h) {
    char stack[256];
    char *tmp;
    if (n < 0) n = 0;
    if ((size_t)n + 1 <= sizeof stack) {
        tmp = stack;
    } else {
        tmp = (char *)malloc((size_t)n + 1);
        if (!tmp) abort();
    }
    if (n) memcpy(tmp, s, (size_t)n);
    tmp[n] = '\0';
    if (plat_measure) plat_measure(tmp, px, w, h);
    else measure_default(tmp, px, w, h);
    if (tmp != stack) free(tmp);
}

static int64_t wrap_line_h(int64_t px) {
    int64_t w = 0, h = 0;
    measure_span("Mg", 2, px, &w, &h);
    if (h < 1) h = px + 4;
    return h;
}

static void wrap_text(const char *s, int64_t n, int64_t px, int64_t max_w, int paint,
                      int64_t x0, int64_t y0, int64_t rgb, int64_t *out_w, int64_t *out_h) {
    int64_t i = 0, line_w = 0, maxlw = 0, lines = 0, y = y0;
    int64_t lh, sw = 0, sh = 0;
    if (!s) s = "";
    if (n < 0) n = 0;
    if (max_w < 1) max_w = 1;
    lh = wrap_line_h(px);
    measure_span(" ", 1, px, &sw, &sh);
    while (i < n) {
        int64_t j, ww = 0, wh = 0;
        if (s[i] == '\n') {
            if (line_w > maxlw) maxlw = line_w;
            line_w = 0;
            lines++;
            y += lh;
            i++;
            continue;
        }
        if (s[i] == ' ' || s[i] == '\t') {
            if (line_w > 0) {
                if (line_w + sw > max_w) {
                    if (line_w > maxlw) maxlw = line_w;
                    line_w = 0;
                    lines++;
                    y += lh;
                } else {
                    if (paint && have_draw && paint_draw.text)
                        paint_draw.text(paint_ctx, x0 + line_w, y, " ", rgb & 0xFFFFFF, px);
                    line_w += sw;
                }
            }
            i++;
            continue;
        }
        j = i;
        while (j < n && s[j] != ' ' && s[j] != '\t' && s[j] != '\n') j++;
        measure_span(s + i, j - i, px, &ww, &wh);
        if (line_w > 0 && line_w + ww > max_w) {
            if (line_w > maxlw) maxlw = line_w;
            line_w = 0;
            lines++;
            y += lh;
        }
        if (paint && have_draw && paint_draw.text) {
            char stack[256];
            char *tmp;
            int64_t wn = j - i;
            if (wn < 0) wn = 0;
            if ((size_t)wn + 1 <= sizeof stack) tmp = stack;
            else {
                tmp = (char *)malloc((size_t)wn + 1);
                if (!tmp) abort();
            }
            if (wn) memcpy(tmp, s + i, (size_t)wn);
            tmp[wn] = '\0';
            paint_draw.text(paint_ctx, x0 + line_w, y, tmp, rgb & 0xFFFFFF, px);
            if (tmp != stack) free(tmp);
        }
        line_w += ww;
        i = j;
    }
    if (n > 0) {
        if (line_w > maxlw) maxlw = line_w;
        lines++;
    }
    if (out_w) *out_w = maxlw;
    if (out_h) *out_h = lines * lh;
}

void yuga_zeus_plat_measure_wrap(yuga_str s, int64_t px, int64_t max_w, int64_t *w, int64_t *h) {
    wrap_text(s.ptr, s.len, px, max_w, 0, 0, 0, 0, w, h);
}

void yuga_zeus_plat_text_wrap(int64_t x, int64_t y, yuga_str s, int64_t rgb, int64_t font,
                             int64_t max_w) {
    wrap_text(s.ptr, s.len, font, max_w, 1, x, y, rgb, NULL, NULL);
}

void yuga_zeus_plat_set_window(yuga_str title, int64_t width, int64_t height) {
    free(win_title);
    win_title = dup_ys(title);
#if defined(YUGA_IOS) || defined(YUGA_ANDROID)
    /* Phone hosts layout to the view every frame. Page(560, 520) must not
       replace that width or the UI is clipped on the right. */
    (void)width;
    (void)height;
#else
    if (width > 0) win_w = width;
    if (height > 0) win_h = height;
#endif
}

#ifdef __wasm32__
__attribute__((import_module("zeus"), import_name("view_w")))
int32_t zeus_js_view_w(void);
__attribute__((import_module("zeus"), import_name("view_h")))
int32_t zeus_js_view_h(void);
#endif

int64_t yuga_zeus_plat_view_width(void) {
#ifdef __wasm32__
    int32_t w = zeus_js_view_w();
    if (w > 0) return (int64_t)w;
#endif
    return win_w;
}

int64_t yuga_zeus_plat_view_height(void) {
#ifdef __wasm32__
    int32_t h = zeus_js_view_h();
    if (h > 0) return (int64_t)h;
#endif
    return win_h;
}

void zeus_set_window_size(int64_t width, int64_t height) {
    if (width > 0) win_w = width;
    if (height > 0) win_h = height;
}

static int64_t g_inset_t, g_inset_r, g_inset_b, g_inset_l;

void zeus_set_insets(int64_t top, int64_t right, int64_t bottom, int64_t left) {
    g_inset_t = top < 0 ? 0 : top;
    g_inset_r = right < 0 ? 0 : right;
    g_inset_b = bottom < 0 ? 0 : bottom;
    g_inset_l = left < 0 ? 0 : left;
}

int64_t yuga_zeus_plat_overlay_scroll(void) {
#if defined(YUGA_IOS) || defined(YUGA_ANDROID)
    return 1;
#else
    return 0;
#endif
}

int64_t yuga_zeus_plat_inset_top(void) { return g_inset_t; }
int64_t yuga_zeus_plat_inset_right(void) { return g_inset_r; }
int64_t yuga_zeus_plat_inset_bottom(void) { return g_inset_b; }
int64_t yuga_zeus_plat_inset_left(void) { return g_inset_l; }

/* std/zeus/platform.yuga is the GPUI-style platform leaf. Empty Yuga stubs
   compile to yuga_platform_plat_*; the Cocoa implementations stay as
   yuga_zeus_plat_* so existing callers do not change. */
void yuga_platform_plat_run(void) { yuga_zeus_plat_run(); }
int64_t yuga_platform_plat_headless(void) { return yuga_zeus_plat_headless(); }
void yuga_platform_plat_fill(int64_t x, int64_t y, int64_t w, int64_t h, int64_t rgb,
                             int64_t radius) {
    yuga_zeus_plat_fill(x, y, w, h, rgb, radius);
}
void yuga_platform_plat_fill_a(int64_t x, int64_t y, int64_t w, int64_t h, int64_t rgb,
                               int64_t radius, int64_t alpha) {
    yuga_zeus_plat_fill_a(x, y, w, h, rgb, radius, alpha);
}
void yuga_platform_plat_text(int64_t x, int64_t y, yuga_str s, int64_t rgb, int64_t font) {
    yuga_zeus_plat_text(x, y, s, rgb, font);
}

void yuga_platform_plat_text_rot(int64_t x, int64_t y, yuga_str s, int64_t rgb, int64_t font,
                                 int64_t deg) {
    yuga_zeus_plat_text_rot(x, y, s, rgb, font, deg);
}
void yuga_platform_plat_text_int(int64_t x, int64_t y, int64_t v, int64_t rgb, int64_t font) {
    yuga_zeus_plat_text_int(x, y, v, rgb, font);
}
void yuga_platform_plat_svg(int64_t x, int64_t y, int64_t w, int64_t h, yuga_str markup,
                            int64_t rgb, int64_t alpha) {
    yuga_zeus_plat_svg(x, y, w, h, markup, rgb, alpha);
}
void yuga_platform_plat_save(void) { yuga_zeus_plat_save(); }
void yuga_platform_plat_clip(int64_t x, int64_t y, int64_t w, int64_t h) {
    yuga_zeus_plat_clip(x, y, w, h);
}
void yuga_platform_plat_restore(void) { yuga_zeus_plat_restore(); }
int64_t yuga_platform_plat_key_intern(yuga_str name) {
    char *s = dup_ys(name);
    int id = zeus_key_context_id(s);
    free(s);
    return id;
}
int64_t yuga_platform_plat_key_intern_action(yuga_str action) {
    char *s = dup_ys(action);
    int id = zeus_key_context_from_action(s);
    free(s);
    return id;
}
static void intern_fn_reset(void);

void yuga_platform_plat_key_reset(void) {
    zeus_key_reset_handlers();
    intern_fn_reset();
}
void yuga_platform_plat_map_key(yuga_str spec, yuga_str action, int64_t ctx) {
    char *a = dup_ys(spec), *b = dup_ys(action);
    zeus_key_map_ctx(a, b, (int)ctx);
    free(a);
    free(b);
}

int64_t yuga_platform_plat_key_ev(int64_t key, int64_t mods) {
    return zeus_handle_key_ev((int)key, (int)mods);
}

#define ZEUS_EDIT_SLOTS 64
#define ZEUS_EDIT_MAX 65536

static char *edit_buf[ZEUS_EDIT_SLOTS];
static int edit_len[ZEUS_EDIT_SLOTS];
static int edit_cap[ZEUS_EDIT_SLOTS];

static int edit_ok(int64_t slot) {
    return slot >= 0 && slot < ZEUS_EDIT_SLOTS;
}

static int edit_grow(int slot, int need) {
    int cap;
    char *p;
    if (need < 1) need = 1;
    if (need > ZEUS_EDIT_MAX) return 0;
    if (need <= edit_cap[slot]) return 1;
    cap = edit_cap[slot] ? edit_cap[slot] * 2 : 512;
    while (cap < need) cap *= 2;
    if (cap > ZEUS_EDIT_MAX) cap = ZEUS_EDIT_MAX;
    p = (char *)realloc(edit_buf[slot], (size_t)cap);
    if (!p) return 0;
    edit_buf[slot] = p;
    edit_cap[slot] = cap;
    return 1;
}

static int edit_put(int slot, char ch) {
    int n = edit_len[slot];
    if (!edit_grow(slot, n + 2)) return 1;
    edit_buf[slot][n] = ch;
    edit_buf[slot][n + 1] = '\0';
    edit_len[slot] = n + 1;
    return 1;
}

int64_t yuga_platform_plat_edit_append(int64_t slot, int64_t key) {
    int i;
    if (!edit_ok(slot)) return 0;
    if (key == 13) key = 10;
    if (key == 9) {
        for (i = 0; i < 4; i++) {
            if (!edit_put((int)slot, ' ')) return 1;
        }
        return 1;
    }
    if (key != 10 && (key < 32 || key >= 127)) return 0;
    return edit_put((int)slot, (char)key);
}

int64_t yuga_platform_plat_edit_back(int64_t slot) {
    if (!edit_ok(slot)) return 0;
    if (edit_len[slot] <= 0) return 0;
    edit_len[slot]--;
    if (edit_buf[slot]) edit_buf[slot][edit_len[slot]] = '\0';
    return 1;
}

int64_t yuga_platform_plat_edit_len(int64_t slot) {
    if (!edit_ok(slot)) return 0;
    return edit_len[slot];
}

yuga_str yuga_platform_plat_edit_text(int64_t slot) {
    if (!edit_ok(slot) || edit_len[slot] <= 0 || !edit_buf[slot])
        return (yuga_str){"", 0};
    return (yuga_str){edit_buf[slot], edit_len[slot]};
}

int64_t yuga_platform_plat_edit_set(int64_t slot, yuga_str text) {
    int n;
    if (!edit_ok(slot)) return 0;
    n = text.len > 0 && text.ptr ? (int)text.len : 0;
    if (n > ZEUS_EDIT_MAX - 1) n = ZEUS_EDIT_MAX - 1;
    if (!edit_grow((int)slot, n + 1)) return 0;
    if (n && text.ptr) memcpy(edit_buf[slot], text.ptr, (size_t)n);
    edit_buf[slot][n] = '\0';
    edit_len[slot] = n;
    return 1;
}

/* Drop the text of a slot (keeps the buffer) so a recycled slot starts
   empty. Called when the input node holding the slot is torn down. */
void yuga_platform_plat_edit_reset(int64_t slot) {
    if (!edit_ok(slot)) return;
    if (edit_buf[slot]) edit_buf[slot][0] = '\0';
    edit_len[slot] = 0;
}

void yuga_platform_plat_measure(yuga_str s, int64_t px, int64_t *w, int64_t *h) {
    yuga_zeus_plat_measure(s, px, w, h);
}
void yuga_platform_plat_measure_int(int64_t v, int64_t px, int64_t *w, int64_t *h) {
    yuga_zeus_plat_measure_int(v, px, w, h);
}
void yuga_platform_plat_measure_wrap(yuga_str s, int64_t px, int64_t max_w, int64_t *w,
                                     int64_t *h) {
    yuga_zeus_plat_measure_wrap(s, px, max_w, w, h);
}
void yuga_platform_plat_text_wrap(int64_t x, int64_t y, yuga_str s, int64_t rgb, int64_t font,
                                  int64_t max_w) {
    yuga_zeus_plat_text_wrap(x, y, s, rgb, font, max_w);
}
void yuga_platform_plat_set_window(yuga_str title, int64_t width, int64_t height) {
    yuga_zeus_plat_set_window(title, width, height);
}
int64_t yuga_platform_plat_view_width(void) { return yuga_zeus_plat_view_width(); }
int64_t yuga_platform_plat_view_height(void) { return yuga_zeus_plat_view_height(); }

int64_t yuga_platform_plat_overlay_scroll(void) {
    return yuga_zeus_plat_overlay_scroll();
}
int64_t yuga_platform_plat_inset_top(void) { return yuga_zeus_plat_inset_top(); }
int64_t yuga_platform_plat_inset_right(void) { return yuga_zeus_plat_inset_right(); }
int64_t yuga_platform_plat_inset_bottom(void) { return yuga_zeus_plat_inset_bottom(); }
int64_t yuga_platform_plat_inset_left(void) { return yuga_zeus_plat_inset_left(); }

/* Interned handlers and reactive prop thunks share this table. A page of
   declarative widgets interns one entry per prop, so it grows rather than
   capping — a full table used to return 0 and silently drop every prop past
   the limit. */
static yuga_fn *click_fns;
static int nclick_fns = 1;
static int click_fns_cap;

/* Freed interned-handler slots, recycled by plat_intern_fn. A slot is freed
   only when the subtree that interned it is torn down, so recycling never
   aliases a live handler. */
static int *free_fns;
static int nfree_fns;
static int cfree_fns;

static void free_fn_push(int id) {
    if (nfree_fns == cfree_fns) {
        int cap = cfree_fns ? cfree_fns * 2 : 256;
        int *next = (int *)realloc(free_fns, (size_t)cap * sizeof(int));
        if (!next) {
            fprintf(stderr, "zeus: out of memory\n");
            abort();
        }
        free_fns = next;
        cfree_fns = cap;
    }
    free_fns[nfree_fns++] = id;
}

static int free_fn_pop(void) {
    if (nfree_fns <= 0) return 0;
    return free_fns[--nfree_fns];
}

static int intern_fn_grow(void) {
    if (nclick_fns < click_fns_cap) return 1;
    int cap = click_fns_cap ? click_fns_cap * 2 : 2048;
    yuga_fn *next = (yuga_fn *)realloc(click_fns, (size_t)cap * sizeof(yuga_fn));
    if (!next) return 0;
    memset(next + click_fns_cap, 0, (size_t)(cap - click_fns_cap) * sizeof(yuga_fn));
    click_fns = next;
    click_fns_cap = cap;
    return 1;
}

static void intern_fn_reset(void) {
    int i;
    for (i = 1; i < nclick_fns; i++) {
        free(click_fns[i].env);
        click_fns[i].fn = NULL;
        click_fns[i].env = NULL;
        click_fns[i].env_size = 0;
    }
    nclick_fns = 1;
    nfree_fns = 0;
}

/* Free one interned handler; its id becomes recyclable. */
void yuga_platform_plat_intern_free(int64_t id) {
    if (id <= 0 || id >= nclick_fns) return;
    if (!click_fns[id].fn && !click_fns[id].env) return;
    free(click_fns[id].env);
    click_fns[id].fn = NULL;
    click_fns[id].env = NULL;
    click_fns[id].env_size = 0;
    free_fn_push((int)id);
}

int64_t yuga_platform_plat_intern_fn(yuga_fn handler) {
    yuga_fn kept;
    int id = free_fn_pop();
    if (id == 0) {
        if (!intern_fn_grow()) return 0;
        id = nclick_fns++;
    }
    kept = handler;
    /* Snapshot the env. The Yuga value is often a field that is dropped when
       the props struct returns, which would free the original and leave the
       interned click/styled handler dangling. */
    if (handler.env && handler.env_size > 0) {
        void *copy = malloc(handler.env_size);
        if (copy) {
            memcpy(copy, handler.env, handler.env_size);
            kept.env = copy;
        }
    }
    click_fns[id] = kept;
    return id;
}

/* Reactive prop thunks share the interned-handler table; only the call
   signature differs. Interning yields an int a `||` closure can capture. */
int64_t yuga_platform_plat_intern_int_fn(yuga_fn thunk) {
    return yuga_platform_plat_intern_fn(thunk);
}

int64_t yuga_platform_plat_invoke_int_fn(int64_t id) {
    if (id <= 0 || id >= nclick_fns) return 0;
    yuga_fn h = click_fns[id];
    if (!h.fn) return 0;
    return ((int64_t (*)(void *))h.fn)(h.env);
}

int64_t yuga_platform_plat_intern_bool_fn(yuga_fn thunk) {
    return yuga_platform_plat_intern_fn(thunk);
}

int64_t yuga_platform_plat_invoke_bool_fn(int64_t id) {
    if (id <= 0 || id >= nclick_fns) return 0;
    yuga_fn h = click_fns[id];
    if (!h.fn) return 0;
    return ((bool (*)(void *))h.fn)(h.env) ? 1 : 0;
}

int64_t yuga_platform_plat_intern_str_fn(yuga_fn thunk) {
    return yuga_platform_plat_intern_fn(thunk);
}

yuga_str yuga_platform_plat_invoke_str_fn(int64_t id) {
    if (id <= 0 || id >= nclick_fns) return (yuga_str){ "", 0 };
    yuga_fn h = click_fns[id];
    if (!h.fn) return (yuga_str){ "", 0 };
    return ((yuga_str (*)(void *))h.fn)(h.env);
}

void yuga_platform_plat_invoke_fn(int64_t id) {
    if (id <= 0 || id >= nclick_fns) return;
    yuga_fn h = click_fns[id];
    if (h.fn) ((void (*)(void *))h.fn)(h.env);
}

const char *zeus_window_title(void) { return win_title ? win_title : ""; }
int64_t zeus_window_width(void) { return win_w; }
int64_t zeus_window_height(void) { return win_h; }

void zeus_layout(int64_t width, int64_t height) {
    yuga_zeus_engine_layout(width, height);
}

int zeus_step(float dt) {
    (void)dt;
    return (int)yuga_zeus_engine_step();
}

void zeus_bind_draw(ZeusDraw draw) {
    paint_ctx = NULL;
    paint_draw = draw;
    have_draw = 1;
}

void zeus_paint(void *ctx, ZeusDraw draw) {
    paint_ctx = ctx;
    paint_draw = draw;
    have_draw = 1;
    yuga_zeus_engine_paint();
    have_draw = 0;
}

int zeus_handle_click(int64_t x, int64_t y) {
    return (int)yuga_zeus_engine_click(x, y);
}

int zeus_handle_hover(int64_t x, int64_t y) {
    return (int)yuga_zeus_engine_hover(x, y);
}

int zeus_over_button(void) { return (int)yuga_zeus_engine_over_button(); }

/* CSS cursor name under the pointer; "" or unknown = default arrow. */
const char *zeus_cursor(void) {
    static char buf[64];
    yuga_str s = yuga_zeus_engine_cursor();
    size_t n = s.len < 0 ? 0 : (size_t)s.len;
    if (n > sizeof buf - 1) n = sizeof buf - 1;
    if (n && s.ptr) memcpy(buf, s.ptr, n);
    buf[n] = '\0';
    return buf;
}

int zeus_handle_scroll(int64_t x, int64_t y, int64_t dx, int64_t dy) {
    return (int)yuga_zeus_engine_scroll(x, y, dx, dy);
}

int zeus_handle_drag(int64_t x, int64_t y) {
    return (int)yuga_zeus_engine_drag(x, y);
}

void zeus_handle_mouseup(void) { yuga_zeus_engine_mouseup(); }

int zeus_handle_key(int key) {
    return (int)yuga_zeus_engine_key(key);
}

int zeus_handle_key_up(int key, int mods) {
    return (int)yuga_zeus_engine_key_up(key, mods);
}

int zeus_handle_key_ev(int key, int mods) {
    if (zeus_key_dispatch(key, mods)) return 1;
    if (key == ZEUS_K_TAB && !(mods & ~ZEUS_MOD_SHIFT))
        return zeus_focus_step(mods & ZEUS_MOD_SHIFT);
    if (mods & ~ZEUS_MOD_SHIFT) return 0;
    return zeus_handle_key(key);
}

int zeus_focus_chain(int *nodes_out, int *ctxs, int max) {
    yuga_zeus_engine_fill_focus();
    int n = (int)yuga_zeus_engine_focus_depth();
    if (n > max) n = max;
    if (n <= 0) {
        if (nodes_out) nodes_out[0] = 0;
        if (ctxs) ctxs[0] = 0;
        return 1;
    }
    for (int i = 0; i < n; i++) {
        if (nodes_out) nodes_out[i] = (int)yuga_zeus_engine_focus_node(i);
        if (ctxs) ctxs[i] = (int)yuga_zeus_engine_focus_ctx(i);
    }
    return n;
}

void zeus_key_apply(int sig, int mode, int64_t value, int64_t lo, int64_t hi) {
    yuga_zeus_engine_key_apply((int64_t)sig, (int64_t)mode, value, lo, hi);
}

int zeus_focus_captures_text(void) {
    return (int)yuga_zeus_engine_focus_captures_text();
}

int zeus_focus_step(int back) {
    return (int)yuga_zeus_engine_focus_step(back);
}

Node yuga_zeus_on_action(Node node, yuga_str action, Signal sig, int64_t mode, int64_t value) {
    char *s = dup_ys(action);
    zeus_key_on_action((int)node.id, s, (int)sig.id, (int)mode, value);
    free(s);
    return node;
}

Node yuga_zeus_on_range(Node node, yuga_str action, Signal sig, int64_t delta, int64_t lo,
                       int64_t hi) {
    char *s = dup_ys(action);
    zeus_key_on_range((int)node.id, s, (int)sig.id, delta, lo, hi);
    free(s);
    return node;
}

void yuga_zeus_on_action_global(yuga_str action, Signal sig, int64_t mode, int64_t value) {
    char *s = dup_ys(action);
    zeus_key_on_action(0, s, (int)sig.id, (int)mode, value);
    free(s);
}

void yuga_zeus_map_key(yuga_str spec, yuga_str action, yuga_str ctx) {
    char *a = dup_ys(spec), *b = dup_ys(action), *c = dup_ys(ctx);
    zeus_key_map(a, b, c);
    free(a);
    free(b);
    free(c);
}

void yuga_zeus_remap_key(yuga_str spec, yuga_str action, yuga_str ctx) {
    char *a = dup_ys(spec), *b = dup_ys(action), *c = dup_ys(ctx);
    zeus_key_remap(a, b, c);
    free(a);
    free(b);
    free(c);
}
