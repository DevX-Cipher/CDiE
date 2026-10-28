#include "common.h"
#include "install.h"
#include "ui.h"
#include <shellapi.h>
#include <wchar.h>

#define WM_APP_PROGRESS  (WM_APP + 1)
#define WM_APP_DONE      (WM_APP + 2)
#define WM_APP_ACTIVATE  (WM_APP + 3)

#define IDC_PATH   1000
#define MAXIT      16
#define LOG_MAX    5

#define C_BG      RGB(0x16, 0x17, 0x1a)
#define C_PANEL   RGB(0x1e, 0x20, 0x24)
#define C_CARD    RGB(0x1a, 0x1b, 0x1e)
#define C_HEAD    RGB(0x23, 0x25, 0x2a)
#define C_BORDER  RGB(0x2e, 0x30, 0x36)
#define C_SEL     RGB(0x2a, 0x2d, 0x33)
#define C_BTNHOV  RGB(0x2e, 0x31, 0x38)
#define C_BTNBD   RGB(0x3a, 0x3d, 0x45)
#define C_ACCENT  RGB(0x3b, 0x82, 0xf6)
#define C_ACCHOV  RGB(0x5a, 0x97, 0xf8)
#define C_TEXT    RGB(0xe6, 0xe6, 0xe6)
#define C_TEXT2   RGB(0x8b, 0x8f, 0x98)
#define C_DIM     RGB(0x5a, 0x5d, 0x66)
#define C_ERR     RGB(0xf0, 0x7a, 0x7a)
#define C_WHITE   RGB(0xff, 0xff, 0xff)

#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#endif

enum { IK_HEAD, IK_TEXT, IK_NOTE, IK_CHECK, IK_BTN, IK_ACCENT };
typedef struct { int kind, id, enabled; const WCHAR* text; RECT r; } Item;

typedef struct GpGraphics GpGraphics;
typedef struct GpPath GpPath;
typedef struct GpBrush GpBrush;
typedef struct GpPen GpPen;
typedef struct {
	UINT32 GdiplusVersion;
	void* DebugEventCallback;
	BOOL   SuppressBackgroundThread;
	BOOL   SuppressExternalCodecs;
} GdiplusStartupInput;

int  WINAPI GdiplusStartup(ULONG_PTR*, const GdiplusStartupInput*, void*);
void WINAPI GdiplusShutdown(ULONG_PTR);
int  WINAPI GdipCreateFromHDC(HDC, GpGraphics**);
int  WINAPI GdipDeleteGraphics(GpGraphics*);
int  WINAPI GdipSetSmoothingMode(GpGraphics*, int);
int  WINAPI GdipCreateSolidFill(UINT32, GpBrush**);
int  WINAPI GdipDeleteBrush(GpBrush*);
int  WINAPI GdipCreatePen1(UINT32, float, int, GpPen**);
int  WINAPI GdipDeletePen(GpPen*);
int  WINAPI GdipSetPenStartCap(GpPen*, int);
int  WINAPI GdipSetPenEndCap(GpPen*, int);
int  WINAPI GdipCreatePath(int, GpPath**);
int  WINAPI GdipDeletePath(GpPath*);
int  WINAPI GdipAddPathArc(GpPath*, float, float, float, float, float, float);
int  WINAPI GdipClosePathFigure(GpPath*);
int  WINAPI GdipFillPath(GpGraphics*, GpBrush*, GpPath*);
int  WINAPI GdipFillEllipse(GpGraphics*, GpBrush*, float, float, float, float);
int  WINAPI GdipDrawLine(GpGraphics*, GpPen*, float, float, float, float);

static HWND    g_hwnd, g_edit;
static HFONT   g_font, g_fontb, g_fontt;
static HBRUSH  g_editbrush;
static WNDPROC g_editproc;
static int     g_dpi = 96;
static int     g_page, g_hov = -1, g_tracking;
static Item    g_it[MAXIT];
static int     g_nit;
static int     g_allusers, g_desktop = 1, g_ctx = 1, g_launch = 1;
static int     g_installing, g_ok, g_pct;
static WCHAR   g_msg[160], g_status[200], g_err[256], g_space[96], g_dir0[MAX_PATH];
static WCHAR   g_log[LOG_MAX][120];
static int     g_nlog;
static InstallOpts g_opts;
static int     g_mode, g_confirmed, g_un_all, g_existing;
static WCHAR   g_un_dir[MAX_PATH];

static ULONG_PTR g_gdiptok;

enum {
	PG_WELCOME, PG_LOCATION, PG_INSTALL, PG_FINISH, PG_COUNT,
	PG_UCONFIRM = PG_COUNT, PG_UREMOVE, PG_UDONE, PG_EXISTING, PG_ALL
};

enum {
	ID_NONE, ID_CANCEL, ID_BACK, ID_NEXT, ID_FINISH, ID_BROWSE,
	ID_CHK_ALL, ID_CHK_DESK, ID_CHK_CTX, ID_CHK_LAUNCH, ID_UNINSTALL, ID_RUN_UNINST
};

static const WCHAR* s_nav[PG_ALL] = { L"Welcome", L"Location", L"Install", L"Finish",
																				L"Confirm", L"Remove", L"Finish", L"Welcome" };
static const WCHAR* s_title[PG_ALL] = { L"Welcome", L"Install location", L"Installing", L"Finish",
																				L"Uninstall", L"Removing", L"Finish", L"Already installed" };
static const WCHAR* s_sub[PG_ALL] = {
	L"Set up cdie on this computer", L"Choose where cdie is installed",
	L"Copying files and creating shortcuts", L"Setup is complete",
	L"Remove cdie from this computer", L"Deleting files and shortcuts", L"Uninstall is complete",
	L"Update or remove the current install"
};

static int S(int v) { return MulDiv(v, g_dpi, 96); }

static RECT R(int l, int t, int r, int b)
{
	RECT x;
	x.left = S(l); x.top = S(t); x.right = S(r); x.bottom = S(b);
	return x;
}

static RECT RAW(int l, int t, int r, int b)
{
	RECT x;
	x.left = l; x.top = t; x.right = r; x.bottom = b;
	return x;
}

static WCHAR* dupw(const WCHAR* s)
{
	size_t n = (size_t)lstrlenW(s) + 1;
	WCHAR* c = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, n * sizeof(WCHAR));
	if (c) CopyMemory(c, s, n * sizeof(WCHAR));
	return c;
}

static void fillr(HDC dc, RECT r, COLORREF c)
{
	HBRUSH b = CreateSolidBrush(c);
	FillRect(dc, &r, b);
	DeleteObject(b);
}

static UINT32 argb(COLORREF c)
{
	return 0xFF000000u | ((UINT32)GetRValue(c) << 16) | ((UINT32)GetGValue(c) << 8) | GetBValue(c);
}

static GpGraphics* gfx(HDC dc)
{
	GpGraphics* g = NULL;
	if (GdipCreateFromHDC(dc, &g) != 0) return NULL;
	GdipSetSmoothingMode(g, 4);
	return g;
}

static GpPath* rr_path(float x, float y, float w, float h, float r)
{
	GpPath* p = NULL;
	float d;
	if (r > w / 2) r = w / 2;
	if (r > h / 2) r = h / 2;
	if (r < 0.5f) r = 0.5f;
	d = r * 2;
	if (GdipCreatePath(0, &p) != 0) return NULL;
	GdipAddPathArc(p, x, y, d, d, 180, 90);
	GdipAddPathArc(p, x + w - d, y, d, d, 270, 90);
	GdipAddPathArc(p, x + w - d, y + h - d, d, d, 0, 90);
	GdipAddPathArc(p, x, y + h - d, d, d, 90, 90);
	GdipClosePathFigure(p);
	return p;
}

static void fill_rr(GpGraphics* g, COLORREF c, float x, float y, float w, float h, float r)
{
	GpBrush* b = NULL;
	GpPath* p = rr_path(x, y, w, h, r);
	if (p && GdipCreateSolidFill(argb(c), &b) == 0) {
		GdipFillPath(g, b, p);
		GdipDeleteBrush(b);
	}
	if (p) GdipDeletePath(p);
}

static void rrect(HDC dc, RECT r, COLORREF fill, COLORREF border, int rad)
{
	GpGraphics* g = gfx(dc);
	float x = (float)r.left, y = (float)r.top;
	float w = (float)(r.right - r.left), h = (float)(r.bottom - r.top);
	float rr = (float)S(rad);
	if (!g) return;
	if (fill != border) {
		fill_rr(g, border, x, y, w, h, rr);
		fill_rr(g, fill, x + 1, y + 1, w - 2, h - 2, rr - 1);
	}
	else fill_rr(g, fill, x, y, w, h, rr);
	GdipDeleteGraphics(g);
}

static void line(HDC dc, int x1, int y1, int x2, int y2, COLORREF c, int wd)
{
	GpGraphics* g;
	GpPen* p = NULL;

	if (wd <= 1 && (x1 == x2 || y1 == y2)) {
		HPEN gp = CreatePen(PS_SOLID, 1, c);
		HGDIOBJ o = SelectObject(dc, gp);
		MoveToEx(dc, x1, y1, NULL);
		LineTo(dc, x2, y2);
		SelectObject(dc, o);
		DeleteObject(gp);
		return;
	}
	g = gfx(dc);
	if (!g) return;
	if (GdipCreatePen1(argb(c), (float)wd, 2, &p) == 0) {
		if (wd > 1) { GdipSetPenStartCap(p, 2); GdipSetPenEndCap(p, 2); }
		GdipDrawLine(g, p, x1 + 0.5f, y1 + 0.5f, x2 + 0.5f, y2 + 0.5f);
		GdipDeletePen(p);
	}
	GdipDeleteGraphics(g);
}

static void circle(HDC dc, int cx, int cy, int rad, COLORREF c)
{
	GpGraphics* g = gfx(dc);
	GpBrush* b = NULL;
	if (!g) return;
	if (GdipCreateSolidFill(argb(c), &b) == 0) {
		GdipFillEllipse(g, b, (float)(cx - rad), (float)(cy - rad), (float)(rad * 2), (float)(rad * 2));
		GdipDeleteBrush(b);
	}
	GdipDeleteGraphics(g);
}

#define TXT_L  (DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX)
#define TXT_C  (DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX)
#define TXT_R  (DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX)
#define TXT_W  (DT_LEFT | DT_WORDBREAK | DT_NOPREFIX)

static void txt(HDC dc, HFONT f, COLORREF c, RECT r, UINT fmt, const WCHAR* t)
{
	SelectObject(dc, f);
	SetTextColor(dc, c);
	SetBkMode(dc, TRANSPARENT);
	DrawTextW(dc, t, -1, &r, fmt);
}

static int* chk_ptr(int id)
{
	switch (id) {
	case ID_CHK_ALL:    return &g_allusers;
	case ID_CHK_DESK:   return &g_desktop;
	case ID_CHK_CTX:    return &g_ctx;
	case ID_CHK_LAUNCH: return &g_launch;
	default:            return NULL;
	}
}

static void add(int kind, int id, const WCHAR* text, int enabled, int l, int t, int r, int b)
{
	Item* it;
	if (g_nit >= MAXIT) return;
	it = &g_it[g_nit++];
	it->kind = kind; it->id = id; it->text = text; it->enabled = enabled;
	it->r = R(l, t, r, b);
}

static void build_items(void)
{
	g_nit = 0;
	switch (g_page) {
	case PG_WELCOME:
		add(IK_BTN, ID_CANCEL, L"Cancel", 1, 492, 330, 572, 364);
		add(IK_ACCENT, ID_NEXT, L"Next", 1, 580, 330, 660, 364);
		break;

	case PG_LOCATION:
		add(IK_HEAD, 0, L"INSTALL FOLDER", 1, 208, 72, 650, 92);
		add(IK_BTN, ID_BROWSE, L"Browse", 1, 554, 96, 650, 126);
		add(IK_NOTE, 0, g_space, 1, 208, 134, 650, 154);
		add(IK_HEAD, 0, L"OPTIONS", 1, 208, 172, 650, 192);
		add(IK_CHECK, ID_CHK_ALL, L"Install for all users (needs administrator)", 1, 208, 196, 650, 230);
		add(IK_CHECK, ID_CHK_DESK, L"Create a desktop shortcut", 1, 208, 230, 650, 264);
		add(IK_CHECK, ID_CHK_CTX, L"Add \"Scan with cdie\" to the Explorer menu", 1, 208, 264, 650, 298);
		add(IK_BTN, ID_CANCEL, L"Cancel", 1, 404, 330, 484, 364);
		add(IK_BTN, ID_BACK, L"Back", 1, 492, 330, 572, 364);
		add(IK_ACCENT, ID_NEXT, L"Install", 1, 580, 330, 660, 364);
		break;

	case PG_INSTALL:
		add(IK_HEAD, 0, L"PROGRESS", 1, 208, 72, 650, 92);
		add(IK_HEAD, 0, L"LOG", 1, 208, 152, 650, 172);
		add(IK_BTN, ID_CANCEL, L"Cancel", 0, 492, 330, 572, 364);
		add(IK_ACCENT, ID_NEXT, L"Next", 0, 580, 330, 660, 364);
		break;

	case PG_FINISH:
		if (g_ok) add(IK_CHECK, ID_CHK_LAUNCH, L"Launch cdie", 1, 208, 176, 650, 210);
		add(IK_ACCENT, ID_FINISH, g_ok ? L"Finish" : L"Close", 1, 580, 330, 660, 364);
		break;

	case PG_UCONFIRM:
		add(IK_BTN, ID_CANCEL, L"Cancel", 1, 492, 330, 572, 364);
		add(IK_ACCENT, ID_UNINSTALL, L"Uninstall", 1, 580, 330, 660, 364);
		break;

	case PG_UREMOVE:
		add(IK_HEAD, 0, L"PROGRESS", 1, 208, 72, 650, 92);
		add(IK_HEAD, 0, L"LOG", 1, 208, 152, 650, 172);
		add(IK_BTN, ID_CANCEL, L"Cancel", 0, 492, 330, 572, 364);
		add(IK_ACCENT, ID_FINISH, L"Close", 0, 580, 330, 660, 364);
		break;

	case PG_UDONE:
		add(IK_ACCENT, ID_FINISH, L"Close", 1, 580, 330, 660, 364);
		break;

	case PG_EXISTING:
		add(IK_BTN, ID_CANCEL, L"Cancel", 1, 404, 330, 484, 364);
		add(IK_BTN, ID_RUN_UNINST, L"Uninstall", 1, 492, 330, 572, 364);
		add(IK_ACCENT, ID_NEXT, L"Update", 1, 580, 330, 660, 364);
		break;
	}
}

static void position_edit(void)
{
	int h = S(18);
	MoveWindow(g_edit, S(208 + 10), S(96) + (S(30) - h) / 2, S(336 - 20), h, TRUE);
}

static void set_page(int p)
{
	g_page = p;
	g_hov = -1;
	g_msg[0] = 0;
	ShowWindow(g_edit, p == PG_LOCATION ? SW_SHOW : SW_HIDE);
	build_items();
	if (p == PG_LOCATION) SetFocus(g_edit);
	InvalidateRect(g_hwnd, NULL, FALSE);
}

static void paint_item(HDC dc, int i)
{
	Item* it = &g_it[i];
	RECT r = it->r;
	int hov = (g_hov == i && it->enabled);
	COLORREF fill, fg;

	switch (it->kind) {
	case IK_HEAD: txt(dc, g_fontb, C_TEXT2, r, TXT_L, it->text); break;
	case IK_TEXT: txt(dc, g_font, C_TEXT, r, TXT_L, it->text); break;
	case IK_NOTE: txt(dc, g_font, C_TEXT2, r, TXT_L, it->text); break;

	case IK_CHECK: {
		int* v = chk_ptr(it->id);
		int on = v && *v;
		int bx = r.left, by = r.top + ((r.bottom - r.top) - S(16)) / 2;
		RECT tr;
		if (hov)
			rrect(dc, RAW(r.left - S(6), r.top + S(2), r.right, r.bottom - S(2)), C_SEL, C_SEL, 6);
		rrect(dc, RAW(bx, by, bx + S(16), by + S(16)), on ? C_ACCENT : C_BG, on ? C_ACCENT : C_BTNBD, 4);
		if (on) {
			line(dc, bx + S(3), by + S(8), bx + S(6), by + S(11), C_WHITE, S(2));
			line(dc, bx + S(6), by + S(11), bx + S(12), by + S(5), C_WHITE, S(2));
		}
		tr = RAW(r.left + S(26), r.top, r.right - S(6), r.bottom);
		txt(dc, g_font, C_TEXT, tr, TXT_L, it->text);
		break;
	}

	case IK_ACCENT:
		if (!it->enabled) {
			rrect(dc, r, C_HEAD, C_BORDER, 8);
			txt(dc, g_font, C_DIM, r, TXT_C, it->text);
		}
		else {
			fill = hov ? C_ACCHOV : C_ACCENT;
			rrect(dc, r, fill, fill, 8);
			txt(dc, g_font, C_WHITE, r, TXT_C, it->text);
		}
		break;

	case IK_BTN:
		fill = !it->enabled ? C_HEAD : (hov ? C_BTNHOV : C_HEAD);
		fg = it->enabled ? C_TEXT : C_DIM;
		rrect(dc, r, fill, it->enabled ? C_BTNBD : C_BORDER, 8);
		txt(dc, g_font, fg, r, TXT_C, it->text);
		break;
	}
}

static void draw_logo(HDC dc)
{
	RECT logo = R(208, 84, 264, 140);
	rrect(dc, logo, C_ACCENT, C_ACCENT, 14);
	txt(dc, g_fontt, C_WHITE, logo, TXT_C, L"cd");
}

static void draw_check_badge(HDC dc)
{
	int cx = S(236), cy = S(112), rad = S(28);
	circle(dc, cx, cy, rad, C_ACCENT);
	line(dc, cx - S(14), cy, cx - S(4), cy + S(10), C_WHITE, S(3));
	line(dc, cx - S(4), cy + S(10), cx + S(15), cy - S(11), C_WHITE, S(3));
}

static void draw_welcome(HDC dc)
{
	draw_logo(dc);
	txt(dc, g_fontt, C_TEXT, R(280, 88, 650, 114), TXT_L, APP_TITLE);
	txt(dc, g_font, C_TEXT2, R(280, 114, 650, 136), TXT_L, L"Version " APP_VERSION);
	txt(dc, g_font, C_TEXT, R(208, 168, 650, 190), TXT_L,
		L"This installer copies cdie and its signature databases to your computer.");
	txt(dc, g_font, C_TEXT, R(208, 190, 650, 212), TXT_L, L"It takes about a minute.");
	txt(dc, g_font, C_TEXT2, R(208, 236, 650, 258), TXT_L,
		L"Close any running copy of cdie before you continue.");
}

static void draw_location(HDC dc)
{
	RECT box = R(208, 96, 544, 126);
	int focus = (GetFocus() == g_edit);
	rrect(dc, box, C_HEAD, focus ? C_ACCENT : C_BORDER, 8);
}

static void draw_install(HDC dc)
{
	RECT track = R(208, 96, 650, 110);
	RECT bar;
	WCHAR pct[16];
	int i, y, w = track.right - track.left;

	rrect(dc, track, C_HEAD, C_BORDER, 7);
	if (g_pct > 0) {
		bar = track;
		bar.right = bar.left + w * g_pct / 100;
		if (bar.right - bar.left < S(14)) bar.right = bar.left + S(14);
		rrect(dc, bar, C_ACCENT, C_ACCENT, 7);
	}
	wsprintfW(pct, L"%d%%", g_pct);
	txt(dc, g_font, C_TEXT2, R(208, 118, 580, 140), TXT_L, g_status);
	txt(dc, g_font, C_TEXT, R(580, 118, 650, 140), TXT_R, pct);

	rrect(dc, R(208, 176, 650, 300), C_BG, C_BORDER, 8);
	y = 184;
	for (i = 0; i < g_nlog; ++i, y += 22) {
		RECT lr = R(220, y, 640, y + 22);
		line(dc, lr.left, lr.top + S(11), lr.left + S(3), lr.top + S(14), C_ACCENT, S(2));
		line(dc, lr.left + S(3), lr.top + S(14), lr.left + S(9), lr.top + S(8), C_ACCENT, S(2));
		lr.left += S(18);
		txt(dc, g_font, C_TEXT, lr, TXT_L, g_log[i]);
	}
	if (g_installing) {
		RECT lr = R(220, y, 640, y + 22);
		circle(dc, lr.left + S(3), lr.top + S(11), S(3), C_TEXT2);
		lr.left += S(18);
		txt(dc, g_font, C_TEXT2, lr, TXT_L, g_status);
	}
}

static void draw_finish(HDC dc)
{
	int cx = S(236), cy = S(112), rad = S(28);
	if (g_ok) {
		draw_check_badge(dc);
		txt(dc, g_fontt, C_TEXT, R(280, 90, 650, 116), TXT_L, APP_NAME L" is installed");
		txt(dc, g_font, C_TEXT2, R(280, 116, 650, 138), TXT_L, g_opts.dir);
	}
	else {
		circle(dc, cx, cy, rad, RGB(0xe2, 0x4b, 0x4a));
		txt(dc, g_fontt, C_WHITE, RAW(cx - rad, cy - rad, cx + rad, cy + rad), TXT_C, L"!");
		txt(dc, g_fontt, C_TEXT, R(280, 90, 650, 116), TXT_L, L"Setup couldn't finish");
		txt(dc, g_font, C_TEXT2, R(280, 124, 650, 280), TXT_W, g_err);
	}
}

static void draw_uconfirm(HDC dc)
{
	draw_logo(dc);
	txt(dc, g_fontt, C_TEXT, R(280, 88, 650, 114), TXT_L, L"Remove " APP_NAME L"?");
	txt(dc, g_font, C_TEXT2, R(280, 114, 650, 136), TXT_L, g_un_dir);
	txt(dc, g_font, C_TEXT, R(208, 168, 650, 190), TXT_L,
		L"This removes cdie and its signature databases from this computer.");
	txt(dc, g_font, C_TEXT2, R(208, 198, 650, 220), TXT_L,
		L"Your settings in %LOCALAPPDATA%\\cdie are kept.");
	txt(dc, g_font, C_TEXT2, R(208, 220, 650, 242), TXT_L,
		L"Close any running copy of cdie before you continue.");
}

static void draw_udone(HDC dc)
{
	draw_check_badge(dc);
	txt(dc, g_fontt, C_TEXT, R(280, 90, 650, 116), TXT_L, APP_NAME L" was removed");
	txt(dc, g_font, C_TEXT2, R(280, 116, 650, 138), TXT_L, L"Your settings were kept.");
}

static void draw_existing(HDC dc)
{
	draw_logo(dc);
	txt(dc, g_fontt, C_TEXT, R(280, 88, 650, 114), TXT_L, APP_NAME L" is already installed");
	txt(dc, g_font, C_TEXT2, R(280, 114, 650, 136), TXT_L, g_dir0);
	txt(dc, g_font, C_TEXT, R(208, 168, 650, 190), TXT_L,
		L"Update installs this version over the current one.");
	txt(dc, g_font, C_TEXT, R(208, 190, 650, 212), TXT_L,
		L"Uninstall removes cdie from this computer.");
	txt(dc, g_font, C_TEXT2, R(208, 236, 650, 258), TXT_L,
		L"Close any running copy of cdie before you continue.");
}

static void draw_all(HDC dc, int w, int h)
{
	int i;
	WCHAR buf[32];

	fillr(dc, RAW(0, 0, w, h), C_BG);

	rrect(dc, R(12, 12, 172, 314), C_PANEL, C_BORDER, 12);
	{
		int first = g_mode ? PG_UCONFIRM : 0;
		int cnt = g_mode ? 3 : PG_COUNT;
		int cur = (g_page == PG_EXISTING) ? PG_WELCOME : g_page;
		for (i = 0; i < cnt; ++i) {
			int y = 24 + i * 38;
			int pg = first + i;
			if (pg == cur) rrect(dc, R(20, y, 164, y + 34), C_SEL, C_SEL, 6);
			txt(dc, g_font, pg == cur ? C_TEXT : C_TEXT2, R(34, y, 160, y + 34), TXT_L, s_nav[pg]);
		}
	}

	rrect(dc, R(184, 12, 668, 314), C_CARD, C_BORDER, 12);
	rrect(dc, R(185, 13, 667, 68), C_HEAD, C_HEAD, 11);
	fillr(dc, R(185, 56, 667, 68), C_CARD);
	line(dc, S(185), S(56), S(667), S(56), C_BORDER, 1);
	txt(dc, g_fontb, C_TEXT, R(204, 12, 360, 56), TXT_L, s_title[g_page]);
	txt(dc, g_font, C_TEXT2, R(360, 12, 656, 56), TXT_L, s_sub[g_page]);

	line(dc, 0, S(320), w, S(320), C_BORDER, 1);
	wsprintfW(buf, L"Step %d of %d", g_mode ? g_page - PG_UCONFIRM + 1 : (g_page == PG_EXISTING ? 1 : g_page + 1), g_mode ? 3 : PG_COUNT);
	txt(dc, g_font, C_TEXT2, R(16, 326, 110, 368), TXT_L, buf);
	if (g_msg[0]) txt(dc, g_font, C_ERR, R(112, 326, 396, 368), TXT_L, g_msg);

	switch (g_page) {
	case PG_WELCOME:  draw_welcome(dc); break;
	case PG_LOCATION: draw_location(dc); break;
	case PG_INSTALL:  draw_install(dc); break;
	case PG_FINISH:   draw_finish(dc); break;
	case PG_UCONFIRM: draw_uconfirm(dc); break;
	case PG_UREMOVE:  draw_install(dc); break;
	case PG_UDONE:    draw_udone(dc); break;
	case PG_EXISTING: draw_existing(dc); break;
	}
	for (i = 0; i < g_nit; ++i) paint_item(dc, i);
}

static void on_paint(HWND hwnd)
{
	PAINTSTRUCT ps;
	HDC dc = BeginPaint(hwnd, &ps);
	RECT cr;
	HDC mdc;
	HBITMAP bmp, old;

	GetClientRect(hwnd, &cr);
	mdc = CreateCompatibleDC(dc);
	bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
	old = (HBITMAP)SelectObject(mdc, bmp);
	draw_all(mdc, cr.right, cr.bottom);
	BitBlt(dc, 0, 0, cr.right, cr.bottom, mdc, 0, 0, SRCCOPY);
	SelectObject(mdc, old);
	DeleteObject(bmp);
	DeleteDC(mdc);
	EndPaint(hwnd, &ps);
}

static void on_progress(void* ud, int pct, const WCHAR* text, int step)
{
	(void)ud;
	PostMessageW(g_hwnd, WM_APP_PROGRESS,
		(WPARAM)((pct & 0xFFFF) | ((step ? 1 : 0) << 16)),
		(LPARAM)(text ? dupw(text) : NULL));
}

static DWORD WINAPI install_thread(LPVOID param)
{
	WCHAR err[256];
	int ok;
	(void)param;
	err[0] = 0;
	ok = run_install_flow(&g_opts, on_progress, NULL, err, 256);
	PostMessageW(g_hwnd, WM_APP_DONE, (WPARAM)ok,
		(LPARAM)dupw(ok ? L"" : (err[0] ? err : L"Setup could not finish.")));
	return 0;
}

static void start_install(void)
{
	WCHAR d[MAX_PATH];
	int n, a, b;
	HANDLE t;

	GetWindowTextW(g_edit, d, MAX_PATH);
	a = 0;
	while (d[a] == L' ') ++a;
	if (a) MoveMemory(d, d + a, (size_t)(lstrlenW(d + a) + 1) * sizeof(WCHAR));
	n = lstrlenW(d);
	while (n > 3 && (d[n - 1] == L'\\' || d[n - 1] == L' ')) d[--n] = 0;
	b = (n >= 3 && d[1] == L':' && d[2] == L'\\') || (n >= 3 && d[0] == L'\\' && d[1] == L'\\');
	if (!b) {
		lstrcpyW(g_msg, L"Enter a full folder path, such as C:\\Apps\\cdie.");
		InvalidateRect(g_hwnd, NULL, FALSE);
		return;
	}

	ZeroMemory(&g_opts, sizeof(g_opts));
	lstrcpynW(g_opts.dir, d, MAX_PATH);
	g_opts.allusers = g_allusers;
	g_opts.desktop = g_desktop;
	g_opts.context = g_ctx;

	g_nlog = 0; g_pct = 0; g_ok = 0; g_err[0] = 0;
	lstrcpyW(g_status, L"Preparing...");
	g_installing = 1;
	set_page(PG_INSTALL);

	t = CreateThread(NULL, 0, install_thread, NULL, 0, NULL);
	if (t) CloseHandle(t);
	else PostMessageW(g_hwnd, WM_APP_DONE, 0, (LPARAM)dupw(L"Could not start the install thread."));
}

static DWORD WINAPI remove_thread(LPVOID param)
{
	(void)param;
	Sleep(800);
	uninstall_remove(g_un_dir, g_un_all, on_progress, NULL);
	PostMessageW(g_hwnd, WM_APP_DONE, 1, (LPARAM)dupw(L""));
	return 0;
}

static void start_remove(void)
{
	HANDLE t;
	g_nlog = 0; g_pct = 0; g_ok = 0; g_err[0] = 0;
	lstrcpyW(g_status, L"Preparing...");
	g_installing = 1;
	t = CreateThread(NULL, 0, remove_thread, NULL, 0, NULL);
	if (t) CloseHandle(t);
	else PostMessageW(g_hwnd, WM_APP_DONE, 1, (LPARAM)dupw(L""));
}

static int confirm_exit(void)
{
	if (g_installing) {
		MessageBoxW(g_hwnd, g_mode ? L"Uninstall is still running. Please wait for it to finish."
			: L"Setup is still installing. Please wait for it to finish.",
			g_mode ? L"cdie uninstall" : L"cdie setup", MB_OK | MB_ICONINFORMATION);
		return 0;
	}
	if (g_page == PG_FINISH || g_page == PG_UCONFIRM || g_page == PG_UDONE || g_page == PG_EXISTING) return 1;
	return MessageBoxW(g_hwnd, L"Exit setup? cdie has not been installed yet.",
		L"cdie setup", MB_YESNO | MB_ICONQUESTION) == IDYES;
}

static void browse_folder(void)
{
	BROWSEINFOW bi;
	LPITEMIDLIST pidl;
	WCHAR path[MAX_PATH];

	ZeroMemory(&bi, sizeof(bi));
	bi.hwndOwner = g_hwnd;
	bi.lpszTitle = L"Select the folder to install into";
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
	pidl = SHBrowseForFolderW(&bi);
	if (!pidl) return;
	if (SHGetPathFromIDListW(pidl, path)) {
		int n = lstrlenW(path);
		while (n > 3 && path[n - 1] == L'\\') path[--n] = 0;
		if (n < 5 || lstrcmpiW(path + n - 5, L"\\cdie") != 0) {
			if (n + 6 < MAX_PATH) lstrcatW(path, n > 3 ? L"\\cdie" : L"cdie");
		}
		SetWindowTextW(g_edit, path);
	}
	CoTaskMemFree(pidl);
}

static void toggle_allusers(void)
{
	WCHAR cur[MAX_PATH], olddef[MAX_PATH], newdef[MAX_PATH];
	GetWindowTextW(g_edit, cur, MAX_PATH);
	default_install_dir(g_allusers, olddef, MAX_PATH);
	g_allusers = !g_allusers;
	default_install_dir(g_allusers, newdef, MAX_PATH);
	if (!lstrcmpiW(cur, olddef)) SetWindowTextW(g_edit, newdef);
}

static void activate(int id)
{
	int* v;
	switch (id) {
	case ID_CANCEL: if (confirm_exit()) DestroyWindow(g_hwnd); break;
	case ID_BACK:
		if (g_page == PG_LOCATION && g_existing) set_page(PG_EXISTING);
		else if (g_page > PG_WELCOME) set_page(g_page - 1);
		break;
	case ID_NEXT:
		if (g_page == PG_WELCOME || g_page == PG_EXISTING) set_page(PG_LOCATION);
		else if (g_page == PG_LOCATION) start_install();
		break;
	case ID_RUN_UNINST: {
		WCHAR u[MAX_PATH];
		path_join(u, MAX_PATH, g_dir0, UNINSTALL_EXE);
		ShellExecuteW(NULL, L"open", u, NULL, g_dir0, SW_SHOWNORMAL);
		DestroyWindow(g_hwnd);
		break;
	}
	case ID_BROWSE: browse_folder(); break;
	case ID_CHK_ALL: toggle_allusers(); break;
	case ID_UNINSTALL:
		g_confirmed = 1;
		DestroyWindow(g_hwnd);
		break;
	case ID_FINISH:
		if (!g_mode && g_ok && g_launch) {
			WCHAR exe[MAX_PATH];
			path_join(exe, MAX_PATH, g_opts.dir, APP_EXE);
			ShellExecuteW(NULL, L"open", exe, NULL, g_opts.dir, SW_SHOWNORMAL);
		}
		DestroyWindow(g_hwnd);
		break;
	default:
		v = chk_ptr(id);
		if (v) *v = !*v;
		break;
	}
}

static int hit_item(int x, int y)
{
	int i;
	POINT pt;
	pt.x = x; pt.y = y;
	for (i = 0; i < g_nit; ++i)
		if (g_it[i].kind >= IK_CHECK && PtInRect(&g_it[i].r, pt)) return i;
	return -1;
}

static LRESULT CALLBACK edit_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	if (m == WM_KEYDOWN && w == VK_RETURN) { PostMessageW(g_hwnd, WM_APP_ACTIVATE, ID_NEXT, 0); return 0; }
	if (m == WM_KEYDOWN && w == VK_ESCAPE) { PostMessageW(g_hwnd, WM_APP_ACTIVATE, ID_CANCEL, 0); return 0; }
	if (m == WM_CHAR && (w == VK_RETURN || w == VK_ESCAPE)) return 0;
	return CallWindowProcW(g_editproc, h, m, w, l);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
	case WM_CREATE: {
		HINSTANCE hi = ((CREATESTRUCTW*)lp)->hInstance;
		g_hwnd = hwnd;
		g_edit = CreateWindowExW(0, L"EDIT", g_dir0, WS_CHILD | ES_AUTOHSCROLL,
			0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_PATH, hi, NULL);
		SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_font, FALSE);
		g_editproc = (WNDPROC)SetWindowLongPtrW(g_edit, GWLP_WNDPROC, (LONG_PTR)edit_proc);
		position_edit();
		build_items();
		ShowWindow(g_edit, SW_HIDE);
		return 0;
	}

	case WM_PAINT:      on_paint(hwnd); return 0;
	case WM_ERASEBKGND: return 1;

	case WM_CTLCOLOREDIT: {
		HDC dc = (HDC)wp;
		SetTextColor(dc, C_TEXT);
		SetBkColor(dc, C_HEAD);
		return (LRESULT)g_editbrush;
	}

	case WM_COMMAND:
		if (LOWORD(wp) == IDC_PATH &&
			(HIWORD(wp) == EN_SETFOCUS || HIWORD(wp) == EN_KILLFOCUS))
			InvalidateRect(hwnd, NULL, FALSE);
		return 0;

	case WM_MOUSEMOVE: {
		int h = hit_item((short)LOWORD(lp), (short)HIWORD(lp));
		if (!g_tracking) {
			TRACKMOUSEEVENT te;
			te.cbSize = sizeof(te); te.dwFlags = TME_LEAVE; te.hwndTrack = hwnd; te.dwHoverTime = 0;
			TrackMouseEvent(&te);
			g_tracking = 1;
		}
		if (h != g_hov) { g_hov = h; InvalidateRect(hwnd, NULL, FALSE); }
		return 0;
	}

	case WM_MOUSELEAVE:
		g_tracking = 0;
		if (g_hov != -1) { g_hov = -1; InvalidateRect(hwnd, NULL, FALSE); }
		return 0;

	case WM_SETCURSOR:
		if (LOWORD(lp) == HTCLIENT) {
			POINT pt;
			int h;
			GetCursorPos(&pt);
			ScreenToClient(hwnd, &pt);
			h = hit_item(pt.x, pt.y);
			if (h >= 0 && g_it[h].enabled) { SetCursor(LoadCursorW(NULL, IDC_HAND)); return TRUE; }
		}
		break;

	case WM_LBUTTONDOWN: {
		int h = hit_item((short)LOWORD(lp), (short)HIWORD(lp));
		SetFocus(hwnd);
		if (h >= 0 && g_it[h].enabled) {
			activate(g_it[h].id);
			if (IsWindow(hwnd)) InvalidateRect(hwnd, NULL, FALSE);
		}
		return 0;
	}

	case WM_KEYDOWN:
		if (wp == VK_ESCAPE) activate(ID_CANCEL);
		else if (wp == VK_RETURN) {
			if (g_page == PG_FINISH || g_page == PG_UDONE) activate(ID_FINISH);
			else if (g_page != PG_INSTALL && g_page != PG_UREMOVE && g_page != PG_UCONFIRM) activate(ID_NEXT);
		}
		return 0;

	case WM_APP_ACTIVATE:
		activate((int)wp);
		if (IsWindow(hwnd)) InvalidateRect(hwnd, NULL, FALSE);
		return 0;

	case WM_APP_PROGRESS: {
		int pct = (int)(wp & 0xFFFF), step = (int)((wp >> 16) & 1);
		WCHAR* t = (WCHAR*)lp;
		if (pct > 100) pct = 100;
		if (pct > g_pct) g_pct = pct;
		if (t) {
			if (step) {
				if (g_nlog == LOG_MAX) {
					int i;
					for (i = 1; i < LOG_MAX; ++i) lstrcpyW(g_log[i - 1], g_log[i]);
					--g_nlog;
				}
				lstrcpynW(g_log[g_nlog++], t, 120);
			}
			else lstrcpynW(g_status, t, 200);
			HeapFree(GetProcessHeap(), 0, t);
		}
		InvalidateRect(hwnd, NULL, FALSE);
		return 0;
	}

	case WM_APP_DONE: {
		WCHAR* t = (WCHAR*)lp;
		g_installing = 0;
		g_ok = (int)wp;
		if (g_ok) g_pct = 100;
		lstrcpynW(g_err, t ? t : L"", 256);
		if (t) HeapFree(GetProcessHeap(), 0, t);
		set_page(g_mode ? PG_UDONE : PG_FINISH);
		return 0;
	}

	case WM_CLOSE:
		if (confirm_exit()) DestroyWindow(hwnd);
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

static void make_fonts(void)
{
	g_font = CreateFontW(-S(13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
		CLEARTYPE_QUALITY, 0, L"Segoe UI");
	g_fontb = CreateFontW(-S(13), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
		CLEARTYPE_QUALITY, 0, L"Segoe UI");
	g_fontt = CreateFontW(-S(18), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
		CLEARTYPE_QUALITY, 0, L"Segoe UI");
}

static void dark_title_bar(HWND hwnd)
{
	typedef HRESULT(WINAPI* SetAttr)(HWND, DWORD, LPCVOID, DWORD);
	HMODULE m = LoadLibraryW(L"dwmapi.dll");
	if (m) {
		SetAttr f = (SetAttr)GetProcAddress(m, "DwmSetWindowAttribute");
		BOOL dark = TRUE;
		if (f) f(hwnd, 20, &dark, sizeof(dark));
		FreeLibrary(m);
	}
}

static int ui_open(const WCHAR* title)
{
	GdiplusStartupInput gi;
	WNDCLASSW wc;
	RECT rc;
	DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
	HDC sd;
	HINSTANCE hi = GetModuleHandleW(NULL);

	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

	ZeroMemory(&gi, sizeof(gi));
	gi.GdiplusVersion = 1;
	GdiplusStartup(&g_gdiptok, &gi, NULL);

	sd = GetDC(NULL);
	g_dpi = GetDeviceCaps(sd, LOGPIXELSX);
	ReleaseDC(NULL, sd);
	make_fonts();
	g_editbrush = CreateSolidBrush(C_HEAD);

	ZeroMemory(&wc, sizeof(wc));
	wc.lpfnWndProc = wnd_proc;
	wc.hInstance = hi;
	wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
	wc.lpszClassName = L"CdieSetupWnd";
	RegisterClassW(&wc);

	rc = R(0, 0, 680, 372);
	AdjustWindowRect(&rc, style, FALSE);
	g_hwnd = CreateWindowExW(0, wc.lpszClassName, title, style,
		(GetSystemMetrics(SM_CXSCREEN) - (rc.right - rc.left)) / 2,
		(GetSystemMetrics(SM_CYSCREEN) - (rc.bottom - rc.top)) / 2,
		rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, hi, NULL);
	if (!g_hwnd) return 0;

	dark_title_bar(g_hwnd);
	return 1;
}

static int ui_loop(void)
{
	MSG m;
	m.wParam = 0;
	while (GetMessageW(&m, NULL, 0, 0) > 0) {
		TranslateMessage(&m);
		DispatchMessageW(&m);
	}
	return (int)m.wParam;
}

static void ui_close(void)
{
	if (g_font)      DeleteObject(g_font);
	if (g_fontb)     DeleteObject(g_fontb);
	if (g_fontt)     DeleteObject(g_fontt);
	if (g_editbrush) DeleteObject(g_editbrush);
	g_font = g_fontb = g_fontt = NULL;
	g_editbrush = NULL;
	GdiplusShutdown(g_gdiptok);
}

int ui_setup_run(const WCHAR* dir, int allusers, int existing, const WCHAR* space)
{
	int rc;

	g_mode = 0;
	g_allusers = allusers;
	g_existing = existing;
	lstrcpynW(g_dir0, dir, MAX_PATH);
	lstrcpynW(g_space, space ? space : L"", 96);

	if (!ui_open(L"cdie setup")) return 1;
	set_page(existing ? PG_EXISTING : PG_WELCOME);
	ShowWindow(g_hwnd, SW_SHOW);
	UpdateWindow(g_hwnd);

	rc = ui_loop();
	ui_close();
	return rc;
}

static void ui_run_uninstall(int page, const WCHAR* dir, int allusers)
{
	g_mode = 1;
	lstrcpynW(g_un_dir, dir, MAX_PATH);
	g_un_all = allusers;
	g_dir0[0] = 0;

	if (!ui_open(L"cdie uninstall")) return;
	set_page(page);
	ShowWindow(g_hwnd, SW_SHOW);
	UpdateWindow(g_hwnd);
	if (page == PG_UREMOVE) start_remove();

	ui_loop();
	ui_close();
}

int ui_uninstall_confirm(const WCHAR* dir)
{
	g_confirmed = 0;
	ui_run_uninstall(PG_UCONFIRM, dir, 0);
	return g_confirmed;
}

void ui_uninstall_progress(const WCHAR* dir, int allusers)
{
	ui_run_uninstall(PG_UREMOVE, dir, allusers);
}