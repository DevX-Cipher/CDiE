/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Cocoa application integration around the shared xxwidgets scan panel.
 * All engine calls and widget updates run on the main thread.
 */
#import <AppKit/AppKit.h>
#include <xxwidgets/xxwidgets.h>
#include <xxwidgets/xxwidgets_scan_panel.h>
#include <xxwidgets/xxwidgets_combobox.h>
#include <xxfclib/strings/xx_string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "gui_backend.h"
#include "app/cdie_translation.h"
#include "gui_scan_options.h"
#include "gui_options.h"
#include "gui_frontend_preferences.h"
#include "gui_about.h"
#include "gui_settings.h"
#include "gui_tree.h"
#include "gui_file_information.h"
#include "gui_entropy.h"
#include "gui_hash.h"
#include "gui_strings.h"
#include "gui_hex_view.h"
#include "gui_disasm.h"
#include "gui_memory_map.h"
#include "gui_imports.h"
#include "gui_exports.h"
#include "gui_resources.h"
#include "gui_symbols.h"
#include "gui_extractor.h"

enum { ACTION_NONE, ACTION_SCAN, ACTION_OPEN, ACTION_REPORT, ACTION_OPTIONS,
       ACTION_DATABASES, ACTION_OPTIMIZATION, ACTION_ABOUT, ACTION_FILE_INFORMATION, ACTION_ENTROPY, ACTION_HASH, ACTION_STRINGS, ACTION_HEX_VIEW, ACTION_DISASM, ACTION_MEMORY_MAP, ACTION_IMPORTS, ACTION_EXPORTS, ACTION_RESOURCES, ACTION_SYMBOLS, ACTION_EXTRACTOR, ACTION_RECENT };
static xxwidgets_app *g_app;
static xxwidgets_widget *g_window, *g_path, *g_browse, *g_status, *g_options;
static xxwidgets_widget *g_file_information, *g_entropy, *g_hash, *g_strings, *g_hex_view, *g_disasm, *g_memory_map, *g_imports, *g_exports, *g_resources, *g_symbols, *g_extractor;
static xxwidgets_scan_panel *g_panel;
static int g_running = 1, g_pending, g_layout;
static gui_options_page_id g_options_page = GUI_OPTIONS_SCAN;
static xx_settings *g_settings;
static NSString *g_type_path;

static NSString *string(const char *text)
{
    return text ? ([NSString stringWithUTF8String:text] ?: @"") : @"";
}
static NSString *cdie_ns_tr(NSString *source) { return string(CDIE_TR([source UTF8String])); }
static NSString *cdie_ns_fmt(NSString *source) { return string(CDIE_FMT([source UTF8String])); }

static xxwidgets_widget *control(xxwidgets_scan_panel_control_id which)
{
    return xxwidgets_scan_panel_control(g_panel, which);
}
static xxwidgets_status refresh_panel_tags(void)
{
    char *tags = gui_backend_used_tags();
    xxwidgets_status status;
    if (!tags) return XXWIDGETS_OUT_OF_MEMORY;
    status = xxwidgets_scan_panel_set_available_tags(g_panel, tags);
    gui_backend_free(tags);
    return status;
}
static xxwidgets_status apply_panel_tag_filters(void)
{
    size_t include_size = 0, exclude_size = 0;
    char *include_tags = NULL, *exclude_tags = NULL;
    xxwidgets_status status = xxwidgets_scan_panel_get_include_tags(g_panel, NULL, 0, &include_size);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_scan_panel_get_exclude_tags(g_panel, NULL, 0, &exclude_size);
    if (status != XXWIDGETS_OK) return status;
    include_tags = malloc(include_size ? include_size : 1);
    exclude_tags = malloc(exclude_size ? exclude_size : 1);
    if (!include_tags || !exclude_tags) {
        free(include_tags); free(exclude_tags);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    status = xxwidgets_scan_panel_get_include_tags(g_panel, include_tags, include_size ? include_size : 1, NULL);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_scan_panel_get_exclude_tags(g_panel, exclude_tags, exclude_size ? exclude_size : 1, NULL);
    if (status == XXWIDGETS_OK && !gui_backend_set_tag_filters(include_tags, exclude_tags))
        status = XXWIDGETS_OUT_OF_MEMORY;
    free(include_tags); free(exclude_tags);
    return status;
}
static void report_ui_error(const char *operation, xxwidgets_status status)
{
    NSString *message = [NSString stringWithFormat:cdie_ns_fmt(@"%s: %s"), operation, xxwidgets_status_string(status)];
    xxwidgets_widget_set_text(g_status, [message UTF8String]);
}
static void ready(void)
{
    NSString *message = gui_backend_signature_count() ?
        [NSString stringWithFormat:cdie_ns_fmt(@"Ready. %d signatures loaded. Choose a file or drop one into the window."), gui_backend_signature_count()] :
        cdie_ns_tr(@"No signature database loaded. Choose its folder in Options > Scan.");
    xxwidgets_widget_set_text(g_status, [message UTF8String]);
}
static void open_path(NSString *path)
{
    xxwidgets_widget_set_text(g_path, [path UTF8String]);
    gui_frontend_remember_file([path UTF8String]);
    g_pending = gui_frontend_scan_after_open() ? ACTION_SCAN : ACTION_NONE;
}
static void layout(void)
{
    xxwidgets_rect bounds;
    if (!g_panel || xxwidgets_widget_get_rect(g_window, &bounds) != XXWIDGETS_OK) return;
    int width = bounds.width, height = bounds.height;
    xxwidgets_widget_set_rect(g_path, (xxwidgets_rect){2, 1, width - 17, 2});
    xxwidgets_widget_set_rect(g_browse, (xxwidgets_rect){width - 13, 1, 11, 2});
    /* At least the panel's minimum height (14) in a short window. */
    xxwidgets_scan_panel_set_rect(g_panel, (xxwidgets_rect){2, 4, width - 4, height - 13 < 14 ? 14 : height - 13});
    xxwidgets_widget_set_rect(g_options, (xxwidgets_rect){2, height - 8, 16, 2});
    xxwidgets_widget_set_rect(g_imports, (xxwidgets_rect){20, height - 8, 18, 2});
    xxwidgets_widget_set_rect(g_exports, (xxwidgets_rect){40, height - 8, 18, 2});
    xxwidgets_widget_set_rect(g_resources, (xxwidgets_rect){60, height - 8, 22, 2});
    xxwidgets_widget_set_rect(g_symbols, (xxwidgets_rect){84, height - 8, 18, 2});
    xxwidgets_widget_set_rect(g_extractor, (xxwidgets_rect){104, height - 8, 22, 2});
    xxwidgets_widget_set_rect(g_file_information, (xxwidgets_rect){2, height - 5, 26, 2});
    xxwidgets_widget_set_rect(g_entropy, (xxwidgets_rect){30, height - 5, 16, 2});
    xxwidgets_widget_set_rect(g_hash, (xxwidgets_rect){48, height - 5, 14, 2});
    xxwidgets_widget_set_rect(g_strings, (xxwidgets_rect){64, height - 5, 18, 2});
    xxwidgets_widget_set_rect(g_hex_view, (xxwidgets_rect){84, height - 5, 18, 2});
    xxwidgets_widget_set_rect(g_disasm, (xxwidgets_rect){104, height - 5, 18, 2});
    xxwidgets_widget_set_rect(g_memory_map, (xxwidgets_rect){124, height - 5, 20, 2});
    xxwidgets_widget_set_rect(g_status, (xxwidgets_rect){2, height - 2, width - 4, 1});
}
static void on_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    (void)app; (void)user;
    if (event->type == XXWIDGETS_EVENT_CLOSE && event->widget == g_window) g_running = 0;
    else if (event->type == XXWIDGETS_EVENT_RESIZE && event->widget == g_window) g_layout = 1;
    else if (event->type == XXWIDGETS_EVENT_SHORTCUT) g_pending = event->value;
    else if (event->type == XXWIDGETS_EVENT_DROP_FILES && event->widget == g_window && event->path_count) open_path(string(event->paths[0]));
    else if (event->type == XXWIDGETS_EVENT_CHANGE && event->widget == g_path) {
        xxwidgets_scan_panel_clear(g_panel);
        xxwidgets_widget_set_enabled(control(XXWIDGETS_SCAN_PANEL_FILE_TYPE), 0);
    } else if (event->type == XXWIDGETS_EVENT_CHANGE &&
               (event->widget == control(XXWIDGETS_SCAN_PANEL_INCLUDE_TAGS) ||
                event->widget == control(XXWIDGETS_SCAN_PANEL_EXCLUDE_TAGS))) {
        xxwidgets_status status = apply_panel_tag_filters();
        if (status != XXWIDGETS_OK) report_ui_error("Read tag filters", status);
    } else if (event->type == XXWIDGETS_EVENT_SELECT && event->widget == control(XXWIDGETS_SCAN_PANEL_FILE_TYPE)) g_pending = ACTION_SCAN;
    else if (gui_tree_event(g_panel, event)) {
        /* S or ? on a result row: shown after polling returns. */
    } else if (event->type == XXWIDGETS_EVENT_CLICK) {
        if (event->widget == g_browse) g_pending = ACTION_OPEN;
        else if (event->widget == control(XXWIDGETS_SCAN_PANEL_SCAN)) g_pending = ACTION_SCAN;
        else if (event->widget == control(XXWIDGETS_SCAN_PANEL_REPORT)) g_pending = ACTION_REPORT;
        else if (event->widget == g_options) g_pending = ACTION_OPTIONS;
        else if (event->widget == g_file_information) g_pending = ACTION_FILE_INFORMATION;
        else if (event->widget == g_entropy) g_pending = ACTION_ENTROPY;
        else if (event->widget == g_hash) g_pending = ACTION_HASH;
        else if (event->widget == g_strings) g_pending = ACTION_STRINGS;
        else if (event->widget == g_hex_view) g_pending = ACTION_HEX_VIEW;
        else if (event->widget == g_disasm) g_pending = ACTION_DISASM;
        else if (event->widget == g_memory_map) g_pending = ACTION_MEMORY_MAP;
        else if (event->widget == g_imports) g_pending = ACTION_IMPORTS;
        else if (event->widget == g_exports) g_pending = ACTION_EXPORTS;
        else if (event->widget == g_resources) g_pending = ACTION_RESOURCES;
        else if (event->widget == g_symbols) g_pending = ACTION_SYMBOLS;
        else if (event->widget == g_extractor) g_pending = ACTION_EXTRACTOR;
    }
}
static void collect_type(int type, const char *name, void *user)
{
    [(NSMutableArray *)user addObject:@{@"id": @(type), @"name": string(type == 0 ? CDIE_TR("Automatic") : name)}];
}
static int refresh_types(NSString *path)
{
    NSMutableArray *types = [NSMutableArray array];
    xx_var selected = {0};
    xxwidgets_widget *combo = control(XXWIDGETS_SCAN_PANEL_FILE_TYPE);
    if ([path isEqualToString:g_type_path]) xxwidgets_combobox_get_current(combo, &selected);
    if (!gui_backend_file_types([path UTF8String], collect_type, types)) return 0;
    size_t count = [types count];
    xx_meta_string *records = calloc(count, sizeof(*records));
    xx_str_w_s *labels = calloc(count, sizeof(*labels));
    if (!records || !labels) { free(records); free(labels); return 0; }
    int index = 0, ok = 1;
    for (size_t i = 0; i < count; ++i) {
        NSDictionary *item = types[i];
        labels[i].data = xx_str_utf8_to_unicode([item[@"name"] UTF8String]);
        if (!labels[i].data) { ok = 0; break; }
        labels[i].length = wcslen(labels[i].data);
        labels[i].capacity = labels[i].length + 1;
        labels[i].is_view = true;
        records[i].meta_string = &labels[i];
        records[i].var.type = XX_VAR_TYPE_UINT64;
        records[i].var.val.u64 = [item[@"id"] unsignedLongLongValue];
        if (selected.type == XX_VAR_TYPE_UINT64 && records[i].var.val.u64 == selected.val.u64) index = (int)i;
    }
    if (ok) ok = xxwidgets_combobox_set_records(combo, records, count) == XXWIDGETS_OK;
    if (ok) ok = xxwidgets_widget_set_value(combo, index) == XXWIDGETS_OK;
    for (size_t i = 0; i < count; ++i) if (labels[i].data) xx_str_wfree(labels[i].data);
    free(labels); free(records);
    if (ok) { [g_type_path release]; g_type_path = [path copy]; }
    return ok;
}
static int do_scan(void)
{
    size_t length = 0;
    if (xxwidgets_widget_get_text(g_path, NULL, 0, &length) != XXWIDGETS_OK) return 0;
    char *text = malloc(length ? length : 1);
    if (!text) return 0;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, text, length, NULL);
    NSString *path = status == XXWIDGETS_OK ? [string(text) stringByExpandingTildeInPath] : nil;
    free(text);
    if (!path) return 0;
    xxwidgets_widget *combo = control(XXWIDGETS_SCAN_PANEL_FILE_TYPE);
    xxwidgets_scan_panel_clear(g_panel);
    if (![path length]) { NSBeep(); xxwidgets_widget_focus(g_path); return 0; }
    if (!refresh_types(path)) {
        xxwidgets_widget_set_enabled(combo, 0);
        xxwidgets_widget_set_text(g_status, CDIE_TR("Cannot open this file. Check the path and permissions."));
        return 0;
    }
    xx_var type_id = {0};
    unsigned int flags = 0, databases = 0;
    if (xxwidgets_combobox_get_current(combo, &type_id) != XXWIDGETS_OK || type_id.type != XX_VAR_TYPE_UINT64 ||
        xxwidgets_scan_panel_get_flags(g_panel, &flags) != XXWIDGETS_OK ||
        xxwidgets_scan_panel_get_databases(g_panel, &databases) != XXWIDGETS_OK) return 0;
    {
        xxwidgets_status status = apply_panel_tag_filters();
        if (status != XXWIDGETS_OK) { report_ui_error("Read tag filters", status); return 0; }
    }
    gui_backend_set_flags(flags & 1, flags & 2, flags & 4, flags & 8, flags & 16, flags & 32);
    gui_backend_set_databases(databases & 1, databases & 2);
    xxwidgets_widget_set_text(g_status, CDIE_TR("ScanningÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦"));
    xxwidgets_widget_set_enabled(g_window, 0);
    [NSApp updateWindows];
    NSTimeInterval started = [NSDate timeIntervalSinceReferenceDate];
    char *type = NULL;
    int cancelled = 0;
    int ok = gui_tree_scan(g_window, g_panel, [path UTF8String], (int)type_id.val.u64, &type, &cancelled);
    NSString *message = ok ? [NSString stringWithFormat:cdie_ns_fmt(@"Scan complete (%@). %lu results Ãƒâ€šÃ‚Â· %.0f ms"), string(type),
        (unsigned long)xxwidgets_scan_panel_count(g_panel), ([NSDate timeIntervalSinceReferenceDate] - started) * 1000] :
        cancelled ? cdie_ns_tr(@"Scan cancelled.") : cdie_ns_tr(@"Scan failed.");
    xxwidgets_widget_set_text(g_status, [message UTF8String]);
    gui_backend_free(type);
    xxwidgets_widget_set_enabled(g_window, 1);
    xxwidgets_widget_set_enabled(combo, 1);
    if (ok) gui_frontend_remember_file([path UTF8String]);
    return ok;
}
static void do_open(void)
{
    NSOpenPanel *picker = [NSOpenPanel openPanel];
    [picker setTitle:cdie_ns_tr(@"Choose a file to scan")];
    [picker setCanChooseDirectories:NO];
    [picker setAllowsMultipleSelection:NO];
    gui_options_values preferences;
    gui_preferences_get(&preferences);
    if (preferences.file.save_last_directory && preferences.file.last_directory[0])
        [picker setDirectoryURL:[NSURL fileURLWithPath:string(preferences.file.last_directory) isDirectory:YES]];
    if ([picker runModal] == NSModalResponseOK) open_path([[picker URL] path]);
}
static void do_file_information(void)
{
    size_t length = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &length);
    if (status != XXWIDGETS_OK) { report_ui_error("Read file path", status); return; }
    path = malloc(length ? length : 1);
    if (!path) { report_ui_error("Read file path", XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, length ? length : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a PE, ELF or Mach-O file to inspect."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) {
        NSString *expanded = [string(path) stringByExpandingTildeInPath];
        status = gui_file_information_show(g_window, [expanded UTF8String]);
    }
    free(path);
    if (status != XXWIDGETS_OK) report_ui_error("File Information", status);
}

static void do_entropy(void)
{
    size_t length = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &length);
    if (status != XXWIDGETS_OK) { report_ui_error("Read file path", status); return; }
    path = malloc(length ? length : 1);
    if (!path) { report_ui_error("Read file path", XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, length ? length : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to calculate entropy."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) {
        NSString *expanded = [string(path) stringByExpandingTildeInPath];
        status = gui_entropy_show(g_window, [expanded UTF8String]);
    }
    free(path);
    if (status != XXWIDGETS_OK) report_ui_error("Entropy", status);
}

static void do_hash(void)
{
    size_t length = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &length);
    if (status != XXWIDGETS_OK) { report_ui_error("Read file path", status); return; }
    path = malloc(length ? length : 1);
    if (!path) { report_ui_error("Read file path", XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, length ? length : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to calculate hashes."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) {
        NSString *expanded = [string(path) stringByExpandingTildeInPath];
        status = gui_hash_show(g_window, [expanded UTF8String]);
    }
    free(path);
    if (status != XXWIDGETS_OK) report_ui_error("Hash", status);
}

static void do_strings(void)
{
    size_t length = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &length);
    if (status != XXWIDGETS_OK) { report_ui_error("Read file path", status); return; }
    path = malloc(length ? length : 1);
    if (!path) { report_ui_error("Read file path", XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, length ? length : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to extract strings."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) {
        NSString *expanded = [string(path) stringByExpandingTildeInPath];
        status = gui_strings_show(g_window, [expanded UTF8String]);
    }
    free(path);
    if (status != XXWIDGETS_OK) report_ui_error("Strings", status);
}

static void do_hex_view(void)
{
    size_t length = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &length);
    if (status != XXWIDGETS_OK) { report_ui_error("Read file path", status); return; }
    path = malloc(length ? length : 1);
    if (!path) { report_ui_error("Read file path", XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, length ? length : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to view its bytes."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) {
        NSString *expanded = [string(path) stringByExpandingTildeInPath];
        status = gui_hex_view_show(g_window, [expanded UTF8String]);
    }
    free(path);
    if (status != XXWIDGETS_OK) report_ui_error("Hex View", status);
}

static void do_disasm(void)
{
    size_t length = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &length);
    if (status != XXWIDGETS_OK) { report_ui_error("Read file path", status); return; }
    path = malloc(length ? length : 1);
    if (!path) { report_ui_error("Read file path", XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, length ? length : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to disassemble."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) {
        NSString *expanded = [string(path) stringByExpandingTildeInPath];
        status = gui_disasm_show(g_window, [expanded UTF8String]);
    }
    free(path);
    if (status != XXWIDGETS_OK) report_ui_error("Disasm", status);
}

static void do_memory_map(void)
{
    size_t length = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &length);
    if (status != XXWIDGETS_OK) { report_ui_error("Read file path", status); return; }
    path = malloc(length ? length : 1);
    if (!path) { report_ui_error("Read file path", XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, length ? length : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to map."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) {
        NSString *expanded = [string(path) stringByExpandingTildeInPath];
        status = gui_memory_map_show(g_window, [expanded UTF8String]);
    }
    free(path);
    if (status != XXWIDGETS_OK) report_ui_error("Memory Map", status);
}

static void do_imports(void)
{
    size_t length = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &length);
    if (status != XXWIDGETS_OK) { report_ui_error("Read file path", status); return; }
    path = malloc(length ? length : 1);
    if (!path) { report_ui_error("Read file path", XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, length ? length : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to inspect imports."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) {
        NSString *expanded = [string(path) stringByExpandingTildeInPath];
        status = gui_imports_show(g_window, [expanded UTF8String]);
    }
    free(path);
    if (status != XXWIDGETS_OK) report_ui_error("Imports", status);
}

static void do_exports(void)
{
    size_t length = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &length);
    if (status != XXWIDGETS_OK) { report_ui_error("Read file path", status); return; }
    path = malloc(length ? length : 1);
    if (!path) { report_ui_error("Read file path", XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, length ? length : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to inspect exports."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) {
        NSString *expanded = [string(path) stringByExpandingTildeInPath];
        status = gui_exports_show(g_window, [expanded UTF8String]);
    }
    free(path);
    if (status != XXWIDGETS_OK) report_ui_error("Exports", status);
}

static void do_resources(void)
{
    size_t length = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &length);
    if (status != XXWIDGETS_OK) { report_ui_error("Read file path", status); return; }
    path = malloc(length ? length : 1);
    if (!path) { report_ui_error("Read file path", XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, length ? length : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to inspect resources."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) {
        NSString *expanded = [string(path) stringByExpandingTildeInPath];
        status = gui_resources_show(g_window, [expanded UTF8String]);
    }
    free(path);
    if (status != XXWIDGETS_OK) report_ui_error("Resources", status);
}

static void do_symbols(void)
{
    size_t length = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &length);
    if (status != XXWIDGETS_OK) { report_ui_error("Read file path", status); return; }
    path = malloc(length ? length : 1);
    if (!path) { report_ui_error("Read file path", XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, length ? length : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to inspect symbols."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) {
        NSString *expanded = [string(path) stringByExpandingTildeInPath];
        status = gui_symbols_show(g_window, [expanded UTF8String]);
    }
    free(path);
    if (status != XXWIDGETS_OK) report_ui_error("Symbols", status);
}

static void do_extractor(void)
{
    size_t length = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &length);
    if (status != XXWIDGETS_OK) { report_ui_error("Read file path", status); return; }
    path = malloc(length ? length : 1);
    if (!path) { report_ui_error("Read file path", XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, length ? length : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to extract embedded files."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) {
        NSString *expanded = [string(path) stringByExpandingTildeInPath];
        status = gui_extractor_show(g_window, [expanded UTF8String]);
    }
    free(path);
    if (status != XXWIDGETS_OK) report_ui_error("Extractor", status);
}

static xxwidgets_status browse_database(xxwidgets_widget *dialog, const char *initial, char **path, int *accepted, void *user)
{
    NSOpenPanel *picker = [NSOpenPanel openPanel];
    (void)dialog; (void)user;
    *path = NULL; *accepted = 0;
    [picker setCanChooseDirectories:YES]; [picker setCanChooseFiles:NO];
    [picker setTitle:cdie_ns_tr(@"Choose a signature database folder")];
    if (initial && initial[0]) [picker setDirectoryURL:[NSURL fileURLWithPath:string(initial)]];
    if ([picker runModal] != NSModalResponseOK) return XXWIDGETS_OK;
    const char *chosen = [[[picker URL] path] UTF8String];
    if (!chosen) return XXWIDGETS_INVALID_ARGUMENT;
    /* The form releases the chosen path with free(). */
    *path = strdup(chosen);
    if (!*path) return XXWIDGETS_OUT_OF_MEMORY;
    *accepted = 1;
    return XXWIDGETS_OK;
}

/* The frontend's share of the Options settings, as currently applied. */
static xxwidgets_status current_options(gui_options_values *values)
{
    xxwidgets_status status;
    gui_frontend_get_preferences(values);
    status = xxwidgets_scan_panel_get_flags(g_panel, &values->scan.options.flags);
    if (status == XXWIDGETS_OK) status = xxwidgets_scan_panel_get_databases(g_panel, &values->scan.options.databases);
    if (status == XXWIDGETS_OK) status = xxwidgets_app_get_font_options(g_app, &values->fonts);
    values->scan.modes = gui_backend_get_scan_modes();
    gui_hex_view_get_options(&values->hex);
    return status;
}

/* Applies settings from Options or from the store; fonts only when changed. */
static xxwidgets_status apply_options(const gui_options_values *values)
{
    xxwidgets_font_options fonts;
    xxwidgets_status status;
    gui_backend_set_scan_modes(values->scan.modes);
    status = xxwidgets_scan_panel_set_flags(g_panel, values->scan.options.flags);
    if (status == XXWIDGETS_OK) status = xxwidgets_scan_panel_set_databases(g_panel, values->scan.options.databases);
    gui_hex_view_set_options(&values->hex);
    if (status == XXWIDGETS_OK) status = xxwidgets_app_get_font_options(g_app, &fonts);
    if (status == XXWIDGETS_OK && memcmp(&values->fonts, &fonts, sizeof(fonts))) {
        status = xxwidgets_app_set_font_options(g_app, &values->fonts);
        if (status == XXWIDGETS_OK) layout();
    }
    if (status == XXWIDGETS_OK) status = gui_frontend_apply_preferences(g_app, values);
    if (status == XXWIDGETS_OK) [(NSWindow *)xxwidgets_widget_native_handle(g_window) setLevel:values->appearance.stay_on_top ? NSFloatingWindowLevel : NSNormalWindowLevel];
    return status;
}

/* The shared Options window; unsupported platform integrations are disabled. */
static void do_options(gui_options_page_id page)
{
    gui_options_host host = {(1u << GUI_OPTIONS_PAGE_COUNT) - 1u, NULL, browse_database, NULL};
    gui_options_values values;
    int accepted = 0, reloaded = 0;
    xxwidgets_status status = current_options(&values);
    g_options_page = page;
    if (status == XXWIDGETS_OK) status = gui_options_dialog(g_window, &host, &g_options_page, &values, &accepted, &reloaded);
    if (status != XXWIDGETS_OK) { report_ui_error("Options", status); return; }
    if (reloaded) {
        status = refresh_panel_tags();
        if (status != XXWIDGETS_OK) { report_ui_error("Refresh signature tags", status); return; }
        ready();
    }
    if (!accepted) return;
    status = apply_options(&values);
    if (status != XXWIDGETS_OK) report_ui_error("Apply options", status);
    else if (!gui_settings_save(g_settings, &values, g_options_page))
        xxwidgets_widget_set_text(g_status, CDIE_TR("Options applied, but they could not be saved for the next start."));
    else if (!reloaded) xxwidgets_widget_set_text(g_status, CDIE_TR("Options applied."));
}

static void do_about(void)
{
    xxwidgets_about_dialog *about = NULL;
    xxwidgets_status status = gui_about_create(cdie_about_png, cdie_about_png_size, &about);
    if (status == XXWIDGETS_OK) status = xxwidgets_about_dialog_show(about, g_window);
    if (about) xxwidgets_about_dialog_destroy(about);
    if (status != XXWIDGETS_OK) report_ui_error("About", status);
}

@interface CDIEApplication : NSObject <NSApplicationDelegate>
- (void)action:(id)sender;
@end
@implementation CDIEApplication
- (void)action:(id)sender { g_pending = (int)[sender tag]; }
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
    (void)sender;
    g_running = 0;
    xxwidgets_app_quit(g_app, 0);
    if ([NSApp modalWindow]) [NSApp abortModal];
    return NSTerminateCancel;
}
- (BOOL)application:(NSApplication *)sender openFile:(NSString *)filename
{ (void)sender; open_path(filename); return YES; }
@end

static void install_menu(CDIEApplication *delegate)
{
    NSMenu *menu = [NSApp mainMenu];
    NSMenu *application = [[menu itemAtIndex:0] submenu];
    NSMenuItem *about = [[[NSMenuItem alloc] initWithTitle:cdie_ns_tr(@"About cdie") action:@selector(action:) keyEquivalent:@""] autorelease];
    [about setTarget:delegate]; [about setTag:ACTION_ABOUT]; [application insertItem:about atIndex:0];
    NSMenuItem *settings = [[[NSMenuItem alloc] initWithTitle:cdie_ns_tr(@"SettingsÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦") action:@selector(action:) keyEquivalent:@","] autorelease];
    [settings setTarget:delegate]; [settings setTag:ACTION_OPTIONS]; [application insertItem:settings atIndex:1];
    NSMenuItem *file = [[[NSMenuItem alloc] initWithTitle:cdie_ns_tr(@"File") action:NULL keyEquivalent:@""] autorelease];
    NSMenu *submenu = [[[NSMenu alloc] initWithTitle:cdie_ns_tr(@"File")] autorelease];
    NSArray *titles = @[cdie_ns_tr(@"OpenÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦"), cdie_ns_tr(@"Scan"), cdie_ns_tr(@"Report"), cdie_ns_tr(@"File Information"), cdie_ns_tr(@"Entropy"), cdie_ns_tr(@"Hash"), cdie_ns_tr(@"Strings"), cdie_ns_tr(@"Hex View"), cdie_ns_tr(@"Disasm"), cdie_ns_tr(@"Memory Map"), cdie_ns_tr(@"Imports"), cdie_ns_tr(@"Exports"), cdie_ns_tr(@"Resources"), cdie_ns_tr(@"Symbols"), cdie_ns_tr(@"Extractor")];
    NSArray *keys = @[@"o", @"r", @"R", @"i", @"e", @"h", @"s", @"x", @"a", @"m", @"i", @"e", @"u", @"y", @"t"];
    int actions[] = {ACTION_OPEN, ACTION_SCAN, ACTION_REPORT, ACTION_FILE_INFORMATION, ACTION_ENTROPY, ACTION_HASH, ACTION_STRINGS, ACTION_HEX_VIEW, ACTION_DISASM, ACTION_MEMORY_MAP, ACTION_IMPORTS, ACTION_EXPORTS, ACTION_RESOURCES, ACTION_SYMBOLS, ACTION_EXTRACTOR};
    for (NSUInteger i = 0; i < [titles count]; ++i) {
        NSMenuItem *item = [submenu addItemWithTitle:titles[i] action:@selector(action:) keyEquivalent:keys[i]];
        if (actions[i] == ACTION_HASH || actions[i] == ACTION_STRINGS || actions[i] == ACTION_HEX_VIEW || actions[i] == ACTION_DISASM || actions[i] == ACTION_MEMORY_MAP || actions[i] == ACTION_IMPORTS || actions[i] == ACTION_EXPORTS || actions[i] == ACTION_RESOURCES || actions[i] == ACTION_SYMBOLS || actions[i] == ACTION_EXTRACTOR)
            [item setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagShift];
        [item setTarget:delegate]; [item setTag:actions[i]];
    }
    NSMenuItem *recent = [submenu addItemWithTitle:cdie_ns_tr(@"Recent filesÃ¢â‚¬Â¦") action:@selector(action:) keyEquivalent:@"r"];
    [recent setTarget:delegate]; [recent setTag:ACTION_RECENT];
    for (NSMenuItem *item in [submenu itemArray]) if ([item tag] == ACTION_SCAN) [item setKeyEquivalent:@""];
    [file setSubmenu:submenu]; [menu insertItem:file atIndex:1];
}

static int create_controls(void)
{
    if (xxwidgets_widget_create(g_app, g_window, XXWIDGETS_EDIT, "", (xxwidgets_rect){2, 1, 75, 2}, &g_path) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("OpenÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦"), (xxwidgets_rect){79, 1, 11, 2}, &g_browse) != XXWIDGETS_OK ||
        xxwidgets_scan_panel_create(g_window, (xxwidgets_rect){2, 4, 88, 21}, &g_panel) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("OptionsÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦"), (xxwidgets_rect){2, 26, 16, 2}, &g_options) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("File InformationÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦"), (xxwidgets_rect){2, 29, 26, 2}, &g_file_information) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("EntropyÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦"), (xxwidgets_rect){30, 29, 16, 2}, &g_entropy) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("HashÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦"), (xxwidgets_rect){48, 29, 14, 2}, &g_hash) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("StringsÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦"), (xxwidgets_rect){64, 29, 18, 2}, &g_strings) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("Hex ViewÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦"), (xxwidgets_rect){84, 29, 18, 2}, &g_hex_view) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("DisasmÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦"), (xxwidgets_rect){104, 29, 18, 2}, &g_disasm) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("Memory MapÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦"), (xxwidgets_rect){124, 29, 20, 2}, &g_memory_map) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("ImportsÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦"), (xxwidgets_rect){60, 26, 18, 2}, &g_imports) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("ExportsÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦"), (xxwidgets_rect){80, 26, 18, 2}, &g_exports) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("ResourcesÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦"), (xxwidgets_rect){100, 26, 22, 2}, &g_resources) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("SymbolsÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦"), (xxwidgets_rect){124, 26, 18, 2}, &g_symbols) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("Extractor"), (xxwidgets_rect){104, 26, 22, 2}, &g_extractor) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_LABEL, CDIE_TR("Loading databasesÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦"), (xxwidgets_rect){2, 29, 88, 1}, &g_status) != XXWIDGETS_OK) return 0;
    NSTextField *field = xxwidgets_widget_native_handle(g_path);
    [field setPlaceholderString:cdie_ns_tr(@"Choose or drop a file to scan")];
    [field setAccessibilityLabel:cdie_ns_tr(@"File to scan")];
    xxwidgets_widget_set_enabled(control(XXWIDGETS_SCAN_PANEL_FILE_TYPE), 0);
    NSButton *scan = xxwidgets_widget_native_handle(control(XXWIDGETS_SCAN_PANEL_SCAN));
    [scan setKeyEquivalent:@"\r"];
    const xxwidgets_shortcut shortcuts[] = {{"F5", ACTION_SCAN}, {"Cmd+O", ACTION_OPEN}, {"Cmd+R", ACTION_RECENT}, {"Cmd+Shift+R", ACTION_REPORT}};
    return xxwidgets_window_set_shortcuts(g_window, shortcuts, sizeof(shortcuts) / sizeof(shortcuts[0])) == XXWIDGETS_OK;
}

/* Run the actual Cocoa controls and backend against a supplied file. This also
 * covers bundle database lookup when launched from an unrelated directory. */
static int smoke_test(NSString *path)
{
    gui_backend_optimization_options saved, changed, actual;
    gui_backend_optimization_capabilities capabilities;
    if (!gui_backend_get_optimization_options(&saved) ||
        !gui_backend_get_optimization_capabilities(&capabilities)) return 9;
    changed = saved;
    changed.use_arm_pmull = 0;
    if (!gui_backend_set_optimization_options(&changed) ||
        !gui_backend_get_optimization_options(&actual) || actual.use_arm_pmull) return 10;
    changed.use_arm_pmull = 1;
    if (!gui_backend_set_optimization_options(&changed) ||
        !gui_backend_get_optimization_options(&actual) || actual.use_arm_pmull != capabilities.arm_pmull) return 11;
    changed.use_arm_pmull = 2;
    if (gui_backend_set_optimization_options(&changed) || !gui_backend_set_optimization_options(&saved)) return 12;
    if (!gui_backend_signature_count()) { fprintf(stderr, "No bundled signatures loaded\n"); return 1; }
    {
        xxwidgets_widget *include_combo = control(XXWIDGETS_SCAN_PANEL_INCLUDE_TAGS);
        xxwidgets_widget *exclude_combo = control(XXWIDGETS_SCAN_PANEL_EXCLUDE_TAGS);
        size_t include_size = 0, exclude_size = 0;
        if (xxwidgets_widget_kind(include_combo) != XXWIDGETS_CHECKCOMBOBOX ||
            xxwidgets_widget_kind(exclude_combo) != XXWIDGETS_CHECKCOMBOBOX ||
            !xxwidgets_combobox_count(include_combo) ||
            xxwidgets_combobox_count(include_combo) != xxwidgets_combobox_count(exclude_combo) ||
            xxwidgets_checkcombobox_set_checked(include_combo, 0, 1) != XXWIDGETS_OK ||
            xxwidgets_checkcombobox_set_checked(exclude_combo, xxwidgets_combobox_count(exclude_combo) - 1, 1) != XXWIDGETS_OK ||
            xxwidgets_scan_panel_get_include_tags(g_panel, NULL, 0, &include_size) != XXWIDGETS_OK || include_size <= 1 ||
            xxwidgets_scan_panel_get_exclude_tags(g_panel, NULL, 0, &exclude_size) != XXWIDGETS_OK || exclude_size <= 1 ||
            apply_panel_tag_filters() != XXWIDGETS_OK ||
            xxwidgets_checkcombobox_set_checked(include_combo, 0, 0) != XXWIDGETS_OK ||
            xxwidgets_checkcombobox_set_checked(exclude_combo, xxwidgets_combobox_count(exclude_combo) - 1, 0) != XXWIDGETS_OK ||
            apply_panel_tag_filters() != XXWIDGETS_OK) return 14;
    }
    const char *dropped_paths[] = {[path UTF8String], "/unused/second-file"};
    xxwidgets_event drop = {0};
    drop.type = XXWIDGETS_EVENT_DROP_FILES; drop.widget = g_window;
    drop.paths = dropped_paths; drop.path_count = 2;
    g_pending = ACTION_NONE;
    on_event(g_app, &drop, NULL);
    size_t drop_length = strlen(dropped_paths[0]) + 1;
    char *drop_text = malloc(drop_length);
    int drop_ok = drop_text && g_pending == ACTION_SCAN &&
        xxwidgets_widget_get_text(g_path, drop_text, drop_length, NULL) == XXWIDGETS_OK && !strcmp(drop_text, dropped_paths[0]);
    free(drop_text);
    if (!drop_ok) return 13;
    g_pending = ACTION_NONE;
    if (!do_scan()) return 2;
    size_t length = 0;
    if (xxwidgets_scan_panel_get_report(g_panel, NULL, 0, &length) != XXWIDGETS_OK || length <= 1 ||
        xxwidgets_treeview_count(control(XXWIDGETS_SCAN_PANEL_RESULTS)) == 0) return 3;
    xxwidgets_widget *combo = control(XXWIDGETS_SCAN_PANEL_FILE_TYPE);
    int found = 0;
    for (size_t i = 0; i < xxwidgets_combobox_count(combo); ++i) {
        const xx_meta_string *record;
        if (xxwidgets_combobox_get_record(combo, i, &record) == XXWIDGETS_OK &&
            wcscmp(record->meta_string->data, L"Binary") == 0) {
            xxwidgets_widget_set_value(combo, (int)i);
            if (!do_scan()) return 4;
            char status[512];
            xxwidgets_widget_get_text(g_status, status, sizeof(status), NULL);
            if (!strstr(status, "(Binary)")) return 5;
            found = 1; break;
        }
    }
    if (!found) return 6;
    open_path(@"/nonexistent/cdie-smoke-test-file"); g_pending = ACTION_NONE;
    if (do_scan() || xxwidgets_scan_panel_count(g_panel)) return 7;
    open_path(path); g_pending = ACTION_NONE;
    if (!do_scan() || xxwidgets_app_poll(g_app, 0) != XXWIDGETS_OK) return 8;
    {
        gui_file_information_data *data = NULL;
        xxwidgets_file_information *information = NULL;
        char *error = NULL;
        int ok = gui_file_information_read([path UTF8String], NULL, &data, &error);
        if (ok) ok = xxwidgets_file_information_create(g_window, (xxwidgets_rect){1, 1, 88, 20}, &information) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_file_information_set_data(information, data->path, data->file_type, data->size, data->items, data->count) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_data_struct_view_count(xxwidgets_file_information_view(information)) == data->count && data->count > 0;
        if (information && xxwidgets_file_information_destroy(information) != XXWIDGETS_OK) ok = 0;
        gui_file_information_free(data); gui_backend_free(error);
        if (!ok) return 15;
    }
    {
        cdie_entropy_data *data = NULL;
        xxwidgets_entropy *view = NULL;
        char *error = NULL;
        int ok = cdie_entropy_read([path UTF8String], NULL, &data, &error);
        if (ok) ok = xxwidgets_entropy_create(g_window, (xxwidgets_rect){1, 1, 100, 27}, &view) == XXWIDGETS_OK;
        if (ok) ok = gui_entropy_set_data(view, data) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_entropy_region_count(view) == data->region_count && data->region_count > 0;
        if (view && xxwidgets_entropy_destroy(view) != XXWIDGETS_OK) ok = 0;
        gui_backend_free(data); gui_backend_free(error);
        if (!ok) return 1;
    }
    {
        cdie_hash_data *data = NULL;
        xxwidgets_hash *view = NULL;
        char *error = NULL;
        int ok = cdie_hash_read([path UTF8String], NULL, &data, &error);
        if (ok) ok = xxwidgets_hash_create(g_window, (xxwidgets_rect){1, 1, 100, 20}, &view) == XXWIDGETS_OK;
        if (ok) ok = gui_hash_set_data(view, [path UTF8String], data) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_hash_count(view) == CDIE_HASH_COUNT;
        if (view && xxwidgets_hash_destroy(view) != XXWIDGETS_OK) ok = 0;
        gui_backend_free(data); gui_backend_free(error);
        if (!ok) return 16;
    }
    {
        xxwidgets_strings *view = NULL;
        xxwidgets_string_record record = {0, 18, 18, "ASCII", "CDIE Strings smoke", NULL, 0, 0};
        int ok = xxwidgets_strings_create(g_window, (xxwidgets_rect){1, 1, 100, 20}, &view) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_strings_set_records(view, &record, 1) == XXWIDGETS_OK && xxwidgets_strings_count(view) == 1;
        if (view && xxwidgets_strings_destroy(view) != XXWIDGETS_OK) ok = 0;
        if (!ok) return 17;
    }
    {
        xx_io_device *device = NULL;
        xxwidgets_hexview_browser *view = NULL;
        char *error = NULL;
        int ok = gui_hex_view_open([path UTF8String], &device, &error);
        if (ok) ok = xxwidgets_hexview_browser_create(g_window, (xxwidgets_rect){1, 1, 106, 24}, &view) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_hexview_browser_set_device(view, [path UTF8String], device) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_hexview_size(xxwidgets_hexview_browser_control(view, XXWIDGETS_HEX_BROWSER_HEX)) > 0;
        if (view && xxwidgets_hexview_browser_destroy(view) != XXWIDGETS_OK) ok = 0;
        if (device) xx_io_close(device);
        gui_backend_free(error);
        if (!ok) return 18;
    }
    {
        cdie_disasm_context *context = NULL;
        gui_disasm_source source;
        xxwidgets_disasm_browser *view = NULL;
        char *error = NULL;
        int ok = cdie_disasm_open([path UTF8String], NULL, &context, &error);
        if (ok) {
            gui_disasm_source_init(&source, context, [path UTF8String]);
            ok = xxwidgets_disasm_browser_create(g_window, (xxwidgets_rect){1,1,106,24}, &source.source, &view) == XXWIDGETS_OK;
        }
        if (ok) ok = xxwidgets_disasm_browser_count(view) > 0;
        if (view && xxwidgets_disasm_browser_destroy(view) != XXWIDGETS_OK) ok = 0;
        cdie_disasm_close(context); gui_backend_free(error);
        if (!ok) return 19;
    }
    {
        cdie_memory_map_data *data = NULL;
        xxwidgets_memory_map *view = NULL;
        char *error = NULL;
        int ok = cdie_memory_map_read([path UTF8String], NULL, &data, &error);
        if (ok) ok = xxwidgets_memory_map_create(g_window, (xxwidgets_rect){1,1,106,24}, &view) == XXWIDGETS_OK;
        if (ok) ok = gui_memory_map_set_data(view, [path UTF8String], data) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_memory_map_table_count(xxwidgets_memory_map_control(view, XXWIDGETS_MEMORY_MAP_TABLE)) > 0;
        if (view && xxwidgets_memory_map_destroy(view) != XXWIDGETS_OK) ok = 0;
        gui_backend_free(data); gui_backend_free(error);
        if (!ok) return 20;
    }
    {
        cdie_imports_data *data = NULL;
        xxwidgets_imports *view = NULL;
        char *error = NULL;
        int ok = cdie_imports_read([path UTF8String], NULL, &data, &error);
        if (ok) ok = xxwidgets_imports_create(g_window, (xxwidgets_rect){1,1,106,24}, &view) == XXWIDGETS_OK;
        if (ok) ok = gui_imports_set_data(view, [path UTF8String], data) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_imports_visible_count(view) > 0;
        if (view && xxwidgets_imports_destroy(view) != XXWIDGETS_OK) ok = 0;
        cdie_imports_free(data); gui_backend_free(error);
        if (!ok) return 21;
    }
    {
        xx_export_record record = {"CDIEExport", NULL, 7, 0, UINT64_C(0x100001000)};
        xxwidgets_exports_data snapshot = {"synthetic fixture", "Mach-O", 16, &record, 1};
        xxwidgets_exports *view = NULL;
        int ok = xxwidgets_exports_create(g_window, (xxwidgets_rect){1,1,106,24}, &view) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_exports_set_data(view, &snapshot) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_exports_visible_count(view) == 1;
        if (view && xxwidgets_exports_destroy(view) != XXWIDGETS_OK) ok = 0;
        if (!ok) return 22;
    }
    {
        xx_resource_record record = {10, 1, 1033, NULL, NULL, NULL, 0, 16, 1200};
        xxwidgets_resources_data snapshot = {"synthetic fixture", "PE resource fixture", 16, &record, 1};
        xxwidgets_resources *view = NULL;
        int ok = xxwidgets_resources_create(g_window, (xxwidgets_rect){1,1,106,24}, &view) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_resources_set_data(view, &snapshot) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_resources_visible_count(view) == 1;
        if (view && xxwidgets_resources_destroy(view) != XXWIDGETS_OK) ok = 0;
        if (!ok) return 23;
    }
    {
        xx_symbol_record record = {0};
        xxwidgets_symbols_data snapshot;
        xxwidgets_symbols *view = NULL;
        record.name = "CDIESymbol"; record.file_name = "test.c"; record.kind = XX_SYMBOL_KIND_FUNCTION;
        record.binding = XX_SYMBOL_BINDING_GLOBAL; record.address = UINT64_C(0x100001000);
        snapshot = (xxwidgets_symbols_data){"synthetic fixture", "Mach-O", 16, &record, 1};
        int ok = xxwidgets_symbols_create(g_window, (xxwidgets_rect){1,1,106,24}, &view) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_symbols_set_data(view, &snapshot) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_symbols_visible_count(view) == 1;
        if (view && xxwidgets_symbols_destroy(view) != XXWIDGETS_OK) ok = 0;
        if (!ok) return 24;
    }
    printf("Cocoa GUI smoke test passed: %d signatures, file drop, scan, type selection, report, missing-file recovery, File Information, Entropy, Hash, Strings, Hex View, Disasm, Memory Map, Imports, Exports, Resources, Symbols\n",
        gui_backend_signature_count());
    return 0;
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        const char *translation=NULL;
        if (!cdie_translation_arguments(&argc,argv,&translation)) { fprintf(stderr,"--translation requires a PO file\n"); return 1; }
        BOOL smoke=NO, positional=NO;
        NSString *startup=nil;
        for (int i=1;i<argc;++i) {
            if (!positional && !strcmp(argv[i],"--")) { positional=YES; continue; }
            if (!positional && !strcmp(argv[i],"--smoke-test")) { smoke=YES; continue; }
            if (!positional && !strncmp(argv[i],"-psn_",5)) continue;
            if (startup) { fprintf(stderr,"Only one initial file is supported.\n"); return 1; }
            startup=string(argv[i]);
        }
        xxwidgets_config config = {XXWIDGETS_BACKEND_NATIVE, on_event, NULL};
        xxwidgets_status status = xxwidgets_app_create(&config, &g_app);
        if (status != XXWIDGETS_OK) { fprintf(stderr, "%s\n", xxwidgets_status_string(status)); return 1; }
        int result = 0;
        CDIEApplication *delegate = nil;
        size_t translation_line=0;
        char *stored_translation = !translation && !getenv("CDIE_TRANSLATION") ? gui_frontend_stored_language(smoke) : NULL;
        status=cdie_translation_open(g_app,translation ? translation : stored_translation,&translation_line);
        if (status != XXWIDGETS_OK && stored_translation) {
            fprintf(stderr,"Saved translation cannot be loaded; using System language.\n");
            status = cdie_translation_open(g_app,NULL,&translation_line);
        }
        free(stored_translation);
        if (status != XXWIDGETS_OK) { fprintf(stderr,"Cannot load translation (line %zu)\n",translation_line); result=1; goto finish; }
        if (xxwidgets_widget_create(g_app, NULL, XXWIDGETS_WINDOW, gui_backend_app_title(),
                (xxwidgets_rect){4, 3, 146, 34}, &g_window) != XXWIDGETS_OK || !create_controls()) {
            result = 1; goto finish;
        }
        xxwidgets_window_set_minimum_size(g_window, 146, 28);
        if (xxwidgets_window_set_accept_file_drops(g_window, 1) != XXWIDGETS_OK) { result = 1; goto finish; }
        NSWindow *window = xxwidgets_widget_native_handle(g_window);
        delegate = [[CDIEApplication alloc] init];
        [NSApp setDelegate:delegate];
        install_menu(delegate);
        gui_backend_init();
        if (!smoke) {
            /* Folders saved by earlier versions; the shared settings below win. */
            NSArray *paths = [[NSUserDefaults standardUserDefaults] arrayForKey:@"DatabasePaths"];
            if ([paths count] == 3 && [paths[0] isKindOfClass:[NSString class]] &&
                [paths[1] isKindOfClass:[NSString class]] && [paths[2] isKindOfClass:[NSString class]])
                gui_backend_set_db_paths([paths[0] UTF8String], [paths[1] UTF8String], [paths[2] UTF8String]);
        }
        int settings_read = 1, settings_databases = 1;
        {
            /* Saved Options apply before the first scan; smoke tests keep them in memory. */
            gui_options_values values;
            g_settings = gui_settings_open(smoke, &settings_read);
            status = current_options(&values);
            if (status == XXWIDGETS_OK) settings_databases = gui_settings_load(g_settings, &values, &g_options_page);
            if (status == XXWIDGETS_OK) status = apply_options(&values);
            if (status != XXWIDGETS_OK) { report_ui_error("Apply saved options", status); result = 1; goto finish; }
        }
        status = refresh_panel_tags();
        if (status != XXWIDGETS_OK) { report_ui_error("Load signature tags", status); result = 1; goto finish; }
        ready(); layout();
        if (!settings_databases) xxwidgets_widget_set_text(g_status, CDIE_TR("The saved signature folders cannot be loaded; the default ones are active."));
        else if (!settings_read) xxwidgets_widget_set_text(g_status, CDIE_TR("Saved options cannot be read; the defaults are used."));
        if (smoke) { result = smoke_test(startup ?: [[NSBundle mainBundle] executablePath]); goto finish; }
        {
            int instance=gui_frontend_instance_start(g_window,startup?[startup UTF8String]:NULL);
            if(instance<=0){if(instance<0){fprintf(stderr,"Cannot acquire application instance lock.\n");result=1;}goto finish;}
        }
        [window center]; [window makeKeyAndOrderFront:nil]; [NSApp activateIgnoringOtherApps:YES];
        xxwidgets_widget_focus(g_path);
        if (startup) open_path(startup);
        while (g_running) {
            @autoreleasepool {
                status = xxwidgets_app_poll(g_app, 30);
                if (status != XXWIDGETS_OK) { result = 1; break; }
                if (!g_running) break;
                if (g_layout) { g_layout = 0; layout(); }
                const char *message = gui_tree_process(g_window);
                if (message) xxwidgets_widget_set_text(g_status, message);
                int action = g_pending; g_pending = ACTION_NONE;
                switch (action) {
                case ACTION_SCAN: do_scan(); break;
                case ACTION_OPEN: do_open(); break;
                case ACTION_REPORT:
                    status = xxwidgets_scan_panel_show_report(g_panel);
                    if (status != XXWIDGETS_OK) report_ui_error("Scan report", status);
                    break;
                case ACTION_OPTIONS: do_options(g_options_page); break;
                case ACTION_DATABASES: do_options(GUI_OPTIONS_SCAN); break;
                case ACTION_RECENT: {
                    char recent[GUI_PREFERENCE_PATH_MAX];int accepted=0;
                    status=gui_frontend_recent_dialog(g_window,recent,sizeof(recent),&accepted);
                    if(status==XXWIDGETS_OK&&accepted)open_path(string(recent));
                    else if(status!=XXWIDGETS_OK)report_ui_error("Recent files",status);
                    break;
                }
                case ACTION_OPTIMIZATION: do_options(GUI_OPTIONS_OPTIMIZATION); break;
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
                case ACTION_EXTRACTOR: do_extractor(); break;
                case ACTION_ABOUT: do_about(); break;
                default: break;
                }
            }
        }
finish:
        if (!smoke && g_settings && g_window) {
            gui_options_values values;
            if(current_options(&values)==XXWIDGETS_OK)(void)gui_settings_save(g_settings,&values,g_options_page);
        }
        gui_frontend_instance_close();
        gui_settings_close(g_settings);
        gui_tree_reset();
        gui_backend_shutdown();
        [g_type_path release]; g_type_path = nil;
        if (g_panel) xxwidgets_scan_panel_destroy(g_panel);
        (cdie_translation_close(), xxwidgets_app_destroy(g_app));
        [delegate release];
        return result;
    }
}
