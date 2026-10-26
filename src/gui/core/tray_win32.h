#ifndef CDIE_TRAY_WIN32_H
#define CDIE_TRAY_WIN32_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

  typedef void (*TrayExitFn)(void* user);

  /* Call once after the main window exists. exit_cb runs when the user picks Exit. */
  void tray_init(TrayExitFn exit_cb, void* user);

  /* "Minimize to tray" setting */
  void tray_set_enabled(int enabled);
  int  tray_get_enabled(void);

  /* Call at the top of the main window subclass.
     Returns 1 if the message was handled (use *result as the return value). */
  int  tray_handle_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* result);

  /* Remove the icon (safe to call any time) */
  void tray_remove(void);

#ifdef __cplusplus
}
#endif
#endif