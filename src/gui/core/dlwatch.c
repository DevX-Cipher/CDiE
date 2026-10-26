#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shlobj.h>
#include <string.h>
#include "core/dlwatch.h"
#include "core/gui_backend.h"
#include "core/config.h"
#include "core/tray_win32.h"

#define IDT_DLSTABLE 0xD101
#define DL_MAX       8

typedef struct { WCHAR path[MAX_PATH]; ULONGLONG size; int stable, age; } DlPending;
typedef struct { char text[200]; int count; } quick_scan;
typedef struct { int id, got; } first_type;

static HANDLE g_thread, g_stop;
static HWND   g_wnd;
static UINT   g_msg;

static DlWatchHost g_host;
static int       g_on;
static char* g_lastDl;
static DlPending g_dl[DL_MAX];
static int       g_dlCount;
static WCHAR     g_dlDone[MAX_PATH];
static ULONGLONG g_dlDoneTick;

int dlwatch_running(void) { return g_thread != NULL; }
void dlwatch_init(const DlWatchHost* host) { g_host = *host; }
int  dlwatch_enabled(void) { return g_on; }

int dlwatch_is_temp(const WCHAR* path)
{
  static const WCHAR* ext[] = { L".crdownload", L".part", L".partial", L".tmp",
    L".download", L".opdownload", L".!ut", L".filepart" };
  const WCHAR* dot = NULL, * p;
  size_t i;
  for (p = path; *p; ++p) {
    if (*p == L'\\' || *p == L'/') dot = NULL;
    else if (*p == L'.') dot = p;
  }
  if (!dot) return 0;
  for (i = 0; i < sizeof(ext) / sizeof(ext[0]); ++i)
    if (!lstrcmpiW(dot, ext[i])) return 1;
  return 0;
}

static void handle_file(const WCHAR* dir, const WCHAR* name, int complete)
{
  WCHAR full[MAX_PATH * 2];
  DWORD attr;
  WCHAR* copy;
  SIZE_T len;

  if (dlwatch_is_temp(name)) return;
  if (lstrlenW(dir) + lstrlenW(name) + 2 >= (int)(sizeof(full) / sizeof(WCHAR))) return;
  lstrcpyW(full, dir);
  lstrcatW(full, L"\\");
  lstrcatW(full, name);

  attr = GetFileAttributesW(full);
  if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) return;

  len = (SIZE_T)lstrlenW(full) + 1;
  copy = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
  if (!copy) return;
  CopyMemory(copy, full, len * sizeof(WCHAR));
  if (!PostMessageW(g_wnd, g_msg, (WPARAM)complete, (LPARAM)copy))
    HeapFree(GetProcessHeap(), 0, copy);
}

static DWORD WINAPI watch_thread(LPVOID param)
{
  PWSTR known = NULL;
  WCHAR dir[MAX_PATH * 2];
  HANDLE hDir;
  OVERLAPPED ov;
  DWORD buf[4096];
  int prev_temp = 0;
  (void)param;

  if (FAILED(SHGetKnownFolderPath(&FOLDERID_Downloads, 0, NULL, &known)) || !known)
    return 1;
  lstrcpynW(dir, known, (int)(sizeof(dir) / sizeof(WCHAR)));
  CoTaskMemFree(known);

  hDir = CreateFileW(dir, FILE_LIST_DIRECTORY,
    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
  if (hDir == INVALID_HANDLE_VALUE) return 1;

  ZeroMemory(&ov, sizeof(ov));
  ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
  if (!ov.hEvent) { CloseHandle(hDir); return 1; }

  for (;;) {
    HANDLE waits[2];
    DWORD w, n = 0;
    ResetEvent(ov.hEvent);
    if (!ReadDirectoryChangesW(hDir, buf, sizeof(buf), FALSE,
      FILE_NOTIFY_CHANGE_FILE_NAME, NULL, &ov, NULL))
      break;
    waits[0] = g_stop; waits[1] = ov.hEvent;
    w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
    if (w == WAIT_OBJECT_0) {
      CancelIoEx(hDir, &ov);
      GetOverlappedResult(hDir, &ov, &n, TRUE);
      break;
    }
    if (!GetOverlappedResult(hDir, &ov, &n, FALSE) || n == 0) continue;
    {
      BYTE* p = (BYTE*)buf;
      for (;;) {
        FILE_NOTIFY_INFORMATION* fni = (FILE_NOTIFY_INFORMATION*)p;
        WCHAR name[MAX_PATH];
        DWORD chars = fni->FileNameLength / sizeof(WCHAR);
        if (chars > 0 && chars < MAX_PATH) {
          CopyMemory(name, fni->FileName, chars * sizeof(WCHAR));
          name[chars] = 0;
          if (fni->Action == FILE_ACTION_RENAMED_OLD_NAME) {
            prev_temp = dlwatch_is_temp(name);
          }
          else if (fni->Action == FILE_ACTION_RENAMED_NEW_NAME) {
            handle_file(dir, name, prev_temp);   
            prev_temp = 0;
          }
          else if (fni->Action == FILE_ACTION_ADDED) {
            handle_file(dir, name, 0);           
          }
        }
        if (!fni->NextEntryOffset) break;
        p += fni->NextEntryOffset;
      }
    }
  }
  CloseHandle(ov.hEvent);
  CloseHandle(hDir);
  return 0;
}

int dlwatch_start(HWND notify, UINT msg)
{
  if (g_thread) return 1;
  g_wnd = notify; g_msg = msg;
  g_stop = CreateEventW(NULL, TRUE, FALSE, NULL);
  if (!g_stop) return 0;
  g_thread = CreateThread(NULL, 0, watch_thread, NULL, 0, NULL);
  if (!g_thread) { CloseHandle(g_stop); g_stop = NULL; return 0; }
  return 1;
}

void dlwatch_stop(void)
{
  if (!g_thread) return;
  SetEvent(g_stop);
  WaitForSingleObject(g_thread, 3000);
  CloseHandle(g_thread); g_thread = NULL;
  CloseHandle(g_stop);   g_stop = NULL;
}


static WCHAR* utf8_to_wide(const char* text)
{
  int length;
  WCHAR* wide;
  if (!text) return NULL;
  length = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
  if (length <= 0) return NULL;
  wide = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)length * sizeof(WCHAR));
  if (wide) MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, length);
  return wide;
}

static char* wide_to_utf8(const WCHAR* wide)
{
  int length;
  char* text;
  if (!wide) return NULL;
  length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
  if (length <= 0) return NULL;
  text = (char*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)length);
  if (text) WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, length, NULL, NULL);
  return text;
}

static void host_status(const char* text)
{
  if (g_host.set_status) g_host.set_status(text, g_host.user);
}

static void first_type_cb(int type_id, const char* name, void* u)
{
  first_type* f = (first_type*)u;
  (void)name;
  if (!f->got) { f->id = type_id; f->got = 1; }
}

static int contains_i(const char* s, const char* needle)
{
  size_t n = lstrlenA(needle);
  if (!s) return 0;
  for (; *s; ++s)
    if (CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, s, (int)n, needle, (int)n) == CSTR_EQUAL
      && lstrlenA(s) >= (int)n)
      return 1;
  return 0;
}

static void quick_cb(const char* type, const char* name, const char* version,
  const char* info, void* u)
{
  quick_scan* q = (quick_scan*)u;
  char piece[200];
  int used;

  if (!type || (!contains_i(type, "Packer") && !contains_i(type, "Protector")))
    return;

  q->count++;
  if (q->count > 2) return;
  used = lstrlenA(q->text);
  wsprintfA(piece, "%s%s: %s%s%s%s%s%s",
    q->count > 1 ? "\n" : "",
    type,
    name ? name : "",
    (version && version[0]) ? " " : "",
    version ? version : "",
    (info && info[0]) ? " [" : "",
    info ? info : "",
    (info && info[0]) ? "]" : "");
  lstrcpynA(q->text + used, piece, (int)sizeof(q->text) - used);
}

static void on_download_found(const WCHAR* wpath)
{
  char* path = wide_to_utf8(wpath);
  first_type ft = { 0, 0 };
  quick_scan q;
  char* report = NULL;
  char* type = NULL;
  WCHAR text[1024];
  ULONGLONG t0 = GetTickCount64();
  unsigned scan_ms;
  WCHAR* summary;
  const WCHAR* name = wpath;
  const WCHAR* p;

  if (!path) return;
  if (config_db_required()) { HeapFree(GetProcessHeap(), 0, path); return; }
  ZeroMemory(&q, sizeof(q));

  gui_backend_set_flags(0, 1, 1, 0, 0, 0);
  gui_backend_file_types(path, first_type_cb, &ft);
  gui_backend_scan_results_type(path, ft.id, quick_cb, &q, &report, &type);
  if (report) gui_backend_free(report);
  if (type)   gui_backend_free(type);
  if (g_host.restore_flags) g_host.restore_flags(g_host.user);

  scan_ms = (unsigned)(GetTickCount64() - t0);
  for (p = wpath; *p; ++p)
    if (*p == L'\\') name = p + 1;
  summary = utf8_to_wide(q.count ? q.text : "No packer or protector found");
  wsprintfW(text, L"%s\n%s (%u ms)", name, summary ? summary : L"", scan_ms);
  if (summary) HeapFree(GetProcessHeap(), 0, summary);

  tray_notify(g_host.hwnd, L"New download scanned", text);

  if (g_lastDl) HeapFree(GetProcessHeap(), 0, g_lastDl);
  g_lastDl = path;
}

static int dl_file_size(const WCHAR* path, ULONGLONG* size)
{
  WIN32_FILE_ATTRIBUTE_DATA fa;
  if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fa)) return 0;
  if (fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return 0;
  *size = ((ULONGLONG)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
  return 1;
}

static void dl_queue(HWND hwnd, const WCHAR* path)
{
  int i;
  if (dlwatch_is_temp(path)) return;
  if (!lstrcmpiW(path, g_dlDone) && GetTickCount64() - g_dlDoneTick < 15000) return;
  for (i = 0; i < g_dlCount; ++i)
    if (!lstrcmpiW(g_dl[i].path, path)) { g_dl[i].stable = 0; return; }
  if (g_dlCount == DL_MAX) return;
  lstrcpynW(g_dl[g_dlCount].path, path, MAX_PATH);
  {
    ULONGLONG s;
    g_dl[g_dlCount].size = dl_file_size(path, &s) ? s : (ULONGLONG)-1;
  }
  g_dl[g_dlCount].stable = 0;
  g_dl[g_dlCount].age = 0;
  g_dlCount++;
  SetTimer(hwnd, IDT_DLSTABLE, 250, NULL);
}

static void dl_clear(HWND hwnd)
{
  g_dlCount = 0;
  KillTimer(hwnd, IDT_DLSTABLE);
}

static void dl_tick(HWND hwnd)
{
  int i;
  for (i = g_dlCount - 1; i >= 0; --i) {
    DlPending* e = &g_dl[i];
    ULONGLONG size;
    HANDLE h;
    WCHAR path[MAX_PATH];
    int remove = 0, ready = 0;

    if (!dl_file_size(e->path, &size) || ++e->age > 14400) remove = 1;
    else if (size != e->size) { e->size = size; e->stable = 0; }
    else if (size > 0 && ++e->stable >= 2) {
      h = CreateFileW(e->path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
      if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); ready = 1; remove = 1; }
    }
    if (remove) {
      lstrcpynW(path, e->path, MAX_PATH);
      g_dl[i] = g_dl[--g_dlCount];
      if (ready) {
        lstrcpynW(g_dlDone, path, MAX_PATH);
        g_dlDoneTick = GetTickCount64();
        on_download_found(path);
      }
    }
  }
  if (!g_dlCount) KillTimer(hwnd, IDT_DLSTABLE);
}

static void dl_complete(HWND hwnd, const WCHAR* path)
{
  int i;
  HANDLE h;
  if (dlwatch_is_temp(path)) return;
  if (!lstrcmpiW(path, g_dlDone) && GetTickCount64() - g_dlDoneTick < 15000) return;

  for (i = 0; i < g_dlCount; ++i)
    if (!lstrcmpiW(g_dl[i].path, path)) { g_dl[i] = g_dl[--g_dlCount]; break; }

  h = CreateFileW(path, GENERIC_READ,
    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
  if (h == INVALID_HANDLE_VALUE) { dl_queue(hwnd, path); return; }
  CloseHandle(h);

  lstrcpynW(g_dlDone, path, MAX_PATH);
  g_dlDoneTick = GetTickCount64();
  on_download_found(path);
}



void dlwatch_set_enabled(int on)
{
  if (on) {
    if (!dlwatch_start(g_host.hwnd, WM_DLWATCH)) {
      g_on = 0;
      tray_set_monitor(0);
      host_status("Could not watch the Downloads folder.");
      return;
    }
    g_on = 1;
    tray_set_monitor(1);
    tray_set_persistent(g_host.hwnd, 1);
    host_status("Download monitoring on. Closing the window keeps cdie in the tray.");
  }
  else {
    dlwatch_stop();
    dl_clear(g_host.hwnd);
    g_on = 0;
    tray_set_monitor(0);
    tray_set_persistent(g_host.hwnd, 0);
    host_status("Download monitoring off.");
  }
}

void dlwatch_shutdown(void)
{
  dlwatch_stop();
  if (g_lastDl) HeapFree(GetProcessHeap(), 0, g_lastDl);
  g_lastDl = NULL;
}

int dlwatch_handle_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* result)
{
  if (msg == WM_DLWATCH) {
    WCHAR* p = (WCHAR*)lp;
    if (p) {
      if (g_on) {
        if (wp) dl_complete(hwnd, p);  
        else    dl_queue(hwnd, p);     
      }
      HeapFree(GetProcessHeap(), 0, p);
    }
    *result = 0;
    return 1;
  }
  if (msg == WM_TIMER && wp == IDT_DLSTABLE) {
    dl_tick(hwnd);
    *result = 0;
    return 1;
  }
  return 0;
}

void dlwatch_balloon_click(void* user)
{
  (void)user;
  if (g_lastDl && g_host.request_scan)
    g_host.request_scan(g_lastDl, g_host.user);
}

void dlwatch_menu_toggle(void* user, int enabled)
{
  (void)user;
  dlwatch_set_enabled(enabled);
}