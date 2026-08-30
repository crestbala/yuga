/* mac.m — Cocoa window + Core Text paint for Zeus (zeus/desktop). */
#import <Cocoa/Cocoa.h>
#include "zeus_rt.h"
#include "zeus_key.h"
#include <string.h>
#include <stdlib.h>

static NSView *g_view;

static NSFont *font_at(int64_t px) {
    static NSFont *cache[72];
    int i = (int)px;
    if (i < 8) i = 8;
    if (i > 71) i = 71;
    if (!cache[i])
        cache[i] = [[NSFont systemFontOfSize:(CGFloat)i] retain];
    return cache[i];
}

static NSColor *zeus_color(int64_t rgb) {
    CGFloat r = ((rgb >> 16) & 255) / 255.0;
    CGFloat g = ((rgb >> 8) & 255) / 255.0;
    CGFloat b = (rgb & 255) / 255.0;
    return [NSColor colorWithCalibratedRed:r green:g blue:b alpha:1.0];
}

static NSDictionary *zeus_attrs(int64_t rgb, int64_t px) {
    static NSDictionary *cache[24];
    static int64_t keys[24];
    static unsigned n;
    int64_t key = (rgb << 8) ^ (px & 255);
    for (unsigned i = 0; i < n; i++)
        if (keys[i] == key) return cache[i];
    NSDictionary *a = [@{
        NSFontAttributeName: font_at(px),
        NSForegroundColorAttributeName: zeus_color(rgb)
    } retain];
    if (n < 24) {
        keys[n] = key;
        cache[n] = a;
        n++;
    }
    return a;
}

static void mac_measure(const char *s, int64_t px, int64_t *w, int64_t *h) {
    NSString *str = s ? [NSString stringWithUTF8String:s] : @"";
    NSDictionary *a = zeus_attrs(0x000000, px);
    NSSize sz = [str sizeWithAttributes:a];
    *w = (int64_t)(sz.width + 0.999);
    *h = (int64_t)(sz.height + 0.999);
}

static void mac_fill_a(void *ctx, int64_t x, int64_t y, int64_t w, int64_t h,
                       int64_t rgb, int64_t radius, int64_t alpha) {
    (void)ctx;
    CGFloat a = alpha < 0 ? 0 : (alpha > 255 ? 1.0 : (CGFloat)alpha / 255.0);
    CGFloat r = ((rgb >> 16) & 255) / 255.0;
    CGFloat g = ((rgb >> 8) & 255) / 255.0;
    CGFloat b = (rgb & 255) / 255.0;
    NSRect rect = NSMakeRect((CGFloat)x, (CGFloat)y, (CGFloat)w, (CGFloat)h);
    [[NSColor colorWithCalibratedRed:r green:g blue:b alpha:a] setFill];
    if (radius > 0) {
        CGFloat rad = (CGFloat)radius;
        if (rad > rect.size.width / 2) rad = rect.size.width / 2;
        if (rad > rect.size.height / 2) rad = rect.size.height / 2;
        [[NSBezierPath bezierPathWithRoundedRect:rect xRadius:rad yRadius:rad] fill];
    } else {
        [[NSBezierPath bezierPathWithRect:rect] fill];
    }
}

static void mac_fill(void *ctx, int64_t x, int64_t y, int64_t w, int64_t h,
                     int64_t rgb, int64_t radius) {
    (void)ctx;
    NSRect r = NSMakeRect((CGFloat)x, (CGFloat)y, (CGFloat)w, (CGFloat)h);
    [zeus_color(rgb) setFill];
    if (radius > 0) {
        CGFloat rad = (CGFloat)radius;
        if (rad > r.size.width / 2) rad = r.size.width / 2;
        if (rad > r.size.height / 2) rad = r.size.height / 2;
        [[NSBezierPath bezierPathWithRoundedRect:r xRadius:rad yRadius:rad] fill];
    } else {
        NSRectFill(r);
    }
}

static void mac_text(void *ctx, int64_t x, int64_t y, const char *s,
                     int64_t rgb, int64_t font) {
    (void)ctx;
    NSString *str = s ? [NSString stringWithUTF8String:s] : @"";
    [str drawAtPoint:NSMakePoint((CGFloat)x, (CGFloat)y) withAttributes:zeus_attrs(rgb, font)];
}

static void mac_save(void *ctx) {
    (void)ctx;
    [NSGraphicsContext saveGraphicsState];
}

static void mac_clip(void *ctx, int64_t x, int64_t y, int64_t w, int64_t h) {
    (void)ctx;
    NSRectClip(NSMakeRect((CGFloat)x, (CGFloat)y, (CGFloat)w, (CGFloat)h));
}

static void mac_restore(void *ctx) {
    (void)ctx;
    [NSGraphicsContext restoreGraphicsState];
}

/* --- SVG (viewBox, g, path, circle; Solar Linear subset) --- */

typedef struct {
    const char *p;
} SvgScan;

typedef struct {
    int has_fill, has_stroke;
    int64_t fill, stroke;
    float sw;
    int cap;  /* 0 butt, 1 round, 2 square */
    int join; /* 0 miter, 1 round, 2 bevel */
} SvgStyle;

static void svg_skip(SvgScan *s) {
    while (*s->p && (*s->p == ' ' || *s->p == '\t' || *s->p == '\n' ||
                     *s->p == '\r' || *s->p == ','))
        s->p++;
}

static int svg_num(SvgScan *s, float *out) {
    svg_skip(s);
    const char *p = s->p;
    if (!*p) return 0;
    char *end = NULL;
    float v = strtof(p, &end);
    if (end == p) return 0;
    s->p = end;
    *out = v;
    return 1;
}

static int svg_attr(const char *tag, const char *name, char *out, int cap) {
    size_t nlen = strlen(name);
    const char *p = tag;
    while (*p) {
        if ((p == tag || p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n') &&
            strncmp(p, name, nlen) == 0 && p[nlen] == '=') {
            p += nlen + 1;
            char q = *p;
            if (q != '"' && q != '\'') return 0;
            p++;
            int i = 0;
            while (*p && *p != q && i + 1 < cap) out[i++] = *p++;
            out[i] = 0;
            return 1;
        }
        p++;
    }
    return 0;
}

static int svg_color(const char *s, int64_t current, int64_t *out) {
    if (!s || !*s) return 0;
    if (strcmp(s, "none") == 0 || strcmp(s, "transparent") == 0) return 0;
    if (strcmp(s, "currentColor") == 0 || strcmp(s, "currentcolor") == 0) {
        *out = current;
        return 1;
    }
    if (s[0] == '#') {
        unsigned v = 0;
        int n = 0;
        const char *p = s + 1;
        while (*p && n < 8) {
            int d;
            if (*p >= '0' && *p <= '9') d = *p - '0';
            else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
            else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
            else break;
            v = (v << 4) | (unsigned)d;
            p++;
            n++;
        }
        if (n == 3)
            *out = (int64_t)(((v >> 8) & 0xF) * 0x110000 + ((v >> 4) & 0xF) * 0x1100 +
                             (v & 0xF) * 0x11);
        else if (n == 6)
            *out = (int64_t)v;
        else
            return 0;
        return 1;
    }
    return 0;
}

static void svg_style_from(const char *tag, SvgStyle *st, int64_t current) {
    char buf[128];
    int64_t c;
    if (svg_attr(tag, "fill", buf, (int)sizeof buf)) {
        if (svg_color(buf, current, &c)) {
            st->has_fill = 1;
            st->fill = c;
        } else {
            st->has_fill = 0;
        }
    }
    if (svg_attr(tag, "stroke", buf, (int)sizeof buf)) {
        if (svg_color(buf, current, &c)) {
            st->has_stroke = 1;
            st->stroke = c;
        } else {
            st->has_stroke = 0;
        }
    }
    if (svg_attr(tag, "stroke-width", buf, (int)sizeof buf)) {
        SvgScan sc = {buf};
        float v;
        if (svg_num(&sc, &v) && v > 0) st->sw = v;
    }
    if (svg_attr(tag, "stroke-linecap", buf, (int)sizeof buf)) {
        if (strcmp(buf, "round") == 0) st->cap = 1;
        else if (strcmp(buf, "square") == 0) st->cap = 2;
        else st->cap = 0;
    }
    if (svg_attr(tag, "stroke-linejoin", buf, (int)sizeof buf)) {
        if (strcmp(buf, "round") == 0) st->join = 1;
        else if (strcmp(buf, "bevel") == 0) st->join = 2;
        else st->join = 0;
    }
}

static NSColor *svg_nscolor(int64_t rgb, int64_t alpha) {
    CGFloat a = alpha < 0 ? 0 : (alpha > 255 ? 1.0 : (CGFloat)alpha / 255.0);
    return [NSColor colorWithCalibratedRed:((rgb >> 16) & 255) / 255.0
                                     green:((rgb >> 8) & 255) / 255.0
                                      blue:(rgb & 255) / 255.0
                                     alpha:a];
}

static void svg_paint_path(NSBezierPath *path, const SvgStyle *st, float scale,
                           int64_t alpha) {
    if (!path || [path isEmpty]) return;
    [path setLineWidth:(CGFloat)(st->sw * scale)];
    [path setLineCapStyle:st->cap == 1 ? NSLineCapStyleRound
                         : st->cap == 2 ? NSLineCapStyleSquare
                                        : NSLineCapStyleButt];
    [path setLineJoinStyle:st->join == 1 ? NSLineJoinStyleRound
                          : st->join == 2 ? NSLineJoinStyleBevel
                                          : NSLineJoinStyleMiter];
    if (st->has_fill) {
        [svg_nscolor(st->fill, alpha) setFill];
        [path fill];
    }
    if (st->has_stroke) {
        [svg_nscolor(st->stroke, alpha) setStroke];
        [path stroke];
    }
}

static NSPoint svg_xf(float x, float y, float ox, float oy, float s) {
    return NSMakePoint((CGFloat)(ox + x * s), (CGFloat)(oy + y * s));
}

static int svg_cmd_is(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static void svg_parse_d(const char *d, float ox, float oy, float s, NSBezierPath *path) {
    SvgScan sc = {d};
    float cx = 0, cy = 0, sx = 0, sy = 0, ox1 = 0, oy1 = 0;
    int prev = 0, started = 0;
    while (1) {
        svg_skip(&sc);
        if (!*sc.p) break;
        int cmd = *sc.p;
        if (svg_cmd_is(cmd)) {
            sc.p++;
        } else {
            if (!prev) break;
            cmd = (prev == 'M') ? 'L' : (prev == 'm') ? 'l' : prev;
        }
        int rel = cmd >= 'a';
        int op = rel ? cmd - 32 : cmd;
        if (op == 'M') {
            float x, y;
            if (!svg_num(&sc, &x) || !svg_num(&sc, &y)) break;
            if (rel) {
                x += cx;
                y += cy;
            }
            cx = x;
            cy = y;
            sx = cx;
            sy = cy;
            [path moveToPoint:svg_xf(cx, cy, ox, oy, s)];
            started = 1;
            prev = rel ? 'm' : 'M';
            continue;
        }
        if (!started) {
            prev = cmd;
            continue;
        }
        if (op == 'L') {
            float x, y;
            if (!svg_num(&sc, &x) || !svg_num(&sc, &y)) break;
            if (rel) {
                x += cx;
                y += cy;
            }
            cx = x;
            cy = y;
            [path lineToPoint:svg_xf(cx, cy, ox, oy, s)];
        } else if (op == 'H') {
            float x;
            if (!svg_num(&sc, &x)) break;
            if (rel) x += cx;
            cx = x;
            [path lineToPoint:svg_xf(cx, cy, ox, oy, s)];
        } else if (op == 'V') {
            float y;
            if (!svg_num(&sc, &y)) break;
            if (rel) y += cy;
            cy = y;
            [path lineToPoint:svg_xf(cx, cy, ox, oy, s)];
        } else if (op == 'C') {
            float x1, y1, x2, y2, x, y;
            if (!svg_num(&sc, &x1) || !svg_num(&sc, &y1) || !svg_num(&sc, &x2) ||
                !svg_num(&sc, &y2) || !svg_num(&sc, &x) || !svg_num(&sc, &y))
                break;
            if (rel) {
                x1 += cx;
                y1 += cy;
                x2 += cx;
                y2 += cy;
                x += cx;
                y += cy;
            }
            [path curveToPoint:svg_xf(x, y, ox, oy, s)
                 controlPoint1:svg_xf(x1, y1, ox, oy, s)
                 controlPoint2:svg_xf(x2, y2, ox, oy, s)];
            ox1 = x2;
            oy1 = y2;
            cx = x;
            cy = y;
        } else if (op == 'S') {
            float x2, y2, x, y;
            if (!svg_num(&sc, &x2) || !svg_num(&sc, &y2) || !svg_num(&sc, &x) ||
                !svg_num(&sc, &y))
                break;
            if (rel) {
                x2 += cx;
                y2 += cy;
                x += cx;
                y += cy;
            }
            float x1 = cx, y1 = cy;
            if (prev == 'C' || prev == 'c' || prev == 'S' || prev == 's') {
                x1 = 2 * cx - ox1;
                y1 = 2 * cy - oy1;
            }
            [path curveToPoint:svg_xf(x, y, ox, oy, s)
                 controlPoint1:svg_xf(x1, y1, ox, oy, s)
                 controlPoint2:svg_xf(x2, y2, ox, oy, s)];
            ox1 = x2;
            oy1 = y2;
            cx = x;
            cy = y;
        } else if (op == 'Q') {
            float qx, qy, x, y;
            if (!svg_num(&sc, &qx) || !svg_num(&sc, &qy) || !svg_num(&sc, &x) ||
                !svg_num(&sc, &y))
                break;
            if (rel) {
                qx += cx;
                qy += cy;
                x += cx;
                y += cy;
            }
            float x1 = cx + 2.f / 3.f * (qx - cx);
            float y1 = cy + 2.f / 3.f * (qy - cy);
            float x2 = x + 2.f / 3.f * (qx - x);
            float y2 = y + 2.f / 3.f * (qy - y);
            [path curveToPoint:svg_xf(x, y, ox, oy, s)
                 controlPoint1:svg_xf(x1, y1, ox, oy, s)
                 controlPoint2:svg_xf(x2, y2, ox, oy, s)];
            ox1 = qx;
            oy1 = qy;
            cx = x;
            cy = y;
        } else if (op == 'T') {
            float x, y;
            if (!svg_num(&sc, &x) || !svg_num(&sc, &y)) break;
            if (rel) {
                x += cx;
                y += cy;
            }
            float qx = cx, qy = cy;
            if (prev == 'Q' || prev == 'q' || prev == 'T' || prev == 't') {
                qx = 2 * cx - ox1;
                qy = 2 * cy - oy1;
            }
            float x1 = cx + 2.f / 3.f * (qx - cx);
            float y1 = cy + 2.f / 3.f * (qy - cy);
            float x2 = x + 2.f / 3.f * (qx - x);
            float y2 = y + 2.f / 3.f * (qy - y);
            [path curveToPoint:svg_xf(x, y, ox, oy, s)
                 controlPoint1:svg_xf(x1, y1, ox, oy, s)
                 controlPoint2:svg_xf(x2, y2, ox, oy, s)];
            ox1 = qx;
            oy1 = qy;
            cx = x;
            cy = y;
        } else if (op == 'Z') {
            [path closePath];
            cx = sx;
            cy = sy;
        } else if (op == 'A') {
            /* Arc: skip the 7 parameters. */
            float dump;
            int i;
            for (i = 0; i < 7; i++)
                if (!svg_num(&sc, &dump)) break;
        } else {
            break;
        }
        prev = cmd;
    }
}

static const char *svg_tag_end(const char *p, int *self_close) {
    *self_close = 0;
    while (*p && *p != '>') {
        if (*p == '/' && p[1] == '>') {
            *self_close = 1;
            return p + 2;
        }
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            while (*p && *p != q) p++;
            if (*p) p++;
            continue;
        }
        p++;
    }
    if (*p == '>') p++;
    return p;
}

static void svg_draw_tree(const char **pp, SvgStyle st, float ox, float oy, float scale,
                          int64_t current, int64_t alpha, const char *stop) {
    const char *p = *pp;
    while (*p) {
        if (stop && strncmp(p, stop, strlen(stop)) == 0) {
            p += strlen(stop);
            break;
        }
        if (p[0] == '<' && p[1] == '/') {
            while (*p && *p != '>') p++;
            if (*p) p++;
            break;
        }
        if (*p != '<') {
            p++;
            continue;
        }
        if (strncmp(p, "<!--", 4) == 0) {
            p = strstr(p, "-->");
            p = p ? p + 3 : p;
            if (!p) break;
            continue;
        }
        p++;
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        const char *name = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '/' && *p != '>') p++;
        int nlen = (int)(p - name);
        int self_close = 0;
        const char *gt = svg_tag_end(p, &self_close);
        int tlen = (int)(gt - name);
        if (tlen > 2047) tlen = 2047;
        char tag[2048];
        memcpy(tag, name, (size_t)tlen);
        tag[tlen] = 0;
        p = gt;
        SvgStyle kid = st;
        svg_style_from(tag, &kid, current);
        if (nlen == 1 && name[0] == 'g') {
            if (!self_close) {
                const char *rest = p;
                svg_draw_tree(&rest, kid, ox, oy, scale, current, alpha, "</g>");
                p = rest;
            }
        } else if (nlen == 3 && strncmp(name, "svg", 3) == 0) {
            if (!self_close) {
                const char *rest = p;
                svg_draw_tree(&rest, kid, ox, oy, scale, current, alpha, "</svg>");
                p = rest;
            }
        } else if (nlen == 4 && strncmp(name, "path", 4) == 0) {
            char d[4096];
            if (svg_attr(tag, "d", d, (int)sizeof d)) {
                NSBezierPath *path = [NSBezierPath bezierPath];
                svg_parse_d(d, ox, oy, scale, path);
                svg_paint_path(path, &kid, scale, alpha);
            }
        } else if (nlen == 6 && strncmp(name, "circle", 6) == 0) {
            char buf[64];
            float cx = 0, cy = 0, r = 0, v;
            SvgScan sc;
            if (svg_attr(tag, "cx", buf, (int)sizeof buf)) {
                sc.p = buf;
                if (svg_num(&sc, &v)) cx = v;
            }
            if (svg_attr(tag, "cy", buf, (int)sizeof buf)) {
                sc.p = buf;
                if (svg_num(&sc, &v)) cy = v;
            }
            if (svg_attr(tag, "r", buf, (int)sizeof buf)) {
                sc.p = buf;
                if (svg_num(&sc, &v)) r = v;
            }
            if (r > 0) {
                NSPoint c = svg_xf(cx, cy, ox, oy, scale);
                CGFloat rr = (CGFloat)(r * scale);
                NSBezierPath *path =
                    [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(c.x - rr, c.y - rr,
                                                                      rr * 2, rr * 2)];
                svg_paint_path(path, &kid, scale, alpha);
            }
        }
        (void)self_close;
    }
    *pp = p;
}

static void mac_svg(void *ctx, int64_t x, int64_t y, int64_t w, int64_t h,
                    const char *markup, int64_t rgb, int64_t alpha) {
    (void)ctx;
    if (!markup || w <= 0 || h <= 0) return;
    float vb_x = 0, vb_y = 0, vb_w = 24, vb_h = 24;
    const char *svg = strstr(markup, "<svg");
    if (!svg) svg = markup;
    char tag[2048], vb[128];
    int self_close = 0;
    const char *name = svg + 1;
    while (*name == ' ') name++;
    const char *gt = svg_tag_end(name, &self_close);
    int tlen = (int)(gt - name);
    if (tlen > 2047) tlen = 2047;
    memcpy(tag, name, (size_t)tlen);
    tag[tlen] = 0;
    if (svg_attr(tag, "viewBox", vb, (int)sizeof vb) ||
        svg_attr(tag, "viewbox", vb, (int)sizeof vb)) {
        SvgScan sc = {vb};
        svg_num(&sc, &vb_x);
        svg_num(&sc, &vb_y);
        svg_num(&sc, &vb_w);
        svg_num(&sc, &vb_h);
        if (vb_w < 1) vb_w = 24;
        if (vb_h < 1) vb_h = 24;
    }
    float sx = (float)w / vb_w, sy = (float)h / vb_h;
    float scale = sx < sy ? sx : sy;
    float ox = (float)x + ((float)w - vb_w * scale) * 0.5f - vb_x * scale;
    float oy = (float)y + ((float)h - vb_h * scale) * 0.5f - vb_y * scale;
    SvgStyle st;
    memset(&st, 0, sizeof st);
    st.sw = 1.f;
    st.cap = 1;
    st.join = 1;
    svg_style_from(tag, &st, rgb);
    const char *body = gt;
    svg_draw_tree(&body, st, ox, oy, scale, rgb, alpha, NULL);
}

static void mac_redraw(void) {
    [g_view setNeedsDisplay:YES];
}

@interface ZeusView : NSView {
    NSTrackingArea *track;
}
@end

@implementation ZeusView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)preservesContentDuringLiveResize { return NO; }

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (track) [self removeTrackingArea:track];
    track = [[NSTrackingArea alloc]
        initWithRect:self.bounds
             options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited |
                     NSTrackingActiveInKeyWindow
               owner:self
            userInfo:nil];
    [self addTrackingArea:track];
}

- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    @autoreleasepool {
        static CFAbsoluteTime last;
        CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
        float dt = (last > 0) ? (float)(now - last) : (1.f / 60.f);
        last = now;
        if (dt > 0.05f) dt = 0.05f;
        if (dt < 0.001f) dt = 1.f / 60.f;
        NSRect b = self.bounds;
        zeus_layout((int64_t)b.size.width, (int64_t)b.size.height);
        int more = zeus_step(dt);
        ZeusDraw d;
        memset(&d, 0, sizeof d);
        d.fill = mac_fill;
        d.fill_a = mac_fill_a;
        d.text = mac_text;
        d.save = mac_save;
        d.clip = mac_clip;
        d.restore = mac_restore;
        d.svg = mac_svg;
        zeus_paint(NULL, d);
        /* setNeedsDisplay inside drawRect is dropped by AppKit; queue the next frame. */
        if (more && g_view) {
            NSView *v = g_view;
            dispatch_async(dispatch_get_main_queue(), ^{
                [v setNeedsDisplay:YES];
            });
        }
    }
}

- (void)mouseMoved:(NSEvent *)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    int dirty = zeus_handle_hover((int64_t)p.x, (int64_t)p.y);
    if (zeus_over_button())
        [[NSCursor pointingHandCursor] set];
    else
        [[NSCursor arrowCursor] set];
    if (dirty) [self setNeedsDisplay:YES];
}

- (void)mouseExited:(NSEvent *)event {
    (void)event;
    if (zeus_handle_hover(-1, -1)) [self setNeedsDisplay:YES];
    [[NSCursor arrowCursor] set];
}

- (void)mouseDown:(NSEvent *)event {
    [[self window] makeFirstResponder:self];
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    if (zeus_handle_click((int64_t)p.x, (int64_t)p.y))
        [self setNeedsDisplay:YES];
}

- (void)mouseDragged:(NSEvent *)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    int dirty = zeus_handle_drag((int64_t)p.x, (int64_t)p.y);
    dirty |= zeus_handle_hover((int64_t)p.x, (int64_t)p.y);
    if (dirty) [self setNeedsDisplay:YES];
}

- (void)mouseUp:(NSEvent *)event {
    (void)event;
    zeus_handle_mouseup();
}

- (void)scrollWheel:(NSEvent *)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    CGFloat dx = [event scrollingDeltaX];
    CGFloat dy = [event scrollingDeltaY];
    if (![event hasPreciseScrollingDeltas]) {
        dx *= 16.0;
        dy *= 16.0;
    }
    if (([event modifierFlags] & NSEventModifierFlagShift) && dx == 0.0 && dy != 0.0) {
        dx = dy;
        dy = 0.0;
    }
    if (zeus_handle_scroll((int64_t)p.x, (int64_t)p.y, (int64_t)(-dx), (int64_t)(-dy)))
        [self setNeedsDisplay:YES];
}

- (void)keyDown:(NSEvent *)event {
    int key = 0;
    unsigned short code = [event keyCode];
    if (code == 53)
        key = 27;
    else if (code == 51)
        key = 8;
    else if (code == 36)
        key = 13;
    else if (code == 123)
        key = 1000;
    else if (code == 124)
        key = 1001;
    else if (code == 126)
        key = 1002;
    else if (code == 125)
        key = 1003;
    else if (code == 116)
        key = 1004;
    else if (code == 121)
        key = 1005;
    else {
        NSString *chars = [event characters];
        if ([chars length] > 0) {
            unichar c = [chars characterAtIndex:0];
            if (c >= 32 && c < 127) key = (int)c;
        }
    }
    NSEventModifierFlags f = [event modifierFlags];
    int mods = 0;
    if (f & NSEventModifierFlagShift) mods |= ZEUS_MOD_SHIFT;
    if (f & NSEventModifierFlagControl) mods |= ZEUS_MOD_CTRL;
    if (f & NSEventModifierFlagOption) mods |= ZEUS_MOD_ALT;
    if (f & NSEventModifierFlagCommand) mods |= ZEUS_MOD_CMD;
    if (code == 48) key = ZEUS_K_TAB;
    /* A shortcut is defined by the unmodified key, so Cmd+Shift+S and
       Cmd+S cannot resolve to two different chords. */
    if (!key || (mods & ~ZEUS_MOD_SHIFT)) {
        NSString *raw = [event charactersIgnoringModifiers];
        if ([raw length] > 0) {
            unichar c = [raw characterAtIndex:0];
            if (c >= 'A' && c <= 'Z') c = c + 32;
            if (c >= 32 && c < 127) key = (int)c;
        }
    }
    if (key && zeus_handle_key_ev(key, mods))
        [self setNeedsDisplay:YES];
    else
        [super keyDown:event];
}
@end

@interface ZeusAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation ZeusAppDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app {
    (void)app;
    return YES;
}
@end

static void mac_run(void) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        ZeusAppDelegate *del = [ZeusAppDelegate new];
        [NSApp setDelegate:del];

        NSRect frame = NSMakeRect(0, 0, (CGFloat)zeus_window_width(),
                                  (CGFloat)zeus_window_height());
        NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                           NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
        NSWindow *win = [[NSWindow alloc] initWithContentRect:frame
                                                    styleMask:style
                                                      backing:NSBackingStoreBuffered
                                                        defer:NO];
        const char *t = zeus_window_title();
        [win setTitle:t ? [NSString stringWithUTF8String:t] : @"Zeus"];
        ZeusView *view = [[ZeusView alloc] initWithFrame:frame];
        g_view = view;
        [win setReleasedWhenClosed:YES];
        [win setRestorable:NO];
        [win setCollectionBehavior:NSWindowCollectionBehaviorFullScreenPrimary];
        [view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
        [win setContentView:view];
        [win makeFirstResponder:view];
        [win center];
        [win makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        [NSApp run];
    }
}

__attribute__((constructor))
static void zeus_mac_register(void) {
    zeus_set_platform(mac_run, mac_measure, mac_redraw);
}
