/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#include "core/tray_win32.h"

#define WM_TRAYICON      (WM_APP + 1)
#define IDM_TRAY_SHOW    1
#define IDM_TRAY_EXIT    2
#define IDM_TRAY_MONITOR 3

#ifndef NIN_BALLOONUSERCLICK
#define NIN_BALLOONUSERCLICK (WM_USER + 5)
#endif

static int g_enabled;       
static int g_added;
static int g_persistent;
static int g_monitor;
static UINT g_taskbarCreated;
static NOTIFYICONDATAW g_nid;
static TrayExitFn g_exit_cb;
static void* g_exit_user;
static TrayClickFn g_click_cb;
static void* g_click_user;
static TrayMonitorFn g_monitor_cb;
static void* g_monitor_user;

void tray_init(TrayExitFn exit_cb, void* user)
{
  g_exit_cb = exit_cb;
  g_exit_user = user;
  g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
}

void tray_set_callbacks(TrayClickFn click, void* cu, TrayMonitorFn mon, void* mu)
{
  g_click_cb = click; g_click_user = cu;
  g_monitor_cb = mon; g_monitor_user = mu;
}

void tray_set_enabled(int enabled) { g_enabled = enabled ? 1 : 0; }
int  tray_get_enabled(void) { return g_enabled; }
void tray_set_monitor(int on) { g_monitor = on ? 1 : 0; }
int  tray_get_monitor(void) { return g_monitor; }

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

void tray_set_persistent(HWND hwnd, int on)
{
  g_persistent = on ? 1 : 0;
  if (on) {
    tray_add(hwnd);
  }
  else {
    tray_remove();
    if (!IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_SHOW);
  }
}

void tray_notify(HWND hwnd, const WCHAR* title, const WCHAR* text)
{
  NOTIFYICONDATAW n;
  tray_add(hwnd);
  if (!g_added) return;
  n = g_nid;
  n.uFlags = NIF_INFO;
  n.dwInfoFlags = NIIF_INFO;
  lstrcpynW(n.szInfoTitle, title, ARRAYSIZE(n.szInfoTitle));
  lstrcpynW(n.szInfo, text, ARRAYSIZE(n.szInfo));
  Shell_NotifyIconW(NIM_MODIFY, &n);
}

static void tray_restore(HWND hwnd)
{
  if (!g_persistent) tray_remove();
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
  AppendMenuW(menu, MF_STRING | (g_monitor ? MF_CHECKED : 0),
    IDM_TRAY_MONITOR, L"Monitor downloads");
  AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
  AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT, L"Exit");
  SetMenuDefaultItem(menu, IDM_TRAY_SHOW, FALSE);
  GetCursorPos(&pt);
  SetForegroundWindow(hwnd);
  cmd = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
    pt.x, pt.y, 0, hwnd, NULL);
  PostMessageW(hwnd, WM_NULL, 0, 0);
  DestroyMenu(menu);

  if (cmd == IDM_TRAY_SHOW) {
    tray_restore(hwnd);
  }
  else if (cmd == IDM_TRAY_MONITOR) {
    g_monitor = !g_monitor;
    if (g_monitor_cb) g_monitor_cb(g_monitor_user, g_monitor);
  }
  else if (cmd == IDM_TRAY_EXIT) {
    tray_remove();
    if (g_exit_cb) g_exit_cb(g_exit_user);
  }
}

int tray_handle_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* result)
{
  *result = 0;

  if (g_taskbarCreated && msg == g_taskbarCreated) {
    g_added = 0;
    if (g_persistent) tray_add(hwnd);
    return 0;
  }

  switch (msg) {
  case WM_CLOSE:
    if (g_persistent) {         
      ShowWindow(hwnd, SW_HIDE);
      return 1;
    }
    return 0;

  case WM_SYSCOMMAND:
    if ((wp & 0xFFF0) == SC_MINIMIZE && g_enabled || g_persistent) {
      tray_add(hwnd);
      if (g_added) {
        ShowWindow(hwnd, SW_HIDE);
        return 1;
      }
    }
    return 0;

  case WM_TRAYICON:
    if (lp == WM_LBUTTONUP || lp == WM_LBUTTONDBLCLK) tray_restore(hwnd);
    else if (lp == WM_RBUTTONUP) tray_menu(hwnd);
    else if (lp == NIN_BALLOONUSERCLICK) {
      tray_restore(hwnd);
      if (g_click_cb) g_click_cb(g_click_user);
    }
    return 1;

  case WM_NCDESTROY:
    tray_remove();
    return 0;

  default:
    return 0;
  }
}