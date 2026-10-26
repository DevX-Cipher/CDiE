#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#include "core/tray_win32.h"

#define WM_TRAYICON    (WM_APP + 1)
#define IDM_TRAY_SHOW  1
#define IDM_TRAY_EXIT  2

static int g_enabled;
static int g_added;
static NOTIFYICONDATAW g_nid;
static TrayExitFn g_exit_cb;
static void* g_exit_user;

void tray_init(TrayExitFn exit_cb, void* user)
{
  g_exit_cb = exit_cb;
  g_exit_user = user;
}

void tray_set_enabled(int enabled) { g_enabled = enabled ? 1 : 0; }
int  tray_get_enabled(void) { return g_enabled; }

static void tray_add(HWND hwnd)
{
  HICON icon;
  if (g_added) return;
  icon = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0);
  if (!icon) icon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICONSM);
  if (!icon) icon = LoadIconW(NULL, IDI_APPLICATION);
  ZeroMemory(&g_nid, sizeof(g_nid));
  g_nid.cbSize = sizeof(g_nid);
  g_nid.hWnd = hwnd;
  g_nid.uID = 1;
  g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  g_nid.uCallbackMessage = WM_TRAYICON;
  g_nid.hIcon = icon;
  lstrcpynW(g_nid.szTip, L"cdie", ARRAYSIZE(g_nid.szTip));
  g_added = Shell_NotifyIconW(NIM_ADD, &g_nid) ? 1 : 0;
}

void tray_remove(void)
{
  if (!g_added) return;
  Shell_NotifyIconW(NIM_DELETE, &g_nid);
  g_added = 0;
}

static void tray_restore(HWND hwnd)
{
  tray_remove();
  ShowWindow(hwnd, SW_SHOW);
  if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
  SetForegroundWindow(hwnd);
}

static void tray_menu(HWND hwnd)
{
  POINT pt;
  int cmd;
  HMENU menu = CreatePopupMenu();
  if (!menu) return;
  AppendMenuW(menu, MF_STRING, IDM_TRAY_SHOW, L"Show cdie");
  AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
  AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT, L"Exit");
  SetMenuDefaultItem(menu, IDM_TRAY_SHOW, FALSE);
  GetCursorPos(&pt);
  SetForegroundWindow(hwnd);   /* required or the menu won't close */
  cmd = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
    pt.x, pt.y, 0, hwnd, NULL);
  PostMessageW(hwnd, WM_NULL, 0, 0);
  DestroyMenu(menu);

  if (cmd == IDM_TRAY_SHOW) {
    tray_restore(hwnd);
  }
  else if (cmd == IDM_TRAY_EXIT) {
    tray_remove();
    if (g_exit_cb) g_exit_cb(g_exit_user);
  }
}

int tray_handle_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* result)
{
  *result = 0;
  switch (msg) {
  case WM_SYSCOMMAND:
    if ((wp & 0xFFF0) == SC_MINIMIZE && g_enabled) {
      tray_add(hwnd);
      if (g_added) {               /* only hide if the icon really exists */
        ShowWindow(hwnd, SW_HIDE);
        return 1;
      }
    }
    return 0;

  case WM_TRAYICON:
    if (lp == WM_LBUTTONUP || lp == WM_LBUTTONDBLCLK) tray_restore(hwnd);
    else if (lp == WM_RBUTTONUP) tray_menu(hwnd);
    return 1;

  case WM_NCDESTROY:
    tray_remove();
    return 0;                      /* let the caller continue its own cleanup */

  default:
    return 0;
  }
}