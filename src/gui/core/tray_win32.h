#ifndef CDIE_TRAY_WIN32_H
#define CDIE_TRAY_WIN32_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

  typedef void (*TrayExitFn)(void* user);

  typedef void (*TrayClickFn)(void* user);
  typedef void (*TrayMonitorFn)(void* user, int enabled);

  void tray_set_persistent(HWND hwnd, int on);
  void tray_set_monitor(int on);
  int  tray_get_monitor(void);
  void tray_set_callbacks(TrayClickFn click, void* click_user,
    TrayMonitorFn monitor, void* monitor_user);

  void tray_notify(HWND hwnd, const WCHAR* title, const WCHAR* text);
  void tray_init(TrayExitFn exit_cb, void* user);
  void tray_set_enabled(int enabled);
  int  tray_get_enabled(void);

  int  tray_handle_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* result);

  void tray_remove(void);

#ifdef __cplusplus
}
#endif
#endif