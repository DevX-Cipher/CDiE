#ifndef CDIE_THEME_H
#define CDIE_THEME_H

#include "platform/platform.h"
#ifdef _WIN32
#include <windows.h>
#endif

#include "xxwidgets/xxwidgets_theme.h"

#define CLR_BG              PL_RGB(0x1F,0x21,0x2E)
#define CLR_SIDEBAR_BG      PL_RGB(0x17,0x17,0x23)
#define CLR_SIDEBAR_SEL     PL_RGB(0x2B,0x2B,0x35)
#define CLR_SIDEBAR_HOVER   PL_RGB(0x21,0x21,0x2C)
#define CLR_PANEL           PL_RGB(0x26,0x28,0x35)
#define CLR_PANEL_BORDER    PL_RGB(0x33,0x35,0x42)
#define CLR_ACCENT          PL_RGB(0x4A,0x7F,0xFF)
#define CLR_ACCENT_HOVER    PL_RGB(0x6B,0x97,0xFF)
#define CLR_TEXT_PRIMARY    PL_RGB(0xEC,0xEC,0xF8)
#define CLR_TEXT_SECONDARY  PL_RGB(0x8A,0x89,0x97)
#define CLR_TEXT_CODE       PL_RGB(0x9C,0x9B,0xA9)
#define CLR_TEXT_ACCENT     PL_RGB(0x85,0xAB,0xFE)
#define CLR_HIGHLIGHT_BG    PL_RGB(0x3E,0x2E,0x3B)
#define CLR_HIGHLIGHT_TXT   PL_RGB(0xED,0x94,0xAA)
#define CLR_SEPARATOR       PL_RGB(0x27,0x27,0x33)
#define CLR_SEARCH_BG       PL_RGB(0x25,0x25,0x2F)
#define CLR_LOGO_BG         PL_RGB(0x21,0x23,0x32)

#define CLR_PANEL_HEADER    PL_RGB(0x27,0x29,0x36)  /* "Automatic scan" strip   */
#define CLR_PANEL_BODY      PL_RGB(0x28,0x2A,0x37)  /* scan results body        */
#define CLR_BTN_GHOST       PL_RGB(0x2C,0x2C,0x39)  /* Memory map / Hex / etc.  */
#define CLR_BTN_GHOST_HOVER  PL_RGB(0x38, 0x39, 0x4A)
#define CLR_BTN_GHOST_BD     PL_RGB(0x3E, 0x40, 0x54)

#define SIDEBAR_W      268   /* sidebar total width                      */
#define LOGO_H          50   /* logo + app-name row                      */
#define SEARCH_H        36   /* search box height                        */
#define SEARCH_MX       12   /* horizontal margin around search box      */
#define SEARCH_MY        8   /* vertical margin                          */
#define NAV_H           40   /* each nav-item row height                 */
#define HEADER_H        90   /* breadcrumb + filename block              */
#define INFOBAR_H       84   /* 4 info-card row                          */
#define TABBAR_H        46   /* tab strip                                */
#define BOTTOMBAR_H     56   /* action bar at bottom                     */
#define CONTENT_PAD     20   /* left/right padding inside content area   */

#define FONT_UI_PT   9    /* base UI font size for all custom-drawn panels */

#ifdef _WIN32
#define TH_CR(c) RGB(((c) >> 16) & 255, ((c) >> 8) & 255, (c) & 255)

/* Internal: takes a real COLORREF. */
static __inline void th_fill_cr(HDC dc, int l, int t, int r, int b, COLORREF c)
{
  RECT rc; HBRUSH br;
  rc.left = l; rc.top = t; rc.right = r; rc.bottom = b;
  br = CreateSolidBrush(c);
  FillRect(dc, &rc, br);
  DeleteObject(br);
}

/* Public: takes a CLR_* colour. */
static __inline void th_fill(HDC dc, int l, int t, int r, int b, PlColor c)
{
  th_fill_cr(dc, l, t, r, b, TH_CR(c));
}

static __inline void th_line(HDC dc, int x0, int y0, int x1, int y1, PlColor c)
{
  HPEN pen = CreatePen(PS_SOLID, 1, TH_CR(c));
  HPEN old = (HPEN)SelectObject(dc, pen);
  MoveToEx(dc, x0, y0, NULL); LineTo(dc, x1, y1);
  SelectObject(dc, old); DeleteObject(pen);
}

static __inline void th_aa_corner(HDC dc, int x0, int y0, int cx, int cy,
  int rr, COLORREF color)
{
  int px, py, i, j;
  int r8 = rr * 8, cx8 = cx * 8, cy8 = cy * 8;
  int sr = GetRValue(color), sg = GetGValue(color), sb = GetBValue(color);

  for (py = y0; py < y0 + rr; ++py) {
    for (px = x0; px < x0 + rr; ++px) {
      int cov = 0;
      COLORREF d;
      for (j = 0; j < 4; ++j) {
        for (i = 0; i < 4; ++i) {
          int dx = px * 8 + 2 * i + 1 - cx8;
          int dy = py * 8 + 2 * j + 1 - cy8;
          if (dx * dx + dy * dy <= r8 * r8) ++cov;
        }
      }
      if (cov == 0) continue;
      if (cov == 16) { SetPixelV(dc, px, py, color); continue; }
      d = GetPixel(dc, px, py);
      if (d == CLR_INVALID) continue;
      SetPixelV(dc, px, py, RGB(
        (sr * cov + GetRValue(d) * (16 - cov) + 8) / 16,
        (sg * cov + GetGValue(d) * (16 - cov) + 8) / 16,
        (sb * cov + GetBValue(d) * (16 - cov) + 8) / 16));
    }
  }
}

static __inline void th_aa_shape(HDC dc, int l, int t, int r, int b,
  int rr, COLORREF color)
{
  int w = r - l, h = b - t;
  if (w <= 0 || h <= 0) return;
  if (rr > w / 2) rr = w / 2;
  if (rr > h / 2) rr = h / 2;
  if (rr <= 0) { th_fill_cr(dc, l, t, r, b, color); return; }

  th_fill_cr(dc, l + rr, t, r - rr, b, color);          /* middle band, full height */
  th_fill_cr(dc, l, t + rr, l + rr, b - rr, color);       /* left band                */
  th_fill_cr(dc, r - rr, t + rr, r, b - rr, color);       /* right band               */
  th_aa_corner(dc, l, t, l + rr, t + rr, rr, color);   /* top-left     */
  th_aa_corner(dc, r - rr, t, r - rr, t + rr, rr, color);   /* top-right    */
  th_aa_corner(dc, l, b - rr, l + rr, b - rr, rr, color);   /* bottom-left  */
  th_aa_corner(dc, r - rr, b - rr, r - rr, b - rr, rr, color);   /* bottom-right */
}

static __inline void th_round_rect(HDC dc, int l, int t, int r, int b,
  PlColor fillc, PlColor borderc, int radius)
{
  int rr = radius / 2;
  COLORREF fill = TH_CR(fillc), border = TH_CR(borderc);
  if (border != fill) {
    th_aa_shape(dc, l, t, r, b, rr, border);
    th_aa_shape(dc, l + 1, t + 1, r - 1, b - 1, rr > 0 ? rr - 1 : 0, fill);
  }
  else {
    th_aa_shape(dc, l, t, r, b, rr, fill);
  }
}

typedef struct {
  HDC     target;   /* real window DC                          */
  HDC     dc;       /* memory DC (or == target on fallback)    */
  HBITMAP bmp;
  HBITMAP old;
  int     w, h;
} ThBuf;

static __inline HDC th_buf_begin(ThBuf* b, HDC target, int w, int h)
{
  b->target = target; b->dc = target; b->bmp = NULL; b->old = NULL;
  b->w = w; b->h = h;
  if (w <= 0 || h <= 0) return target;
  b->dc = CreateCompatibleDC(target);
  if (b->dc) {
    b->bmp = CreateCompatibleBitmap(target, w, h);
    if (b->bmp) {
      b->old = (HBITMAP)SelectObject(b->dc, b->bmp);
      return b->dc;
    }
    DeleteDC(b->dc);
  }
  b->dc = target;
  return target;
}

static __inline void th_buf_end(ThBuf* b)
{
  if (b->bmp) {
    BitBlt(b->target, 0, 0, b->w, b->h, b->dc, 0, 0, SRCCOPY);
    SelectObject(b->dc, b->old);
    DeleteObject(b->bmp);
    DeleteDC(b->dc);
    b->bmp = NULL;
  }
}

static __inline void th_dot(HDC dc, int cx, int cy, int radius, PlColor color)
{
  int r = radius;

  th_aa_shape(dc,
    cx - r,
    cy - r,
    cx + r,
    cy + r,
    r,
    TH_CR(color));
}

static __inline HFONT th_font(int pt, int bold, const WCHAR* face)
{
  HDC screen = GetDC(NULL);
  int height = -MulDiv(pt, GetDeviceCaps(screen, LOGPIXELSY), 72);
  ReleaseDC(NULL, screen);
  return CreateFontW(height, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
    0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
    DEFAULT_PITCH | FF_SWISS, face);
}

#endif /* _WIN32 */

static __inline void apply_xx_theme(xxwidgets_app* app)
{
  xxwidgets_theme t;
  t.background = CLR_BG;
  t.surface = CLR_PANEL;
  t.border = CLR_PANEL_BORDER;
  t.text = CLR_TEXT_PRIMARY;
  t.text_muted = CLR_TEXT_SECONDARY;
  t.accent = CLR_ACCENT;
  t.dark = 1;
  xxwidgets_app_set_theme(app, &t);
}

#endif /* CDIE_THEME_H */