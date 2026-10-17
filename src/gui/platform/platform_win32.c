#ifdef _WIN32
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <stdlib.h>
#include "platform/platform.h"

#ifdef _MSC_VER
#pragma comment(lib, "msimg32.lib")   
#endif

#define PL_CLASS L"CDiE_PlWidget"

struct PlCanvas { HDC dc; };
struct PlFont   { HFONT h; };
struct PlWidget {
    HWND        hw;
    PlWidgetOps ops;
    void       *user;
    int         tracking;
    int        *freed_flag;     
};

static COLORREF to_cr(PlColor c) { return RGB((c >> 16) & 255, (c >> 8) & 255, c & 255); }

static int to_wide(const char *s, WCHAR *out, int cap)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, out, cap);
    if (n <= 0) { out[0] = 0; return 0; }
    return n - 1;
}

static LRESULT CALLBACK pl_proc(HWND hw, UINT m, WPARAM wp, LPARAM lp)
{
    PlWidget *w = (PlWidget *)GetWindowLongPtrW(hw, GWLP_USERDATA);

    switch (m) {
    case WM_NCCREATE:
        SetWindowLongPtrW(hw, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)lp)->lpCreateParams);
        break;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC wdc = BeginPaint(hw, &ps);
        if (w && w->hw && w->ops.paint) {
            RECT cr; HDC mem; HBITMAP bmp, ob; PlCanvas c;
            GetClientRect(hw, &cr);
            mem = CreateCompatibleDC(wdc);
            bmp = CreateCompatibleBitmap(wdc, cr.right, cr.bottom);
            ob = (HBITMAP)SelectObject(mem, bmp);
            c.dc = mem;
            w->ops.paint(w->user, &c, cr.right, cr.bottom);
            BitBlt(wdc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, ob);
            DeleteObject(bmp);
            DeleteDC(mem);
        }
        EndPaint(hw, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_SIZE:
      if (w && w->hw && w->ops.resize) w->ops.resize(w->user, LOWORD(lp), HIWORD(lp));
        return 0;

    case WM_MOUSEMOVE:
        if (w) {
            if (!w->tracking) {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hw, 0 };
                if (TrackMouseEvent(&tme)) w->tracking = 1;
            }
            if (w->ops.mouse_move)
                w->ops.mouse_move(w->user, (short)LOWORD(lp), (short)HIWORD(lp));
        }
        return 0;

    case WM_MOUSELEAVE:
        if (w) {
            w->tracking = 0;
            if (w->ops.mouse_leave) w->ops.mouse_leave(w->user);
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (w && w->ops.mouse_down)
            w->ops.mouse_down(w->user, (short)LOWORD(lp), (short)HIWORD(lp));
        return 0;

    case WM_SETCURSOR:
        if (w && w->ops.hand_cursor_at && LOWORD(lp) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hw, &pt);
            if (w->ops.hand_cursor_at(w->user, pt.x, pt.y)) {
                SetCursor(LoadCursorW(NULL, IDC_HAND));
                return TRUE;
            }
        }
        break;

    case WM_DESTROY:
        if (w && w->ops.destroy) {
            void (*fn)(void *) = w->ops.destroy;
            w->ops.destroy = NULL;
            fn(w->user);
        }
        return 0;

    case WM_NCDESTROY:
        if (w) {
            if (w->freed_flag) *w->freed_flag = 1;
            SetWindowLongPtrW(hw, GWLP_USERDATA, 0);
            free(w);
        }
        break;
    }
    return DefWindowProcW(hw, m, wp, lp);
}

static int register_class(void)
{
    static int done = 0;
    WNDCLASSEXW wc = { 0 };
    if (done) return 1;
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = pl_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = PL_CLASS;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 0;
    done = 1;
    return 1;
}

PlWidget *pl_widget_create(void *parent_native, const PlWidgetOps *ops, void *user,
                           int x, int y, int w, int h)
{
    PlWidget *pw;
    int freed = 0;
    HWND hw;

    if (!register_class()) return NULL;
    pw = (PlWidget *)calloc(1, sizeof *pw);
    if (!pw) return NULL;
    pw->ops = *ops; pw->user = user; pw->freed_flag = &freed;

    hw = CreateWindowExW(0, PL_CLASS, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                         x, y, w, h, (HWND)parent_native, NULL, GetModuleHandleW(NULL), pw);
    if (!hw) {
        if (!freed) free(pw);
        return NULL;
    }
    pw->hw = hw;
    pw->freed_flag = NULL;
    return pw;
}

void pl_widget_destroy(PlWidget *w) { if (w) DestroyWindow(w->hw); }

void pl_widget_set_rect(PlWidget *w, int x, int y, int width, int height)
{
    SetWindowPos(w->hw, NULL, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

int pl_parent_size(PlWidget *w, int *width, int *height)
{
    RECT r;
    if (!GetClientRect(GetParent(w->hw), &r)) return 0;
    *width = r.right; *height = r.bottom;
    return 1;
}

void pl_invalidate(PlWidget *w, const PlRect *r)
{
    if (r) {
        RECT rc;
        rc.left = r->l; rc.top = r->t; rc.right = r->r; rc.bottom = r->b;
        InvalidateRect(w->hw, &rc, FALSE);
    } else {
        InvalidateRect(w->hw, NULL, FALSE);
    }
}

PlFont *pl_font_create(int pt, int bold)
{
    PlFont *f = (PlFont *)calloc(1, sizeof *f);
    HDC dc;
    int height;
    if (!f) return NULL;
    dc = GetDC(NULL);
    height = -MulDiv(pt, GetDeviceCaps(dc, LOGPIXELSY), 72);
    ReleaseDC(NULL, dc);
    f->h = CreateFontW(height, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    return f;
}

void pl_font_destroy(PlFont *f)
{
    if (!f) return;
    if (f->h) DeleteObject(f->h);
    free(f);
}

int pl_text_width(PlFont *f, const char *s)
{
    WCHAR buf[256];
    SIZE sz = { 0, 0 };
    HDC dc = GetDC(NULL);
    HFONT old = (HFONT)SelectObject(dc, f->h);
    int n = to_wide(s, buf, 256);
    GetTextExtentPoint32W(dc, buf, n, &sz);
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    return sz.cx;
}

static HPEN make_pen(PlColor c)     { return c == PL_NONE ? (HPEN)GetStockObject(NULL_PEN) : CreatePen(PS_SOLID, 1, to_cr(c)); }
static HBRUSH make_brush(PlColor c) { return c == PL_NONE ? (HBRUSH)GetStockObject(NULL_BRUSH) : CreateSolidBrush(to_cr(c)); }

void pl_fill(PlCanvas *c, int l, int t, int r, int b, PlColor col)
{
    RECT rc; HBRUSH br;
    rc.left = l; rc.top = t; rc.right = r; rc.bottom = b;
    br = CreateSolidBrush(to_cr(col));
    FillRect(c->dc, &rc, br);
    DeleteObject(br);
}

void pl_line(PlCanvas *c, int x1, int y1, int x2, int y2, PlColor col)
{
    HPEN p = CreatePen(PS_SOLID, 1, to_cr(col));
    HPEN op = (HPEN)SelectObject(c->dc, p);
    MoveToEx(c->dc, x1, y1, NULL);
    LineTo(c->dc, x2, y2);
    SelectObject(c->dc, op);
    DeleteObject(p);
}

static void aa_fill(HDC dc, int l, int t, int r, int b, COLORREF col)
{
  RECT rc; HBRUSH br;
  if (r <= l || b <= t) return;
  rc.left = l; rc.top = t; rc.right = r; rc.bottom = b;
  br = CreateSolidBrush(col);
  FillRect(dc, &rc, br);
  DeleteObject(br);
}

static void aa_corner(HDC dc, int x0, int y0, int cx, int cy, int rr, COLORREF color)
{
  int px, py, i, j;
  int r8 = rr * 8, cx8 = cx * 8, cy8 = cy * 8;
  int sr = GetRValue(color), sg = GetGValue(color), sb = GetBValue(color);

  for (py = y0; py < y0 + rr; ++py) {
    for (px = x0; px < x0 + rr; ++px) {
      int cov = 0;
      COLORREF d;
      for (j = 0; j < 4; ++j)
        for (i = 0; i < 4; ++i) {
          int dx = px * 8 + 2 * i + 1 - cx8;
          int dy = py * 8 + 2 * j + 1 - cy8;
          if (dx * dx + dy * dy <= r8 * r8) ++cov;
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

static void aa_shape(HDC dc, int l, int t, int r, int b, int rr, COLORREF color)
{
  int w = r - l, h = b - t;
  if (w <= 0 || h <= 0) return;
  if (rr > w / 2) rr = w / 2;
  if (rr > h / 2) rr = h / 2;
  if (rr <= 0) { aa_fill(dc, l, t, r, b, color); return; }

  aa_fill(dc, l + rr, t, r - rr, b, color);
  aa_fill(dc, l, t + rr, l + rr, b - rr, color);
  aa_fill(dc, r - rr, t + rr, r, b - rr, color);
  aa_corner(dc, l, t, l + rr, t + rr, rr, color);
  aa_corner(dc, r - rr, t, r - rr, t + rr, rr, color);
  aa_corner(dc, l, b - rr, l + rr, b - rr, rr, color);
  aa_corner(dc, r - rr, b - rr, r - rr, b - rr, rr, color);
}

void pl_round_rect(PlCanvas* c, PlRect r, PlColor fill, PlColor border, int radius)
{
  if (fill == PL_NONE) {
    HPEN p = make_pen(border), op;
    HBRUSH b = make_brush(fill), ob;
    op = (HPEN)SelectObject(c->dc, p);
    ob = (HBRUSH)SelectObject(c->dc, b);
    RoundRect(c->dc, r.l, r.t, r.r, r.b, radius * 2, radius * 2);
    SelectObject(c->dc, op); SelectObject(c->dc, ob);
    if (border != PL_NONE) DeleteObject(p);
    return;
  }

  if (border != PL_NONE && border != fill) {
    aa_shape(c->dc, r.l, r.t, r.r, r.b, radius, to_cr(border));
    aa_shape(c->dc, r.l + 1, r.t + 1, r.r - 1, r.b - 1,
      radius > 0 ? radius - 1 : 0, to_cr(fill));
  }
  else {
    aa_shape(c->dc, r.l, r.t, r.r, r.b, radius, to_cr(fill));
  }
}

void pl_ellipse(PlCanvas *c, int l, int t, int r, int b, PlColor fill, PlColor border)
{
    HPEN p = make_pen(border), op;
    HBRUSH br = make_brush(fill), ob;
    op = (HPEN)SelectObject(c->dc, p);
    ob = (HBRUSH)SelectObject(c->dc, br);
    Ellipse(c->dc, l, t, r, b);
    SelectObject(c->dc, op); SelectObject(c->dc, ob);
    if (border != PL_NONE) DeleteObject(p);
    if (fill != PL_NONE) DeleteObject(br);
}

void pl_image(PlCanvas* c, int x, int y, int w, int h,
  int sw, int sh, const unsigned char* rgba)
{
  BITMAPINFO bi;
  void* bits = NULL;
  HBITMAP bmp, ob;
  HDC mem;
  BLENDFUNCTION bf;
  unsigned char* dst;
  int dx, dy;

  if (!rgba || w <= 0 || h <= 0 || sw <= 0 || sh <= 0) return;

  ZeroMemory(&bi, sizeof bi);
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;           /* top-down */
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  bmp = CreateDIBSection(c->dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  if (!bmp || !bits) { if (bmp) DeleteObject(bmp); return; }
  dst = (unsigned char*)bits;

  for (dy = 0; dy < h; ++dy) {
    int sy0 = dy * sh / h;
    int sy1 = ((dy + 1) * sh + h - 1) / h;
    if (sy1 <= sy0) sy1 = sy0 + 1;
    if (sy1 > sh) sy1 = sh;
    for (dx = 0; dx < w; ++dx) {
      int sx0 = dx * sw / w;
      int sx1 = ((dx + 1) * sw + w - 1) / w;
      unsigned long sa = 0, sr = 0, sg = 0, sb = 0;
      int n, sx, sy;
      unsigned char* o = dst + ((size_t)dy * w + dx) * 4;
      if (sx1 <= sx0) sx1 = sx0 + 1;
      if (sx1 > sw) sx1 = sw;
      n = (sx1 - sx0) * (sy1 - sy0);
      for (sy = sy0; sy < sy1; ++sy)
        for (sx = sx0; sx < sx1; ++sx) {
          const unsigned char* p = rgba + ((size_t)sy * sw + sx) * 4;
          sa += p[3];
          sr += (unsigned long)p[0] * p[3];
          sg += (unsigned long)p[1] * p[3];
          sb += (unsigned long)p[2] * p[3];
        }
      /* premultiplied BGRA, as AlphaBlend expects */
      o[0] = (unsigned char)(sb / (255UL * n));
      o[1] = (unsigned char)(sg / (255UL * n));
      o[2] = (unsigned char)(sr / (255UL * n));
      o[3] = (unsigned char)(sa / n);
    }
  }

  mem = CreateCompatibleDC(c->dc);
  ob = (HBITMAP)SelectObject(mem, bmp);
  bf.BlendOp = AC_SRC_OVER; bf.BlendFlags = 0;
  bf.SourceConstantAlpha = 255; bf.AlphaFormat = AC_SRC_ALPHA;
  AlphaBlend(c->dc, x, y, w, h, mem, 0, 0, w, h, bf);
  SelectObject(mem, ob);
  DeleteDC(mem);
  DeleteObject(bmp);
}

void pl_text(PlCanvas *c, PlFont *f, PlRect r, const char *s, PlColor col, int flags)
{
    WCHAR buf[256];
    RECT rc;
    UINT fl = DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    HFONT old;
    int n = to_wide(s, buf, 256);

    rc.left = r.l; rc.top = r.t; rc.right = r.r; rc.bottom = r.b;
    if (flags & PL_TEXT_CENTER)   fl |= DT_CENTER;
    if (flags & PL_TEXT_ELLIPSIS) fl |= DT_END_ELLIPSIS;
    SetBkMode(c->dc, TRANSPARENT);
    SetTextColor(c->dc, to_cr(col));
    old = (HFONT)SelectObject(c->dc, f->h);
    DrawTextW(c->dc, buf, n, &rc, fl);
    SelectObject(c->dc, old);
}

#endif /* _WIN32 */