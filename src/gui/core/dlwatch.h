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
#ifndef DLWATCH_H
#define DLWATCH_H
#ifdef _WIN32
#include <windows.h>
#endif

typedef struct DlWatchHost {
  void (*set_status)(const char* text, void* user);
  void (*restore_flags)(void* user);
  void (*request_scan)(const char* utf8_path, void* user);
  void* user;
  void* native;           
} DlWatchHost;

void dlwatch_init(const DlWatchHost* host);
int  dlwatch_running(void);
int  dlwatch_enabled(void);
void dlwatch_set_enabled(int on);
void dlwatch_shutdown(void);
int  dlwatch_is_temp(const char* utf8_path);
void dlwatch_balloon_click(void* user);
void dlwatch_menu_toggle(void* user, int enabled);

void dlwatch_on_event(int complete, const char* utf8_path);
void dlwatch_on_timer(void);
const DlWatchHost* dlwatch_host(void);

#ifdef _WIN32
int dlwatch_handle_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* result);
#endif
#endif
