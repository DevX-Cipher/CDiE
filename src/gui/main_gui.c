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

 /* The main controls and common modal forms belong to xxwidgets. Win32 supplies
  * file/folder pickers, drag-and-drop and pixel layout. The engine stays behind
  * gui_backend.h because its PE declarations must not meet <windows.h>.
  * Scratch storage uses the process heap; xxwidgets supplies the hosted GUI
  * runtime while the console keeps its own runtime configuration. */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")

#ifndef TVM_SETLINECOLOR
#define TVM_SETLINECOLOR (TV_FIRST + 40)
#endif
#include <xxwidgets/xxwidgets.h>
#include <xxwidgets/xxwidgets_scan_panel.h>
#include <xxwidgets/xxwidgets_combobox.h>
#include <xxwidgets/xxwidgets_scan_options.h>
#include <xxwidgets/xxwidgets_font_options.h>
#include <xxwidgets/xxwidgets_context_options.h>
#include <xxwidgets/xxwidgets_optimization_options.h>
#include <limits.h>
#include <string.h>

#include "core/gui_backend.h"
#include "resource.h"
#include "theme/theme.h"
#include "controls/sidebar.h"
#include "panels/header.h"
#include "panels/infobar.h"
#include "controls/tabbar.h"
#include "controls/bottombar.h"

#define CR(c) RGB(((c) >> 16) & 255, ((c) >> 8) & 255, (c) & 255)

#define IDC_EDIT_PATH      102
#define IDC_BTN_BROWSE     103
#define IDC_COMBO_TYPE     104
#define IDC_COMBO_FLAGS    105
#define IDC_COMBO_DATABASES 106
#define IDC_STATIC_MSEC    107
#define IDC_BTN_SCAN       108
#define IDC_SCANRESULTS    109
#define IDC_BTN_EXIT       110
#define IDC_BTN_OPTIONS    111
#define IDC_BTN_ABOUT      112
#define IDC_BTN_REPORT     113
#define IDC_STATUS         114
#define IDC_BTN_DB_PATHS   115
#define IDC_BTN_FONTS      116
#define IDC_BTN_CONTEXT    117
#define IDC_BTN_OPTIMIZATION 118

enum gui_action {
    ACTION_NONE, ACTION_SCAN, ACTION_BROWSE,
    ACTION_OPTIONS, ACTION_ABOUT, ACTION_REPORT,
    ACTION_DATABASE_PATHS, ACTION_FONTS, ACTION_CONTEXT, ACTION_OPTIMIZATION
};

static HINSTANCE g_hInst;
static xxwidgets_app    *g_app;
static xxwidgets_widget *g_window, *g_fileLabel, *g_path, *g_browse, *g_type;
static xxwidgets_widget *g_flags, *g_databases, *g_msec, *g_scan, *g_results;
static xxwidgets_widget *g_typeLabel, *g_flagLabel, *g_databaseLabel, *g_databasePaths;
static xxwidgets_widget *g_options, *g_aboutButton, *g_reportButton, *g_exit, *g_status;
static xxwidgets_widget *g_fontsButton, *g_contextButton;
static xxwidgets_widget *g_optimizationButton;
static xxwidgets_scan_panel  *g_panel;
static xxwidgets_about_dialog *g_about;

/* Custom dark-chrome child windows */
static Sidebar* g_sidebar;
static Header* g_header_wnd;
static Infobar* g_infobar_wnd;
//static Tabbar* g_tabbar;
static Bottombar* g_bottombar;

static int  g_running, g_pending;
static char *g_typePath;
static int  g_minWindowWidth  = 900;
static int  g_minWindowHeight = 540;

static int g_bFlagDeep = 1, g_bFlagHeur = 1, g_bFlagVerbose = 1;
static int g_bFlagAggr, g_bFlagHideUnknown, g_bFlagFormat;
static int g_bDbExtra = 1, g_bDbCustom = 1;
static void layout(void);

static HWND native(xxwidgets_widget *widget)
{
    return (HWND)xxwidgets_widget_native_handle(widget);
}

static void apply_flags_to_backend(void)
{
    gui_backend_set_flags(g_bFlagDeep, g_bFlagHeur, g_bFlagVerbose,
                          g_bFlagAggr, g_bFlagHideUnknown, g_bFlagFormat);
    gui_backend_set_databases(g_bDbExtra, g_bDbCustom);
}

static WCHAR *utf8_to_wide(const char *text)
{
    int length;
    WCHAR *wide;
    if (!text) return NULL;
    length = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (length <= 0) return NULL;
    wide = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)length * sizeof(WCHAR));
    if (wide) MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, length);
    return wide;
}

static char *wide_to_utf8(const WCHAR *wide)
{
    int length;
    char *text;
    if (!wide) return NULL;
    length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (length <= 0) return NULL;
    text = (char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)length);
    if (text) WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, length, NULL, NULL);
    return text;
}

static char *copy_utf8(const char *text)
{
    SIZE_T length = 0;
    char *copy;
    if (!text) text = "";
    while (text[length]) ++length;
    copy = (char *)HeapAlloc(GetProcessHeap(), 0, length + 1);
    if (copy) CopyMemory(copy, text, length + 1);
    return copy;
}

static void set_status_wide(const WCHAR *wide)
{
    char *text = wide_to_utf8(wide);
    if (text) {
        xxwidgets_widget_set_text(g_status, text);
        HeapFree(GetProcessHeap(), 0, text);
    }
}

static void report_ui_error(const char *operation, xxwidgets_status status)
{
    WCHAR text[256];
    WCHAR *name   = utf8_to_wide(operation);
    WCHAR *detail = utf8_to_wide(xxwidgets_status_string(status));
    wsprintfW(text, L"%s: %s",
              name   ? name   : L"Interface error",
              detail ? detail : L"unknown error");
    set_status_wide(text);
    if (name)   HeapFree(GetProcessHeap(), 0, name);
    if (detail) HeapFree(GetProcessHeap(), 0, detail);
}

typedef struct {
    WCHAR type_str[80];
    WCHAR size_str[80];
    WCHAR size_sub[80];
    WCHAR ep_str[80];
    WCHAR ep_sub[80];
    WCHAR base_str[80];
    WCHAR base_sub[80];
} FileInfo;

static void format_size(ULONGLONG bytes, WCHAR *main_out, WCHAR *sub_out)
{
    if (bytes >= 1024ULL * 1024) {
        ULONGLONG n100 = (bytes * 100ULL) / (1024ULL * 1024);
        wsprintfW(main_out, L"%u.%02u MiB",
                  (unsigned)(n100 / 100), (unsigned)(n100 % 100));
    } else if (bytes >= 1024) {
        ULONGLONG n10 = (bytes * 10ULL) / 1024;
        wsprintfW(main_out, L"%u.%u KiB",
                  (unsigned)(n10 / 10), (unsigned)(n10 % 10));
    } else {
        wsprintfW(main_out, L"%u B", (unsigned)bytes);
    }
    if (bytes < 1000000ULL) {
        wsprintfW(sub_out, L"%u bytes", (unsigned)bytes);
    } else {
        unsigned hi  = (unsigned)(bytes / 1000000ULL);
        unsigned lo  = (unsigned)(bytes % 1000000ULL);
        wsprintfW(sub_out, L"%u,%06u bytes", hi, lo);
    }
}

static void get_file_info(const char *utf8_path, const char *type_name, FileInfo *fi)
{
    WCHAR *wpath = utf8_to_wide(utf8_path);

    /* type string */
    if (type_name && type_name[0]) {
        WCHAR *wt = utf8_to_wide(type_name);
        if (wt) { lstrcpynW(fi->type_str, wt, 79); HeapFree(GetProcessHeap(), 0, wt); }
        else lstrcpyW(fi->type_str, L"Unknown");
    } else {
        lstrcpyW(fi->type_str, L"Unknown");
    }

    /* file size */
    lstrcpyW(fi->size_str, L"N/A");
    lstrcpyW(fi->size_sub, L"");
    if (wpath) {
        WIN32_FILE_ATTRIBUTE_DATA fa = {0};
        if (GetFileAttributesExW(wpath, GetFileExInfoStandard, &fa)) {
            ULONGLONG bytes =
                ((ULONGLONG)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
            format_size(bytes, fi->size_str, fi->size_sub);
        }
    }

    /* default EP / base */
    lstrcpyW(fi->ep_str,   L"N/A");
    lstrcpyW(fi->ep_sub,   L"");
    lstrcpyW(fi->base_str, L"N/A");
    lstrcpyW(fi->base_sub, L"");

    /* Try to read PE optional header */
    if (wpath) {
        HANDLE hf = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ,
                                NULL, OPEN_EXISTING, 0, NULL);
        if (hf != INVALID_HANDLE_VALUE) {
            WORD  dos_magic = 0;
            DWORD read = 0, pe_off = 0;
            if (ReadFile(hf, &dos_magic, 2, &read, NULL) && read == 2
                && dos_magic == 0x5A4D) {
                SetFilePointer(hf, 0x3C, NULL, FILE_BEGIN);
                if (ReadFile(hf, &pe_off, 4, &read, NULL) && read == 4) {
                    DWORD pe_sig = 0;
                    SetFilePointer(hf, (LONG)pe_off, NULL, FILE_BEGIN);
                    if (ReadFile(hf, &pe_sig, 4, &read, NULL)
                        && read == 4 && pe_sig == 0x00004550) {
                        WORD opt_magic = 0;
                        SetFilePointer(hf, (LONG)(pe_off + 24), NULL, FILE_BEGIN);
                        if (ReadFile(hf, &opt_magic, 2, &read, NULL) && read == 2) {
                            if (opt_magic == 0x10B) { /* PE32 */
                                DWORD ep32 = 0, base32 = 0;
                                SetFilePointer(hf, (LONG)(pe_off + 40), NULL, FILE_BEGIN);
                                ReadFile(hf, &ep32, 4, &read, NULL);
                                SetFilePointer(hf, (LONG)(pe_off + 52), NULL, FILE_BEGIN);
                                ReadFile(hf, &base32, 4, &read, NULL);
                                wsprintfW(fi->ep_str,   L"0x%08X", ep32);
                                wsprintfW(fi->base_str, L"0x%08X", base32);
                                lstrcpyW(fi->ep_sub,   L"RVA");
                                lstrcpyW(fi->base_sub, L"Preferred load");
                            } else if (opt_magic == 0x20B) { /* PE32+ */
                                DWORD ep64 = 0;
                                ULONGLONG base64 = 0;
                                SetFilePointer(hf, (LONG)(pe_off + 40), NULL, FILE_BEGIN);
                                ReadFile(hf, &ep64, 4, &read, NULL);
                                SetFilePointer(hf, (LONG)(pe_off + 48), NULL, FILE_BEGIN);
                                ReadFile(hf, &base64, 8, &read, NULL);
                                wsprintfW(fi->ep_str,   L"0x%08X", ep64);
                                if (base64 > 0xFFFFFFFF)
                                    wsprintfW(fi->base_str, L"0x%08X%08X",
                                              (unsigned)(base64 >> 32),
                                              (unsigned)(base64 & 0xFFFFFFFF));
                                else
                                    wsprintfW(fi->base_str, L"0x%08X",
                                              (unsigned)base64);
                                lstrcpyW(fi->ep_sub,   L"RVA");
                                lstrcpyW(fi->base_sub, L"Preferred load");
                            }
                        }
                    }
                }
            }
            CloseHandle(hf);
        }
        HeapFree(GetProcessHeap(), 0, wpath);
    }
}

static void w2u(const WCHAR* w, char* out, int cap)
{
  out[0] = 0;
  if (w && !WideCharToMultiByte(CP_UTF8, 0, w, -1, out, cap, NULL, NULL))
    out[0] = 0;
}

static void update_chrome_after_scan(const char* utf8_path, const char* type_name)
{
  FileInfo fi = { { 0 },{ 0 },{ 0 },{ 0 },{ 0 },{ 0 },{ 0 } };

  get_file_info(utf8_path, type_name, &fi);

  if (g_header_wnd) {
    const char* last = NULL, * p;
    for (p = utf8_path; *p; ++p)
      if (*p == '\\' || *p == '/') last = p;
    if (last) {
      char* folder = copy_utf8(utf8_path);
      if (folder) {
        folder[last - utf8_path + 1] = 0;
        header_set_path(g_header_wnd, folder, last + 1);
        HeapFree(GetProcessHeap(), 0, folder);
      }
    }
    else {
      header_set_path(g_header_wnd, "", utf8_path);
    }
  }

  if (g_infobar_wnd) {
    char type[256], size[256], size_sub[256];
    char ep[256], ep_sub[256], base[256], base_sub[256];
    InfoCard cards[4];

    w2u(fi.type_str, type, 256);
    w2u(fi.size_str, size, 256);
    w2u(fi.size_sub, size_sub, 256);
    w2u(fi.ep_str, ep, 256);
    w2u(fi.ep_sub, ep_sub, 256);
    w2u(fi.base_str, base, 256);
    w2u(fi.base_sub, base_sub, 256);

    cards[0].label = "FILE TYPE";    cards[0].value = type; cards[0].subvalue = "";
    cards[1].label = "FILE SIZE";    cards[1].value = size; cards[1].subvalue = size_sub;
    cards[2].label = "ENTRY POINT";  cards[2].value = ep;   cards[2].subvalue = ep_sub;
    cards[3].label = "BASE ADDRESS"; cards[3].value = base; cards[3].subvalue = base_sub;

    infobar_set_cards(g_infobar_wnd, cards, 4);
  }
}

static unsigned int flags_mask(void)
{
    return (g_bFlagDeep ? 1u : 0u) | (g_bFlagHeur ? 2u : 0u) |
           (g_bFlagVerbose ? 4u : 0u) | (g_bFlagAggr ? 8u : 0u) |
           (g_bFlagHideUnknown ? 16u : 0u) | (g_bFlagFormat ? 32u : 0u);
}

static unsigned int databases_mask(void)
{
    return (g_bDbExtra ? 1u : 0u) | (g_bDbCustom ? 2u : 0u);
}

static xxwidgets_status sync_panel_options(void)
{
    xxwidgets_status status = xxwidgets_scan_panel_set_flags(g_panel, flags_mask());
    if (status == XXWIDGETS_OK)
        status = xxwidgets_scan_panel_set_databases(g_panel, databases_mask());
    return status;
}

static xxwidgets_status read_panel_options(void)
{
    unsigned int flags = 0, databases = 0;
    xxwidgets_status status = xxwidgets_scan_panel_get_flags(g_panel, &flags);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_scan_panel_get_databases(g_panel, &databases);
    if (status != XXWIDGETS_OK) return status;
    g_bFlagDeep        = (flags & 1u)  != 0;
    g_bFlagHeur        = (flags & 2u)  != 0;
    g_bFlagVerbose     = (flags & 4u)  != 0;
    g_bFlagAggr        = (flags & 8u)  != 0;
    g_bFlagHideUnknown = (flags & 16u) != 0;
    g_bFlagFormat      = (flags & 32u) != 0;
    g_bDbExtra         = (databases & 1u) != 0;
    g_bDbCustom        = (databases & 2u) != 0;
    apply_flags_to_backend();
    return XXWIDGETS_OK;
}

static void clear_scan(void)
{
    xxwidgets_scan_panel_clear(g_panel);
    xxwidgets_widget_set_text(g_msec, "");
    if (g_infobar_wnd) infobar_clear(g_infobar_wnd);
    if (g_header_wnd)  header_clear(g_header_wnd);
}

typedef struct file_type_buffer {
    xx_meta_string *records;
    SIZE_T count, capacity;
    int failed;
} file_type_buffer;

static void free_file_types(file_type_buffer *buffer)
{
    SIZE_T i;
    for (i = 0; i < buffer->count; ++i) {
        HeapFree(GetProcessHeap(), 0, buffer->records[i].meta_string->data);
        HeapFree(GetProcessHeap(), 0, buffer->records[i].meta_string);
    }
    if (buffer->records) HeapFree(GetProcessHeap(), 0, buffer->records);
}

static void collect_file_type(int type_id, const char *name, void *user_data)
{
    file_type_buffer *buffer = (file_type_buffer *)user_data;
    xx_meta_string record = {0};
    WCHAR *wide;
    SIZE_T length = 0;
    if (buffer->failed) return;
    if (buffer->count == buffer->capacity) {
        SIZE_T capacity = buffer->capacity ? buffer->capacity * 2 : 8;
        xx_meta_string *records;
        if (capacity < buffer->capacity ||
            capacity > ((SIZE_T)-1) / sizeof(*records)) {
            buffer->failed = 1; return;
        }
        records = buffer->records
            ? (xx_meta_string *)HeapReAlloc(GetProcessHeap(), 0,
                buffer->records, capacity * sizeof(*records))
            : (xx_meta_string *)HeapAlloc(GetProcessHeap(), 0,
                capacity * sizeof(*records));
        if (!records) { buffer->failed = 1; return; }
        buffer->records = records; buffer->capacity = capacity;
    }
    wide = utf8_to_wide(name);
    record.meta_string = (xx_str_w_s *)HeapAlloc(GetProcessHeap(),
                          HEAP_ZERO_MEMORY, sizeof(*record.meta_string));
    if (!wide || !record.meta_string) {
        if (wide) HeapFree(GetProcessHeap(), 0, wide);
        if (record.meta_string) HeapFree(GetProcessHeap(), 0, record.meta_string);
        buffer->failed = 1; return;
    }
    while (wide[length]) ++length;
    record.meta_string->data     = wide;
    record.meta_string->length   = length;
    record.meta_string->capacity = length + 1;
    record.meta_string->is_view  = true;
    record.var.type      = XX_VAR_TYPE_UINT64;
    record.var.val.u64   = (uint64_t)type_id;
    buffer->records[buffer->count++] = record;
}

static xxwidgets_status selected_file_type(int *type_id)
{
    xx_var value = {0};
    xxwidgets_status status = xxwidgets_combobox_get_current(g_type, &value);
    if (status != XXWIDGETS_OK) return status;
    if (value.type != XX_VAR_TYPE_UINT64 || value.val.u64 > INT_MAX)
        return XXWIDGETS_INVALID_ARGUMENT;
    *type_id = (int)value.val.u64;
    return XXWIDGETS_OK;
}

static xxwidgets_status refresh_file_types(const char *path)
{
    file_type_buffer buffer = {0};
    xxwidgets_status status;
    char *new_path;
    int type_id = 0, selected = 0;
    SIZE_T i;
    if (g_typePath && !lstrcmpA(g_typePath, path)) {
        status = selected_file_type(&type_id);
        if (status != XXWIDGETS_OK) return status;
    }
    if (!gui_backend_file_types(path, collect_file_type, &buffer)) {
        free_file_types(&buffer);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    if (buffer.failed) { free_file_types(&buffer); return XXWIDGETS_OUT_OF_MEMORY; }
    new_path = copy_utf8(path);
    if (!new_path) { free_file_types(&buffer); return XXWIDGETS_OUT_OF_MEMORY; }
    for (i = 0; i < buffer.count; ++i)
        if (buffer.records[i].var.val.u64 == (uint64_t)type_id) {
            selected = (int)i; break;
        }
    status = xxwidgets_combobox_set_records(g_type, buffer.records, buffer.count);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_widget_set_value(g_type, selected);
    free_file_types(&buffer);
    if (status != XXWIDGETS_OK) {
        HeapFree(GetProcessHeap(), 0, new_path); return status;
    }
    if (g_typePath) HeapFree(GetProcessHeap(), 0, g_typePath);
    g_typePath = new_path;
    return XXWIDGETS_OK;
}

typedef struct result_buffer {
    xxwidgets_scan_result *records;
    SIZE_T count, capacity;
    int failed;
} result_buffer;

static void free_result_record(xxwidgets_scan_result *record)
{
    if (record->type)    HeapFree(GetProcessHeap(), 0, (void *)record->type);
    if (record->name)    HeapFree(GetProcessHeap(), 0, (void *)record->name);
    if (record->version) HeapFree(GetProcessHeap(), 0, (void *)record->version);
    if (record->info)    HeapFree(GetProcessHeap(), 0, (void *)record->info);
}

static void free_result_buffer(result_buffer *buffer)
{
    SIZE_T i;
    for (i = 0; i < buffer->count; ++i)
        free_result_record(&buffer->records[i]);
    if (buffer->records) HeapFree(GetProcessHeap(), 0, buffer->records);
}

static void collect_result(const char *type, const char *name,
                           const char *version, const char *info,
                           void *user_data)
{
    result_buffer *buffer = (result_buffer *)user_data;
    xxwidgets_scan_result record;
    if (buffer->failed) return;
    if (buffer->count == buffer->capacity) {
        SIZE_T capacity = buffer->capacity ? buffer->capacity * 2 : 16;
        xxwidgets_scan_result *records;
        if (capacity < buffer->capacity ||
            capacity > ((SIZE_T)-1) / sizeof(*records)) {
            buffer->failed = 1; return;
        }
        records = buffer->records
            ? (xxwidgets_scan_result *)HeapReAlloc(GetProcessHeap(), 0,
                buffer->records, capacity * sizeof(*records))
            : (xxwidgets_scan_result *)HeapAlloc(GetProcessHeap(), 0,
                capacity * sizeof(*records));
        if (!records) { buffer->failed = 1; return; }
        buffer->records  = records;
        buffer->capacity = capacity;
    }
    record.type    = copy_utf8(type);
    record.name    = copy_utf8(name);
    record.version = copy_utf8(version);
    record.info    = copy_utf8(info);
    if (!record.type || !record.name || !record.version || !record.info) {
        free_result_record(&record);
        buffer->failed = 1;
        return;
    }
    buffer->records[buffer->count++] = record;
}

static void do_scan(void)
{
    SIZE_T required = 0;
    char *path, *report = NULL, *type = NULL;
    result_buffer buffer = {0};
    LARGE_INTEGER frequency, started, ended;
    xxwidgets_status status;
    int ok, type_id = 0;

    status = xxwidgets_widget_get_text(g_path, NULL, 0, &required);
    if (status != XXWIDGETS_OK) {
        report_ui_error("Read file name", status); return;
    }
    if (required <= 1) {
        MessageBeep(MB_ICONASTERISK);
        xxwidgets_widget_focus(g_path);
        return;
    }
    path = (char *)HeapAlloc(GetProcessHeap(), 0, required);
    if (!path) { report_ui_error("Read file name", XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, required, NULL);
    if (status != XXWIDGETS_OK) {
        HeapFree(GetProcessHeap(), 0, path);
        report_ui_error("Read file name", status); return;
    }
    status = read_panel_options();
    if (status != XXWIDGETS_OK) {
        HeapFree(GetProcessHeap(), 0, path);
        report_ui_error("Read scan options", status); return;
    }
    status = refresh_file_types(path);
    if (status == XXWIDGETS_OK) status = selected_file_type(&type_id);
    if (status != XXWIDGETS_OK) {
        clear_scan();
        xxwidgets_widget_set_enabled(g_type, 0);
        HeapFree(GetProcessHeap(), 0, path);
        report_ui_error("Load file types", status); return;
    }
    clear_scan();
    xxwidgets_widget_set_enabled(g_scan, 0);
    xxwidgets_widget_set_enabled(g_type, 0);
    xxwidgets_widget_set_text(g_msec, "Scanning...");
    xxwidgets_widget_set_text(g_status, "Scanning the selected file...");
    UpdateWindow(native(g_window));
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&started);
    ok = gui_backend_scan_results_type(path, type_id, collect_result,
                                       &buffer, &report, &type);
    QueryPerformanceCounter(&ended);
    if (ok) {
        WCHAR text[512];
        unsigned int msec = frequency.QuadPart > 0
            ? (unsigned int)(((ended.QuadPart - started.QuadPart) * 1000)
                             / frequency.QuadPart) : 0;
        status = buffer.failed ? XXWIDGETS_OUT_OF_MEMORY
            : xxwidgets_scan_panel_set_results(g_panel, path,
                buffer.records, buffer.count, report);
        if (status != XXWIDGETS_OK)
            report_ui_error("Display scan results", status);
        else {
            WCHAR *type_name = utf8_to_wide(type && type[0] ? type : "Unknown type");
            wsprintfW(text,
                L"Scan complete (%s). %u result%s. Use Report for full engine output.",
                type_name ? type_name : L"Unknown type",
                (unsigned int)buffer.count,
                buffer.count == 1 ? L"" : L"s");
            set_status_wide(text);
            if (type_name) HeapFree(GetProcessHeap(), 0, type_name);

            update_chrome_after_scan(path, type);
        }
        wsprintfW(text, L"%u ms", msec);
        { char *u = wide_to_utf8(text);
          if (u) { xxwidgets_widget_set_text(g_msec, u);
                   HeapFree(GetProcessHeap(), 0, u); } }
    } else {
        status = xxwidgets_scan_panel_set_results(g_panel, path, NULL, 0, report);
        xxwidgets_widget_set_text(g_status,
            report && report[0] ? report : "Cannot open the file.");
        xxwidgets_widget_set_text(g_msec, "");
        if (status != XXWIDGETS_OK) report_ui_error("Display scan report", status);
    }
    HeapFree(GetProcessHeap(), 0, path);
    if (report) gui_backend_free(report);
    if (type)   gui_backend_free(type);
    free_result_buffer(&buffer);
    xxwidgets_widget_set_enabled(g_type, 1);
    xxwidgets_widget_set_enabled(g_scan, 1);
}

static void do_browse(void)
{
    OPENFILENAMEW picker;
    WCHAR file[32768];
    file[0] = 0;
    GetWindowTextW(native(g_path), file, (int)(sizeof(file) / sizeof(file[0])));
    ZeroMemory(&picker, sizeof(picker));
    picker.lStructSize = sizeof(picker);
    picker.hwndOwner   = native(g_window);
    picker.lpstrFile   = file;
    picker.nMaxFile    = (DWORD)(sizeof(file) / sizeof(file[0]));
    picker.lpstrFilter = L"All files\0*.*\0";
    picker.lpstrTitle  = L"Select a file to scan";
    picker.Flags       = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER;
    if (GetOpenFileNameW(&picker)) {
        char *path = wide_to_utf8(file);
        if (path) {
            xxwidgets_widget_set_text(g_path, path);
            HeapFree(GetProcessHeap(), 0, path);
            do_scan();
        }
    }
}

static int do_options(void)
{
    xxwidgets_scan_options options = {flags_mask(), databases_mask()};
    int accepted = 0;
    xxwidgets_status status =
        xxwidgets_scan_options_dialog(g_window, "cdie Scan options",
                                     &options, &accepted);
    if (status != XXWIDGETS_OK) {
        report_ui_error("Open Options", status); return -1;
    }
    if (accepted) {
        g_bFlagDeep        = (options.flags & XXWIDGETS_SCAN_DEEP)         != 0;
        g_bFlagHeur        = (options.flags & XXWIDGETS_SCAN_HEURISTIC)    != 0;
        g_bFlagVerbose     = (options.flags & XXWIDGETS_SCAN_VERBOSE)      != 0;
        g_bFlagAggr        = (options.flags & XXWIDGETS_SCAN_AGGRESSIVE)   != 0;
        g_bFlagHideUnknown = (options.flags & XXWIDGETS_SCAN_HIDE_UNKNOWN) != 0;
        g_bFlagFormat      = (options.flags & XXWIDGETS_SCAN_FORMAT)       != 0;
        g_bDbExtra  = (options.databases & XXWIDGETS_SCAN_DATABASE_EXTRA)  != 0;
        g_bDbCustom = (options.databases & XXWIDGETS_SCAN_DATABASE_CUSTOM) != 0;
        apply_flags_to_backend();
        status = sync_panel_options();
        if (status != XXWIDGETS_OK)
            report_ui_error("Update scan options", status);
        else
            xxwidgets_widget_set_text(g_status,
                "Options applied to the next scan. "
                "Use Database paths to choose signature folders.");
    }
    return accepted;
}

static int do_optimization(void)
{
    gui_backend_optimization_options     current;
    gui_backend_optimization_capabilities features;
    xxwidgets_optimization_options     options;
    xxwidgets_optimization_capabilities capabilities;
    xxwidgets_status status;
    int accepted = 0;
    if (!gui_backend_get_optimization_options(&current) ||
        !gui_backend_get_optimization_capabilities(&features)) {
        report_ui_error("Read optimization options", XXWIDGETS_PLATFORM_ERROR);
        return -1;
    }
    options.buffer_size      = current.buffer_size;
    options.file_buffer_size = current.file_buffer_size;
    options.use_sse2 = current.use_sse2;
    options.use_avx2 = current.use_avx2;
    capabilities.sse2 = features.sse2;
    capabilities.avx2 = features.avx2;
    status = xxwidgets_optimization_options_dialog(g_window, "cdie Optimization",
                 &capabilities, &options, &accepted);
    if (status != XXWIDGETS_OK) {
        report_ui_error("Open optimization options", status); return -1;
    }
    if (accepted) {
        current.buffer_size      = options.buffer_size;
        current.file_buffer_size = options.file_buffer_size;
        current.use_sse2 = options.use_sse2;
        current.use_avx2 = options.use_avx2;
        if (!gui_backend_set_optimization_options(&current)) {
            report_ui_error("Apply optimization options", XXWIDGETS_PLATFORM_ERROR);
            return -1;
        }
        xxwidgets_widget_set_text(g_status,
            "Optimization settings applied to future scans.");
    }
    return accepted;
}

static int do_fonts(void)
{
    xxwidgets_font_options options;
    int accepted = 0;
    xxwidgets_status status = xxwidgets_app_get_font_options(g_app, &options);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_font_options_dialog(g_window, "cdie Fonts",
                     &options, &accepted);
    if (status == XXWIDGETS_OK && accepted)
        status = xxwidgets_app_set_font_options(g_app, &options);
    if (status != XXWIDGETS_OK) {
        report_ui_error("Set interface fonts", status); return -1;
    }
    if (accepted) {
        layout();
        xxwidgets_widget_set_text(g_status,
            "Fonts applied to the interface, results and dialogs.");
    }
    return accepted;
}

static int do_context(void)
{
    WCHAR executable[32768];
    char *path;
    DWORD length = GetModuleFileNameW(NULL, executable,
                   (DWORD)(sizeof(executable)/sizeof(executable[0])));
    xxwidgets_context_config config = {
        "cdie", "Scan with cdie", "*", NULL, XXWIDGETS_CONTEXT_CURRENT_USER
    };
    xxwidgets_status status;
    int accepted = 0;
    if (!length || length >= sizeof(executable)/sizeof(executable[0])) {
        report_ui_error("Read program path", XXWIDGETS_PLATFORM_ERROR); return -1;
    }
    path = wide_to_utf8(executable);
    if (!path) { report_ui_error("Read program path", XXWIDGETS_OUT_OF_MEMORY); return -1; }
    config.executable = path;
    status = xxwidgets_context_options_dialog(g_window, "cdie Context menu",
                 &config, &accepted);
    HeapFree(GetProcessHeap(), 0, path);
    if (status != XXWIDGETS_OK) {
        report_ui_error("Update Explorer context menu", status); return -1;
    }
    if (accepted)
        xxwidgets_widget_set_text(g_status,
            "Explorer context menu updated for the current user.");
    return accepted;
}

static xxwidgets_status configure_about(void)
{
    static const char license[] =
        "MIT License\n\n"
        "Permission is hereby granted, free of charge, to any person obtaining a copy "
        "of this software and associated documentation files (the Software), to deal "
        "in the Software without restriction, including without limitation the rights "
        "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell "
        "copies of the Software, and to permit persons to whom the Software is "
        "furnished to do so, subject to the following conditions:\n\n"
        "The above copyright notice and this permission notice shall be included in all "
        "copies or substantial portions of the Software.\n\n"
        "THE SOFTWARE IS PROVIDED AS IS, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR "
        "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, "
        "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE "
        "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER "
        "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, "
        "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.";
    const char *title   = gui_backend_app_title();
    const char *version = title;
    xxwidgets_status status;
    while (*version && *version != ' ') ++version;
    if (*version) ++version;
    status = xxwidgets_about_dialog_create(&g_about);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_about_dialog_set_text(g_about, XXWIDGETS_ABOUT_TITLE, "About cdie");
    if (status == XXWIDGETS_OK)
        status = xxwidgets_about_dialog_set_text(g_about, XXWIDGETS_ABOUT_PROGRAM_NAME,
                                                 "Detect It Easy - cdie");
    if (status == XXWIDGETS_OK)
        status = xxwidgets_about_dialog_set_text(g_about, XXWIDGETS_ABOUT_VERSION, version);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_about_dialog_set_text(g_about, XXWIDGETS_ABOUT_DESCRIPTION,
            "Detect It Easy scan engine, ported to C.\n"
            "Native graphical frontend with the reusable xxwidgets scan panel and dialogs.");
    if (status == XXWIDGETS_OK)
        status = xxwidgets_about_dialog_set_text(g_about, XXWIDGETS_ABOUT_COPYRIGHT,
                                                 "Copyright (C) 2026 hors <horsicq@gmail.com>");
    if (status == XXWIDGETS_OK)
        status = xxwidgets_about_dialog_set_text(g_about, XXWIDGETS_ABOUT_WEBSITE,
                                                 "https://ntinfo.biz");
    if (status == XXWIDGETS_OK)
        status = xxwidgets_about_dialog_set_text(g_about, XXWIDGETS_ABOUT_LICENSE, license);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_about_dialog_set_text(g_about, XXWIDGETS_ABOUT_CREDITS,
            "Detect It Easy by hors and contributors\n"
            "xxfclib - C format and runtime support\n"
            "xxwidgets - native widgets and common dialogs");
    return status;
}

static void do_about(void)
{
    xxwidgets_status status = xxwidgets_about_dialog_show(g_about, g_window);
    if (status != XXWIDGETS_OK) report_ui_error("Open About", status);
}

static void set_dlg_edit_utf8(HWND dialog, int id, const char *text)
{
    WCHAR *wide = utf8_to_wide(text);
    SetDlgItemTextW(dialog, id, wide ? wide : L"");
    if (wide) HeapFree(GetProcessHeap(), 0, wide);
}

static char *get_dlg_edit_utf8(HWND dialog, int id)
{
    HWND edit = GetDlgItem(dialog, id);
    int   length = GetWindowTextLengthW(edit);
    WCHAR *wide  = (WCHAR *)HeapAlloc(GetProcessHeap(), 0,
                    (SIZE_T)(length + 1) * sizeof(WCHAR));
    char  *text;
    if (!wide) return NULL;
    GetWindowTextW(edit, wide, length + 1);
    text = wide_to_utf8(wide);
    HeapFree(GetProcessHeap(), 0, wide);
    return text;
}

static void browse_folder_into(HWND dialog, int id)
{
    BROWSEINFOW picker;
    LPITEMIDLIST item;
    WCHAR path[MAX_PATH];
    ZeroMemory(&picker, sizeof(picker));
    picker.hwndOwner = dialog;
    picker.lpszTitle = L"Select the database folder";
    picker.ulFlags   = BIF_RETURNONLYFSDIRS;
    item = SHBrowseForFolderW(&picker);
    if (item) {
        if (SHGetPathFromIDListW(item, path))
            SetDlgItemTextW(dialog, id, path);
        CoTaskMemFree(item);
    }
}

static INT_PTR CALLBACK database_dlg_proc(HWND dialog, UINT message,
                                          WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (message) {
    case WM_INITDIALOG:
        set_dlg_edit_utf8(dialog, IDC_OPT_MAIN,   gui_backend_main_db_path());
        set_dlg_edit_utf8(dialog, IDC_OPT_EXTRA,  gui_backend_extra_db_path());
        set_dlg_edit_utf8(dialog, IDC_OPT_CUSTOM, gui_backend_custom_db_path());
        SendDlgItemMessageW(dialog, IDC_OPT_MAIN,   EM_SETLIMITTEXT, 32767, 0);
        SendDlgItemMessageW(dialog, IDC_OPT_EXTRA,  EM_SETLIMITTEXT, 32767, 0);
        SendDlgItemMessageW(dialog, IDC_OPT_CUSTOM, EM_SETLIMITTEXT, 32767, 0);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_OPT_MAIN_BROWSE:
            browse_folder_into(dialog, IDC_OPT_MAIN);   return TRUE;
        case IDC_OPT_EXTRA_BROWSE:
            browse_folder_into(dialog, IDC_OPT_EXTRA);  return TRUE;
        case IDC_OPT_CUSTOM_BROWSE:
            browse_folder_into(dialog, IDC_OPT_CUSTOM); return TRUE;
        case IDOK: {
            char *main_path   = get_dlg_edit_utf8(dialog, IDC_OPT_MAIN);
            char *extra_path  = get_dlg_edit_utf8(dialog, IDC_OPT_EXTRA);
            char *custom_path = get_dlg_edit_utf8(dialog, IDC_OPT_CUSTOM);
            int ok = main_path && extra_path && custom_path;
            if (ok) ok = gui_backend_set_db_paths(main_path, extra_path, custom_path);
            if (main_path)   HeapFree(GetProcessHeap(), 0, main_path);
            if (extra_path)  HeapFree(GetProcessHeap(), 0, extra_path);
            if (custom_path) HeapFree(GetProcessHeap(), 0, custom_path);
            if (ok) EndDialog(dialog, 1);
            else {
                SetDlgItemTextW(dialog, IDC_OPT_ERROR,
                    L"Cannot load the main signature database. "
                    L"Check its folder and try again.");
                SetFocus(GetDlgItem(dialog, IDC_OPT_MAIN));
            }
            return TRUE;
        }
        case IDCANCEL: EndDialog(dialog, 0); return TRUE;
        default: break;
        }
        break;
    case WM_CLOSE: EndDialog(dialog, 0); return TRUE;
    default: break;
    }
    return FALSE;
}

static void do_database_paths(void)
{
    INT_PTR accepted = DialogBoxParamW(g_hInst,
        MAKEINTRESOURCEW(IDD_OPTIONS), native(g_window), database_dlg_proc, 0);
    if (accepted == 1) {
        WCHAR text[160];
        clear_scan();
        wsprintfW(text,
            L"Database reloaded: %d signatures. Choose a file and press Scan.",
            gui_backend_signature_count());
        set_status_wide(text);
    } else if (accepted == -1) {
        report_ui_error("Open database paths", XXWIDGETS_PLATFORM_ERROR);
    }
}

static RECT g_resultsFrame;  

static void style_results_tree(void)
{
  HWND tv = native(g_results);
  LONG_PTR s;

  s = GetWindowLongPtrW(tv, GWL_STYLE);
  SetWindowLongPtrW(tv, GWL_STYLE, s & ~(LONG_PTR)WS_BORDER);
  s = GetWindowLongPtrW(tv, GWL_EXSTYLE);
  SetWindowLongPtrW(tv, GWL_EXSTYLE, s & ~(LONG_PTR)WS_EX_CLIENTEDGE);
  SetWindowPos(tv, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
    SWP_NOACTIVATE | SWP_FRAMECHANGED);

  SetWindowTheme(tv, L"DarkMode_Explorer", NULL);

  SendMessageW(tv, TVM_SETBKCOLOR, 0, (LPARAM)CR(CLR_PANEL_BODY));
  SendMessageW(tv, TVM_SETTEXTCOLOR, 0, (LPARAM)CR(CLR_TEXT_CODE));
  SendMessageW(tv, TVM_SETLINECOLOR, 0, (LPARAM)CR(CLR_PANEL_BORDER));
}

static LRESULT CALLBACK tree_parent_subclass(HWND h, UINT msg, WPARAM wp, LPARAM lp,
  UINT_PTR id, DWORD_PTR ref)
{
  (void)id; (void)ref;
  if (msg == WM_NOTIFY) {
    NMHDR* hdr = (NMHDR*)lp;
    if (g_results && hdr->hwndFrom == native(g_results) &&
      hdr->code == NM_CUSTOMDRAW) {
      NMTVCUSTOMDRAW* cd = (NMTVCUSTOMDRAW*)lp;
      if (cd->nmcd.dwDrawStage == CDDS_PREPAINT)
        return CDRF_NOTIFYITEMDRAW;
      if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
        int sel = (cd->nmcd.uItemState & CDIS_SELECTED) != 0;
        cd->nmcd.uItemState &= ~(UINT)(CDIS_SELECTED | CDIS_FOCUS);
        cd->clrText = sel ? CR(CLR_TEXT_PRIMARY) : CR(CLR_TEXT_CODE);
        cd->clrTextBk = sel ? CR(CLR_SIDEBAR_SEL) : CR(CLR_PANEL_BODY);
        return CDRF_NEWFONT;
      }
    }
  }
  if (msg == WM_NCDESTROY) RemoveWindowSubclass(h, tree_parent_subclass, 2);
  return DefSubclassProc(h, msg, wp, lp);
}

static void layout(void)
{
    RECT bounds, window_bounds;
    int W, H, cx, cy, cw;
    if (!g_results) return;

    GetClientRect(native(g_window), &bounds);
    W = bounds.right; H = bounds.bottom;

    if (g_sidebar)
      pl_widget_set_rect(sidebar_widget(g_sidebar), 0, 0, SIDEBAR_W, H);

    cx = SIDEBAR_W;
    cw = W - SIDEBAR_W;
    cy = 0;

    if (g_header_wnd) {
      pl_widget_set_rect(header_widget(g_header_wnd), cx, cy, cw, HEADER_H);
      cy += HEADER_H;
    }
    if (g_infobar_wnd) {
      pl_widget_set_rect(infobar_widget(g_infobar_wnd), cx, cy, cw, INFOBAR_H);
      cy += INFOBAR_H;
    }
    /*if (g_tabbar) {
      pl_widget_set_rect(tabbar_widget(g_tabbar), cx, cy, cw, TABBAR_H);
      cy += TABBAR_H;
    */}
    if (g_bottombar)
      pl_widget_set_rect(bottombar_widget(g_bottombar),
        cx, H - BOTTOMBAR_H, cw, BOTTOMBAR_H);

    {
      int tx = cx + CONTENT_PAD, tw = cw - 2 * CONTENT_PAD;
      int ty = cy + 4, th = H - cy - BOTTOMBAR_H - 8;
      HRGN rgn;
      if (tw < 40) tw = 40;
      if (th < 40) th = 40;

      g_resultsFrame.left = tx - 1;
      g_resultsFrame.top = ty - 1;
      g_resultsFrame.right = tx + tw + 1;
      g_resultsFrame.bottom = ty + th + 1;

      MoveWindow(native(g_results), tx, ty, tw, th, TRUE);
      rgn = CreateRoundRectRgn(0, 0, tw + 1, th + 1, 10, 10);
      SetWindowRgn(native(g_results), rgn, TRUE);
      InvalidateRect(native(g_window), NULL, TRUE);
    }

#define HIDE(w) if (w) MoveWindow(native(w), -9999, 0, 100, 26, FALSE)
    HIDE(g_path);  HIDE(g_browse);    HIDE(g_fileLabel); HIDE(g_msec);
    HIDE(g_status);HIDE(g_options);   HIDE(g_optimizationButton);
    HIDE(g_fontsButton); HIDE(g_contextButton); HIDE(g_databasePaths);
    HIDE(g_aboutButton); HIDE(g_reportButton); HIDE(g_exit);
    HIDE(g_scan);  HIDE(g_type);      HIDE(g_flags);     HIDE(g_databases);
    HIDE(g_typeLabel); HIDE(g_flagLabel); HIDE(g_databaseLabel);
#undef HIDE

    GetWindowRect(native(g_window), &window_bounds);
    g_minWindowWidth  = SIDEBAR_W + 520 +
        (window_bounds.right  - window_bounds.left - W);
    if (g_minWindowWidth < 900) g_minWindowWidth = 900;
    g_minWindowHeight = HEADER_H + INFOBAR_H + TABBAR_H + BOTTOMBAR_H + 200 +
        (window_bounds.bottom - window_bounds.top  - H);
    if (g_minWindowHeight < 540) g_minWindowHeight = 540;
}

static LRESULT CALLBACK path_subclass(HWND handle, UINT message,
                                      WPARAM wp, LPARAM lp,
                                      UINT_PTR subclass_id, DWORD_PTR reference)
{
    (void)subclass_id; (void)reference;
    if (message == WM_GETDLGCODE) {
        MSG *key = (MSG *)lp;
        LRESULT code = DefSubclassProc(handle, message, wp, lp);
        if (key && key->wParam == VK_RETURN) code |= DLGC_WANTMESSAGE;
        return code;
    }
    if (message == WM_KEYDOWN && wp == VK_RETURN) { g_pending = ACTION_SCAN; return 0; }
    if (message == WM_CHAR    && wp == VK_RETURN) return 0;
    if (message == WM_NCDESTROY) RemoveWindowSubclass(handle, path_subclass, 1);
    return DefSubclassProc(handle, message, wp, lp);
}

static void on_browse(void* user)
{
  (void)user;
  g_pending = ACTION_BROWSE;
}

static void on_nav(void* user, int nav_id)
{
  (void)user;
  switch (nav_id) {
  case NAV_OPTIONS:
    g_pending = ACTION_OPTIONS;
    break;

  case NAV_SIGNATURES:
    break;

  default:
    break;
  }
}

static void on_tab(void* user, int tab_id)
{
  (void)user;
  //if (tab_id == TAB_SCAN && g_tabbar)
  //  tabbar_set_active(g_tabbar, TAB_SCAN);
}

static LRESULT CALLBACK window_subclass(HWND handle, UINT message,
                                        WPARAM wp, LPARAM lp,
                                        UINT_PTR subclass_id, DWORD_PTR reference)
{
    (void)subclass_id; (void)reference;
    switch (message) {
    case WM_GETMINMAXINFO: {
        MINMAXINFO *info = (MINMAXINFO *)lp;
        info->ptMinTrackSize.x = g_minWindowWidth;
        info->ptMinTrackSize.y = g_minWindowHeight;
        return 0;
    }

    case WM_ERASEBKGND: {
      HDC dc = (HDC)wp;
      RECT r; GetClientRect(handle, &r);
      th_fill(dc, r.left, r.top, r.right, r.bottom, CLR_BG);
      if (g_resultsFrame.right > g_resultsFrame.left)
        th_round_rect(dc, g_resultsFrame.left, g_resultsFrame.top,
          g_resultsFrame.right, g_resultsFrame.bottom,
          CLR_PANEL_BODY, CLR_PANEL_BORDER, 12);
      return 1;
    }

    case WM_DROPFILES: {
        HDROP drop   = (HDROP)wp;
        UINT  length = DragQueryFileW(drop, 0, NULL, 0);
        WCHAR *wide  = (WCHAR *)HeapAlloc(GetProcessHeap(), 0,
                        ((SIZE_T)length + 1) * sizeof(WCHAR));
        if (wide && DragQueryFileW(drop, 0, wide, length + 1)) {
            char *path = wide_to_utf8(wide);
            if (path) {
                xxwidgets_widget_set_text(g_path, path);
                HeapFree(GetProcessHeap(), 0, path);
                g_pending = ACTION_SCAN;
            }
        }
        if (wide) HeapFree(GetProcessHeap(), 0, wide);
        DragFinish(drop);
        return 0;
    }

    case WM_NCDESTROY:
        RemoveWindowSubclass(handle, window_subclass, 1);
        break;

    default: break;
    }
    return DefSubclassProc(handle, message, wp, lp);
}

static void on_event(xxwidgets_app *app, const xxwidgets_event *event,
                     void *user_data)
{
    (void)user_data;
    if ((event->type == XXWIDGETS_EVENT_CLOSE  && event->widget == g_window) ||
      (event->type == XXWIDGETS_EVENT_CLICK && event->widget == g_exit)) {
      
      g_running = 0;
        xxwidgets_app_quit(app, 0);
    } else if (event->type == XXWIDGETS_EVENT_RESIZE && event->widget == g_window) {
        layout();
    } else if (event->type == XXWIDGETS_EVENT_CLICK) {
        if      (event->widget == g_scan)           g_pending = ACTION_SCAN;
        else if (event->widget == g_browse)         g_pending = ACTION_BROWSE;
        else if (event->widget == g_options)        g_pending = ACTION_OPTIONS;
        else if (event->widget == g_aboutButton)    g_pending = ACTION_ABOUT;
        else if (event->widget == g_reportButton)   g_pending = ACTION_REPORT;
        else if (event->widget == g_databasePaths)  g_pending = ACTION_DATABASE_PATHS;
        else if (event->widget == g_fontsButton)    g_pending = ACTION_FONTS;
        else if (event->widget == g_contextButton)  g_pending = ACTION_CONTEXT;
        else if (event->widget == g_optimizationButton) g_pending = ACTION_OPTIMIZATION;
    } else if (event->type == XXWIDGETS_EVENT_CHANGE &&
               (event->widget == g_flags || event->widget == g_databases)) {
        xxwidgets_status status = read_panel_options();
        if (status != XXWIDGETS_OK) report_ui_error("Read scan options", status);
    } else if (event->type == XXWIDGETS_EVENT_CHANGE &&
               event->widget == g_path && g_type) {
        xxwidgets_widget_set_enabled(g_type, 0);
    } else if (event->type == XXWIDGETS_EVENT_SELECT && event->widget == g_type) {
        SIZE_T required = 0;
        if (xxwidgets_widget_get_text(g_path, NULL, 0, &required) == XXWIDGETS_OK
            && required > 1)
            g_pending = ACTION_SCAN;
    } else if (event->type == XXWIDGETS_EVENT_SHORTCUT) {
        g_pending = event->value;
    }
}

static int create_control(xxwidgets_widget **control, xxwidgets_kind kind,
                          const char *text, int id)
{
    xxwidgets_rect bounds = {1, 1, 12, 1};
    xxwidgets_status status =
        xxwidgets_widget_create(g_app, g_window, kind, text, bounds, control);
    if (status != XXWIDGETS_OK) return 0;
    if (id) SetWindowLongPtrW(native(*control), GWLP_ID, id);
    return 1;
}

static LRESULT CALLBACK status_subclass(HWND handle, UINT message, WPARAM wp, LPARAM lp,
  UINT_PTR subclass_id, DWORD_PTR reference)
{
  (void)subclass_id; (void)reference;
  if (message == WM_SETTEXT && g_bottombar) {
    char* utf8 = wide_to_utf8((const WCHAR*)lp);
    if (utf8) {
      bottombar_set_status(g_bottombar, utf8);
      HeapFree(GetProcessHeap(), 0, utf8);
    }
  }
  if (message == WM_NCDESTROY) RemoveWindowSubclass(handle, status_subclass, 3);
  return DefSubclassProc(handle, message, wp, lp);
}

static int create_controls(void)
{
    xxwidgets_rect panel_bounds = {1, 4, 96, 18};
    xxwidgets_shortcut shortcuts[] = {
        {"F5",          ACTION_SCAN},
        {"Ctrl+O",      ACTION_BROWSE},
        {"F1",          ACTION_ABOUT},
        {"Ctrl+Shift+D",ACTION_DATABASE_PATHS}
    };

    /* ── Legacy xxwidgets controls (kept for scan engine + smoke test) ── */
    if (!create_control(&g_fileLabel,          XXWIDGETS_LABEL,  "File name", 0) ||
        !create_control(&g_path,               XXWIDGETS_EDIT,   "", IDC_EDIT_PATH) ||
        !create_control(&g_browse,             XXWIDGETS_BUTTON, "...", IDC_BTN_BROWSE) ||
        !create_control(&g_msec,               XXWIDGETS_LABEL,  "", IDC_STATIC_MSEC) ||
        !create_control(&g_status,             XXWIDGETS_LABEL,  "", IDC_STATUS) ||
        !create_control(&g_options,            XXWIDGETS_BUTTON, "Scan options",   IDC_BTN_OPTIONS) ||
        !create_control(&g_optimizationButton, XXWIDGETS_BUTTON, "Optimization",   IDC_BTN_OPTIMIZATION) ||
        !create_control(&g_fontsButton,        XXWIDGETS_BUTTON, "Fonts",          IDC_BTN_FONTS) ||
        !create_control(&g_contextButton,      XXWIDGETS_BUTTON, "Context menu",   IDC_BTN_CONTEXT) ||
        !create_control(&g_aboutButton,        XXWIDGETS_BUTTON, "About",          IDC_BTN_ABOUT) ||
        !create_control(&g_databasePaths,      XXWIDGETS_BUTTON, "Database paths", IDC_BTN_DB_PATHS) ||
        !create_control(&g_exit,               XXWIDGETS_BUTTON, "Exit",           IDC_BTN_EXIT))
        return 0;

    if (xxwidgets_scan_panel_create(g_window, panel_bounds, &g_panel) != XXWIDGETS_OK)
        return 0;

    g_type        = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_FILE_TYPE);
    g_typeLabel   = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_FILE_TYPE_LABEL);
    g_flags       = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_FLAGS);
    g_databases   = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_DATABASES);
    g_scan        = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_SCAN);
    g_reportButton= xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_REPORT);
    g_results     = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_RESULTS);
    g_flagLabel   = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_FLAGS_LABEL);
    g_databaseLabel=xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_DATABASES_LABEL);

    if (!g_type || !g_typeLabel || !g_flags || !g_databases || !g_scan ||
        !g_reportButton || !g_results || !g_flagLabel || !g_databaseLabel ||
        sync_panel_options() != XXWIDGETS_OK)
        return 0;

    SetWindowLongPtrW(native(g_type),        GWLP_ID, IDC_COMBO_TYPE);
    SetWindowLongPtrW(native(g_flags),       GWLP_ID, IDC_COMBO_FLAGS);
    SetWindowLongPtrW(native(g_databases),   GWLP_ID, IDC_COMBO_DATABASES);
    SetWindowLongPtrW(native(g_scan),        GWLP_ID, IDC_BTN_SCAN);
    SetWindowLongPtrW(native(g_reportButton),GWLP_ID, IDC_BTN_REPORT);
    SetWindowLongPtrW(native(g_results), GWLP_ID, IDC_SCANRESULTS);
    style_results_tree();
    SetWindowSubclass(GetParent(native(g_results)), tree_parent_subclass, 2, 0);
    xxwidgets_widget_set_text(g_databaseLabel, "Databases (Main always)");

    if (!SetWindowSubclass(native(g_path),   path_subclass,   1, 0) ||
        !SetWindowSubclass(native(g_window), window_subclass, 1, 0))
        return 0;

    SetWindowLongPtrW(native(g_msec), GWL_STYLE,
        (GetWindowLongPtrW(native(g_msec), GWL_STYLE)
         & ~(LONG_PTR)SS_TYPEMASK) | SS_RIGHT);

    DragAcceptFiles(native(g_window), TRUE);

    if (xxwidgets_window_set_shortcuts(g_window, shortcuts,
        sizeof(shortcuts) / sizeof(shortcuts[0])) != XXWIDGETS_OK)
        return 0;

    g_sidebar = sidebar_create(native(g_window), 0, 0, SIDEBAR_W, 600, on_nav, NULL);
    g_header_wnd = header_create(native(g_window), SIDEBAR_W, 0, 800, HEADER_H,
      on_browse, NULL);
    g_infobar_wnd = infobar_create(native(g_window), SIDEBAR_W, HEADER_H, 800, INFOBAR_H);
    //g_tabbar = tabbar_create(native(g_window), SIDEBAR_W, HEADER_H + INFOBAR_H, 800, TABBAR_H, on_tab, NULL);
    g_bottombar = bottombar_create(native(g_window), SIDEBAR_W, 500, 800, BOTTOMBAR_H, NULL, NULL);
    SetWindowSubclass(native(g_status), status_subclass, 3, 0);

    if (!g_sidebar || !g_header_wnd || !g_infobar_wnd || /*!g_tabbar ||*/ !g_bottombar) {
      WCHAR msg[200];
      wsprintfW(msg,
        L"Chrome creation failed (1 = ok):\nsidebar=%d header=%d infobar=%d "
        L"tabbar=%d bottombar=%d\nGetLastError=%lu",
        g_sidebar != NULL, g_header_wnd != NULL, g_infobar_wnd != NULL,
        /*g_tabbar != NULL,*/ g_bottombar != NULL, GetLastError());
     MessageBoxW(NULL, msg, L"cdie", MB_ICONERROR);
      return 0;
    }
    return 1;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Smoke test  (unchanged logic; only the specific window geometry has
 * changed so the control lookups still work because the HWNDs exist).
 * ═══════════════════════════════════════════════════════════════════════ */
static const WCHAR *g_smokeTitle;
static UINT_PTR g_smokeTimer;
static int g_smokeAction, g_smokeDone, g_smokeTicks, g_smokeFailed;

static BOOL CALLBACK smoke_children(HWND child, LPARAM data)
{
    HWND *controls = (HWND *)data;
    WCHAR text[64], class_name[32];
    GetClassNameW(child, class_name, 32);
    if (lstrcmpW(class_name, L"Button")) return TRUE;
    GetWindowTextW(child, text, 64);
    if (!lstrcmpW(text, L"OK"))     controls[1] = child;
    else if (!lstrcmpW(text, L"Cancel")) controls[2] = child;
    if ((GetWindowLongPtrW(child, GWL_STYLE) & BS_TYPEMASK) == BS_AUTOCHECKBOX
        && !controls[0]) controls[0] = child;
    return TRUE;
}

static BOOL CALLBACK smoke_optimization_children(HWND child, LPARAM data)
{
    HWND *controls = (HWND *)data;
    WCHAR class_name[32], text[32];
    GetClassNameW(child, class_name, 32);
    if (!lstrcmpiW(class_name, L"COMBOBOX")) {
        if (!controls[0]) controls[0] = child;
        else if (!controls[1]) controls[1] = child;
    } else if ((GetWindowLongPtrW(child, GWL_STYLE) & BS_TYPEMASK)
               == BS_AUTOCHECKBOX && !lstrcmpiW(class_name, L"BUTTON")) {
        if (!controls[2]) controls[2] = child;
        else if (!controls[3]) controls[3] = child;
    }
    GetWindowTextW(child, text, 32);
    if (!lstrcmpW(text, L"OK"))     controls[4] = child;
    else if (!lstrcmpW(text, L"Cancel")) controls[5] = child;
    return TRUE;
}

static int smoke_select_size(HWND window, HWND combo, const WCHAR *label)
{
    LRESULT count = SendMessageW(combo, CB_GETCOUNT, 0, 0), i;
    for (i = 0; i < count; ++i) {
        WCHAR text[96];
        LRESULT length = SendMessageW(combo, CB_GETLBTEXTLEN, (WPARAM)i, 0);
        if (length < 0 || length >= 96) continue;
        if (SendMessageW(combo, CB_GETLBTEXT, (WPARAM)i, (LPARAM)text) == CB_ERR
            || lstrcmpW(text, label)) continue;
        if (SendMessageW(combo, CB_SETCURSEL, (WPARAM)i, 0) == CB_ERR) return 0;
        SendMessageW(window, WM_COMMAND,
            MAKEWPARAM(GetDlgCtrlID(combo), CBN_SELCHANGE), (LPARAM)combo);
        return 1;
    }
    return 0;
}

static BOOL CALLBACK smoke_windows(HWND window, LPARAM data)
{
    WCHAR title[128];
    HWND  controls[3] = {0};
    (void)data;
    if (GetWindow(window, GW_OWNER) != native(g_window)) return TRUE;
    GetWindowTextW(window, title, 128);
    if (lstrcmpW(title, g_smokeTitle)) return TRUE;
    if (IsWindowEnabled(native(g_window))) return TRUE;
    if (g_smokeAction == 4 || g_smokeAction == 5) {
        HWND optimization[6] = {0};
        gui_backend_optimization_capabilities capabilities;
        gui_backend_optimization_options current;
        int i;
        EnumChildWindows(window, smoke_optimization_children, (LPARAM)optimization);
        for (i = 0; i < 6; ++i) if (!optimization[i]) return TRUE;
        if (!gui_backend_get_optimization_capabilities(&capabilities) ||
            !gui_backend_get_optimization_options(&current) ||
            !!IsWindowEnabled(optimization[2]) != capabilities.sse2 ||
            !!IsWindowEnabled(optimization[3]) != capabilities.avx2 ||
            (SendMessageW(optimization[2], BM_GETCHECK, 0, 0) == BST_CHECKED) != current.use_sse2 ||
            (SendMessageW(optimization[3], BM_GETCHECK, 0, 0) == BST_CHECKED) != current.use_avx2 ||
            !smoke_select_size(window, optimization[0], L"1 KiB") ||
            !smoke_select_size(window, optimization[1], L"2 KiB")) {
            g_smokeFailed = 1;
            SendMessageW(window, WM_CLOSE, 0, 0);
        } else {
            for (i = 2; i < 4; ++i)
                if (SendMessageW(optimization[i], BM_GETCHECK, 0, 0) == BST_CHECKED)
                    SendMessageW(optimization[i], BM_CLICK, 0, 0);
            SendMessageW(optimization[g_smokeAction == 5 ? 4 : 5], BM_CLICK, 0, 0);
        }
    } else if (g_smokeAction == 3) {
        HWND cancel = GetDlgItem(window, IDCANCEL);
        if (!cancel) return TRUE;
        SetDlgItemTextW(window, IDC_OPT_MAIN, L"Smoke test cancelled database path");
        SendMessageW(cancel, BM_CLICK, 0, 0);
    } else if (g_smokeAction) {
        EnumChildWindows(window, smoke_children, (LPARAM)controls);
        if (!controls[0] || !controls[1] || !controls[2]) return TRUE;
        SendMessageW(controls[0], BM_CLICK, 0, 0);
        SendMessageW(controls[g_smokeAction == 1 ? 1 : 2], BM_CLICK, 0, 0);
    } else {
        PostMessageW(window, WM_CLOSE, 0, 0);
    }
    g_smokeDone = 1;
    KillTimer(NULL, g_smokeTimer);
    g_smokeTimer = 0;
    return FALSE;
}

static VOID CALLBACK smoke_tick(HWND window, UINT message, UINT_PTR timer, DWORD time)
{
    (void)window; (void)message; (void)timer; (void)time;
    EnumThreadWindows(GetCurrentThreadId(), smoke_windows, 0);
    if (!g_smokeDone && ++g_smokeTicks >= 250) {
        KillTimer(NULL, g_smokeTimer);
        g_smokeTimer = 0;
        xxwidgets_app_quit(g_app, 1);
    }
}

static int smoke_begin(const WCHAR *title, int action)
{
    g_smokeTitle  = title; g_smokeAction = action;
    g_smokeDone   = g_smokeTicks = g_smokeFailed = 0;
    g_smokeTimer  = SetTimer(NULL, 0, 20, smoke_tick);
    return g_smokeTimer != 0;
}

static int smoke_finish(void)
{
    if (g_smokeTimer) KillTimer(NULL, g_smokeTimer);
    g_smokeTimer = 0;
    return g_smokeDone && !g_smokeFailed && IsWindowEnabled(native(g_window));
}

static int smoke_failed(unsigned int line)
{
    char text[80];
    DWORD written;
    int length = wsprintfA(text, "cdie GUI smoke failed at main_gui.c:%u\r\n", line);
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, (DWORD)length, &written, NULL);
    return 1;
}

static int run_smoke(void)
{
    xxwidgets_scan_result row = {"Compiler","Microsoft Visual C/C++","19.29","Smoke test"};
    unsigned int flags, databases;
    WCHAR class_name[32];
    SIZE_T report_length = 0;
    xxwidgets_status status;
    if (!GetClassNameW(native(g_type),      class_name, 32) || lstrcmpiW(class_name, L"COMBOBOX") ||
        !GetClassNameW(native(g_flags),     class_name, 32) || lstrcmpiW(class_name, L"COMBOBOX") ||
        !GetClassNameW(native(g_databases), class_name, 32) || lstrcmpiW(class_name, L"COMBOBOX"))
        return smoke_failed(__LINE__);
    if (xxwidgets_widget_kind(g_results)      != XXWIDGETS_TREEVIEW     ||
        xxwidgets_widget_kind(g_type)         != XXWIDGETS_COMBOBOX     ||
        xxwidgets_widget_kind(g_flags)        != XXWIDGETS_CHECKCOMBOBOX||
        xxwidgets_widget_kind(g_databases)    != XXWIDGETS_CHECKCOMBOBOX||
        xxwidgets_widget_kind(g_scan)         != XXWIDGETS_BUTTON       ||
        xxwidgets_widget_kind(g_reportButton) != XXWIDGETS_BUTTON       ||
        xxwidgets_scan_panel_get_flags(g_panel, &flags)     != XXWIDGETS_OK || flags     != 7u ||
        xxwidgets_scan_panel_get_databases(g_panel, &databases) != XXWIDGETS_OK || databases != 3u ||
        xxwidgets_scan_panel_set_results(g_panel, "Smoke file", &row, 1,
            "Smoke report\nCompiler: Microsoft Visual C/C++ 19.29") != XXWIDGETS_OK ||
        xxwidgets_scan_panel_count(g_panel) != 1 ||
        TreeView_GetCount(native(g_results)) < 2 ||
        xxwidgets_scan_panel_get_report(g_panel, NULL, 0, &report_length) != XXWIDGETS_OK ||
        report_length <= 1)
        return smoke_failed(__LINE__);
    if (gui_backend_signature_count() > 0) {
        WCHAR executable[32768];
        char  status_text[512];
        size_t i; size_t binary_index = SIZE_MAX;
        int type_id, binary_type = -1;
        DWORD length = GetModuleFileNameW(NULL, executable,
                        (DWORD)(sizeof(executable)/sizeof(executable[0])));
        if (!length || length >= sizeof(executable)/sizeof(executable[0]))
            return smoke_failed(__LINE__);
        if (!SetWindowTextW(native(g_path), executable) ||
            IsWindowEnabled(native(g_type)))
            return smoke_failed(__LINE__);
        do_scan();
        if (xxwidgets_scan_panel_get_report(g_panel, NULL, 0, &report_length) != XXWIDGETS_OK ||
            report_length <= 1 || !xxwidgets_scan_panel_count(g_panel) ||
            TreeView_GetCount(native(g_results)) < 2 ||
            selected_file_type(&type_id) != XXWIDGETS_OK || type_id != 0 ||
            xxwidgets_combobox_count(g_type) < 3 ||
            !IsWindowEnabled(native(g_type)))
            return smoke_failed(__LINE__);
        for (i = 0; i < xxwidgets_combobox_count(g_type); ++i) {
            const xx_meta_string *record;
            if (xxwidgets_combobox_get_record(g_type, i, &record) != XXWIDGETS_OK)
                return smoke_failed(__LINE__);
            if (!lstrcmpiW(record->meta_string->data, L"Binary")) {
                binary_index = i; binary_type = (int)record->var.val.u64; break;
            }
        }
        if (binary_index == SIZE_MAX || binary_type <= 0) return smoke_failed(__LINE__);
        SendMessageW(native(g_type), CB_SETCURSEL, binary_index, 0);
        SendMessageW(native(g_window), WM_COMMAND,
            MAKEWPARAM(IDC_COMBO_TYPE, CBN_SELCHANGE), (LPARAM)native(g_type));
        if (g_pending != ACTION_SCAN ||
            selected_file_type(&type_id) != XXWIDGETS_OK || type_id != binary_type)
            return smoke_failed(__LINE__);
        g_pending = ACTION_NONE;
        do_scan();
        if (g_pending != ACTION_NONE ||
            selected_file_type(&type_id) != XXWIDGETS_OK || type_id != binary_type ||
            xxwidgets_widget_get_text(g_status, status_text, sizeof(status_text), NULL) != XXWIDGETS_OK ||
            !strstr(status_text, "Scan complete (Binary)"))
            return smoke_failed(__LINE__);
        SendMessageW(native(g_type), CB_SETCURSEL, 0, 0);
        SendMessageW(native(g_window), WM_COMMAND,
            MAKEWPARAM(IDC_COMBO_TYPE, CBN_SELCHANGE), (LPARAM)native(g_type));
        if (g_pending != ACTION_SCAN) return smoke_failed(__LINE__);
        g_pending = ACTION_NONE;
        do_scan();
        if (g_pending != ACTION_NONE ||
            selected_file_type(&type_id) != XXWIDGETS_OK || type_id != 0 ||
            !xxwidgets_scan_panel_count(g_panel))
            return smoke_failed(__LINE__);
    }
    if (!smoke_begin(L"cdie Scan options", 2) || do_options() != 0 ||
        !smoke_finish() || !g_bFlagDeep)
        return smoke_failed(__LINE__);
    if (xxwidgets_scan_panel_get_flags(g_panel, &flags) != XXWIDGETS_OK || flags != 7u)
        return smoke_failed(__LINE__);
    if (!smoke_begin(L"cdie Scan options", 1) || do_options() != 1 ||
        !smoke_finish() || g_bFlagDeep)
        return smoke_failed(__LINE__);
    if (xxwidgets_scan_panel_get_flags(g_panel, &flags) != XXWIDGETS_OK || flags != 6u ||
        xxwidgets_scan_panel_get_databases(g_panel, &databases) != XXWIDGETS_OK || databases != 3u)
        return smoke_failed(__LINE__);
    {
        gui_backend_optimization_options before, after;
        if (!gui_backend_get_optimization_options(&before) ||
            !smoke_begin(L"cdie Optimization", 4) || do_optimization() != 0 ||
            !smoke_finish() || !gui_backend_get_optimization_options(&after) ||
            after.buffer_size != before.buffer_size ||
            after.file_buffer_size != before.file_buffer_size ||
            after.use_sse2 != before.use_sse2 || after.use_avx2 != before.use_avx2)
            return smoke_failed(__LINE__);
        if (!smoke_begin(L"cdie Optimization", 5) || do_optimization() != 1 ||
            !smoke_finish() || !gui_backend_get_optimization_options(&after) ||
            after.buffer_size != 1024 || after.file_buffer_size != 2048 ||
            after.use_sse2 || after.use_avx2 ||
            !gui_backend_set_optimization_options(&before))
            return smoke_failed(__LINE__);
    }
    {
        xxwidgets_font_options before, after;
        if (xxwidgets_app_get_font_options(g_app, &before) != XXWIDGETS_OK ||
            !smoke_begin(L"cdie Fonts", 2) || do_fonts() != 0 ||
            !smoke_finish() ||
            xxwidgets_app_get_font_options(g_app, &after) != XXWIDGETS_OK ||
            memcmp(&before, &after, sizeof(before)))
            return smoke_failed(__LINE__);
        if (!smoke_begin(L"cdie Fonts", 1) || do_fonts() != 1 ||
            !smoke_finish() ||
            xxwidgets_app_get_font_options(g_app, &after) != XXWIDGETS_OK ||
            !memcmp(&before, &after, sizeof(before)))
            return smoke_failed(__LINE__);
        if (xxwidgets_app_set_font_options(g_app, &before) != XXWIDGETS_OK)
            return smoke_failed(__LINE__);
        if (!smoke_begin(L"cdie Context menu", 2) || do_context() != 0 ||
            !smoke_finish())
            return smoke_failed(__LINE__);
    }
    if (!smoke_begin(L"About cdie", 0)) return smoke_failed(__LINE__);
    status = xxwidgets_about_dialog_show(g_about, g_window);
    if (status != XXWIDGETS_OK || !smoke_finish()) return smoke_failed(__LINE__);
    if (!smoke_begin(L"Scan report", 0)) return smoke_failed(__LINE__);
    status = xxwidgets_scan_panel_show_report(g_panel);
    if (status != XXWIDGETS_OK || !smoke_finish()) return smoke_failed(__LINE__);
    {
        char *original_path = copy_utf8(gui_backend_main_db_path());
        int   changed;
        if (!original_path || !smoke_begin(L"Database paths", 3)) {
            if (original_path) HeapFree(GetProcessHeap(), 0, original_path);
            return smoke_failed(__LINE__);
        }
        do_database_paths();
        changed = lstrcmpA(original_path, gui_backend_main_db_path());
        HeapFree(GetProcessHeap(), 0, original_path);
        if (!smoke_finish() || changed) return smoke_failed(__LINE__);
    }
    return xxwidgets_scan_panel_clear(g_panel) != XXWIDGETS_OK ||
           xxwidgets_scan_panel_count(g_panel) != 0 ||
           TreeView_GetCount(native(g_results)) != 0;
}

static int is_smoke_requested(void)
{
    int argc = 0, requested = 0, i;
    WCHAR **arguments = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!arguments) return 0;
    for (i = 1; i < argc; ++i)
        if (!lstrcmpW(arguments[i], L"--smoke-test")) requested = 1;
    LocalFree(arguments);
    return requested;
}

static int set_startup_file(void)
{
    int argc = 0, has_file = 0;
    WCHAR **arguments = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (arguments && argc > 1 && lstrcmpW(arguments[1], L"--smoke-test")) {
        char *path = wide_to_utf8(arguments[1]);
        if (path) {
            has_file = xxwidgets_widget_set_text(g_path, path) == XXWIDGETS_OK;
            HeapFree(GetProcessHeap(), 0, path);
        }
    }
    if (arguments) LocalFree(arguments);
    return has_file;
}

int gui_run(void)
{
    xxwidgets_config config = {XXWIDGETS_BACKEND_NATIVE, on_event, NULL};
    xxwidgets_rect   bounds = {8, 4, 118, 40};  
    xxwidgets_status status;
    int   result = 0, smoke = is_smoke_requested();
    WCHAR title[160], ready[512];
    WCHAR *base;
    char  *utf8_title;

    g_hInst = GetModuleHandleW(NULL);
    status = xxwidgets_app_create(&config, &g_app);
    if (status != XXWIDGETS_OK) {
      MessageBoxW(NULL, L"xxwidgets_app_create failed", L"cdie", MB_ICONERROR);
      return 1;
    }

    base = utf8_to_wide(gui_backend_app_title());
#if defined(_WIN64)
    wsprintfW(title, L"%s (x64)", base ? base : L"cdie");
#else
    wsprintfW(title, L"%s (x86)", base ? base : L"cdie");
#endif
    if (base) HeapFree(GetProcessHeap(), 0, base);

    utf8_title = wide_to_utf8(title);
    if (!utf8_title) { result = 1; goto finish; }
    xxwidgets_app_set_create_hidden(g_app, 1);
    status = xxwidgets_widget_create(g_app, NULL, XXWIDGETS_WINDOW,
      utf8_title, bounds, &g_window);
    xxwidgets_app_set_create_hidden(g_app, 0);
    HeapFree(GetProcessHeap(), 0, utf8_title);
    if (status != XXWIDGETS_OK) {
      MessageBoxW(NULL, L"Main window creation failed", L"cdie", MB_ICONERROR);
      result = 1; goto finish;
    }

    {
        BOOL dark = TRUE;
        DwmSetWindowAttribute(native(g_window), 20, &dark, sizeof(dark));
    }

    xxwidgets_widget_set_visible(g_window, 0);
    if (!create_controls() || configure_about() != XXWIDGETS_OK) {
      MessageBoxW(NULL, L"create_controls or configure_about failed", L"cdie", MB_ICONERROR);
      result = 1; goto finish;
    }

    SetWindowPos(native(g_window), NULL, 0, 0, 1150, 700, SWP_NOMOVE | SWP_NOZORDER);

    if (gui_backend_init()) {
      wsprintfW(ready, L"Ready. %d signatures loaded. Choose a file or drag one here, then press Scan.",
        gui_backend_signature_count());
        set_status_wide(ready);
    } else {
        xxwidgets_widget_set_text(g_status,
            "Main signature database not found. "
            "Use Database paths to select the database folder.");
    }
    apply_flags_to_backend();
    layout();

    if (smoke) { result = run_smoke(); goto finish; }

    xxwidgets_widget_set_visible(g_window, 1);
    xxwidgets_widget_focus(g_path);
    if (set_startup_file()) do_scan();

    g_running = 1;
    while (g_running) {
        int action;
        status = xxwidgets_app_poll(g_app, 30);
        if (status != XXWIDGETS_OK) { result = 1; break; }
        if (!g_running) break;
        action = g_pending; g_pending = ACTION_NONE;
        switch (action) {
        case ACTION_SCAN:           do_scan();           break;
        case ACTION_BROWSE:         do_browse();         break;
        case ACTION_OPTIONS:        do_options();        break;
        case ACTION_ABOUT:          do_about();          break;
        case ACTION_DATABASE_PATHS: do_database_paths(); break;
        case ACTION_FONTS:          do_fonts();          break;
        case ACTION_CONTEXT:        do_context();        break;
        case ACTION_OPTIMIZATION:   do_optimization();   break;
        case ACTION_REPORT:
            status = xxwidgets_scan_panel_show_report(g_panel);
            if (status != XXWIDGETS_OK)
                report_ui_error("Open scan report", status);
            break;
        default: break;
        }
    }

finish:
    if (g_smokeTimer) KillTimer(NULL, g_smokeTimer);
    gui_backend_shutdown();
    if (g_about  && xxwidgets_about_dialog_destroy(g_about)   != XXWIDGETS_OK) result = 1;
    g_about = NULL;
    if (g_panel  && xxwidgets_scan_panel_destroy(g_panel)     != XXWIDGETS_OK) result = 1;
    g_panel = NULL;
    if (g_typePath) HeapFree(GetProcessHeap(), 0, g_typePath);
    g_typePath = NULL;
    if (xxwidgets_app_destroy(g_app) != XXWIDGETS_OK) result = 1;
    g_app = NULL;
    return result;
}