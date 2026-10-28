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
#include <xxwidgets/xxwidgets_font_options.h>
#include <xxwidgets/xxwidgets_context_options.h>
#include <limits.h>
#include <string.h>

#include "core/gui_backend.h"
#include "core/config.h"
#include "resource.h"
#include "theme/theme.h"
#include "controls/sidebar.h"
#include "panels/header.h"
#include "panels/infobar.h"
#include "panels/options.h"
#include "controls/tabbar.h"
#include "controls/bottombar.h"
#include "controls/treeview.h"
#include "core/tray.h"
#include "core/dlwatch.h"


#define IDC_EDIT_PATH      102
#define IDC_COMBO_TYPE     104
#define IDC_COMBO_FLAGS    105
#define IDC_COMBO_DATABASES 106
#define IDC_STATIC_MSEC    107
#define IDC_BTN_SCAN       108
#define IDC_SCANRESULTS    109
#define IDC_BTN_REPORT     113
#define IDC_STATUS         114

 /* Store / VSIX builds get updates from Microsoft */
#ifdef CDIE_STORE_BUILD
#define CDIE_SHOW_UPDATES 0
#define CDIE_SHOW_CONTEXT 0
#else
#define CDIE_SHOW_UPDATES 1
#define CDIE_SHOW_CONTEXT 1
#endif

enum gui_action {
	ACTION_NONE, ACTION_SCAN, ACTION_BROWSE,
	ACTION_OPTIONS, ACTION_ABOUT, ACTION_REPORT,
	ACTION_DATABASE_PATHS,
	ACTION_SIGNATURES, ACTION_STRINGS,
	ACTION_MONITOR
};

static xxwidgets_app* g_app;
static xxwidgets_widget* g_window, * g_path, * g_type;
static xxwidgets_widget* g_flags, * g_databases, * g_msec, * g_scan, * g_results;
static xxwidgets_widget* g_typeLabel, * g_flagLabel, * g_databaseLabel;
static xxwidgets_widget* g_reportButton, * g_status;
static xxwidgets_scan_panel* g_panel;

/* Custom dark-chrome child windows */
static Sidebar* g_sidebar;
static Header* g_header_wnd;
static Infobar* g_infobar_wnd;
static Bottombar* g_bottombar;
static ResultsTree* g_tree;

static int  g_running, g_pending;
static int  g_startMonitor;
static char* g_typePath;
static char* g_lastPath;
static int  g_minWindowWidth = 900;
static int  g_minWindowHeight = 540;
static int g_prefsReady;

static int g_bFlagDeep = 1, g_bFlagHeur = 1, g_bFlagVerbose = 1;
static int g_bFlagAggr, g_bFlagHideUnknown, g_bFlagFormat;
static int g_bDbExtra = 1, g_bDbCustom = 1;
static void layout(void);

static void on_tray_exit(void* user)
{
	(void)user;
	g_running = 0;
	xxwidgets_app_quit(g_app, 0);
}

static HWND native(xxwidgets_widget* widget)
{
	return (HWND)xxwidgets_widget_native_handle(widget);
}

static void apply_flags_to_backend(void)
{
	gui_backend_set_flags(g_bFlagDeep, g_bFlagHeur, g_bFlagVerbose,
		g_bFlagAggr, g_bFlagHideUnknown, g_bFlagFormat);
	gui_backend_set_databases(g_bDbExtra, g_bDbCustom);
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

static char* copy_utf8(const char* text)
{
	SIZE_T length = 0;
	char* copy;
	if (!text) text = "";
	while (text[length]) ++length;
	copy = (char*)HeapAlloc(GetProcessHeap(), 0, length + 1);
	if (copy) CopyMemory(copy, text, length + 1);
	return copy;
}

static void set_status_wide(const WCHAR* wide)
{
	char* text = wide_to_utf8(wide);
	if (text) {
		xxwidgets_widget_set_text(g_status, text);
		HeapFree(GetProcessHeap(), 0, text);
	}
}

static void report_ui_error(const char* operation, xxwidgets_status status)
{
	WCHAR text[256];
	WCHAR* name = utf8_to_wide(operation);
	WCHAR* detail = utf8_to_wide(xxwidgets_status_string(status));
	wsprintfW(text, L"%s: %s",
		name ? name : L"Interface error",
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

static void format_size(ULONGLONG bytes, WCHAR* main_out, WCHAR* sub_out)
{
	if (bytes >= 1024ULL * 1024) {
		ULONGLONG n100 = (bytes * 100ULL) / (1024ULL * 1024);
		wsprintfW(main_out, L"%u.%02u MiB",
			(unsigned)(n100 / 100), (unsigned)(n100 % 100));
	}
	else if (bytes >= 1024) {
		ULONGLONG n10 = (bytes * 10ULL) / 1024;
		wsprintfW(main_out, L"%u.%u KiB",
			(unsigned)(n10 / 10), (unsigned)(n10 % 10));
	}
	else {
		wsprintfW(main_out, L"%u B", (unsigned)bytes);
	}
	if (bytes < 1000000ULL) {
		wsprintfW(sub_out, L"%u bytes", (unsigned)bytes);
	}
	else {
		unsigned hi = (unsigned)(bytes / 1000000ULL);
		unsigned lo = (unsigned)(bytes % 1000000ULL);
		wsprintfW(sub_out, L"%u,%06u bytes", hi, lo);
	}
}

static void get_file_info(const char* utf8_path, const char* type_name, FileInfo* fi)
{
	WCHAR* wpath = utf8_to_wide(utf8_path);

	/* type string */
	if (type_name && type_name[0]) {
		WCHAR* wt = utf8_to_wide(type_name);
		if (wt) { lstrcpynW(fi->type_str, wt, 79); HeapFree(GetProcessHeap(), 0, wt); }
		else lstrcpyW(fi->type_str, L"Unknown");
	}
	else {
		lstrcpyW(fi->type_str, L"Unknown");
	}

	/* file size */
	lstrcpyW(fi->size_str, L"N/A");
	lstrcpyW(fi->size_sub, L"");
	if (wpath) {
		WIN32_FILE_ATTRIBUTE_DATA fa = { 0 };
		if (GetFileAttributesExW(wpath, GetFileExInfoStandard, &fa)) {
			ULONGLONG bytes =
				((ULONGLONG)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
			format_size(bytes, fi->size_str, fi->size_sub);
		}
	}

	/* default EP / base */
	lstrcpyW(fi->ep_str, L"N/A");
	lstrcpyW(fi->ep_sub, L"");
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
								wsprintfW(fi->ep_str, L"0x%08X", ep32);
								wsprintfW(fi->base_str, L"0x%08X", base32);
								lstrcpyW(fi->ep_sub, L"RVA");
								lstrcpyW(fi->base_sub, L"Preferred load");
							}
							else if (opt_magic == 0x20B) { /* PE32+ */
								DWORD ep64 = 0;
								ULONGLONG base64 = 0;
								SetFilePointer(hf, (LONG)(pe_off + 40), NULL, FILE_BEGIN);
								ReadFile(hf, &ep64, 4, &read, NULL);
								SetFilePointer(hf, (LONG)(pe_off + 48), NULL, FILE_BEGIN);
								ReadFile(hf, &base64, 8, &read, NULL);
								wsprintfW(fi->ep_str, L"0x%08X", ep64);
								if (base64 > 0xFFFFFFFF)
									wsprintfW(fi->base_str, L"0x%08X%08X",
										(unsigned)(base64 >> 32),
										(unsigned)(base64 & 0xFFFFFFFF));
								else
									wsprintfW(fi->base_str, L"0x%08X",
										(unsigned)base64);
								lstrcpyW(fi->ep_sub, L"RVA");
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
	g_bFlagDeep = (flags & 1u) != 0;
	g_bFlagHeur = (flags & 2u) != 0;
	g_bFlagVerbose = (flags & 4u) != 0;
	g_bFlagAggr = (flags & 8u) != 0;
	g_bFlagHideUnknown = (flags & 16u) != 0;
	g_bFlagFormat = (flags & 32u) != 0;
	g_bDbExtra = (databases & 1u) != 0;
	g_bDbCustom = (databases & 2u) != 0;
	apply_flags_to_backend();
	return XXWIDGETS_OK;
}

static void clear_scan(void)
{
	xxwidgets_scan_panel_clear(g_panel);
	if (g_tree) resultstree_clear(g_tree);
	xxwidgets_widget_set_text(g_msec, "");
	if (g_infobar_wnd) infobar_clear(g_infobar_wnd);
	if (g_header_wnd)  header_clear(g_header_wnd);
}

typedef struct file_type_buffer {
	xx_meta_string* records;
	SIZE_T count, capacity;
	int failed;
} file_type_buffer;

static void free_file_types(file_type_buffer* buffer)
{
	SIZE_T i;
	for (i = 0; i < buffer->count; ++i) {
		HeapFree(GetProcessHeap(), 0, buffer->records[i].meta_string->data);
		HeapFree(GetProcessHeap(), 0, buffer->records[i].meta_string);
	}
	if (buffer->records) HeapFree(GetProcessHeap(), 0, buffer->records);
}

static void collect_file_type(int type_id, const char* name, void* user_data)
{
	file_type_buffer* buffer = (file_type_buffer*)user_data;
	xx_meta_string record = { 0 };
	WCHAR* wide;
	SIZE_T length = 0;
	if (buffer->failed) return;
	if (buffer->count == buffer->capacity) {
		SIZE_T capacity = buffer->capacity ? buffer->capacity * 2 : 8;
		xx_meta_string* records;
		if (capacity < buffer->capacity ||
			capacity >((SIZE_T)-1) / sizeof(*records)) {
			buffer->failed = 1; return;
		}
		records = buffer->records
			? (xx_meta_string*)HeapReAlloc(GetProcessHeap(), 0,
				buffer->records, capacity * sizeof(*records))
			: (xx_meta_string*)HeapAlloc(GetProcessHeap(), 0,
				capacity * sizeof(*records));
		if (!records) { buffer->failed = 1; return; }
		buffer->records = records; buffer->capacity = capacity;
	}
	wide = utf8_to_wide(name);
	record.meta_string = (xx_str_w_s*)HeapAlloc(GetProcessHeap(),
		HEAP_ZERO_MEMORY, sizeof(*record.meta_string));
	if (!wide || !record.meta_string) {
		if (wide) HeapFree(GetProcessHeap(), 0, wide);
		if (record.meta_string) HeapFree(GetProcessHeap(), 0, record.meta_string);
		buffer->failed = 1; return;
	}
	while (wide[length]) ++length;
	record.meta_string->data = wide;
	record.meta_string->length = length;
	record.meta_string->capacity = length + 1;
	record.meta_string->is_view = true;
	record.var.type = XX_VAR_TYPE_UINT64;
	record.var.val.u64 = (uint64_t)type_id;
	buffer->records[buffer->count++] = record;
}

static xxwidgets_status selected_file_type(int* type_id)
{
	xx_var value = { 0 };
	xxwidgets_status status = xxwidgets_combobox_get_current(g_type, &value);
	if (status != XXWIDGETS_OK) return status;
	if (value.type != XX_VAR_TYPE_UINT64 || value.val.u64 > INT_MAX)
		return XXWIDGETS_INVALID_ARGUMENT;
	*type_id = (int)value.val.u64;
	return XXWIDGETS_OK;
}

static xxwidgets_status refresh_file_types(const char* path)
{
	file_type_buffer buffer = { 0 };
	xxwidgets_status status;
	char* new_path;
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
	xxwidgets_scan_result* records;
	SIZE_T count, capacity;
	int failed;
} result_buffer;

static void free_result_record(xxwidgets_scan_result* record)
{
	if (record->type)    HeapFree(GetProcessHeap(), 0, (void*)record->type);
	if (record->name)    HeapFree(GetProcessHeap(), 0, (void*)record->name);
	if (record->version) HeapFree(GetProcessHeap(), 0, (void*)record->version);
	if (record->info)    HeapFree(GetProcessHeap(), 0, (void*)record->info);
}

static void free_result_buffer(result_buffer* buffer)
{
	SIZE_T i;
	for (i = 0; i < buffer->count; ++i)
		free_result_record(&buffer->records[i]);
	if (buffer->records) HeapFree(GetProcessHeap(), 0, buffer->records);
}

static void collect_result(const char* type, const char* name,
	const char* version, const char* info,
	void* user_data)
{
	result_buffer* buffer = (result_buffer*)user_data;
	xxwidgets_scan_result record;
	if (buffer->failed) return;
	if (buffer->count == buffer->capacity) {
		SIZE_T capacity = buffer->capacity ? buffer->capacity * 2 : 16;
		xxwidgets_scan_result* records;
		if (capacity < buffer->capacity ||
			capacity >((SIZE_T)-1) / sizeof(*records)) {
			buffer->failed = 1; return;
		}
		records = buffer->records
			? (xxwidgets_scan_result*)HeapReAlloc(GetProcessHeap(), 0,
				buffer->records, capacity * sizeof(*records))
			: (xxwidgets_scan_result*)HeapAlloc(GetProcessHeap(), 0,
				capacity * sizeof(*records));
		if (!records) { buffer->failed = 1; return; }
		buffer->records = records;
		buffer->capacity = capacity;
	}
	record.type = copy_utf8(type);
	record.name = copy_utf8(name);
	record.version = copy_utf8(version);
	record.info = copy_utf8(info);
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
	char* path, * report = NULL, * type = NULL;
	result_buffer buffer = { 0 };
	LARGE_INTEGER frequency, started, ended;
	xxwidgets_status status;
	int ok, type_id = 0;

	if (config_db_required()) {
		xxwidgets_widget_set_text(g_status, "Select the signature database folder first.");
		g_pending = ACTION_DATABASE_PATHS;
		return;
	}
	status = xxwidgets_widget_get_text(g_path, NULL, 0, &required);
	if (status != XXWIDGETS_OK) {
		report_ui_error("Read file name", status); return;
	}
	if (required <= 1) {
		MessageBeep(MB_ICONASTERISK);
		xxwidgets_widget_focus(g_path);
		return;
	}
	path = (char*)HeapAlloc(GetProcessHeap(), 0, required);
	if (!path) { report_ui_error("Read file name", XXWIDGETS_OUT_OF_MEMORY); return; }
	status = xxwidgets_widget_get_text(g_path, path, required, NULL);
	if (status != XXWIDGETS_OK) {
		HeapFree(GetProcessHeap(), 0, path);
		report_ui_error("Read file name", status); return;
	}
	apply_flags_to_backend();
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
			SIZE_T k;
			for (k = 0; k < buffer.count; ++k)
				resultstree_add(g_tree, buffer.records[k].type, buffer.records[k].name,
					buffer.records[k].version, buffer.records[k].info);
			{
				WCHAR* type_name = utf8_to_wide(type && type[0] ? type : "Unknown type");
				wsprintfW(text,
					L"Scan complete (%s). %u result%s. Use Report for full engine output.",
					type_name ? type_name : L"Unknown type",
					(unsigned int)buffer.count,
					buffer.count == 1 ? L"" : L"s");
				set_status_wide(text);
				if (type_name) HeapFree(GetProcessHeap(), 0, type_name);

				update_chrome_after_scan(path, type);
				if (g_lastPath) HeapFree(GetProcessHeap(), 0, g_lastPath);
				g_lastPath = copy_utf8(path);
			}
		}
		wsprintfW(text, L"%u ms", msec);
		{
			char* u = wide_to_utf8(text);
			if (u) {
				xxwidgets_widget_set_text(g_msec, u);
				HeapFree(GetProcessHeap(), 0, u);
			}
		}
	}
	else {
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
	picker.hwndOwner = native(g_window);
	picker.lpstrFile = file;
	picker.nMaxFile = (DWORD)(sizeof(file) / sizeof(file[0]));
	picker.lpstrFilter = L"All files\0*.*\0";
	picker.lpstrTitle = L"Select a file to scan";
	picker.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER;
	if (GetOpenFileNameW(&picker)) {
		char* path = wide_to_utf8(file);
		if (path) {
			xxwidgets_widget_set_text(g_path, path);
			HeapFree(GetProcessHeap(), 0, path);
			do_scan();
		}
	}
}

static void opt_get_scan(void* u, OptionsScan* o)
{
	(void)u;
	o->deep = g_bFlagDeep; o->heuristic = g_bFlagHeur; o->verbose = g_bFlagVerbose;
	o->aggressive = g_bFlagAggr; o->hide_unknown = g_bFlagHideUnknown;
	o->format = g_bFlagFormat;
	o->db_extra = g_bDbExtra; o->db_custom = g_bDbCustom;
	o->monitor_downloads = dlwatch_enabled();
}

static int opt_set_scan(void* u, const OptionsScan* o)
{
	(void)u;
	g_bFlagDeep = o->deep; g_bFlagHeur = o->heuristic; g_bFlagVerbose = o->verbose;
	g_bFlagAggr = o->aggressive; g_bFlagHideUnknown = o->hide_unknown;
	g_bFlagFormat = o->format;
	g_bDbExtra = o->db_extra; g_bDbCustom = o->db_custom;
	if ((o->monitor_downloads != 0) != (dlwatch_enabled() != 0))
		dlwatch_set_enabled(o->monitor_downloads ? 1 : 0);
	apply_flags_to_backend();
	return sync_panel_options() == XXWIDGETS_OK;
}

static int opt_get_opt(void* u, OptionsOptimization* o)
{
	gui_backend_optimization_options cur;
	gui_backend_optimization_capabilities caps;
	(void)u;
	if (!gui_backend_get_optimization_options(&cur) ||
		!gui_backend_get_optimization_capabilities(&caps)) return 0;
	o->buffer_size = cur.buffer_size;
	o->file_buffer_size = cur.file_buffer_size;
	o->use_sse2 = cur.use_sse2; o->use_avx2 = cur.use_avx2;
	o->has_sse2 = caps.sse2; o->has_avx2 = caps.avx2;
	return 1;
}

static int opt_set_opt(void* u, const OptionsOptimization* o)
{
	gui_backend_optimization_options cur;
	(void)u;
	if (!gui_backend_get_optimization_options(&cur)) return 0;
	cur.buffer_size = o->buffer_size;
	cur.file_buffer_size = o->file_buffer_size;
	cur.use_sse2 = o->use_sse2; cur.use_avx2 = o->use_avx2;
	return gui_backend_set_optimization_options(&cur) ? 1 : 0;
}

static int opt_browse_folder(void* u, char* out, size_t cap)
{
	BROWSEINFOW picker; LPITEMIDLIST item; WCHAR path[MAX_PATH]; int ok = 0;
	(void)u;
	ZeroMemory(&picker, sizeof(picker));
	picker.hwndOwner = GetActiveWindow();
	picker.lpszTitle = L"Select the database folder";
	picker.ulFlags = BIF_RETURNONLYFSDIRS;
	item = SHBrowseForFolderW(&picker);
	if (item) {
		if (SHGetPathFromIDListW(item, path))
			ok = WideCharToMultiByte(CP_UTF8, 0, path, -1, out, (int)cap, NULL, NULL) > 0;
		CoTaskMemFree(item);
	}
	return ok;
}

static void opt_db_changed(void* u)
{
	WCHAR text[160];
	(void)u;
	clear_scan();
	wsprintfW(text, L"Database reloaded: %d signatures. Choose a file and press Scan.",
		gui_backend_signature_count());
	set_status_wide(text);
}

typedef struct { WCHAR** names; SIZE_T count, cap; int failed; int fixed_only; } font_list;

static int CALLBACK enum_font_proc(const LOGFONTW* lf, const TEXTMETRICW* tm,
	DWORD type, LPARAM param)
{
	font_list* list = (font_list*)param;
	(void)tm;
	if (list->failed) return 0;
	if (lf->lfFaceName[0] == L'@') return 1;
	if (!(type & TRUETYPE_FONTTYPE)) return 1;
	if (list->fixed_only && !(lf->lfPitchAndFamily & FIXED_PITCH)) return 1;

	if (list->count == list->cap) {
		SIZE_T cap = list->cap ? list->cap * 2 : 128;
		WCHAR** n = list->names
			? (WCHAR**)HeapReAlloc(GetProcessHeap(), 0, list->names, cap * sizeof(*n))
			: (WCHAR**)HeapAlloc(GetProcessHeap(), 0, cap * sizeof(*n));
		if (!n) { list->failed = 1; return 0; }
		list->names = n; list->cap = cap;
	}
	{
		SIZE_T len = lstrlenW(lf->lfFaceName) + 1;
		WCHAR* copy = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
		if (!copy) { list->failed = 1; return 0; }
		CopyMemory(copy, lf->lfFaceName, len * sizeof(WCHAR));
		list->names[list->count++] = copy;
	}
	return 1;
}

static int cmp_name(const void* a, const void* b)
{
	return lstrcmpiW(*(WCHAR* const*)a, *(WCHAR* const*)b);
}

static void enumerate_fonts(font_list* list, int fixed_only)
{
	LOGFONTW lf = { 0 };
	HDC dc = GetDC(NULL);
	lf.lfCharSet = DEFAULT_CHARSET;
	list->fixed_only = fixed_only;
	EnumFontFamiliesExW(dc, &lf, enum_font_proc, (LPARAM)list, 0);
	ReleaseDC(NULL, dc);
	if (list->count > 1)
		qsort(list->names, list->count, sizeof(WCHAR*), cmp_name);
	{
		SIZE_T i, out = 0;
		for (i = 0; i < list->count; ++i) {
			if (out && !lstrcmpiW(list->names[out - 1], list->names[i]))
				HeapFree(GetProcessHeap(), 0, list->names[i]);
			else
				list->names[out++] = list->names[i];
		}
		list->count = out;
	}
}

static int opt_list_fonts(void* u, int fixed_only,
	void (*add)(const char* family, void* ud), void* ud)
{
	font_list list = { 0 };
	SIZE_T i;
	(void)u;
	enumerate_fonts(&list, fixed_only);
	if (list.failed) {
		for (i = 0; i < list.count; ++i) HeapFree(GetProcessHeap(), 0, list.names[i]);
		if (list.names) HeapFree(GetProcessHeap(), 0, list.names);
		return 0;
	}
	for (i = 0; i < list.count; ++i) {
		char* utf8 = wide_to_utf8(list.names[i]);
		if (utf8) { add(utf8, ud); HeapFree(GetProcessHeap(), 0, utf8); }
		HeapFree(GetProcessHeap(), 0, list.names[i]);
	}
	if (list.names) HeapFree(GetProcessHeap(), 0, list.names);
	return 1;
}

static int opt_get_fonts(void* u, OptionsFont out[OPTIONS_FONT_ROLES])
{
	xxwidgets_font_options fo;
	int k;
	(void)u;
	if (xxwidgets_app_get_font_options(g_app, &fo) != XXWIDGETS_OK) return 0;
	for (k = 0; k < OPTIONS_FONT_ROLES; ++k) {
		lstrcpynA(out[k].family, fo.fonts[k].family, sizeof(out[k].family));
		out[k].point_size = fo.fonts[k].point_size;
		out[k].bold = fo.fonts[k].bold;
		out[k].italic = fo.fonts[k].italic;
	}
	return 1;
}

static int opt_set_fonts(void* u, const OptionsFont in[OPTIONS_FONT_ROLES])
{
	xxwidgets_font_options fo;
	int k;
	(void)u;
	if (xxwidgets_app_get_font_options(g_app, &fo) != XXWIDGETS_OK) return 0;
	for (k = 0; k < OPTIONS_FONT_ROLES; ++k) {
		lstrcpynA(fo.fonts[k].family, in[k].family, sizeof(fo.fonts[k].family));
		fo.fonts[k].point_size = in[k].point_size;
		fo.fonts[k].bold = in[k].bold ? 1 : 0;
		fo.fonts[k].italic = in[k].italic ? 1 : 0;
	}
	if (xxwidgets_app_set_font_options(g_app, &fo) != XXWIDGETS_OK) return 0;
	layout();
	xxwidgets_widget_set_text(g_status,
		"Fonts applied to the interface, results and dialogs.");
	return 1;
}

static int context_access(int query, int* value)
{
	WCHAR executable[32768];
	char* path;
	xxwidgets_context_config config = {
		"cdie", "Scan with cdie", "*", NULL, XXWIDGETS_CONTEXT_CURRENT_USER
	};
	xxwidgets_status status;
	DWORD length = GetModuleFileNameW(NULL, executable,
		(DWORD)(sizeof(executable) / sizeof(executable[0])));
	if (!length || length >= sizeof(executable) / sizeof(executable[0])) return 0;
	path = wide_to_utf8(executable);
	if (!path) return 0;
	config.executable = path;
	if (query) {
		int registered = 0;
		status = xxwidgets_context_is_registered(&config, &registered);
		if (status == XXWIDGETS_OK) *value = registered != 0;
	}
	else {
		status = xxwidgets_context_set_enabled(&config, *value ? 1 : 0);
	}
	HeapFree(GetProcessHeap(), 0, path);
	return status == XXWIDGETS_OK;
}

static int opt_context_get(void* u, int* enabled)
{
	(void)u;
	return context_access(1, enabled);
}

static int opt_context_set(void* u, int enabled)
{
	int ok;
	(void)u;
	ok = context_access(0, &enabled);
	if (ok)
		xxwidgets_widget_set_text(g_status,
			"Explorer context menu updated for the current user.");
	return ok;
}

static const char* opt_get_version(void* u)
{
	const char* v = gui_backend_app_title();
	(void)u;
	while (*v && *v != ' ') ++v;
	if (*v) ++v;
	return v;
}

static int opt_open_url(void* u, const char* url)
{
	WCHAR* wide = utf8_to_wide(url);
	HINSTANCE r;
	(void)u;
	if (!wide) return 0;
	r = ShellExecuteW(NULL, L"open", wide, NULL, NULL, SW_SHOWNORMAL);
	HeapFree(GetProcessHeap(), 0, wide);
	return (INT_PTR)r > 32;
}

static int opt_copy_text(void* u, const char* text)
{
	WCHAR* wide = utf8_to_wide(text);
	SIZE_T bytes;
	HGLOBAL mem;
	int ok = 0;
	(void)u;
	if (!wide) return 0;
	bytes = ((SIZE_T)lstrlenW(wide) + 1) * sizeof(WCHAR);
	mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
	if (mem) {
		void* p = GlobalLock(mem);
		if (p) {
			CopyMemory(p, wide, bytes);
			GlobalUnlock(mem);
			if (OpenClipboard(native(g_window))) {
				EmptyClipboard();
				if (SetClipboardData(CF_UNICODETEXT, mem)) ok = 1;
				CloseClipboard();
			}
		}
		if (!ok) GlobalFree(mem);
	}
	HeapFree(GetProcessHeap(), 0, wide);
	return ok;
}

static int opt_check_update(void* u, char* out, size_t cap)
{
	HINSTANCE r;
	(void)u;
	r = ShellExecuteW(NULL, L"open",
		L"https://github.com/horsicq/CDiE/releases",
		NULL, NULL, SW_SHOWNORMAL);
	if ((INT_PTR)r <= 32) {
		lstrcpynA(out, "Could not open the browser.", (int)cap);
		return 0;
	}
	lstrcpynA(out, "Opened the releases page in your browser.", (int)cap);
	return 1;
}

static void do_options_window(int page)
{
	OptionsHost host;
	ZeroMemory(&host, sizeof(host));
	host.get_scan = opt_get_scan;
	host.set_scan = opt_set_scan;
	host.get_optimization = opt_get_opt;
	host.set_optimization = opt_set_opt;
	host.context_available = CDIE_SHOW_CONTEXT;
	host.updates_available = CDIE_SHOW_UPDATES;
	host.browse_folder = opt_browse_folder;
	host.db_changed = opt_db_changed;
	host.get_fonts = opt_get_fonts;
	host.set_fonts = opt_set_fonts;
	host.list_fonts = opt_list_fonts;
	host.context_get = opt_context_get;
	host.context_set = opt_context_set;
	host.get_version = opt_get_version;
	host.check_update = opt_check_update;
	host.open_url = opt_open_url;
	host.copy_text = opt_copy_text;
	options_show(g_app, g_window, &host, page);
}

static void do_about(void)
{
	do_options_window(OPTIONS_PAGE_ABOUT);
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
	if (g_bottombar)
		pl_widget_set_rect(bottombar_widget(g_bottombar),
			cx, H - BOTTOMBAR_H, cw, BOTTOMBAR_H);

	{
		int tx = cx + CONTENT_PAD, tw = cw - 2 * CONTENT_PAD;
		int ty = cy + 4, th = H - cy - BOTTOMBAR_H - 8;
		if (tw < 40) tw = 40;
		if (th < 40) th = 40;
		if (g_tree)
			pl_widget_set_rect(resultstree_widget(g_tree), tx, ty, tw, th);
	}

#define HIDE(w) if (w) MoveWindow(native(w), -9999, 0, 100, 26, FALSE)
	HIDE(g_path);  HIDE(g_msec);  HIDE(g_status);  HIDE(g_reportButton);
	HIDE(g_scan);  HIDE(g_type);  HIDE(g_flags);   HIDE(g_databases);
	HIDE(g_typeLabel); HIDE(g_flagLabel); HIDE(g_databaseLabel);
	HIDE(g_results);
#undef HIDE

	GetWindowRect(native(g_window), &window_bounds);
	g_minWindowWidth = SIDEBAR_W + 520 +
		(window_bounds.right - window_bounds.left - W);
	if (g_minWindowWidth < 900) g_minWindowWidth = 900;
	g_minWindowHeight = HEADER_H + INFOBAR_H + TABBAR_H + BOTTOMBAR_H + 200 +
		(window_bounds.bottom - window_bounds.top - H);
	if (g_minWindowHeight < 540) g_minWindowHeight = 540;
}

static LRESULT CALLBACK path_subclass(HWND handle, UINT message,
	WPARAM wp, LPARAM lp,
	UINT_PTR subclass_id, DWORD_PTR reference)
{
	(void)subclass_id; (void)reference;
	if (message == WM_GETDLGCODE) {
		MSG* key = (MSG*)lp;
		LRESULT code = DefSubclassProc(handle, message, wp, lp);
		if (key && key->wParam == VK_RETURN) code |= DLGC_WANTMESSAGE;
		return code;
	}
	if (message == WM_KEYDOWN && wp == VK_RETURN) { g_pending = ACTION_SCAN; return 0; }
	if (message == WM_CHAR && wp == VK_RETURN) return 0;
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
		g_pending = ACTION_SIGNATURES;
		break;

	case NAV_STRINGS:
		g_pending = ACTION_STRINGS;
		break;

	default:
		break;
	}
}

static void mon_request_scan(const char* path, void* u)
{
	(void)u;
	xxwidgets_widget_set_text(g_path, path);
	g_pending = ACTION_SCAN;
}

static void mon_set_status(const char* text, void* u)
{
	(void)u;
	xxwidgets_widget_set_text(g_status, text);
}

static void mon_restore_flags(void* u)
{
	(void)u;
	apply_flags_to_backend();
}

static LRESULT CALLBACK window_subclass(HWND handle, UINT message,
	WPARAM wp, LPARAM lp,
	UINT_PTR subclass_id, DWORD_PTR reference)
{
	LRESULT tray_result;
	(void)subclass_id; (void)reference;
	if (tray_handle_message(handle, message, wp, lp, &tray_result))
		return tray_result;
	if (dlwatch_handle_message(handle, message, wp, lp, &tray_result))
		return tray_result;
	switch (message) {
	case WM_GETMINMAXINFO: {
		MINMAXINFO* info = (MINMAXINFO*)lp;
		info->ptMinTrackSize.x = g_minWindowWidth;
		info->ptMinTrackSize.y = g_minWindowHeight;
		return 0;
	}

	case WM_ERASEBKGND: {
		HDC dc = (HDC)wp;
		RECT r; GetClientRect(handle, &r);
		th_fill(dc, r.left, r.top, r.right, r.bottom, CLR_BG);
		return 1;
	}

	case WM_DROPFILES: {
		HDROP drop = (HDROP)wp;
		UINT  length = DragQueryFileW(drop, 0, NULL, 0);
		WCHAR* wide = (WCHAR*)HeapAlloc(GetProcessHeap(), 0,
			((SIZE_T)length + 1) * sizeof(WCHAR));
		if (wide && DragQueryFileW(drop, 0, wide, length + 1)) {
			char* path = wide_to_utf8(wide);
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

static void on_event(xxwidgets_app* app, const xxwidgets_event* event,
	void* user_data)
{
	(void)user_data;
	if (event->type == XXWIDGETS_EVENT_CLOSE && event->widget == g_window) {
		g_running = 0;
		xxwidgets_app_quit(app, 0);
	}
	else if (event->type == XXWIDGETS_EVENT_RESIZE && event->widget == g_window) {
		layout();
	}
	else if (event->type == XXWIDGETS_EVENT_CLICK) {
		if (event->widget == g_scan)              g_pending = ACTION_SCAN;
		else if (event->widget == g_reportButton) g_pending = ACTION_REPORT;
	}
	else if (event->type == XXWIDGETS_EVENT_CHANGE &&
		event->widget == g_path && g_type) {
		xxwidgets_widget_set_enabled(g_type, 0);
	}
	else if (event->type == XXWIDGETS_EVENT_SELECT && event->widget == g_type) {
		SIZE_T required = 0;
		if (xxwidgets_widget_get_text(g_path, NULL, 0, &required) == XXWIDGETS_OK
			&& required > 1)
			g_pending = ACTION_SCAN;
	}
	else if (event->type == XXWIDGETS_EVENT_SHORTCUT) {
		g_pending = event->value;
	}
}

static int create_control(xxwidgets_widget** control, xxwidgets_kind kind,
	const char* text, int id)
{
	xxwidgets_rect bounds = { 1, 1, 12, 1 };
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
	xxwidgets_rect panel_bounds = { 1, 4, 96, 18 };
	xxwidgets_shortcut shortcuts[] = {
			{ "F5",          ACTION_SCAN },
			{ "Ctrl+O",      ACTION_BROWSE },
			{ "F1",          ACTION_ABOUT },
			{ "Ctrl+Shift+D",ACTION_DATABASE_PATHS },
			{ "Ctrl+M",      ACTION_MONITOR }
	};

	if (!create_control(&g_path, XXWIDGETS_EDIT, "", IDC_EDIT_PATH) ||
		!create_control(&g_msec, XXWIDGETS_LABEL, "", IDC_STATIC_MSEC) ||
		!create_control(&g_status, XXWIDGETS_LABEL, "", IDC_STATUS))
		return 0;

	if (xxwidgets_scan_panel_create(g_window, panel_bounds, &g_panel) != XXWIDGETS_OK)
		return 0;

	g_type = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_FILE_TYPE);
	g_typeLabel = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_FILE_TYPE_LABEL);
	g_flags = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_FLAGS);
	g_databases = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_DATABASES);
	g_scan = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_SCAN);
	g_reportButton = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_REPORT);
	g_results = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_RESULTS);
	g_flagLabel = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_FLAGS_LABEL);
	g_databaseLabel = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_DATABASES_LABEL);

	if (!g_type || !g_typeLabel || !g_flags || !g_databases || !g_scan ||
		!g_reportButton || !g_results || !g_flagLabel || !g_databaseLabel ||
		sync_panel_options() != XXWIDGETS_OK)
		return 0;

	SetWindowLongPtrW(native(g_type), GWLP_ID, IDC_COMBO_TYPE);
	SetWindowLongPtrW(native(g_flags), GWLP_ID, IDC_COMBO_FLAGS);
	SetWindowLongPtrW(native(g_databases), GWLP_ID, IDC_COMBO_DATABASES);
	SetWindowLongPtrW(native(g_scan), GWLP_ID, IDC_BTN_SCAN);
	SetWindowLongPtrW(native(g_reportButton), GWLP_ID, IDC_BTN_REPORT);
	SetWindowLongPtrW(native(g_results), GWLP_ID, IDC_SCANRESULTS);

	xxwidgets_widget_set_text(g_databaseLabel, "Databases (Main always)");

	if (!SetWindowSubclass(native(g_path), path_subclass, 1, 0) ||
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
	g_bottombar = bottombar_create(native(g_window), SIDEBAR_W, 500, 800, BOTTOMBAR_H, NULL, NULL);
	g_tree = resultstree_create(native(g_window), SIDEBAR_W, 0, 100, 100);
	SetWindowSubclass(native(g_status), status_subclass, 3, 0);

	if (!g_sidebar || !g_header_wnd || !g_infobar_wnd || !g_bottombar || !g_tree) {
		WCHAR msg[200];
		wsprintfW(msg,
			L"Chrome creation failed (1 = ok):\nsidebar=%d header=%d infobar=%d "
			L"bottombar=%d tree=%d\nGetLastError=%lu",
			g_sidebar != NULL, g_header_wnd != NULL, g_infobar_wnd != NULL,
			g_bottombar != NULL, g_tree != NULL, GetLastError());
		MessageBoxW(NULL, msg, L"cdie", MB_ICONERROR);
		return 0;
	}
	return 1;
}

static int set_startup_file(void)
{
	int argc = 0, has_file = 0;
	WCHAR** arguments = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (arguments && argc > 1) {
		char* path = wide_to_utf8(arguments[1]);
		if (path) {
			has_file = xxwidgets_widget_set_text(g_path, path) == XXWIDGETS_OK;
			HeapFree(GetProcessHeap(), 0, path);
		}
	}
	if (arguments) LocalFree(arguments);
	return has_file;
}

static void fill_prefs(CdieConfig* cfg)
{
	OptionsOptimization opt;
	OptionsFont fonts[OPTIONS_FONT_ROLES];
	int k;

	cfg->has_prefs = 1;
	cfg->deep = g_bFlagDeep;           cfg->heuristic = g_bFlagHeur;
	cfg->verbose = g_bFlagVerbose;     cfg->aggressive = g_bFlagAggr;
	cfg->hide_unknown = g_bFlagHideUnknown; cfg->format = g_bFlagFormat;
	cfg->db_extra = g_bDbExtra;        cfg->db_custom = g_bDbCustom;
	cfg->monitor_downloads = dlwatch_enabled();

	if (opt_get_opt(NULL, &opt)) {
		cfg->use_sse2 = opt.use_sse2;
		cfg->use_avx2 = opt.use_avx2;
		cfg->buffer_size = (unsigned long long)opt.buffer_size;
		cfg->file_buffer_size = (unsigned long long)opt.file_buffer_size;
	}

	if (opt_get_fonts(NULL, fonts)) {
		for (k = 0; k < OPTIONS_FONT_ROLES && k < CDIE_FONT_ROLES; ++k) {
			snprintf(cfg->font_family[k], sizeof(cfg->font_family[k]), "%s", fonts[k].family);
			cfg->font_size[k] = fonts[k].point_size;
			cfg->font_bold[k] = fonts[k].bold;
			cfg->font_italic[k] = fonts[k].italic;
		}
	}
}

static int init_backend_with_config(void)
{
	CdieConfig cfg;
	int ok = gui_backend_init();
	int have_file = config_load(&cfg);

	if (have_file && cfg.main_db[0]) {
		if (gui_backend_set_db_paths(cfg.main_db, cfg.extra_db, cfg.custom_db))
			ok = 1;            /* saved folders worked, even if the default failed */
		return ok;
	}

	/* Program Files / Store: never use the shipped folder, the user must choose one */
	if (!config_is_portable()) return 0;

	if (!have_file) {
		/* Portable first run: create the file with the paths the engine is using now */
		const char* m = gui_backend_main_db_path();
		const char* e = gui_backend_extra_db_path();
		const char* c = gui_backend_custom_db_path();
		memset(&cfg, 0, sizeof(cfg));
		snprintf(cfg.main_db, sizeof(cfg.main_db), "%s", m ? m : "");
		snprintf(cfg.extra_db, sizeof(cfg.extra_db), "%s", e ? e : "");
		snprintf(cfg.custom_db, sizeof(cfg.custom_db), "%s", c ? c : "");
		fill_prefs(&cfg);
		config_save(&cfg);
	}
	return ok;
}

static void save_config_on_exit(void)
{
	CdieConfig cfg;
	const char* m;
	const char* e;
	const char* c;
	if (config_db_required()) return;
	config_load(&cfg);                 /* keep saved preferences */
	m = gui_backend_main_db_path();
	e = gui_backend_extra_db_path();
	c = gui_backend_custom_db_path();
	snprintf(cfg.main_db, sizeof(cfg.main_db), "%s", m ? m : "");
	snprintf(cfg.extra_db, sizeof(cfg.extra_db), "%s", e ? e : "");
	snprintf(cfg.custom_db, sizeof(cfg.custom_db), "%s", c ? c : "");
	if (g_prefsReady) fill_prefs(&cfg);
	config_save(&cfg);
}

static void apply_saved_prefs(void)
{
	CdieConfig cfg;
	OptionsOptimization opt;
	OptionsFont fonts[OPTIONS_FONT_ROLES];
	int k;

	if (!config_load(&cfg) || !cfg.has_prefs) return;

	g_bFlagDeep = cfg.deep;           g_bFlagHeur = cfg.heuristic;
	g_bFlagVerbose = cfg.verbose;     g_bFlagAggr = cfg.aggressive;
	g_bFlagHideUnknown = cfg.hide_unknown; g_bFlagFormat = cfg.format;
	g_bDbExtra = cfg.db_extra;        g_bDbCustom = cfg.db_custom;
	g_startMonitor = cfg.monitor_downloads;
	apply_flags_to_backend();
	sync_panel_options();

	if (opt_get_opt(NULL, &opt)) {     /* fills has_sse2 / has_avx2 */
		opt.buffer_size = (size_t)cfg.buffer_size;
		opt.file_buffer_size = (size_t)cfg.file_buffer_size;
		opt.use_sse2 = cfg.use_sse2 && opt.has_sse2;
		opt.use_avx2 = cfg.use_avx2 && opt.has_avx2;
		opt_set_opt(NULL, &opt);
	}

	if (opt_get_fonts(NULL, fonts)) {
		for (k = 0; k < OPTIONS_FONT_ROLES && k < CDIE_FONT_ROLES; ++k) {
			snprintf(fonts[k].family, sizeof(fonts[k].family), "%s", cfg.font_family[k]);
			fonts[k].point_size = cfg.font_size[k];
			fonts[k].bold = cfg.font_bold[k];
			fonts[k].italic = cfg.font_italic[k];
		}
		opt_set_fonts(NULL, fonts);
	}
}

int gui_run(void)
{
	xxwidgets_config config = { XXWIDGETS_BACKEND_NATIVE, on_event, NULL };
	xxwidgets_rect   bounds = { 8, 4, 118, 40 };
	xxwidgets_status status;
	int   result = 0, db_ok = 0;
	WCHAR title[160], ready[512];
	WCHAR* base;
	char* utf8_title;

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
	if (!create_controls()) {
		MessageBoxW(NULL, L"create_controls failed", L"cdie", MB_ICONERROR);
		result = 1; goto finish;
	}

	tray_init(on_tray_exit, NULL);
	tray_win32_attach(native(g_window));
	{
		DlWatchHost mh;
		memset(&mh, 0, sizeof(mh));          /* ZeroMemory is Windows-only */
		mh.native = native(g_window);        /* was mh.hwnd; HWND on Windows, ignored elsewhere */
		mh.request_scan = mon_request_scan;
		mh.set_status = mon_set_status;
		mh.restore_flags = mon_restore_flags;
		dlwatch_init(&mh);
	}

	tray_set_callbacks(dlwatch_balloon_click, NULL, dlwatch_menu_toggle, NULL);
	SetWindowPos(native(g_window), NULL, 0, 0, 1150, 700, SWP_NOMOVE | SWP_NOZORDER);
	db_ok = init_backend_with_config();
	apply_saved_prefs();          /* before the status text, since set_fonts writes one */
	g_prefsReady = 1;
	if (db_ok) {
		wsprintfW(ready, L"Ready. %d signatures loaded. Choose a file or drag one here, then press Scan.",
			gui_backend_signature_count());
		set_status_wide(ready);
	}
	else {
		xxwidgets_widget_set_text(g_status,
			"Main signature database not found. "
			"Select the database folder in the window that opens.");
	}
	apply_flags_to_backend();
	layout();

	xxwidgets_widget_set_visible(g_window, 1);
	xxwidgets_widget_focus(g_path);
	if (set_startup_file()) do_scan();
	if (g_startMonitor) dlwatch_set_enabled(1);
	if (!db_ok) g_pending = ACTION_DATABASE_PATHS;

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
		case ACTION_OPTIONS:        do_options_window(OPTIONS_PAGE_SCAN); break;
		case ACTION_ABOUT:          do_about();          break;
		case ACTION_MONITOR:        dlwatch_set_enabled(!dlwatch_enabled()); break;
		case ACTION_DATABASE_PATHS: do_options_window(OPTIONS_PAGE_DATABASES); break;
		case ACTION_SIGNATURES:     signatures_show(g_app, g_window); break;
		case ACTION_STRINGS:
			if (g_lastPath) strings_show(g_app, g_window, g_lastPath);
			else xxwidgets_widget_set_text(g_status, "Scan a file first to view its strings.");
			break;
		case ACTION_REPORT:
			status = xxwidgets_scan_panel_show_report(g_panel);
			if (status != XXWIDGETS_OK)
				report_ui_error("Open scan report", status);
			break;
		default: break;
		}
	}

finish:
	dlwatch_shutdown();
	tray_remove();
	save_config_on_exit();
	gui_backend_shutdown();
	if (g_panel && xxwidgets_scan_panel_destroy(g_panel) != XXWIDGETS_OK) result = 1;
	g_panel = NULL;
	if (g_typePath) HeapFree(GetProcessHeap(), 0, g_typePath);
	g_typePath = NULL;
	if (xxwidgets_app_destroy(g_app) != XXWIDGETS_OK) result = 1;
	g_app = NULL;
	if (g_lastPath) HeapFree(GetProcessHeap(), 0, g_lastPath);
	g_lastPath = NULL;
	return result;
}