#ifndef CDIE_PLATFORM_H
#define CDIE_PLATFORM_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t PlColor;                       /* 0xRRGGBB */
#define PL_RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define PL_NONE 0xFFFFFFFFu                     /* no fill / no border */

typedef struct { int l, t, r, b; } PlRect;
typedef struct PlCanvas PlCanvas;               /* opaque */
typedef struct PlFont   PlFont;                 /* opaque */
typedef struct PlWidget PlWidget;               /* opaque */

enum { PL_TEXT_LEFT = 0, PL_TEXT_CENTER = 1, PL_TEXT_ELLIPSIS = 2 };

typedef struct PlWidgetOps {
    void (*paint)(void *u, PlCanvas *c, int w, int h);
    void (*mouse_move)(void *u, int x, int y);
    void (*mouse_leave)(void *u);
    void (*mouse_down)(void *u, int x, int y);
    void (*resize)(void *u, int w, int h);
    int  (*hand_cursor_at)(void *u, int x, int y);   
    void (*destroy)(void *u);                        
} PlWidgetOps;

PlWidget *pl_widget_create(void *parent_native, const PlWidgetOps *ops, void *user,
                           int x, int y, int w, int h);
void pl_widget_destroy(PlWidget *w);            
void pl_widget_set_rect(PlWidget *w, int x, int y, int width, int height);
int  pl_parent_size(PlWidget *w, int *width, int *height);
void pl_invalidate(PlWidget *w, const PlRect *r);    

PlFont *pl_font_create(int pt, int bold);
void    pl_font_destroy(PlFont *f);
int     pl_text_width(PlFont *f, const char *utf8);

void pl_fill(PlCanvas *c, int l, int t, int r, int b, PlColor col);
void pl_line(PlCanvas *c, int x1, int y1, int x2, int y2, PlColor col);
void pl_round_rect(PlCanvas *c, PlRect r, PlColor fill, PlColor border, int radius);
void pl_ellipse(PlCanvas *c, int l, int t, int r, int b, PlColor fill, PlColor border);
void pl_text(PlCanvas* c, PlFont* f, PlRect r, const char* utf8, PlColor col, int flags);
void pl_image(PlCanvas* c, int x, int y, int w, int h,
  int src_w, int src_h, const unsigned char* rgba);

#ifdef __cplusplus
}
#endif
#endif