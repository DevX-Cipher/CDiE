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
#ifndef TRAY_H
#define TRAY_H
#ifdef _WIN32
#include <windows.h>
#endif

typedef void (*TrayExitFn)(void* user);
typedef void (*TrayClickFn)(void* user);
typedef void (*TrayMonitorFn)(void* user, int enabled);

typedef struct {
  void (*show)(void* user);
  void (*hide)(void* user);
  int  (*is_visible)(void* user);
  void* user;
} TrayWindowOps;

void tray_init(TrayExitFn exit_cb, void* user);
void tray_set_callbacks(TrayClickFn click, void* cu, TrayMonitorFn mon, void* mu);
void tray_set_enabled(int enabled);     /* minimize-to-tray */
int  tray_get_enabled(void);
void tray_set_monitor(int on);
int  tray_get_monitor(void);
void tray_set_persistent(int on);       
void tray_notify(const char* utf8_title, const char* utf8_text);
void tray_remove(void);

#ifdef _WIN32
void tray_win32_attach(HWND hwnd);     
int  tray_handle_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* result);
#else
void tray_set_window_ops(const TrayWindowOps* ops);
int  tray_intercept_close(void);
int  tray_intercept_minimize(void);
#endif
#endif
