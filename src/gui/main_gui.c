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
 * String storage uses xxfclib; other scratch storage uses the process heap.
 * xxwidgets supplies the hosted GUI
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
#include <xxfclib/strings/xx_string.h>
#include <xxwidgets/xxwidgets.h>
#include <xxwidgets/xxwidgets_scan_panel.h>
#include <xxwidgets/xxwidgets_combobox.h>
#include <xxwidgets/xxwidgets_scan_options.h>
#include <xxwidgets/xxwidgets_font_options.h>
#include <xxwidgets/xxwidgets_context_options.h>
#include <xxwidgets/xxwidgets_optimization_options.h>
#include <xxwidgets/xxwidgets_options_pages.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "gui_backend.h"
#include "app/cdie_translation.h"
#include "gui_scan_options.h"
#include "gui_tree.h"
#include "gui_file_information.h"
#include "gui_entropy.h"
#include "gui_hash.h"
#include "gui_strings.h"
#include "gui_hex_view.h"
#include "gui_about.h"
#include "gui_options.h"
#include "gui_settings.h"
#include <xxfclib/rt/xx_rt.h>
#include "gui_disasm.h"
#include "gui_memory_map.h"
#include "gui_imports.h"
#include "gui_exports.h"
#include "gui_resources.h"
#include "gui_symbols.h"
#include "resource.h"

#if defined(_M_ARM64) || defined(_M_ARM64EC) || defined(_M_ARM) || defined(__aarch64__) || defined(__arm__)
#define CDIE_GUI_ARM_TARGET 1
#else
#define CDIE_GUI_ARM_TARGET 0
#endif

#define IDC_EDIT_PATH   102
#define IDC_BTN_BROWSE  103
#define IDC_COMBO_TYPE  104
#define IDC_COMBO_FLAGS 105
#define IDC_COMBO_DATABASES 106
#define IDC_STATIC_MSEC 107
#define IDC_BTN_SCAN    108
#define IDC_SCANRESULTS 109
#define IDC_BTN_EXIT    110
#define IDC_BTN_OPTIONS 111
#define IDC_BTN_ABOUT   112
#define IDC_BTN_REPORT  113
#define IDC_STATUS      114
#define IDC_COMBO_INCLUDE_TAGS 119
#define IDC_COMBO_EXCLUDE_TAGS 120
#define IDC_BTN_FILE_INFORMATION 121
#define IDC_BTN_ENTROPY 122
#define IDC_BTN_HASH 123
#define IDC_BTN_STRINGS 124
#define IDC_BTN_HEX_VIEW 125
#define IDC_BTN_DISASM 126
#define IDC_BTN_MEMORY_MAP 127
#define IDC_BTN_IMPORTS 128
#define IDC_BTN_EXPORTS 129
#define IDC_BTN_RESOURCES 130
#define IDC_BTN_SYMBOLS 131

enum gui_action { ACTION_NONE, ACTION_SCAN, ACTION_BROWSE,
                  ACTION_OPTIONS, ACTION_ABOUT, ACTION_REPORT,
                  ACTION_DATABASE_PATHS, ACTION_FILE_INFORMATION, ACTION_ENTROPY, ACTION_HASH, ACTION_STRINGS, ACTION_HEX_VIEW, ACTION_DISASM, ACTION_MEMORY_MAP, ACTION_IMPORTS, ACTION_EXPORTS, ACTION_RESOURCES, ACTION_SYMBOLS };

static HINSTANCE g_hInst;
static xxwidgets_app *g_app;
static xxwidgets_widget *g_window, *g_fileLabel, *g_path, *g_browse, *g_type;
static xxwidgets_widget *g_flags, *g_databases, *g_msec, *g_scan, *g_results;
static xxwidgets_widget *g_typeLabel, *g_flagLabel, *g_databaseLabel;
static xxwidgets_widget *g_includeTags, *g_excludeTags, *g_includeTagsLabel, *g_excludeTagsLabel;
/* DIE's panel: Signatures, Directory and Log buttons, the file's facts. */
static xxwidgets_widget *g_signaturesButton, *g_directoryButton, *g_logButton, *g_facts[4], *g_factLabels[4];
static xxwidgets_widget *g_options, *g_aboutButton, *g_reportButton, *g_exit, *g_status;
static xxwidgets_widget *g_fileInformationButton, *g_entropyButton, *g_hashButton, *g_stringsButton, *g_hexViewButton, *g_disasmButton, *g_memoryMapButton, *g_importsButton, *g_exportsButton, *g_resourcesButton, *g_symbolsButton;
static xxwidgets_scan_panel *g_panel;
static xxwidgets_about_dialog *g_about;
static int g_running, g_pending;
static char *g_typePath;
static int g_minWindowWidth = 720, g_minWindowHeight = 380;

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

static void set_status_wide(const WCHAR *wide)
{
    char *text = xx_str_unicode_to_utf8(wide);
    if (text) {
        xxwidgets_widget_set_text(g_status, CDIE_TR(text));
        xx_str_free(text);
    }
}

static void report_ui_error(const char *operation, xxwidgets_status status)
{
    WCHAR text[256];
    WCHAR *name = xx_str_utf8_to_unicode(CDIE_TR(operation));
    WCHAR *detail = xx_str_utf8_to_unicode(xxwidgets_status_string(status));
    cdie_translation_swprintf(text, sizeof(text)/sizeof(text[0]), CDIE_N_(L"%s: %s"), name ? name : L"Interface error", detail ? detail : L"unknown error");
    set_status_wide(text);
    if (name) xx_str_wfree(name);
    if (detail) xx_str_wfree(detail);
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

static xxwidgets_status refresh_panel_tags(void)
{
    char *pTags = gui_backend_used_tags();
    xxwidgets_status status;
    if (pTags == NULL) return XXWIDGETS_OUT_OF_MEMORY;
    status = xxwidgets_scan_panel_set_available_tags(g_panel, pTags);
    gui_backend_free(pTags);
    return status;
}

static xxwidgets_status apply_panel_tag_filters(void)
{
    SIZE_T nInclude = 0, nExclude = 0;
    char *pInclude = NULL, *pExclude = NULL;
    xxwidgets_status status = xxwidgets_scan_panel_get_include_tags(g_panel, NULL, 0, &nInclude);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_scan_panel_get_exclude_tags(g_panel, NULL, 0, &nExclude);
    if (status != XXWIDGETS_OK) return status;

    pInclude = (char *)HeapAlloc(GetProcessHeap(), 0, nInclude ? nInclude : 1);
    pExclude = (char *)HeapAlloc(GetProcessHeap(), 0, nExclude ? nExclude : 1);
    if (!pInclude || !pExclude) {
        if (pInclude) HeapFree(GetProcessHeap(), 0, pInclude);
        if (pExclude) HeapFree(GetProcessHeap(), 0, pExclude);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    status = xxwidgets_scan_panel_get_include_tags(g_panel, pInclude, nInclude ? nInclude : 1, NULL);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_scan_panel_get_exclude_tags(g_panel, pExclude, nExclude ? nExclude : 1, NULL);
    if (status == XXWIDGETS_OK && !gui_backend_set_tag_filters(pInclude, pExclude))
        status = XXWIDGETS_OUT_OF_MEMORY;
    HeapFree(GetProcessHeap(), 0, pInclude);
    HeapFree(GetProcessHeap(), 0, pExclude);
    return status;
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
    if (status == XXWIDGETS_OK)
        status = apply_panel_tag_filters();
    if (status != XXWIDGETS_OK) return status;
    g_bFlagDeep = (flags & 1u) != 0; g_bFlagHeur = (flags & 2u) != 0;
    g_bFlagVerbose = (flags & 4u) != 0; g_bFlagAggr = (flags & 8u) != 0;
    g_bFlagHideUnknown = (flags & 16u) != 0; g_bFlagFormat = (flags & 32u) != 0;
    g_bDbExtra = (databases & 1u) != 0; g_bDbCustom = (databases & 2u) != 0;
    apply_flags_to_backend();
    return XXWIDGETS_OK;
}

static void clear_scan(void)
{
    xxwidgets_scan_panel_clear(g_panel);
    xxwidgets_widget_set_text(g_msec, "");
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
        xx_str_wfree(buffer->records[i].meta_string->data);
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
        if (capacity < buffer->capacity || capacity > ((SIZE_T)-1) / sizeof(*records)) {
            buffer->failed = 1; return;
        }
        records = buffer->records
            ? (xx_meta_string *)HeapReAlloc(GetProcessHeap(), 0, buffer->records, capacity * sizeof(*records))
            : (xx_meta_string *)HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*records));
        if (!records) { buffer->failed = 1; return; }
        buffer->records = records; buffer->capacity = capacity;
    }
    wide = xx_str_utf8_to_unicode(type_id == 0 ? CDIE_TR("Automatic") : name);
    record.meta_string = (xx_str_w_s *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*record.meta_string));
    if (!wide || !record.meta_string) {
        if (wide) xx_str_wfree(wide);
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
    new_path = xx_str_create(path);
    if (!new_path) { free_file_types(&buffer); return XXWIDGETS_OUT_OF_MEMORY; }
    for (i = 0; i < buffer.count; ++i)
        if (buffer.records[i].var.val.u64 == (uint64_t)type_id) { selected = (int)i; break; }
    status = xxwidgets_combobox_set_records(g_type, buffer.records, buffer.count);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(g_type, selected);
    free_file_types(&buffer);
    if (status != XXWIDGETS_OK) { xx_str_free(new_path); return status; }
    if (g_typePath) xx_str_free(g_typePath);
    g_typePath = new_path;
    return XXWIDGETS_OK;
}

static void do_scan(void)
{
    SIZE_T required = 0;
    char *path, *type = NULL;
    xxwidgets_status status;
    int ok, cancelled = 0, type_id = 0;

    status = xxwidgets_widget_get_text(g_path, NULL, 0, &required);
    if (status != XXWIDGETS_OK) { report_ui_error("Read file name", status); return; }
    if (required <= 1) { MessageBeep(MB_ICONASTERISK); xxwidgets_widget_focus(g_path); return; }
    path = (char *)HeapAlloc(GetProcessHeap(), 0, required);
    if (!path) { report_ui_error("Read file name", XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, required, NULL);
    if (status != XXWIDGETS_OK) {
        HeapFree(GetProcessHeap(), 0, path);
        report_ui_error("Read file name", status);
        return;
    }
    status = read_panel_options();
    if (status != XXWIDGETS_OK) {
        HeapFree(GetProcessHeap(), 0, path);
        report_ui_error("Read scan options", status);
        return;
    }
    status = refresh_file_types(path);
    if (status == XXWIDGETS_OK) status = selected_file_type(&type_id);
    if (status != XXWIDGETS_OK) {
        clear_scan();
        xxwidgets_widget_set_enabled(g_type, 0);
        HeapFree(GetProcessHeap(), 0, path);
        report_ui_error("Load file types", status);
        return;
    }
    clear_scan();
    xxwidgets_widget_set_enabled(g_scan, 0);
    xxwidgets_widget_set_enabled(g_type, 0);
    xxwidgets_widget_set_text(g_msec, CDIE_TR("Scanning..."));
    xxwidgets_widget_set_text(g_status, CDIE_TR("Scanning the selected file..."));
    /* The engine runs on a worker; the process dialog keeps the window
     * responsive, disables it meanwhile, and offers Cancel after a second.
     * The panel shows the time it took. */
    ok = gui_tree_scan(g_window, g_panel, path, type_id, &type, &cancelled);
    if (ok) {
        WCHAR text[512];
        WCHAR *type_name = xx_str_utf8_to_unicode(type && type[0] ? type : "Unknown type");
        size_t count = xxwidgets_scan_panel_count(g_panel);
        cdie_translation_swprintf(text, sizeof(text)/sizeof(text[0]), CDIE_N_(L"Scan complete (%s). %u result%s. Use > for the full engine output."),
                  type_name ? type_name : L"Unknown type", (unsigned int)count, count == 1 ? L"" : L"s");
        set_status_wide(text);
        if (type_name) xx_str_wfree(type_name);
    } else {
        xxwidgets_widget_set_text(g_status, cancelled ? CDIE_TR("Scan cancelled.") : CDIE_TR("Cannot scan the file."));
        xxwidgets_widget_set_text(g_msec, "");
    }
    HeapFree(GetProcessHeap(), 0, path);
    if (type) gui_backend_free(type);
    xxwidgets_widget_set_enabled(g_type, 1);
    xxwidgets_widget_set_enabled(g_scan, 1);
}

static void do_browse(void)
{
    OPENFILENAMEW picker;
    WCHAR file[32768];
    WCHAR *translation_title = xx_str_utf8_to_unicode(CDIE_TR("Select a file to scan"));
    file[0] = 0;
    GetWindowTextW(native(g_path), file, (int)(sizeof(file) / sizeof(file[0])));
    ZeroMemory(&picker, sizeof(picker));
    picker.lStructSize = sizeof(picker);
    picker.hwndOwner = native(g_window);
    picker.lpstrFile = file;
    picker.nMaxFile = (DWORD)(sizeof(file) / sizeof(file[0]));
    picker.lpstrFilter = L"All files\0*.*\0";
    picker.lpstrTitle = translation_title ? translation_title : L"Select a file to scan";
    picker.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER;
    if (GetOpenFileNameW(&picker)) {
        char *path = xx_str_unicode_to_utf8(file);
        if (path) {
            xxwidgets_widget_set_text(g_path, path);
            xx_str_free(path);
            do_scan();
        }
    }
    xx_str_wfree(translation_title);
}

static void do_file_information(void)
{
    size_t size = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &size);
    if (status != XXWIDGETS_OK) { report_ui_error("Read file path", status); return; }
    path = HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
    if (!path) { report_ui_error("Read file path", XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, size ? size : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a PE, ELF or Mach-O file to inspect."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) status = gui_file_information_show(g_window, path);
    HeapFree(GetProcessHeap(), 0, path);
    if (status != XXWIDGETS_OK) report_ui_error("File Information", status);
}

static void do_entropy(void)
{
    size_t size = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &size);
    if (status != XXWIDGETS_OK) { report_ui_error("Read file path", status); return; }
    path = HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
    if (!path) { report_ui_error("Read file path", XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, size ? size : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to calculate entropy."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) status = gui_entropy_show(g_window, path);
    HeapFree(GetProcessHeap(), 0, path);
    if (status != XXWIDGETS_OK) report_ui_error("Entropy", status);
}

static void do_hash(void)
{
    size_t size = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &size);
    if (status != XXWIDGETS_OK) { report_ui_error("Read file path", status); return; }
    path = HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
    if (!path) { report_ui_error("Read file path", XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, size ? size : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to calculate hashes."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) status = gui_hash_show(g_window, path);
    HeapFree(GetProcessHeap(), 0, path);
    if (status != XXWIDGETS_OK) report_ui_error("Hash", status);
}

static void do_strings(void)
{
    size_t size = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &size);
    if (status != XXWIDGETS_OK) { report_ui_error("Read file path", status); return; }
    path = HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
    if (!path) { report_ui_error("Read file path", XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, size ? size : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to extract strings."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) status = gui_strings_show(g_window, path);
    HeapFree(GetProcessHeap(), 0, path);
    if (status != XXWIDGETS_OK) report_ui_error("Strings", status);
}

static void do_hex_view(void)
{
    size_t size = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &size);
    if (status != XXWIDGETS_OK) { report_ui_error("Read file path", status); return; }
    path = HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
    if (!path) { report_ui_error("Read file path", XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, size ? size : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to view its bytes."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) status = gui_hex_view_show(g_window, path);
    HeapFree(GetProcessHeap(), 0, path);
    if (status != XXWIDGETS_OK) report_ui_error("Hex View", status);
}

static void do_disasm(void)
{
    size_t size = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &size);
    if (status != XXWIDGETS_OK) { report_ui_error("Read file path", status); return; }
    path = HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
    if (!path) { report_ui_error("Read file path", XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, size ? size : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to disassemble."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) status = gui_disasm_show(g_window, path);
    HeapFree(GetProcessHeap(), 0, path);
    if (status != XXWIDGETS_OK) report_ui_error("Disasm", status);
}

static void do_memory_map(void)
{
    size_t size = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &size);
    if (status != XXWIDGETS_OK) { report_ui_error("Read file path", status); return; }
    path = HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
    if (!path) { report_ui_error("Read file path", XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, size ? size : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to map."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) status = gui_memory_map_show(g_window, path);
    HeapFree(GetProcessHeap(), 0, path);
    if (status != XXWIDGETS_OK) report_ui_error("Memory Map", status);
}

static void do_imports(void)
{
    size_t size = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &size);
    if (status != XXWIDGETS_OK) { report_ui_error("Read file path", status); return; }
    path = HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
    if (!path) { report_ui_error("Read file path", XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, size ? size : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to inspect imports."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) status = gui_imports_show(g_window, path);
    HeapFree(GetProcessHeap(), 0, path);
    if (status != XXWIDGETS_OK) report_ui_error("Imports", status);
}

static void do_exports(void)
{
    size_t size = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &size);
    if (status != XXWIDGETS_OK) { report_ui_error("Read file path", status); return; }
    path = HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
    if (!path) { report_ui_error("Read file path", XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, size ? size : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to inspect exports."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) status = gui_exports_show(g_window, path);
    HeapFree(GetProcessHeap(), 0, path);
    if (status != XXWIDGETS_OK) report_ui_error("Exports", status);
}

static void do_resources(void)
{
    size_t size = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &size);
    if (status != XXWIDGETS_OK) { report_ui_error("Read file path", status); return; }
    path = HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
    if (!path) { report_ui_error("Read file path", XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, size ? size : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to inspect resources."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) status = gui_resources_show(g_window, path);
    HeapFree(GetProcessHeap(), 0, path);
    if (status != XXWIDGETS_OK) report_ui_error("Resources", status);
}

static void do_symbols(void)
{
    size_t size = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &size);
    if (status != XXWIDGETS_OK) { report_ui_error("Read file path", status); return; }
    path = HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
    if (!path) { report_ui_error("Read file path", XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, size ? size : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to inspect symbols."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) status = gui_symbols_show(g_window, path);
    HeapFree(GetProcessHeap(), 0, path);
    if (status != XXWIDGETS_OK) report_ui_error("Symbols", status);
}

/* All settings live in one Options window (gui_options.c): a page list on
 * the left and the reusable xxwidgets forms on the right. */
static gui_options_page_id g_optionsPage = GUI_OPTIONS_SCAN;
static xx_settings *g_settings;

/* The frontend's share of the Options settings, as currently applied. */
static xxwidgets_status current_options(gui_options_values *values)
{
    values->scan.options.flags = flags_mask();
    values->scan.options.databases = databases_mask();
    values->scan.modes = gui_backend_get_scan_modes();
    gui_hex_view_get_options(&values->hex);
    return xxwidgets_app_get_font_options(g_app, &values->fonts);
}

/* Applies settings from Options or from the store; fonts only when changed. */
static xxwidgets_status apply_options(const gui_options_values *values)
{
    xxwidgets_font_options fonts;
    xxwidgets_status status;
    gui_backend_set_scan_modes(values->scan.modes);
    g_bFlagDeep = (values->scan.options.flags & XXWIDGETS_SCAN_DEEP) != 0;
    g_bFlagHeur = (values->scan.options.flags & XXWIDGETS_SCAN_HEURISTIC) != 0;
    g_bFlagVerbose = (values->scan.options.flags & XXWIDGETS_SCAN_VERBOSE) != 0;
    g_bFlagAggr = (values->scan.options.flags & XXWIDGETS_SCAN_AGGRESSIVE) != 0;
    g_bFlagHideUnknown = (values->scan.options.flags & XXWIDGETS_SCAN_HIDE_UNKNOWN) != 0;
    g_bFlagFormat = (values->scan.options.flags & XXWIDGETS_SCAN_FORMAT) != 0;
    g_bDbExtra = (values->scan.options.databases & XXWIDGETS_SCAN_DATABASE_EXTRA) != 0;
    g_bDbCustom = (values->scan.options.databases & XXWIDGETS_SCAN_DATABASE_CUSTOM) != 0;
    apply_flags_to_backend();
    status = sync_panel_options();
    if (status != XXWIDGETS_OK) return status;
    gui_hex_view_set_options(&values->hex);
    status = xxwidgets_app_get_font_options(g_app, &fonts);
    if (status == XXWIDGETS_OK && memcmp(&values->fonts, &fonts, sizeof(fonts))) {
        RECT bounds;
        int width, height;
        status = xxwidgets_app_set_font_options(g_app, &values->fonts);
        if (status != XXWIDGETS_OK) return status;
        layout();
        GetWindowRect(native(g_window), &bounds);
        width = bounds.right - bounds.left; height = bounds.bottom - bounds.top;
        if (width < g_minWindowWidth || height < g_minWindowHeight)
            SetWindowPos(native(g_window), NULL, 0, 0,
                width < g_minWindowWidth ? g_minWindowWidth : width,
                height < g_minWindowHeight ? g_minWindowHeight : height,
                SWP_NOMOVE | SWP_NOZORDER);
    }
    return status;
}

static xxwidgets_status browse_database(xxwidgets_widget *dialog, const char *initial, char **path, int *accepted, void *user)
{
    BROWSEINFOW picker;
    LPITEMIDLIST item;
    WCHAR folder[MAX_PATH];
    WCHAR *title = xx_str_utf8_to_unicode(CDIE_TR("Select the database folder"));
    xxwidgets_status status = XXWIDGETS_OK;
    (void)initial; (void)user;
    *path = NULL; *accepted = 0;
    ZeroMemory(&picker, sizeof(picker));
    picker.hwndOwner = native(dialog);
    picker.lpszTitle = title ? title : L"Select the database folder";
    picker.ulFlags = BIF_RETURNONLYFSDIRS;
    item = SHBrowseForFolderW(&picker);
    xx_str_wfree(title);
    if (!item) return XXWIDGETS_OK;
    if (SHGetPathFromIDListW(item, folder)) {
        char *utf8 = xx_str_unicode_to_utf8(folder);
        size_t size = utf8 ? strlen(utf8) + 1 : 0;
        /* The form releases the chosen path with free(). */
        *path = utf8 ? (char *)malloc(size) : NULL;
        if (*path) { memcpy(*path, utf8, size); *accepted = 1; }
        else status = XXWIDGETS_OUT_OF_MEMORY;
        if (utf8) xx_str_free(utf8);
    }
    CoTaskMemFree(item);
    return status;
}

static int do_settings(gui_options_page_id start)
{
    gui_options_host host = {0};
    gui_options_values values;
    WCHAR executable[32768];
    DWORD length = GetModuleFileNameW(NULL, executable, (DWORD)(sizeof(executable) / sizeof(executable[0])));
    char *program = NULL;
    xxwidgets_status status;
    int accepted = 0, reloaded = 0;

    status = current_options(&values);
    if (status != XXWIDGETS_OK) { report_ui_error("Read interface fonts", status); return -1; }
    /* The context menu needs the program path; without it the page is left out. */
    if (length && length < sizeof(executable) / sizeof(executable[0])) program = xx_str_unicode_to_utf8(executable);
    host.pages = GUI_OPTIONS_PAGE(GUI_OPTIONS_FILE) | GUI_OPTIONS_PAGE(GUI_OPTIONS_FONTS) | GUI_OPTIONS_PAGE(GUI_OPTIONS_SCAN) |
                 GUI_OPTIONS_PAGE(GUI_OPTIONS_SIGNATURES) | GUI_OPTIONS_PAGE(GUI_OPTIONS_HEX) | GUI_OPTIONS_PAGE(GUI_OPTIONS_OPTIMIZATION);
    host.executable = program;
    host.browse = browse_database;
    g_optionsPage = start;
    status = gui_options_dialog(g_window, &host, &g_optionsPage, &values, &accepted, &reloaded);
    if (program) xx_str_free(program);
    if (status != XXWIDGETS_OK) { report_ui_error("Open Options", status); return -1; }

    if (reloaded) {
        WCHAR text[160];
        clear_scan();
        status = refresh_panel_tags();
        if (status != XXWIDGETS_OK) { report_ui_error("Refresh signature tags", status); return -1; }
        cdie_translation_swprintf(text, sizeof(text)/sizeof(text[0]), CDIE_N_(L"Database reloaded: %d signatures. Choose a file and press Scan."), gui_backend_signature_count());
        set_status_wide(text);
    }
    if (!accepted) return 0;
    status = apply_options(&values);
    if (status != XXWIDGETS_OK) { report_ui_error("Apply options", status); return -1; }
    if (!gui_settings_save(g_settings, &values, g_optionsPage))
        xxwidgets_widget_set_text(g_status, CDIE_TR("Options applied, but they could not be saved for the next start."));
    else if (!reloaded) xxwidgets_widget_set_text(g_status, CDIE_TR("Options applied."));
    return 1;
}

static xxwidgets_status configure_about(void)
{
    return gui_about_create(cdie_about_png, cdie_about_png_size, &g_about);
}

static void do_about(void)
{
    xxwidgets_status status = xxwidgets_about_dialog_show(g_about, g_window);
    if (status != XXWIDGETS_OK) report_ui_error("Open About", status);
}

/* The native backend intentionally uses text-cell rectangles. Pixel placement
 * here allows the scan panel's result tree to fill
 * the available client area, including sizes between whole text cells. */
static int control_text_width(xxwidgets_widget *widget, int minimum)
{
    WCHAR text[128];
    SIZE size = {0};
    HDC dc = GetDC(native(widget));
    if (dc) {
        HFONT font = (HFONT)SendMessageW(native(widget), WM_GETFONT, 0, 0);
        HGDIOBJ old = font ? SelectObject(dc, font) : NULL;
        int length = GetWindowTextW(native(widget), text, 128);
        GetTextExtentPoint32W(dc, text, length, &size);
        if (old) SelectObject(dc, old);
        ReleaseDC(native(widget), dc);
    }
    return size.cx + 24 > minimum ? size.cx + 24 : minimum;
}

static TEXTMETRICW control_font_metrics(xxwidgets_widget *widget)
{
    TEXTMETRICW metrics = {0};
    HDC dc = GetDC(native(widget));
    if (dc) {
        HFONT font = (HFONT)SendMessageW(native(widget), WM_GETFONT, 0, 0);
        HGDIOBJ old = font ? SelectObject(dc, font) : NULL;
        GetTextMetricsW(dc, &metrics);
        if (old) SelectObject(dc, old);
        ReleaseDC(native(widget), dc);
    }
    return metrics;
}

static int combo_text_width(xxwidgets_widget *widget, int minimum)
{
    HDC dc = GetDC(native(widget));
    size_t i;
    int width = control_text_width(widget, minimum);
    if (dc) {
        HFONT font = (HFONT)SendMessageW(native(widget), WM_GETFONT, 0, 0);
        HGDIOBJ old = font ? SelectObject(dc, font) : NULL;
        for (i = 0; i < xxwidgets_combobox_count(widget); ++i) {
            const xx_meta_string *record;
            SIZE size = {0};
            if (xxwidgets_combobox_get_record(widget, i, &record) == XXWIDGETS_OK &&
                record->meta_string->length <= INT_MAX &&
                GetTextExtentPoint32W(dc, record->meta_string->data, (int)record->meta_string->length, &size) &&
                size.cx + 32 > width) width = size.cx + 32;
        }
        if (old) SelectObject(dc, old);
        ReleaseDC(native(widget), dc);
    }
    return width;
}

static void layout(void)
{
    RECT bounds, window_bounds;
    TEXTMETRICW metrics, edit_metrics;
    int width, height, row = 26, label = 16, i, x, path_y, fact_label_y, fact_y, results_y, results_bottom;
    int row_a, row_b, status_y, tools_y, tools2_y, required_width, status_height = 36, fact_width = 90, type_width;
    int scan_width, browse_width, signatures_width, flags_width, databases_width, tags_width, directory_width, log_width;
    int report_width, elapsed_width = 100, settings_width[5], tool_width[9], exit_width, tags_room;
    xxwidgets_widget *tools[9];
    const int margin = 10, gap = 8, spacing = 6;
    if (!g_results) return;
    tools[0] = g_aboutButton; tools[1] = g_fileInformationButton; tools[2] = g_entropyButton; tools[3] = g_hashButton;
    tools[4] = g_stringsButton; tools[5] = g_hexViewButton; tools[6] = g_disasmButton; tools[7] = g_memoryMapButton; tools[8] = NULL;
    metrics = control_font_metrics(g_scan);
    edit_metrics = control_font_metrics(g_path);
    if (metrics.tmHeight + metrics.tmExternalLeading + 10 > row)
        row = metrics.tmHeight + metrics.tmExternalLeading + 10;
    if (edit_metrics.tmHeight + edit_metrics.tmExternalLeading + 10 > row)
        row = edit_metrics.tmHeight + edit_metrics.tmExternalLeading + 10;
    if (metrics.tmHeight + metrics.tmExternalLeading + 2 > label)
        label = metrics.tmHeight + metrics.tmExternalLeading + 2;
    if (2 * label > status_height) status_height = 2 * label;
    scan_width = control_text_width(g_scan, 110);
    browse_width = control_text_width(g_browse, 44);
    signatures_width = control_text_width(g_signaturesButton, 92);
    directory_width = control_text_width(g_directoryButton, 92);
    log_width = control_text_width(g_logButton, 70);
    report_width = control_text_width(g_reportButton, 36);
    flags_width = combo_text_width(g_flags, 130);
    databases_width = combo_text_width(g_databases, 130);
    exit_width = control_text_width(g_exit, 92);
    for (i = 0; i < 4; ++i) {
        int needed = control_text_width(g_factLabels[i], 60) + 16;
        if (needed > fact_width) fact_width = needed;
    }
    settings_width[0] = control_text_width(g_options, 110);
    settings_width[1] = control_text_width(g_importsButton, 90);
    settings_width[2] = control_text_width(g_exportsButton, 90);
    settings_width[3] = control_text_width(g_resourcesButton, 110);
    settings_width[4] = control_text_width(g_symbolsButton, 90);
    for (i = 0; tools[i]; ++i) tool_width[i] = control_text_width(tools[i], i == 1 ? 150 : 90);
    /* The widest of: the tool rows, DIE's top row, the two button rows. */
    required_width = 2 * margin + exit_width + gap;
    for (i = 0; tools[i]; ++i) required_width += tool_width[i] + spacing;
    x = 2 * margin + settings_width[0] + settings_width[1] + settings_width[2] + settings_width[3] + settings_width[4] + 4 * spacing;
    if (x > required_width) required_width = x;
    x = 2 * margin + 190 + 4 * (fact_width + spacing);
    if (x > required_width) required_width = x;
    x = 2 * margin + signatures_width + flags_width + databases_width + 2 * 120 + 5 * spacing + gap + scan_width;
    if (x > required_width) required_width = x;
    x = 2 * margin + directory_width + log_width + report_width + elapsed_width + 3 * spacing + 2 * gap + scan_width;
    if (x > required_width) required_width = x;
    if (30 * edit_metrics.tmAveCharWidth + browse_width + gap + 2 * margin > required_width)
        required_width = 30 * edit_metrics.tmAveCharWidth + browse_width + gap + 2 * margin;
    path_y = margin + label + 2;
    fact_label_y = path_y + row + gap;
    fact_y = fact_label_y + label + 2;
    results_y = fact_y + row + gap;
    GetClientRect(native(g_window), &bounds);
    width = bounds.right; height = bounds.bottom;
    GetWindowRect(native(g_window), &window_bounds);
    g_minWindowWidth = required_width + (window_bounds.right - window_bounds.left - width);
    if (g_minWindowWidth < 720) g_minWindowWidth = 720;
    g_minWindowHeight = results_y + 60 + 4 * row + status_height + 5 * gap + 4 + margin +
        (window_bounds.bottom - window_bounds.top - height);
    if (g_minWindowHeight < 420) g_minWindowHeight = 420;
    /* From the bottom: two tool rows, the status, DIE's two button rows. */
    tools2_y = height - margin - row;
    tools_y = tools2_y - gap - row;
    status_y = tools_y - gap - status_height;
    row_b = status_y - gap - row;
    row_a = row_b - 4 - row;
    results_bottom = row_a - gap;
    if (results_bottom < results_y + 40) results_bottom = results_y + 40;
    MoveWindow(native(g_fileLabel), margin, margin, width - 2 * margin, label, TRUE);
    MoveWindow(native(g_path), margin, path_y, width - 2 * margin - browse_width - gap, row, TRUE);
    MoveWindow(native(g_browse), width - margin - browse_width, path_y, browse_width, row, TRUE);
    /* Top row: Scan (the file type) and the file's Endianness, Mode, Architecture, Type. */
    type_width = width - 2 * margin - 4 * (fact_width + spacing);
    MoveWindow(native(g_typeLabel), margin, fact_label_y, type_width, label, TRUE);
    /* ComboBox height includes its dropdown list. */
    SendMessageW(native(g_type), CB_SETITEMHEIGHT, (WPARAM)-1, row - 6);
    SendMessageW(native(g_type), CB_SETITEMHEIGHT, 0, row - 6);
    MoveWindow(native(g_type), margin, fact_y, type_width, row + 10 * row, TRUE);
    for (i = 0; i < 4; ++i) {
        x = margin + type_width + spacing + i * (fact_width + spacing);
        MoveWindow(native(g_factLabels[i]), x, fact_label_y, fact_width, label, TRUE);
        MoveWindow(native(g_facts[i]), x, fact_y, fact_width, row, TRUE);
    }
    MoveWindow(native(g_results), margin, results_y, width - 2 * margin, results_bottom - results_y, TRUE);
    /* Signatures, Flags, Database, the tag filters | a tall Scan button. */
    MoveWindow(native(g_scan), width - margin - scan_width, row_a, scan_width, row_b + row - row_a, TRUE);
    tags_room = width - 2 * margin - scan_width - gap - signatures_width - flags_width - databases_width - 4 * spacing;
    tags_width = tags_room / 2 > 250 ? 250 : tags_room / 2;
    if (tags_width < 60) tags_width = 60;
    x = margin;
    MoveWindow(native(g_signaturesButton), x, row_a, signatures_width, row, TRUE);
    x += signatures_width + spacing;
    MoveWindow(native(g_flags), x, row_a, flags_width, row, TRUE);
    x += flags_width + spacing;
    MoveWindow(native(g_databases), x, row_a, databases_width, row, TRUE);
    x += databases_width + spacing;
    MoveWindow(native(g_includeTags), x, row_a, tags_width, row, TRUE);
    x += tags_width + spacing;
    MoveWindow(native(g_excludeTags), x, row_a, tags_width, row, TRUE);
    /* Directory, Log | the report (">") and the scan time beside Scan. */
    MoveWindow(native(g_directoryButton), margin, row_b, directory_width, row, TRUE);
    MoveWindow(native(g_logButton), margin + directory_width + spacing, row_b, log_width, row, TRUE);
    x = width - margin - scan_width - gap - elapsed_width;
    MoveWindow(native(g_msec), x, row_b, elapsed_width, row, TRUE);
    MoveWindow(native(g_reportButton), x - spacing - report_width, row_b, report_width, row, TRUE);
    MoveWindow(native(g_status), margin, status_y, width - 2 * margin, status_height, TRUE);
    x = margin;
    MoveWindow(native(g_options), x, tools_y, settings_width[0], row, TRUE);
    x += settings_width[0] + spacing;
    MoveWindow(native(g_importsButton), x, tools_y, settings_width[1], row, TRUE);
    x += settings_width[1] + spacing;
    MoveWindow(native(g_exportsButton), x, tools_y, settings_width[2], row, TRUE);
    x += settings_width[2] + spacing;
    MoveWindow(native(g_resourcesButton), x, tools_y, settings_width[3], row, TRUE);
    x += settings_width[3] + spacing;
    MoveWindow(native(g_symbolsButton), x, tools_y, settings_width[4], row, TRUE);
    x = margin;
    for (i = 0; tools[i]; ++i) {
        MoveWindow(native(tools[i]), x, tools2_y, tool_width[i], row, TRUE);
        x += tool_width[i] + spacing;
    }
    MoveWindow(native(g_exit), width - margin - exit_width, tools2_y, exit_width, row, TRUE);
}

static LRESULT CALLBACK path_subclass(HWND handle, UINT message, WPARAM wp, LPARAM lp,
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
    if (message == WM_CHAR && wp == VK_RETURN) return 0;
    if (message == WM_NCDESTROY) RemoveWindowSubclass(handle, path_subclass, 1);
    return DefSubclassProc(handle, message, wp, lp);
}

static LRESULT CALLBACK window_subclass(HWND handle, UINT message, WPARAM wp, LPARAM lp,
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
    case WM_NCDESTROY: RemoveWindowSubclass(handle, window_subclass, 1); break;
    default: break;
    }
    return DefSubclassProc(handle, message, wp, lp);
}

static void on_event(xxwidgets_app *app, const xxwidgets_event *event, void *user_data)
{
    (void)user_data;
    if ((event->type == XXWIDGETS_EVENT_CLOSE && event->widget == g_window) ||
        (event->type == XXWIDGETS_EVENT_CLICK && event->widget == g_exit)) {
        g_running = 0;
        xxwidgets_app_quit(app, 0);
    } else if (event->type == XXWIDGETS_EVENT_RESIZE && event->widget == g_window) layout();
    else if (event->type == XXWIDGETS_EVENT_DROP_FILES && event->widget == g_window && event->path_count) {
        if (xxwidgets_widget_set_text(g_path, event->paths[0]) == XXWIDGETS_OK) g_pending = ACTION_SCAN;
    } else if (gui_tree_event(g_panel, event)) {
        /* S or ? on a result row: shown after polling returns. */
    } else if (event->type == XXWIDGETS_EVENT_CLICK) {
        if (event->widget == g_scan) g_pending = ACTION_SCAN;
        else if (event->widget == g_browse) g_pending = ACTION_BROWSE;
        else if (event->widget == g_options) g_pending = ACTION_OPTIONS;
        else if (event->widget == g_aboutButton) g_pending = ACTION_ABOUT;
        else if (event->widget == g_reportButton) g_pending = ACTION_REPORT;
        else if (event->widget == g_fileInformationButton) g_pending = ACTION_FILE_INFORMATION;
        else if (event->widget == g_entropyButton) g_pending = ACTION_ENTROPY;
        else if (event->widget == g_hashButton) g_pending = ACTION_HASH;
        else if (event->widget == g_stringsButton) g_pending = ACTION_STRINGS;
        else if (event->widget == g_hexViewButton) g_pending = ACTION_HEX_VIEW;
        else if (event->widget == g_disasmButton) g_pending = ACTION_DISASM;
        else if (event->widget == g_memoryMapButton) g_pending = ACTION_MEMORY_MAP;
        else if (event->widget == g_importsButton) g_pending = ACTION_IMPORTS;
        else if (event->widget == g_exportsButton) g_pending = ACTION_EXPORTS;
        else if (event->widget == g_resourcesButton) g_pending = ACTION_RESOURCES;
        else if (event->widget == g_symbolsButton) g_pending = ACTION_SYMBOLS;
    } else if (event->type == XXWIDGETS_EVENT_CHANGE &&
               (event->widget == g_flags || event->widget == g_databases ||
                event->widget == g_includeTags || event->widget == g_excludeTags)) {
        xxwidgets_status status = read_panel_options();
        if (status != XXWIDGETS_OK) report_ui_error("Read scan options", status);
    } else if (event->type == XXWIDGETS_EVENT_CHANGE && event->widget == g_path && g_type) {
        /* These choices describe the last scanned file. Refresh them before
         * allowing a type to be selected for a newly entered path. */
        xxwidgets_widget_set_enabled(g_type, 0);
    } else if (event->type == XXWIDGETS_EVENT_SELECT && event->widget == g_type) {
        SIZE_T required = 0;
        if (xxwidgets_widget_get_text(g_path, NULL, 0, &required) == XXWIDGETS_OK && required > 1)
            g_pending = ACTION_SCAN;
    } else if (event->type == XXWIDGETS_EVENT_SHORTCUT) g_pending = event->value;
}

static int create_control(xxwidgets_widget **control, xxwidgets_kind kind, const char *text, int id)
{
    xxwidgets_rect bounds = {1, 1, 12, 1};
    xxwidgets_status status = xxwidgets_widget_create(g_app, g_window, kind, CDIE_TR(text), bounds, control);
    if (status != XXWIDGETS_OK) return 0;
    if (id) SetWindowLongPtrW(native(*control), GWLP_ID, id);
    return 1;
}

static int create_controls(void)
{
    xxwidgets_rect panel_bounds = {1, 4, 96, 18};
    xxwidgets_shortcut shortcuts[] = {
        {"F5", ACTION_SCAN}, {"Ctrl+O", ACTION_BROWSE}, {"F1", ACTION_ABOUT},
        {"Ctrl+Shift+D", ACTION_DATABASE_PATHS}, {"Ctrl+I", ACTION_FILE_INFORMATION}, {"Ctrl+E", ACTION_ENTROPY}, {"Ctrl+H", ACTION_HASH}, {"Ctrl+Shift+S", ACTION_STRINGS}, {"Ctrl+Shift+X", ACTION_HEX_VIEW}, {"Ctrl+Shift+A", ACTION_DISASM}, {"Ctrl+M", ACTION_MEMORY_MAP}, {"Ctrl+Shift+I", ACTION_IMPORTS}, {"Ctrl+Shift+E", ACTION_EXPORTS}, {"Ctrl+Shift+U", ACTION_RESOURCES}, {"Ctrl+Shift+Y", ACTION_SYMBOLS}
    };
    if (!create_control(&g_fileLabel, XXWIDGETS_LABEL, CDIE_N_("File name"), 0) ||
        !create_control(&g_path, XXWIDGETS_EDIT, "", IDC_EDIT_PATH) ||
        !create_control(&g_browse, XXWIDGETS_BUTTON, CDIE_N_("..."), IDC_BTN_BROWSE) ||
        !create_control(&g_status, XXWIDGETS_LABEL, "", IDC_STATUS) ||
        !create_control(&g_options, XXWIDGETS_BUTTON, CDIE_N_("Options"), IDC_BTN_OPTIONS) ||
        !create_control(&g_aboutButton, XXWIDGETS_BUTTON, CDIE_N_("About"), IDC_BTN_ABOUT) ||
        !create_control(&g_fileInformationButton, XXWIDGETS_BUTTON, CDIE_N_("File Information"), IDC_BTN_FILE_INFORMATION) ||
        !create_control(&g_entropyButton, XXWIDGETS_BUTTON, CDIE_N_("Entropy"), IDC_BTN_ENTROPY) ||
        !create_control(&g_hashButton, XXWIDGETS_BUTTON, CDIE_N_("Hash"), IDC_BTN_HASH) ||
        !create_control(&g_stringsButton, XXWIDGETS_BUTTON, CDIE_N_("Strings"), IDC_BTN_STRINGS) ||
        !create_control(&g_hexViewButton, XXWIDGETS_BUTTON, CDIE_N_("Hex View"), IDC_BTN_HEX_VIEW) ||
        !create_control(&g_disasmButton, XXWIDGETS_BUTTON, CDIE_N_("Disasm"), IDC_BTN_DISASM) ||
        !create_control(&g_memoryMapButton, XXWIDGETS_BUTTON, CDIE_N_("Memory Map"), IDC_BTN_MEMORY_MAP) ||
        !create_control(&g_importsButton, XXWIDGETS_BUTTON, CDIE_N_("Imports"), IDC_BTN_IMPORTS) ||
        !create_control(&g_exportsButton, XXWIDGETS_BUTTON, CDIE_N_("Exports"), IDC_BTN_EXPORTS) ||
        !create_control(&g_resourcesButton, XXWIDGETS_BUTTON, CDIE_N_("Resources"), IDC_BTN_RESOURCES) ||
        !create_control(&g_symbolsButton, XXWIDGETS_BUTTON, CDIE_N_("Symbols"), IDC_BTN_SYMBOLS) ||
        !create_control(&g_exit, XXWIDGETS_BUTTON, CDIE_N_("Exit"), IDC_BTN_EXIT)) return 0;
    if (xxwidgets_scan_panel_create(g_window, panel_bounds, &g_panel) != XXWIDGETS_OK) return 0;
    g_type = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_FILE_TYPE);
    g_typeLabel = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_FILE_TYPE_LABEL);
    g_flags = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_FLAGS);
    g_databases = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_DATABASES);
    g_includeTags = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_INCLUDE_TAGS);
    g_excludeTags = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_EXCLUDE_TAGS);
    g_scan = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_SCAN);
    g_reportButton = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_REPORT);
    g_results = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_RESULTS);
    g_flagLabel = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_FLAGS_LABEL);
    g_databaseLabel = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_DATABASES_LABEL);
    g_includeTagsLabel = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_INCLUDE_TAGS_LABEL);
    g_excludeTagsLabel = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_EXCLUDE_TAGS_LABEL);
    g_signaturesButton = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_SIGNATURES);
    g_directoryButton = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_DIRECTORY);
    g_logButton = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_LOG);
    g_msec = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_ELAPSED);
    {
        static const xxwidgets_scan_panel_control_id facts[4][2] = {
            {XXWIDGETS_SCAN_PANEL_ENDIANNESS_LABEL, XXWIDGETS_SCAN_PANEL_ENDIANNESS}, {XXWIDGETS_SCAN_PANEL_MODE_LABEL, XXWIDGETS_SCAN_PANEL_MODE},
            {XXWIDGETS_SCAN_PANEL_ARCHITECTURE_LABEL, XXWIDGETS_SCAN_PANEL_ARCHITECTURE}, {XXWIDGETS_SCAN_PANEL_TYPE_LABEL, XXWIDGETS_SCAN_PANEL_TYPE}};
        for (int i = 0; i < 4; ++i) {
            g_factLabels[i] = xxwidgets_scan_panel_control(g_panel, facts[i][0]);
            g_facts[i] = xxwidgets_scan_panel_control(g_panel, facts[i][1]);
            if (!g_factLabels[i] || !g_facts[i]) return 0;
            /* Boxed and centered, as DIE's read-only fields. */
            SetWindowLongPtrW(native(g_facts[i]), GWL_STYLE, (GetWindowLongPtrW(native(g_facts[i]), GWL_STYLE) &
                ~(LONG_PTR)SS_TYPEMASK) | SS_CENTER | SS_CENTERIMAGE | WS_BORDER);
        }
    }
    if (!g_type || !g_typeLabel || !g_flags || !g_databases || !g_scan || !g_reportButton || !g_results ||
        !g_signaturesButton || !g_directoryButton || !g_logButton || !g_msec ||
        !g_flagLabel || !g_databaseLabel || !g_includeTags || !g_excludeTags ||
        !g_includeTagsLabel || !g_excludeTagsLabel || sync_panel_options() != XXWIDGETS_OK) return 0;
    SetWindowLongPtrW(native(g_type), GWLP_ID, IDC_COMBO_TYPE);
    SetWindowLongPtrW(native(g_flags), GWLP_ID, IDC_COMBO_FLAGS);
    SetWindowLongPtrW(native(g_databases), GWLP_ID, IDC_COMBO_DATABASES);
    SetWindowLongPtrW(native(g_includeTags), GWLP_ID, IDC_COMBO_INCLUDE_TAGS);
    SetWindowLongPtrW(native(g_excludeTags), GWLP_ID, IDC_COMBO_EXCLUDE_TAGS);
    SetWindowLongPtrW(native(g_scan), GWLP_ID, IDC_BTN_SCAN);
    SetWindowLongPtrW(native(g_reportButton), GWLP_ID, IDC_BTN_REPORT);
    SetWindowLongPtrW(native(g_results), GWLP_ID, IDC_SCANRESULTS);
    xxwidgets_widget_set_text(g_databaseLabel, CDIE_TR("Databases (Main always)"));
    if (!SetWindowSubclass(native(g_path), path_subclass, 1, 0) ||
        !SetWindowSubclass(native(g_window), window_subclass, 1, 0)) return 0;
    SetWindowLongPtrW(native(g_msec), GWLP_ID, IDC_STATIC_MSEC);
    SetWindowLongPtrW(native(g_msec), GWL_STYLE,
        (GetWindowLongPtrW(native(g_msec), GWL_STYLE) & ~(LONG_PTR)SS_TYPEMASK) | SS_CENTER | SS_CENTERIMAGE | WS_BORDER);
    if (xxwidgets_window_set_accept_file_drops(g_window, 1) != XXWIDGETS_OK) return 0;
    return xxwidgets_window_set_shortcuts(g_window, shortcuts, sizeof(shortcuts) / sizeof(shortcuts[0])) == XXWIDGETS_OK;
}

/* Explicit --smoke-test mode validates the real GUI and modal ownership with
 * bounded timers on the UI thread. It never scans a user file or alters paths. */
static const WCHAR *g_smokeTitle;
static UINT_PTR g_smokeTimer;
static int g_smokeAction, g_smokeDone, g_smokeTicks, g_smokeFailed, g_smokePhase;

/* Options smoke actions; 4..6 drive the Optimization page. */
enum { SMOKE_SCAN_CANCEL = 20, SMOKE_SCAN_OK, SMOKE_FONTS_CANCEL, SMOKE_FONTS_OK, SMOKE_FILE_CANCEL, SMOKE_SIGNATURES_CANCEL, SMOKE_ABOUT,
       SMOKE_SIGNATURE };
static const WCHAR *g_smokePage;
static int g_smokePageState, g_smokeContextChecked;

typedef struct smoke_search {
    const WCHAR *class_name, *text;
    int visible_only;
    HWND found;
} smoke_search;

static BOOL CALLBACK smoke_find_child(HWND child, LPARAM data)
{
    smoke_search *search = (smoke_search *)data;
    WCHAR class_name[32], text[128];
    GetClassNameW(child, class_name, 32);
    if (search->class_name && lstrcmpiW(class_name, search->class_name)) return TRUE;
    GetWindowTextW(child, text, 128);
    if (search->text && lstrcmpW(text, search->text)) return TRUE;
    if (search->visible_only && !IsWindowVisible(child)) return TRUE;
    search->found = child;
    return FALSE;
}

static HWND smoke_child(HWND window, const WCHAR *class_name, const WCHAR *text, int visible_only)
{
    smoke_search search = {class_name, text, visible_only, NULL};
    EnumChildWindows(window, smoke_find_child, (LPARAM)&search);
    return search.found;
}

/* Each Options page and a control that only that page has. */
static const WCHAR *const g_smokeOptionsPages[][2] = {
    {L"File", L"Enable Explorer context menu"}, {L"Fonts", L"Table views"}, {L"Scan", L"Deep scan"},
    {L"Signatures", L"Main database"}, {L"Hex", L"Choose font"}, {L"Optimization", L"CPU acceleration"}};

static int smoke_select_page(HWND window, const WCHAR *page)
{
    HWND list = smoke_child(window, L"LISTBOX", NULL, 1);
    LRESULT index = list ? SendMessageW(list, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)page) : LB_ERR;
    if (index == LB_ERR || SendMessageW(list, LB_SETCURSEL, (WPARAM)index, 0) == LB_ERR) return 0;
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(list), LBN_SELCHANGE), (LPARAM)list);
    return 1;
}

/* 1 once the page's probe is visible and every other page's probe is hidden. */
static int smoke_page_shown(HWND window, const WCHAR *page)
{
    size_t i;
    int found = 0;
    for (i = 0; i < sizeof(g_smokeOptionsPages) / sizeof(g_smokeOptionsPages[0]); ++i) {
        HWND probe = smoke_child(window, NULL, g_smokeOptionsPages[i][1], 0);
        int selected = !lstrcmpW(page, g_smokeOptionsPages[i][0]);
        if (!probe) {
            if (selected) return 0;
            continue;
        }
        if (!!IsWindowVisible(probe) != selected) return 0;
        found |= selected;
    }
    return found;
}

static void smoke_click(HWND window, const WCHAR *text)
{
    HWND button = smoke_child(window, L"Button", text, 1);
    if (button) SendMessageW(button, BM_CLICK, 0, 0);
    else g_smokeFailed = 1;
}

static BOOL CALLBACK smoke_information_fields(HWND child, LPARAM data)
{
    HWND *table = (HWND *)data;
    WCHAR class_name[32];
    GetClassNameW(child, class_name, 32);
    if (!lstrcmpW(class_name, WC_LISTVIEWW) && ListView_GetItemCount(child) > 0) *table = child;
    return TRUE;
}

static BOOL CALLBACK smoke_hash_rows(HWND child, LPARAM data)
{
    HWND *table = (HWND *)data;
    WCHAR class_name[32];
    GetClassNameW(child, class_name, 32);
    if (!lstrcmpW(class_name, WC_TREEVIEWW) && TreeView_GetCount(child) == CDIE_HASH_COUNT) *table = child;
    return TRUE;
}

/* The Strings table: one selected row under #, Offset, Address, Region,
 * Size, Type and String, with a filter under every title. */
static BOOL CALLBACK smoke_strings_rows(HWND child, LPARAM data)
{
    static const WCHAR *const expected[] = {L"0", L"00000000", L"00401000", L".rdata", L"00000012", L"ASCII", L"CDIE Strings smoke"};
    HWND *rows = (HWND *)data, header;
    WCHAR class_name[32], text[256];
    int column;
    GetClassNameW(child, class_name, 32);
    if (lstrcmpW(class_name, WC_LISTVIEWW) || ListView_GetItemCount(child) != 1 ||
        ListView_GetNextItem(child, -1, LVNI_SELECTED) != 0) return TRUE;
    header = ListView_GetHeader(child);
    if (!header || !(GetWindowLongPtrW(header, GWL_STYLE) & HDS_FILTERBAR) || Header_GetItemCount(header) != 7) return TRUE;
    for (column = 0; column < 7; ++column) {
        text[0] = 0;
        ListView_GetItemText(child, 0, column, text, 256);
        if (lstrcmpW(text, expected[column])) return TRUE;
    }
    *rows = child;
    return TRUE;
}

static BOOL CALLBACK smoke_optimization_children(HWND child, LPARAM data)
{
    HWND *controls = (HWND *)data;
    WCHAR class_name[32], text[32];
    GetClassNameW(child, class_name, 32);
    if (!lstrcmpiW(class_name, L"COMBOBOX") && IsWindowVisible(child)) {
        if (!controls[0]) controls[0] = child;
        else if (!controls[1]) controls[1] = child;
    }
    GetWindowTextW(child, text, 32);
    if (!lstrcmpiW(class_name, L"BUTTON") &&
        (GetWindowLongPtrW(child, GWL_STYLE) & BS_TYPEMASK) == BS_AUTOCHECKBOX) {
        if (!lstrcmpW(text, L"Use SSE2")) controls[2] = child;
        else if (!lstrcmpW(text, L"Use AVX2")) controls[3] = child;
        else if (!lstrcmpW(text, L"Use SSE4.2")) controls[4] = child;
        else if (!lstrcmpW(text, L"Use AES")) controls[5] = child;
        else if (!lstrcmpW(text, L"Use SHA")) controls[6] = child;
        else if (!lstrcmpW(text, L"Use VPCLMULQDQ")) controls[7] = child;
        else if (!lstrcmpW(text, L"Use Arm NEON")) controls[8] = child;
        else if (!lstrcmpW(text, L"Use Arm CRC32")) controls[9] = child;
        else if (!lstrcmpW(text, L"Use Arm AES/SHA")) controls[10] = child;
        else if (!lstrcmpW(text, L"Use Arm Dot Product")) controls[11] = child;
        else if (!lstrcmpW(text, L"Use Arm PMULL")) controls[12] = child;
    }
    if (!lstrcmpW(text, L"Default")) controls[13] = child;
    else if (!lstrcmpW(text, L"OK")) controls[14] = child;
    else if (!lstrcmpW(text, L"Cancel")) controls[15] = child;
    return TRUE;
}

static int smoke_optimization_controls_ready(HWND *controls)
{
    int i;
    if (!controls[0] || !controls[1] || !controls[13] || !controls[14] || !controls[15]) return 0;
    if (!IsWindowVisible(controls[13]) || !IsWindowEnabled(controls[13])) return -1;
#if CDIE_GUI_ARM_TARGET
    for (i = 8; i < 13; ++i)
        if (!controls[i]) return 0;
    for (i = 2; i < 8; ++i)
        if (controls[i] && IsWindowVisible(controls[i])) return -1;
    for (i = 8; i < 13; ++i)
        if (!IsWindowVisible(controls[i])) return -1;
#else
    for (i = 2; i < 8; ++i)
        if (!controls[i]) return 0;
        else if (!IsWindowVisible(controls[i])) return -1;
    for (i = 8; i < 13; ++i)
        if (controls[i] && IsWindowVisible(controls[i])) return -1;
#endif
    return 1;
}

static int smoke_select_size(HWND window, HWND combo, const WCHAR *label)
{
    LRESULT count = SendMessageW(combo, CB_GETCOUNT, 0, 0), i;
    for (i = 0; i < count; ++i) {
        WCHAR text[96];
        LRESULT length = SendMessageW(combo, CB_GETLBTEXTLEN, (WPARAM)i, 0);
        if (length < 0 || length >= 96) continue;
        if (SendMessageW(combo, CB_GETLBTEXT, (WPARAM)i, (LPARAM)text) == CB_ERR || lstrcmpW(text, label)) continue;
        if (SendMessageW(combo, CB_SETCURSEL, (WPARAM)i, 0) == CB_ERR) return 0;
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(combo), CBN_SELCHANGE), (LPARAM)combo);
        return 1;
    }
    return 0;
}

static int smoke_selected_size(HWND combo, const WCHAR *label)
{
    WCHAR text[96];
    LRESULT length, selected = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (selected == CB_ERR) return 0;
    length = SendMessageW(combo, CB_GETLBTEXTLEN, (WPARAM)selected, 0);
    if (length < 0 || length >= 96) return 0;
    return SendMessageW(combo, CB_GETLBTEXT, (WPARAM)selected, (LPARAM)text) != CB_ERR && !lstrcmpW(text, label);
}


static BOOL CALLBACK smoke_windows(HWND window, LPARAM data)
{
    WCHAR title[128];
    (void)data;
    if (GetWindow(window, GW_OWNER) != native(g_window)) return TRUE;
    GetWindowTextW(window, title, 128);
    {
        /* A trailing '*' matches any rest, e.g. a signature's file name. */
        int length = lstrlenW(g_smokeTitle);
        if (length && g_smokeTitle[length - 1] == L'*'
                ? CompareStringOrdinal(title, min(lstrlenW(title), length - 1), g_smokeTitle, length - 1, FALSE) != CSTR_EQUAL
                : lstrcmpW(title, g_smokeTitle) != 0)
            return TRUE;
    }
    if (IsWindowEnabled(native(g_window))) return TRUE;
    /* Options pages share one window: pick the page, then wait until only it shows. */
    if (g_smokePage && g_smokePageState < 2) {
        if (!g_smokePageState) {
            if (!smoke_select_page(window, g_smokePage)) {
                g_smokeFailed = 1;
                PostMessageW(window, WM_CLOSE, 0, 0);
                goto finished;
            }
            g_smokePageState = 1;
            return FALSE;
        }
        if (!smoke_page_shown(window, g_smokePage)) return FALSE;
        g_smokePageState = 2;
    }
    if (g_smokeAction >= 4 && g_smokeAction <= 6) {
        HWND optimization[16] = {0};
        gui_backend_optimization_capabilities capabilities;
        gui_backend_optimization_options current;
        int i, controls_ready;
        EnumChildWindows(window, smoke_optimization_children, (LPARAM)optimization);
        controls_ready = smoke_optimization_controls_ready(optimization);
        if (!controls_ready) return TRUE;
        if (controls_ready < 0) {
            g_smokeFailed = 1;
            SendMessageW(window, WM_CLOSE, 0, 0);
            goto optimization_done;
        }
        if (!gui_backend_get_optimization_capabilities(&capabilities)) {
            g_smokeFailed = 1;
            SendMessageW(window, WM_CLOSE, 0, 0);
            goto optimization_done;
        }
        if (g_smokeAction == 6 && g_smokePhase == 1) {
            /* The dialog applies Defaults after the current poll returns. A
             * second WM_TIMER can arrive first under load, so wait until the
             * two comboboxes expose the reset before checking the flags. */
            if (!smoke_selected_size(optimization[0], L"64 KiB") ||
                !smoke_selected_size(optimization[1], L"64 KiB")) return FALSE;
            if (
#if !CDIE_GUI_ARM_TARGET
                (SendMessageW(optimization[2], BM_GETCHECK, 0, 0) == BST_CHECKED) != capabilities.sse2 ||
                (SendMessageW(optimization[3], BM_GETCHECK, 0, 0) == BST_CHECKED) != capabilities.avx2 ||
                (SendMessageW(optimization[4], BM_GETCHECK, 0, 0) == BST_CHECKED) != capabilities.sse42 ||
                (SendMessageW(optimization[5], BM_GETCHECK, 0, 0) == BST_CHECKED) != capabilities.aes ||
                (SendMessageW(optimization[6], BM_GETCHECK, 0, 0) == BST_CHECKED) != capabilities.sha ||
                (SendMessageW(optimization[7], BM_GETCHECK, 0, 0) == BST_CHECKED) != capabilities.vpclmulqdq) {
#else
                (SendMessageW(optimization[8], BM_GETCHECK, 0, 0) == BST_CHECKED) != capabilities.neon ||
                (SendMessageW(optimization[9], BM_GETCHECK, 0, 0) == BST_CHECKED) != capabilities.arm_crc32 ||
                (SendMessageW(optimization[10], BM_GETCHECK, 0, 0) == BST_CHECKED) != capabilities.arm_crypto ||
                (SendMessageW(optimization[11], BM_GETCHECK, 0, 0) == BST_CHECKED) != capabilities.arm_dotprod ||
                (SendMessageW(optimization[12], BM_GETCHECK, 0, 0) == BST_CHECKED) != capabilities.arm_pmull) {
#endif
                g_smokeFailed = 1;
                SendMessageW(window, WM_CLOSE, 0, 0);
            } else {
                /* Default also unchecks the context menu; keep the current registration. */
                HWND context = smoke_child(window, L"Button", L"Enable Explorer context menu", 0);
                if (context && g_smokeContextChecked) {
                    SendMessageW(context, BM_SETCHECK, BST_CHECKED, 0);
                    SendMessageW(window, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(context), BN_CLICKED), (LPARAM)context);
                }
                SendMessageW(optimization[14], BM_CLICK, 0, 0);
            }
            goto optimization_done;
        }
        if (!gui_backend_get_optimization_options(&current) ||
#if !CDIE_GUI_ARM_TARGET
            !!IsWindowEnabled(optimization[2]) != capabilities.sse2 ||
            !!IsWindowEnabled(optimization[3]) != capabilities.avx2 ||
            !!IsWindowEnabled(optimization[4]) != capabilities.sse42 ||
            !!IsWindowEnabled(optimization[5]) != capabilities.aes ||
            !!IsWindowEnabled(optimization[6]) != capabilities.sha ||
            !!IsWindowEnabled(optimization[7]) != capabilities.vpclmulqdq ||
            (SendMessageW(optimization[2], BM_GETCHECK, 0, 0) == BST_CHECKED) != current.use_sse2 ||
            (SendMessageW(optimization[3], BM_GETCHECK, 0, 0) == BST_CHECKED) != current.use_avx2 ||
            (SendMessageW(optimization[4], BM_GETCHECK, 0, 0) == BST_CHECKED) != current.use_sse42 ||
            (SendMessageW(optimization[5], BM_GETCHECK, 0, 0) == BST_CHECKED) != current.use_aes ||
            (SendMessageW(optimization[6], BM_GETCHECK, 0, 0) == BST_CHECKED) != current.use_sha ||
            (SendMessageW(optimization[7], BM_GETCHECK, 0, 0) == BST_CHECKED) != current.use_vpclmulqdq ||
#else
            !!IsWindowEnabled(optimization[8]) != capabilities.neon ||
            !!IsWindowEnabled(optimization[9]) != capabilities.arm_crc32 ||
            !!IsWindowEnabled(optimization[10]) != capabilities.arm_crypto ||
            !!IsWindowEnabled(optimization[11]) != capabilities.arm_dotprod ||
                !!IsWindowEnabled(optimization[12]) != capabilities.arm_pmull ||
            (SendMessageW(optimization[8], BM_GETCHECK, 0, 0) == BST_CHECKED) != current.use_neon ||
            (SendMessageW(optimization[9], BM_GETCHECK, 0, 0) == BST_CHECKED) != current.use_arm_crc32 ||
            (SendMessageW(optimization[10], BM_GETCHECK, 0, 0) == BST_CHECKED) != current.use_arm_crypto ||
            (SendMessageW(optimization[11], BM_GETCHECK, 0, 0) == BST_CHECKED) != current.use_arm_dotprod ||
                (SendMessageW(optimization[12], BM_GETCHECK, 0, 0) == BST_CHECKED) != current.use_arm_pmull ||
#endif
            !smoke_select_size(window, optimization[0], L"1 KiB") ||
            !smoke_select_size(window, optimization[1], L"2 KiB")) {
            g_smokeFailed = 1;
            SendMessageW(window, WM_CLOSE, 0, 0);
        } else {
            for (i = 2; i < 13; ++i)
                if (optimization[i] && IsWindowVisible(optimization[i]) &&
                    SendMessageW(optimization[i], BM_GETCHECK, 0, 0) == BST_CHECKED)
                    SendMessageW(optimization[i], BM_CLICK, 0, 0);
            if (g_smokeAction == 6) {
                HWND context = smoke_child(window, L"Button", L"Enable Explorer context menu", 0);
                g_smokeContextChecked = context && SendMessageW(context, BM_GETCHECK, 0, 0) == BST_CHECKED;
                SendMessageW(optimization[13], BM_CLICK, 0, 0);
                g_smokePhase = 1;
                return FALSE;
            } else SendMessageW(optimization[g_smokeAction == 5 ? 14 : 15], BM_CLICK, 0, 0);
        }
optimization_done:;
    } else if (g_smokeAction == 9) {
        HWND rows = NULL;
        EnumChildWindows(window, smoke_hash_rows, (LPARAM)&rows);
        if (!rows) g_smokeFailed = 1;
        PostMessageW(window, WM_CLOSE, 0, 0);
    } else if (g_smokeAction == 10) {
        HWND rows = NULL;
        EnumChildWindows(window, smoke_strings_rows, (LPARAM)&rows);
        if (!rows) g_smokeFailed = 1;
        PostMessageW(window, WM_CLOSE, 0, 0);
    } else if (g_smokeAction == 7 || g_smokeAction == 8 || g_smokeAction == 11 || g_smokeAction == 12 || g_smokeAction == 13 || g_smokeAction == 14 || g_smokeAction == 15 || g_smokeAction == 16 || g_smokeAction == 17) {
        HWND fields = NULL;
        EnumChildWindows(window, smoke_information_fields, (LPARAM)&fields);
        if (!fields) g_smokeFailed = 1;
        PostMessageW(window, WM_CLOSE, 0, 0);
    } else if (g_smokeAction == SMOKE_SCAN_CANCEL || g_smokeAction == SMOKE_SCAN_OK) {
        static const WCHAR *const toggles[] = {L"All file types", L"Scan overlays", L"Scan resources",
            L"Scan archive members (one level)", L"Deep scan"};
        size_t i;
        for (i = 0; i < sizeof(toggles) / sizeof(toggles[0]); ++i) smoke_click(window, toggles[i]);
        smoke_click(window, g_smokeAction == SMOKE_SCAN_OK ? L"OK" : L"Cancel");
    } else if (g_smokeAction == SMOKE_FONTS_CANCEL || g_smokeAction == SMOKE_FONTS_OK) {
        smoke_click(window, L"Bold");
        smoke_click(window, g_smokeAction == SMOKE_FONTS_OK ? L"OK" : L"Cancel");
    } else if (g_smokeAction == SMOKE_FILE_CANCEL) {
        /* Opening and cancelling the integration page cannot register cdie. */
        smoke_click(window, L"Cancel");
    } else if (g_smokeAction == SMOKE_ABOUT) {
        /* The Detect It Easy layout: picture, Info/Libraries tabs, Follow me buttons. */
        if (!smoke_child(window, L"SysTabControl32", NULL, 1) || !smoke_child(window, L"Button", L"GitHub", 1) ||
            !smoke_child(window, L"Button", L"Check updates", 1) || !smoke_child(window, L"Static", L"About image", 1))
            g_smokeFailed = 1;
        smoke_click(window, L"OK");
    } else if (g_smokeAction == SMOKE_SIGNATURE) {
        /* S: the script in the signature editor, Run wired to the scanned file. */
        HWND run = smoke_child(window, L"Button", L"Run", 1);
        if (!run || !IsWindowEnabled(run) || !smoke_child(window, L"Button", L"Read-only", 1)) g_smokeFailed = 1;
        smoke_click(window, L"Close");
    } else if (g_smokeAction == SMOKE_SIGNATURES_CANCEL) {
        HWND edit = smoke_child(window, L"Edit", NULL, 1);
        if (!edit || !SetWindowTextW(edit, L"Smoke test cancelled database path")) g_smokeFailed = 1;
        smoke_click(window, L"Cancel");
    } else PostMessageW(window, WM_CLOSE, 0, 0);
    if (g_smokeFailed) PostMessageW(window, WM_CLOSE, 0, 0);
finished:
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
    g_smokeTitle = title; g_smokeAction = action;
    g_smokePage = NULL; g_smokePageState = 0;
    g_smokeDone = g_smokeTicks = g_smokeFailed = g_smokePhase = 0;
    g_smokeTimer = SetTimer(NULL, 0, 20, smoke_tick);
    return g_smokeTimer != 0;
}

/* select = 1 picks the page from the list; 0 expects Options to open on it. */
static int smoke_begin_page(const WCHAR *page, int action, int select)
{
    if (!smoke_begin(L"Options", action)) return 0;
    g_smokePage = page;
    g_smokePageState = select ? 0 : 1;
    return 1;
}

static int smoke_finish(void)
{
    if (g_smokeTimer) KillTimer(NULL, g_smokeTimer);
    g_smokeTimer = 0;
    return g_smokeDone && !g_smokeFailed && IsWindowEnabled(native(g_window));
}

static int smoke_failed(unsigned int line)
{
    char text[80], status[512];
    DWORD written;
    int length = wsprintfA(text, "cdie GUI smoke failed at main_gui.c:%u\r\n", line);
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, (DWORD)length, &written, NULL);
    /* The status line usually says why. */
    if (g_status && xxwidgets_widget_get_text(g_status, status, sizeof(status), NULL) == XXWIDGETS_OK && status[0]) {
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), "Status: ", 8, &written, NULL);
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), status, (DWORD)lstrlenA(status), &written, NULL);
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), "\r\n", 2, &written, NULL);
    }
    return 1;
}

static int run_smoke(void)
{
    xxwidgets_scan_result row = {"Compiler", "Microsoft Visual C/C++", "19.29", "Smoke test"};
    unsigned int flags, databases;
    RECT include_bounds, exclude_bounds, results_bounds, fact_bounds, signatures_bounds, scan_bounds, directory_bounds, msec_bounds;
    WCHAR class_name[32];
    SIZE_T report_length = 0;
    xxwidgets_status status;
    if (!GetClassNameW(native(g_type), class_name, 32) || lstrcmpiW(class_name, L"COMBOBOX") ||
        !GetClassNameW(native(g_flags), class_name, 32) || lstrcmpiW(class_name, L"COMBOBOX") ||
        !GetClassNameW(native(g_databases), class_name, 32) || lstrcmpiW(class_name, L"COMBOBOX") ||
        !GetClassNameW(native(g_includeTags), class_name, 32) || lstrcmpiW(class_name, L"COMBOBOX") ||
        !GetClassNameW(native(g_excludeTags), class_name, 32) || lstrcmpiW(class_name, L"COMBOBOX")) return smoke_failed(__LINE__);
    /* DIE's arrangement: the file's facts above the tree; Signatures, the
     * combo boxes and the tag filters below it, Directory under them, and a
     * tall Scan button beside both rows. The tag labels are gone. */
    GetWindowRect(native(g_includeTags), &include_bounds);
    GetWindowRect(native(g_excludeTags), &exclude_bounds);
    GetWindowRect(native(g_results), &results_bounds);
    GetWindowRect(native(g_facts[0]), &fact_bounds);
    GetWindowRect(native(g_signaturesButton), &signatures_bounds);
    GetWindowRect(native(g_scan), &scan_bounds);
    GetWindowRect(native(g_directoryButton), &directory_bounds);
    GetWindowRect(native(g_msec), &msec_bounds);
    if (fact_bounds.bottom > results_bounds.top || results_bounds.bottom > signatures_bounds.top ||
        signatures_bounds.bottom > directory_bounds.top || include_bounds.right >= exclude_bounds.left ||
        include_bounds.top != signatures_bounds.top || exclude_bounds.right > scan_bounds.left ||
        msec_bounds.right > scan_bounds.left || scan_bounds.top != signatures_bounds.top ||
        scan_bounds.bottom != directory_bounds.bottom || IsWindowVisible(native(g_includeTagsLabel)) ||
        IsWindowVisible(native(g_flagLabel))) return smoke_failed(__LINE__);
    if (xxwidgets_widget_kind(g_results) != XXWIDGETS_TREEVIEW ||
        xxwidgets_widget_kind(g_type) != XXWIDGETS_COMBOBOX ||
        xxwidgets_widget_kind(g_flags) != XXWIDGETS_CHECKCOMBOBOX ||
        xxwidgets_widget_kind(g_databases) != XXWIDGETS_CHECKCOMBOBOX ||
        xxwidgets_widget_kind(g_includeTags) != XXWIDGETS_CHECKCOMBOBOX ||
        xxwidgets_widget_kind(g_excludeTags) != XXWIDGETS_CHECKCOMBOBOX ||
        xxwidgets_widget_kind(g_scan) != XXWIDGETS_BUTTON ||
        xxwidgets_widget_kind(g_reportButton) != XXWIDGETS_BUTTON ||
        xxwidgets_scan_panel_get_flags(g_panel, &flags) != XXWIDGETS_OK || flags != 7u ||
        xxwidgets_scan_panel_get_databases(g_panel, &databases) != XXWIDGETS_OK || databases != 3u ||
        xxwidgets_scan_panel_set_results(g_panel, "Smoke file", &row, 1,
            "Smoke report\nCompiler: Microsoft Visual C/C++ 19.29") != XXWIDGETS_OK ||
        xxwidgets_scan_panel_count(g_panel) != 1 || TreeView_GetCount(native(g_results)) < 2 ||
        xxwidgets_scan_panel_get_report(g_panel, NULL, 0, &report_length) != XXWIDGETS_OK ||
        report_length <= 1) return smoke_failed(__LINE__);
    if (gui_backend_signature_count() > 0) {
        SIZE_T include_size = 0, exclude_size = 0;
        char *include_tags, *exclude_tags;
        WCHAR executable[32768];
        char status_text[512];
        size_t i, binary_index = SIZE_MAX;
        int type_id, binary_type = -1;
        DWORD length = GetModuleFileNameW(NULL, executable, (DWORD)(sizeof(executable) / sizeof(executable[0])));
        if (!xxwidgets_combobox_count(g_includeTags) ||
            xxwidgets_combobox_count(g_includeTags) != xxwidgets_combobox_count(g_excludeTags) ||
            xxwidgets_checkcombobox_set_checked(g_includeTags, 0, 1) != XXWIDGETS_OK ||
            xxwidgets_checkcombobox_set_checked(g_excludeTags, xxwidgets_combobox_count(g_excludeTags) - 1, 1) != XXWIDGETS_OK ||
            xxwidgets_scan_panel_get_include_tags(g_panel, NULL, 0, &include_size) != XXWIDGETS_OK || include_size <= 1 ||
            xxwidgets_scan_panel_get_exclude_tags(g_panel, NULL, 0, &exclude_size) != XXWIDGETS_OK || exclude_size <= 1)
            return smoke_failed(__LINE__);
        include_tags = (char *)HeapAlloc(GetProcessHeap(), 0, include_size);
        exclude_tags = (char *)HeapAlloc(GetProcessHeap(), 0, exclude_size);
        if (!include_tags || !exclude_tags ||
            xxwidgets_scan_panel_get_include_tags(g_panel, include_tags, include_size, NULL) != XXWIDGETS_OK ||
            xxwidgets_scan_panel_get_exclude_tags(g_panel, exclude_tags, exclude_size, NULL) != XXWIDGETS_OK ||
            !include_tags[0] || !exclude_tags[0] || !gui_backend_set_tag_filters(include_tags, exclude_tags)) {
            if (include_tags) HeapFree(GetProcessHeap(), 0, include_tags);
            if (exclude_tags) HeapFree(GetProcessHeap(), 0, exclude_tags);
            return smoke_failed(__LINE__);
        }
        HeapFree(GetProcessHeap(), 0, include_tags);
        HeapFree(GetProcessHeap(), 0, exclude_tags);
        if (xxwidgets_checkcombobox_set_checked(g_includeTags, 0, 0) != XXWIDGETS_OK ||
            xxwidgets_checkcombobox_set_checked(g_excludeTags, xxwidgets_combobox_count(g_excludeTags) - 1, 0) != XXWIDGETS_OK ||
            apply_panel_tag_filters() != XXWIDGETS_OK) return smoke_failed(__LINE__);
        if (!length || length >= sizeof(executable) / sizeof(executable[0])) return smoke_failed(__LINE__);
        if (!SetWindowTextW(native(g_path), executable) || IsWindowEnabled(native(g_type))) return smoke_failed(__LINE__);
        do_scan();
        if (xxwidgets_scan_panel_get_report(g_panel, NULL, 0, &report_length) != XXWIDGETS_OK ||
            report_length <= 1 || !xxwidgets_scan_panel_count(g_panel) || TreeView_GetCount(native(g_results)) < 2 ||
            selected_file_type(&type_id) != XXWIDGETS_OK || type_id != 0 ||
            xxwidgets_combobox_count(g_type) < 3 || !IsWindowEnabled(native(g_type))) return smoke_failed(__LINE__);
        for (i = 0; i < xxwidgets_combobox_count(g_type); ++i) {
            const xx_meta_string *record;
            if (xxwidgets_combobox_get_record(g_type, i, &record) != XXWIDGETS_OK) return smoke_failed(__LINE__);
            if (!lstrcmpiW(record->meta_string->data, L"Binary")) {
                binary_index = i; binary_type = (int)record->var.val.u64; break;
            }
        }
        if (binary_index == SIZE_MAX || binary_type <= 0) return smoke_failed(__LINE__);
        SendMessageW(native(g_type), CB_SETCURSEL, binary_index, 0);
        SendMessageW(native(g_window), WM_COMMAND, MAKEWPARAM(IDC_COMBO_TYPE, CBN_SELCHANGE), (LPARAM)native(g_type));
        if (g_pending != ACTION_SCAN || selected_file_type(&type_id) != XXWIDGETS_OK || type_id != binary_type) return smoke_failed(__LINE__);
        g_pending = ACTION_NONE;
        do_scan();
        if (g_pending != ACTION_NONE || selected_file_type(&type_id) != XXWIDGETS_OK || type_id != binary_type ||
            xxwidgets_widget_get_text(g_status, status_text, sizeof(status_text), NULL) != XXWIDGETS_OK ||
            !strstr(status_text, "Scan complete (Binary)")) return smoke_failed(__LINE__);
        SendMessageW(native(g_type), CB_SETCURSEL, 0, 0);
        SendMessageW(native(g_window), WM_COMMAND, MAKEWPARAM(IDC_COMBO_TYPE, CBN_SELCHANGE), (LPARAM)native(g_type));
        if (g_pending != ACTION_SCAN) return smoke_failed(__LINE__);
        g_pending = ACTION_NONE;
        do_scan();
        if (g_pending != ACTION_NONE || selected_file_type(&type_id) != XXWIDGETS_OK || type_id != 0 ||
            !xxwidgets_scan_panel_count(g_panel)) return smoke_failed(__LINE__);
        {
            /* DIE's S and ? columns: detection rows carry them; S opens the script. */
            size_t node, count = xxwidgets_treeview_count(g_results);
            xxwidgets_event press = {0};
            for (node = 0; node < count && !(xxwidgets_treeview_node_marks(g_results, node) & 1u); ++node) {}
            if (xxwidgets_treeview_mark_count(g_results) != 2 || node == count ||
                lstrcmpA(xxwidgets_treeview_mark_label(g_results, 0), "S") || lstrcmpA(xxwidgets_treeview_mark_label(g_results, 1), "?") ||
                !(xxwidgets_treeview_node_marks(g_results, node) & 2u) || xxwidgets_treeview_node_marks(g_results, 0))
                return smoke_failed(__LINE__);
            press.type = XXWIDGETS_EVENT_CLICK; press.widget = g_results; press.value = (int)node; press.x = 0;
            if (!gui_tree_event(g_panel, &press) || !smoke_begin(L"Signatures", SMOKE_SIGNATURE) ||
                gui_tree_process(g_window) != NULL || !smoke_finish()) return smoke_failed(__LINE__);
        }
    }
    if (!smoke_begin_page(L"Scan", SMOKE_SCAN_CANCEL, 1) || do_settings(GUI_OPTIONS_FONTS) != 0 || !smoke_finish() || !g_bFlagDeep ||
        gui_backend_get_scan_modes() != GUI_SCAN_OVERLAYS) return smoke_failed(__LINE__);
    if (xxwidgets_scan_panel_get_flags(g_panel, &flags) != XXWIDGETS_OK || flags != 7u) return smoke_failed(__LINE__);
    if (!smoke_begin_page(L"Scan", SMOKE_SCAN_OK, 0) || do_settings(GUI_OPTIONS_SCAN) != 1 || !smoke_finish() || g_bFlagDeep ||
        g_optionsPage != GUI_OPTIONS_SCAN) return smoke_failed(__LINE__);
    if (gui_backend_get_scan_modes() != (GUI_SCAN_ALL_TYPES | GUI_SCAN_RESOURCES | GUI_SCAN_ARCHIVES) ||
        !gui_backend_set_scan_modes(GUI_SCAN_OVERLAYS)) return smoke_failed(__LINE__);
    if (xxwidgets_scan_panel_get_flags(g_panel, &flags) != XXWIDGETS_OK || flags != 6u ||
        xxwidgets_scan_panel_get_databases(g_panel, &databases) != XXWIDGETS_OK || databases != 3u) return smoke_failed(__LINE__);
    {
        gui_backend_optimization_options before, after;
        gui_backend_optimization_capabilities capabilities;
        if (!gui_backend_get_optimization_options(&before) ||
            !gui_backend_get_optimization_capabilities(&capabilities) ||
            !smoke_begin_page(L"Optimization", 4, 0) || do_settings(GUI_OPTIONS_OPTIMIZATION) != 0 || !smoke_finish() ||
            !gui_backend_get_optimization_options(&after) ||
            after.buffer_size != before.buffer_size || after.file_buffer_size != before.file_buffer_size ||
            after.use_sse2 != before.use_sse2 || after.use_avx2 != before.use_avx2 ||
            after.use_sse42 != before.use_sse42 || after.use_aes != before.use_aes || after.use_sha != before.use_sha ||
            after.use_vpclmulqdq != before.use_vpclmulqdq || after.use_neon != before.use_neon ||
            after.use_arm_crc32 != before.use_arm_crc32 || after.use_arm_crypto != before.use_arm_crypto ||
            after.use_arm_dotprod != before.use_arm_dotprod || after.use_arm_pmull != before.use_arm_pmull) return smoke_failed(__LINE__);
        if (!smoke_begin_page(L"Optimization", 5, 0) || do_settings(GUI_OPTIONS_OPTIMIZATION) != 1 || !smoke_finish() ||
            !gui_backend_get_optimization_options(&after) ||
            after.buffer_size != 1024 || after.file_buffer_size != 2048 ||
            after.use_sse2 || after.use_avx2 || after.use_sse42 || after.use_aes || after.use_sha || after.use_vpclmulqdq ||
            after.use_neon || after.use_arm_crc32 || after.use_arm_crypto || after.use_arm_dotprod || after.use_arm_pmull) return smoke_failed(__LINE__);
        if (!smoke_begin_page(L"Optimization", 6, 1) || do_settings(GUI_OPTIONS_HEX) != 1 || !smoke_finish() ||
            !gui_backend_get_optimization_options(&after) ||
            after.buffer_size != 65536 || after.file_buffer_size != 65536 ||
            after.use_sse2 != capabilities.sse2 || after.use_avx2 != capabilities.avx2 ||
            after.use_sse42 != capabilities.sse42 || after.use_aes != capabilities.aes ||
            after.use_sha != capabilities.sha || after.use_vpclmulqdq != capabilities.vpclmulqdq ||
            after.use_neon != capabilities.neon || after.use_arm_crc32 != capabilities.arm_crc32 ||
            after.use_arm_crypto != capabilities.arm_crypto || after.use_arm_dotprod != capabilities.arm_dotprod || after.use_arm_pmull != capabilities.arm_pmull ||
            !gui_backend_set_optimization_options(&before)) return smoke_failed(__LINE__);
    }
    {
        xxwidgets_font_options before, after;
        if (xxwidgets_app_get_font_options(g_app, &before) != XXWIDGETS_OK ||
            !smoke_begin_page(L"Fonts", SMOKE_FONTS_CANCEL, 0) || do_settings(GUI_OPTIONS_FONTS) != 0 || !smoke_finish() ||
            xxwidgets_app_get_font_options(g_app, &after) != XXWIDGETS_OK || memcmp(&before, &after, sizeof(before))) return smoke_failed(__LINE__);
        if (!smoke_begin_page(L"Fonts", SMOKE_FONTS_OK, 1) || do_settings(GUI_OPTIONS_SCAN) != 1 || !smoke_finish() ||
            xxwidgets_app_get_font_options(g_app, &after) != XXWIDGETS_OK || !memcmp(&before, &after, sizeof(before))) return smoke_failed(__LINE__);
        {
            xxwidgets_font_options large = after;
            xxwidgets_widget *buttons[] = {g_scan, g_options, g_aboutButton, g_reportButton, g_fileInformationButton, g_entropyButton, g_hashButton, g_stringsButton, g_hexViewButton, g_disasmButton, g_memoryMapButton, g_importsButton, g_exportsButton, g_resourcesButton, g_symbolsButton, g_exit};
            RECT original, button_bounds, flags_bounds, database_bounds, scan_bounds;
            size_t i;
            large.fonts[XXWIDGETS_FONT_CONTROLS].point_size = 24;
            large.fonts[XXWIDGETS_FONT_TEXT_EDITS].point_size = 28;
            GetWindowRect(native(g_window), &original);
            if (xxwidgets_app_set_font_options(g_app, &large) != XXWIDGETS_OK) return smoke_failed(__LINE__);
            layout();
            SetWindowPos(native(g_window), NULL, 0, 0, g_minWindowWidth, g_minWindowHeight, SWP_NOMOVE | SWP_NOZORDER);
            for (i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
                TEXTMETRICW metrics = control_font_metrics(buttons[i]);
                GetClientRect(native(buttons[i]), &button_bounds);
                if (button_bounds.right < control_text_width(buttons[i], 0) ||
                    button_bounds.bottom < metrics.tmHeight + 8) return smoke_failed(__LINE__);
            }
            GetWindowRect(native(g_flags), &flags_bounds);
            GetWindowRect(native(g_databases), &database_bounds);
            GetWindowRect(native(g_scan), &scan_bounds);
            if (flags_bounds.right >= database_bounds.left || database_bounds.right >= scan_bounds.left) return smoke_failed(__LINE__);
            GetClientRect(native(g_path), &button_bounds);
            {
                TEXTMETRICW metrics = control_font_metrics(g_path);
                /* The edit's client rectangle excludes its four-pixel border. */
                if (button_bounds.bottom < metrics.tmHeight + metrics.tmExternalLeading + 4) return smoke_failed(__LINE__);
            }
            {
                gui_backend_optimization_options optimization_before, optimization_after;
                if (!gui_backend_get_optimization_options(&optimization_before) ||
                    !smoke_begin_page(L"Optimization", 4, 0) || do_settings(GUI_OPTIONS_OPTIMIZATION) != 0 || !smoke_finish() ||
                    !gui_backend_get_optimization_options(&optimization_after) ||
                    optimization_after.buffer_size != optimization_before.buffer_size ||
                    optimization_after.file_buffer_size != optimization_before.file_buffer_size ||
                    optimization_after.use_sse2 != optimization_before.use_sse2 ||
                    optimization_after.use_avx2 != optimization_before.use_avx2 ||
                    optimization_after.use_sse42 != optimization_before.use_sse42 ||
                    optimization_after.use_aes != optimization_before.use_aes ||
                    optimization_after.use_sha != optimization_before.use_sha ||
                    optimization_after.use_vpclmulqdq != optimization_before.use_vpclmulqdq ||
                    optimization_after.use_neon != optimization_before.use_neon ||
                    optimization_after.use_arm_crc32 != optimization_before.use_arm_crc32 ||
                    optimization_after.use_arm_crypto != optimization_before.use_arm_crypto ||
                    optimization_after.use_arm_dotprod != optimization_before.use_arm_dotprod || optimization_after.use_arm_pmull != optimization_before.use_arm_pmull) return smoke_failed(__LINE__);
            }
            if (xxwidgets_app_set_font_options(g_app, &after) != XXWIDGETS_OK) return smoke_failed(__LINE__);
            layout();
            SetWindowPos(native(g_window), NULL, 0, 0, original.right - original.left,
                original.bottom - original.top, SWP_NOMOVE | SWP_NOZORDER);
        }
        if (!smoke_begin_page(L"File", SMOKE_FILE_CANCEL, 0) || do_settings(GUI_OPTIONS_FILE) != 0 || !smoke_finish()) return smoke_failed(__LINE__);
    }
    if (!smoke_begin(L"File Information", 7)) return smoke_failed(__LINE__);
    do_file_information();
    if (!smoke_finish()) return smoke_failed(__LINE__);
    if (!smoke_begin(L"Entropy", 8)) return smoke_failed(__LINE__);
    do_entropy();
    if (!smoke_finish()) return smoke_failed(__LINE__);
    if (!smoke_begin(L"Hash", 9)) return smoke_failed(__LINE__);
    do_hash();
    if (!smoke_finish()) return smoke_failed(__LINE__);
    {
        xxwidgets_string_record record = {0, 18, 18, "ASCII", "CDIE Strings smoke", ".rdata", 0x401000, 1};
        if (!smoke_begin(L"Strings", 10)) return smoke_failed(__LINE__);
        status = xxwidgets_strings_dialog(g_window, CDIE_TR("Strings"), "synthetic fixture", 18, &record, 1, NULL, NULL);
        if (status != XXWIDGETS_OK || !smoke_finish()) return smoke_failed(__LINE__);
    }
    if (!smoke_begin(L"Hex View", 11)) return smoke_failed(__LINE__);
    do_hex_view();
    if (!smoke_finish()) return smoke_failed(__LINE__);
    if (!smoke_begin(L"Disasm", 12)) return smoke_failed(__LINE__);
    do_disasm();
    if (!smoke_finish()) return smoke_failed(__LINE__);
    if (!smoke_begin(L"Memory Map", 13)) return smoke_failed(__LINE__);
    do_memory_map();
    if (!smoke_finish()) return smoke_failed(__LINE__);
    if (!smoke_begin(L"Imports", 14)) return smoke_failed(__LINE__);
    do_imports();
    if (!smoke_finish()) return smoke_failed(__LINE__);
    {
        xx_export_record record = {"CDIEExport", NULL, 7, 0, UINT64_C(0x140001000)};
        xxwidgets_exports_data snapshot = {"synthetic fixture", "PE64", 16, &record, 1};
        if (!smoke_begin(L"Exports", 15)) return smoke_failed(__LINE__);
        status = xxwidgets_exports_dialog(g_window, CDIE_TR("Exports"), &snapshot);
        if (status != XXWIDGETS_OK || !smoke_finish()) return smoke_failed(__LINE__);
    }
    if (!smoke_begin(L"Resources", 16)) return smoke_failed(__LINE__);
    do_resources();
    if (!smoke_finish()) return smoke_failed(__LINE__);
    {
        xx_symbol_record record = {0};
        xxwidgets_symbols_data snapshot;
        record.name = "CDIESymbol"; record.file_name = "test.c"; record.kind = XX_SYMBOL_KIND_FUNCTION;
        record.binding = XX_SYMBOL_BINDING_GLOBAL; record.address = UINT64_C(0x140001000);
        snapshot = (xxwidgets_symbols_data){"synthetic fixture", "PE", 16, &record, 1};
        if (!smoke_begin(L"Symbols", 17)) return smoke_failed(__LINE__);
        status = xxwidgets_symbols_dialog(g_window, CDIE_TR("Symbols"), &snapshot);
        if (status != XXWIDGETS_OK || !smoke_finish()) return smoke_failed(__LINE__);
    }
    if (!smoke_begin(L"About", SMOKE_ABOUT)) return smoke_failed(__LINE__);
    status = xxwidgets_about_dialog_show(g_about, g_window);
    if (status != XXWIDGETS_OK || !smoke_finish()) return smoke_failed(__LINE__);
    if (!smoke_begin(L"Scan report", 0)) return smoke_failed(__LINE__);
    status = xxwidgets_scan_panel_show_report(g_panel);
    if (status != XXWIDGETS_OK || !smoke_finish()) return smoke_failed(__LINE__);
    {
        char *original_path = xx_str_create(gui_backend_main_db_path());
        int changed;
        if (!original_path || !smoke_begin_page(L"Signatures", SMOKE_SIGNATURES_CANCEL, 0)) {
            if (original_path) xx_str_free(original_path);
            return smoke_failed(__LINE__);
        }
        do_settings(GUI_OPTIONS_SIGNATURES);
        changed = lstrcmpA(original_path, gui_backend_main_db_path());
        xx_str_free(original_path);
        if (!smoke_finish() || changed) return smoke_failed(__LINE__);
    }
    return xxwidgets_scan_panel_clear(g_panel) != XXWIDGETS_OK ||
           xxwidgets_scan_panel_count(g_panel) != 0 || TreeView_GetCount(native(g_results)) != 0;
}

static int is_smoke_requested(void)
{
    int argc=0, requested=0;
    WCHAR **args=CommandLineToArgvW(GetCommandLineW(),&argc);
    if (!args) return 0;
    for (int i=1;i<argc;++i) {
        if (!lstrcmpW(args[i],L"--")) break;
        if (!lstrcmpW(args[i],L"--translation")) { ++i; continue; }
        if (!lstrcmpW(args[i],L"--smoke-test")) requested=1;
    }
    LocalFree(args); return requested;
}
static xxwidgets_status load_translation(size_t *line)
{
    int argc=0;
    char *path=NULL;
    xxwidgets_status status=XXWIDGETS_OK;
    WCHAR **args=CommandLineToArgvW(GetCommandLineW(),&argc);
    if (!args) return XXWIDGETS_OUT_OF_MEMORY;
    for (int i=1;i<argc;++i) {
        if (!lstrcmpW(args[i],L"--")) break;
        if (!lstrcmpW(args[i],L"--translation")) {
            if (++i >= argc || !args[i][0]) { status=XXWIDGETS_INVALID_ARGUMENT; break; }
            xx_str_free(path); path=xx_str_unicode_to_utf8(args[i]);
            if (!path) { status=XXWIDGETS_OUT_OF_MEMORY; break; }
        }
    }
    if (status == XXWIDGETS_OK) status=cdie_translation_open(g_app,path,line);
    xx_str_free(path); LocalFree(args); return status;
}
static int set_startup_file(void)
{
    int argc=0, has_file=0, positional=0;
    WCHAR **args=CommandLineToArgvW(GetCommandLineW(),&argc);
    if (!args) return 0;
    for (int i=1;i<argc;++i) {
        if (!positional && !lstrcmpW(args[i],L"--")) { positional=1; continue; }
        if (!positional && !lstrcmpW(args[i],L"--translation")) { ++i; continue; }
        if (!positional && !lstrcmpW(args[i],L"--smoke-test")) continue;
        char *path=xx_str_unicode_to_utf8(args[i]);
        if (path) { has_file=xxwidgets_widget_set_text(g_path,path)==XXWIDGETS_OK; xx_str_free(path); }
        break;
    }
    LocalFree(args); return has_file;
}

int gui_run(void)
{
    xxwidgets_config config = {XXWIDGETS_BACKEND_NATIVE, on_event, NULL};
    xxwidgets_rect bounds = {8, 4, 100, 27};
    xxwidgets_status status;
    int result = 0, smoke = is_smoke_requested();
    WCHAR title[160], ready[512];
    WCHAR *base;
    char *utf8_title;

    g_hInst = GetModuleHandleW(NULL);
    status = xxwidgets_app_create(&config, &g_app);
    if (status != XXWIDGETS_OK) return 1;
    {
        size_t line=0;
        status=load_translation(&line);
        if (status != XXWIDGETS_OK) {
            MessageBoxW(NULL,L"Cannot load PO translation. Check --translation FILE and CDIE_TRANSLATION.",L"cdie",MB_OK|MB_ICONERROR);
            result=1; goto finish;
        }
    }
    base = xx_str_utf8_to_unicode(gui_backend_app_title());
#if defined(_WIN64)
    cdie_translation_swprintf(title, sizeof(title)/sizeof(title[0]), CDIE_N_(L"%s (x64)"), base ? base : L"cdie");
#else
    cdie_translation_swprintf(title, sizeof(title)/sizeof(title[0]), CDIE_N_(L"%s (x86)"), base ? base : L"cdie");
#endif
    if (base) xx_str_wfree(base);
    utf8_title = xx_str_unicode_to_utf8(title);
    if (!utf8_title) { result = 1; goto finish; }
    status = xxwidgets_widget_create(g_app, NULL, XXWIDGETS_WINDOW, utf8_title, bounds, &g_window);
    xx_str_free(utf8_title);
    if (status != XXWIDGETS_OK) { result = 1; goto finish; }
    xxwidgets_widget_set_visible(g_window, 0);
    if (!create_controls() || configure_about() != XXWIDGETS_OK) { result = 1; goto finish; }
    {
        /* Standard sizes, so the shared handles need no cleanup. Dialogs take the owner's icons. */
        HICON large_icon = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_CDIE), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED);
        HICON small_icon = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_CDIE), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
        if (large_icon) SendMessageW(native(g_window), WM_SETICON, ICON_BIG, (LPARAM)large_icon);
        if (small_icon) SendMessageW(native(g_window), WM_SETICON, ICON_SMALL, (LPARAM)small_icon);
    }
    SetWindowPos(native(g_window), NULL, 0, 0, 850, 580, SWP_NOMOVE | SWP_NOZORDER);
    /* The first layout measures the controls; never start below that minimum. */
    layout();
    if (g_minWindowWidth > 850 || g_minWindowHeight > 580)
        SetWindowPos(native(g_window), NULL, 0, 0, g_minWindowWidth > 850 ? g_minWindowWidth : 850,
                     g_minWindowHeight > 580 ? g_minWindowHeight : 580, SWP_NOMOVE | SWP_NOZORDER);
    (void)gui_backend_init();
    {
        /* Stored Options apply before the first scan; smoke tests keep them in memory. */
        gui_options_values values;
        int stored = 1, databases = 1;
        g_settings = gui_settings_open(smoke, &stored);
        status = current_options(&values);
        if (status == XXWIDGETS_OK) databases = gui_settings_load(g_settings, &values, &g_optionsPage);
        if (gui_backend_signature_count()) {
            cdie_translation_swprintf(ready, sizeof(ready)/sizeof(ready[0]), CDIE_N_(L"Ready. %d signatures loaded. Choose a file or drag one here, then press Scan."),
                      gui_backend_signature_count());
            set_status_wide(ready);
        } else {
            xxwidgets_widget_set_text(g_status,
                CDIE_TR("Main signature database not found. Use Options > Signatures to select the database folder."));
        }
        if (!databases)
            xxwidgets_widget_set_text(g_status, CDIE_TR("The saved signature folders cannot be loaded; the default ones are active."));
        else if (!stored)
            xxwidgets_widget_set_text(g_status, CDIE_TR("Saved options cannot be read; the defaults are used."));
        if (status == XXWIDGETS_OK) status = apply_options(&values);
        if (status != XXWIDGETS_OK) report_ui_error("Apply saved options", status);
    }
    status = refresh_panel_tags();
    if (status != XXWIDGETS_OK) report_ui_error("Load signature tags", status);
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
        {
            const char *message = gui_tree_process(g_window);
            if (message) xxwidgets_widget_set_text(g_status, message);
        }
        action = g_pending; g_pending = ACTION_NONE;
        /* All modal loops and scans start after returning from app_poll. */
        switch (action) {
        case ACTION_SCAN: do_scan(); break;
        case ACTION_BROWSE: do_browse(); break;
        case ACTION_OPTIONS: do_settings(g_optionsPage); break;
        case ACTION_ABOUT: do_about(); break;
        case ACTION_DATABASE_PATHS: do_settings(GUI_OPTIONS_SIGNATURES); break;
        case ACTION_FILE_INFORMATION: do_file_information(); break;
        case ACTION_ENTROPY: do_entropy(); break;
        case ACTION_HASH: do_hash(); break;
        case ACTION_STRINGS: do_strings(); break;
        case ACTION_HEX_VIEW: do_hex_view(); break;
        case ACTION_DISASM: do_disasm(); break;
        case ACTION_MEMORY_MAP: do_memory_map(); break;
        case ACTION_IMPORTS: do_imports(); break;
        case ACTION_EXPORTS: do_exports(); break;
        case ACTION_RESOURCES: do_resources(); break;
        case ACTION_SYMBOLS: do_symbols(); break;
        case ACTION_REPORT:
            status = xxwidgets_scan_panel_show_report(g_panel);
            if (status != XXWIDGETS_OK) report_ui_error("Open scan report", status);
            break;
        default: break;
        }
    }
finish:
    if (g_smokeTimer) KillTimer(NULL, g_smokeTimer);
    gui_settings_close(g_settings);
    g_settings = NULL;
    gui_tree_reset();
    gui_backend_shutdown();
    if (g_about && xxwidgets_about_dialog_destroy(g_about) != XXWIDGETS_OK) result = 1;
    g_about = NULL;
    if (g_panel && xxwidgets_scan_panel_destroy(g_panel) != XXWIDGETS_OK) result = 1;
    g_panel = NULL;
    if (g_typePath) xx_str_free(g_typePath);
    g_typePath = NULL;
    cdie_translation_close();
    if (xxwidgets_app_destroy(g_app) != XXWIDGETS_OK) result = 1;
    g_app = NULL;
    return result;
}
