#ifndef DLWATCH_H
#define DLWATCH_H

#include <windows.h>

#define WM_DLWATCH (WM_APP + 2)

typedef struct {
  HWND hwnd;                                                /* main window */
  void (*request_scan)(const char* utf8_path, void* user);  /* balloon clicked */
  void (*set_status)(const char* utf8_text, void* user);
  void (*restore_flags)(void* user);                        /* re-apply user's scan flags */
  void* user;
} DlWatchHost;

/* low-level watcher thread */
int  dlwatch_start(HWND notify, UINT msg);
void dlwatch_stop(void);
int  dlwatch_running(void);
int  dlwatch_is_temp(const WCHAR* path);

/* monitor (UI thread) */
void dlwatch_init(const DlWatchHost* host);
void dlwatch_set_enabled(int on);
int  dlwatch_enabled(void);
void dlwatch_shutdown(void);
int  dlwatch_handle_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* result);
void dlwatch_balloon_click(void* user);
void dlwatch_menu_toggle(void* user, int enabled);

#endif