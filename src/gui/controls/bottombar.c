#include <stdlib.h>
#include "controls/bottombar.h"
#include "theme/theme.h"

#define GHOST_COUNT BB_RUN_SCAN
#define BTN_H       34

static const char *s_labels[BB_COUNT] = { "Signatures", "Flags", "Database", "Run scan" };

struct Bottombar {
    PlWidget   *w;
    BottombarCb cb;
    void       *user;
    int         hovered;            
    PlFont     *fontGhost, *fontRun;
    PlRect      btn[BB_COUNT];
};

static void compute_rects(Bottombar *d, int width, int height)
{
    int by = (height - BTN_H) / 2, x = CONTENT_PAD, i;

    for (i = 0; i < GHOST_COUNT; ++i) {
        int bw = pl_text_width(d->fontGhost, s_labels[i]) + 26;
        d->btn[i].l = x;  d->btn[i].r = x + bw;
        d->btn[i].t = by; d->btn[i].b = by + BTN_H;
        x += bw + 8;
    }
    d->btn[BB_RUN_SCAN].r = width - CONTENT_PAD;
    d->btn[BB_RUN_SCAN].l = d->btn[BB_RUN_SCAN].r - 108;
    d->btn[BB_RUN_SCAN].t = by;
    d->btn[BB_RUN_SCAN].b = by + BTN_H;
}

static int hittest(const Bottombar *d, int mx, int my)
{
    int i;
    for (i = 0; i < BB_COUNT; ++i) {
        const PlRect *r = &d->btn[i];
        if (mx >= r->l && mx < r->r && my >= r->t && my < r->b) return i;
    }
    return -1;
}

static void invalidate_button(Bottombar *d, int i)
{
    PlRect r;
    if (i < 0) return;
    r = d->btn[i];
    r.l -= 2; r.t -= 2; r.r += 2; r.b += 2;
    pl_invalidate(d->w, &r);
}

static void on_paint(void *u, PlCanvas *c, int w, int h)
{
    Bottombar *d = (Bottombar *)u;
    int i;

    pl_fill(c, 0, 0, w, h, CLR_BG);
    pl_line(c, 0, 0, w, 0, CLR_SEPARATOR);

    for (i = 0; i < GHOST_COUNT; ++i) {
        int hv = (d->hovered == i);
        pl_round_rect(c, d->btn[i], hv ? CLR_BTN_GHOST_HOVER : CLR_BTN_GHOST,
                      CLR_BTN_GHOST_BD, 8);
        pl_text(c, d->fontGhost, d->btn[i], s_labels[i], CLR_TEXT_PRIMARY, PL_TEXT_CENTER);
    }
    {
        int hv = (d->hovered == BB_RUN_SCAN);
        pl_round_rect(c, d->btn[BB_RUN_SCAN], hv ? CLR_ACCENT_HOVER : CLR_ACCENT,
                      CLR_ACCENT, 8);
        pl_text(c, d->fontRun, d->btn[BB_RUN_SCAN], s_labels[BB_RUN_SCAN],
                CLR_TEXT_PRIMARY, PL_TEXT_CENTER);
    }
}

static void on_resize(void *u, int w, int h)
{
    Bottombar *d = (Bottombar *)u;
    compute_rects(d, w, h);
    pl_invalidate(d->w, NULL);
}

static void on_mouse_move(void *u, int x, int y)
{
    Bottombar *d = (Bottombar *)u;
    int hit = hittest(d, x, y);
    if (d->hovered != hit) {
        invalidate_button(d, d->hovered);
        d->hovered = hit;
        invalidate_button(d, hit);
    }
}

static void on_mouse_leave(void *u)
{
    Bottombar *d = (Bottombar *)u;
    if (d->hovered != -1) {
        int old = d->hovered;
        d->hovered = -1;
        invalidate_button(d, old);
    }
}

static void on_mouse_down(void *u, int x, int y)
{
    Bottombar *d = (Bottombar *)u;
    int hit = hittest(d, x, y);
    if (hit >= 0 && d->cb) d->cb(d->user, hit);
}

static int on_hand(void *u, int x, int y)
{
    return hittest((Bottombar *)u, x, y) >= 0;
}

static void on_destroy(void *u)
{
    Bottombar *d = (Bottombar *)u;
    pl_font_destroy(d->fontGhost);
    pl_font_destroy(d->fontRun);
    free(d);
}

static const PlWidgetOps s_ops = {
    on_paint, on_mouse_move, on_mouse_leave, on_mouse_down, on_resize, on_hand, on_destroy
};

Bottombar *bottombar_create(void *parent_native, int x, int y, int w, int h,
                            BottombarCb cb, void *user)
{
    Bottombar *b = (Bottombar *)calloc(1, sizeof *b);
    if (!b) return NULL;
    b->cb = cb; b->user = user; b->hovered = -1;
    b->fontGhost = pl_font_create(9, 0);
    b->fontRun   = pl_font_create(9, 1);
    compute_rects(b, w, h);
    b->w = pl_widget_create(parent_native, &s_ops, b, x, y, w, h);
    if (!b->w) {
        pl_font_destroy(b->fontGhost);
        pl_font_destroy(b->fontRun);
        free(b);
        return NULL;
    }
    return b;
}

void bottombar_destroy(Bottombar *b)           { if (b) pl_widget_destroy(b->w); }
PlWidget *bottombar_widget(const Bottombar *b) { return b->w; }