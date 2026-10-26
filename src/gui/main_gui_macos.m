/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Cocoa application integration around the shared xxwidgets scan panel.
 * All engine calls and widget updates run on the main thread.
 */
#import <AppKit/AppKit.h>
#include <xxwidgets/xxwidgets.h>
#include <xxwidgets/xxwidgets_scan_panel.h>
#include <xxwidgets/xxwidgets_combobox.h>
#include <xxwidgets/xxwidgets_scan_options.h>
#include <xxwidgets/xxwidgets_optimization_options.h>
#include <xxfclib/strings/xx_string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "gui_backend.h"
#include "gui_tree.h"

enum { ACTION_NONE, ACTION_SCAN, ACTION_OPEN, ACTION_REPORT, ACTION_OPTIONS,
       ACTION_DATABASES, ACTION_OPTIMIZATION, ACTION_ABOUT };
static xxwidgets_app *g_app;
static xxwidgets_widget *g_window, *g_path, *g_browse, *g_status, *g_options, *g_databases, *g_optimization;
static xxwidgets_scan_panel *g_panel;
static int g_running = 1, g_pending, g_layout;
static NSString *g_type_path;

static NSString *string(const char *text)
{
    return text ? ([NSString stringWithUTF8String:text] ?: @"") : @"";
}
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
    NSString *message = [NSString stringWithFormat:@"%s: %s", operation, xxwidgets_status_string(status)];
    xxwidgets_widget_set_text(g_status, [message UTF8String]);
}
static void ready(void)
{
    NSString *message = gui_backend_signature_count() ?
        [NSString stringWithFormat:@"Ready. %d signatures loaded. Choose a file or drop one into the window.", gui_backend_signature_count()] :
        @"No signature database loaded. Choose its folder using Database paths.";
    xxwidgets_widget_set_text(g_status, [message UTF8String]);
}
static void open_path(NSString *path)
{
    xxwidgets_widget_set_text(g_path, [path UTF8String]);
    g_pending = ACTION_SCAN;
}
static void layout(void)
{
    xxwidgets_rect bounds;
    if (!g_panel || xxwidgets_widget_get_rect(g_window, &bounds) != XXWIDGETS_OK) return;
    int width = bounds.width, height = bounds.height;
    xxwidgets_widget_set_rect(g_path, (xxwidgets_rect){2, 1, width - 17, 2});
    xxwidgets_widget_set_rect(g_browse, (xxwidgets_rect){width - 13, 1, 11, 2});
    xxwidgets_scan_panel_set_rect(g_panel, (xxwidgets_rect){2, 4, width - 4, height - 10});
    xxwidgets_widget_set_rect(g_options, (xxwidgets_rect){2, height - 5, 16, 2});
    xxwidgets_widget_set_rect(g_databases, (xxwidgets_rect){20, height - 5, 18, 2});
    xxwidgets_widget_set_rect(g_optimization, (xxwidgets_rect){40, height - 5, 18, 2});
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
    else if (event->type == XXWIDGETS_EVENT_CLICK) {
        if (event->widget == g_browse) g_pending = ACTION_OPEN;
        else if (event->widget == control(XXWIDGETS_SCAN_PANEL_SCAN)) g_pending = ACTION_SCAN;
        else if (event->widget == control(XXWIDGETS_SCAN_PANEL_REPORT)) g_pending = ACTION_REPORT;
        else if (event->widget == g_options) g_pending = ACTION_OPTIONS;
        else if (event->widget == g_databases) g_pending = ACTION_DATABASES;
        else if (event->widget == g_optimization) g_pending = ACTION_OPTIMIZATION;
    }
}
static void collect_type(int type, const char *name, void *user)
{
    [(NSMutableArray *)user addObject:@{@"id": @(type), @"name": string(name)}];
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
        xxwidgets_widget_set_text(g_status, "Cannot open this file. Check the path and permissions.");
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
    xxwidgets_widget_set_text(g_status, "Scanning…");
    xxwidgets_widget_set_enabled(g_window, 0);
    [NSApp updateWindows];
    NSTimeInterval started = [NSDate timeIntervalSinceReferenceDate];
    char *type = NULL;
    int cancelled = 0;
    int ok = gui_tree_scan(g_window, g_panel, [path UTF8String], (int)type_id.val.u64, &type, &cancelled);
    NSString *message = ok ? [NSString stringWithFormat:@"Scan complete (%@). %lu results · %.0f ms", string(type),
        (unsigned long)xxwidgets_scan_panel_count(g_panel), ([NSDate timeIntervalSinceReferenceDate] - started) * 1000] :
        cancelled ? @"Scan cancelled." : @"Scan failed.";
    xxwidgets_widget_set_text(g_status, [message UTF8String]);
    gui_backend_free(type);
    xxwidgets_widget_set_enabled(g_window, 1);
    xxwidgets_widget_set_enabled(combo, 1);
    return ok;
}
static void do_open(void)
{
    NSOpenPanel *picker = [NSOpenPanel openPanel];
    [picker setTitle:@"Choose a file to scan"];
    [picker setCanChooseDirectories:NO];
    [picker setAllowsMultipleSelection:NO];
    if ([picker runModal] == NSModalResponseOK) open_path([[picker URL] path]);
}
static void do_options(void)
{
    xxwidgets_scan_options options = {0};
    int accepted = 0;
    xxwidgets_scan_panel_get_flags(g_panel, &options.flags);
    xxwidgets_scan_panel_get_databases(g_panel, &options.databases);
    xxwidgets_status status = xxwidgets_scan_options_dialog(g_window, "cdie Scan options", &options, &accepted);
    if (status != XXWIDGETS_OK) report_ui_error("Scan options", status);
    else if (accepted) {
        xxwidgets_scan_panel_set_flags(g_panel, options.flags);
        xxwidgets_scan_panel_set_databases(g_panel, options.databases);
    }
}

@interface CDIEApplication : NSObject <NSApplicationDelegate>
- (void)action:(id)sender;
- (void)chooseDatabase:(id)sender;
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
- (void)chooseDatabase:(id)sender
{
    NSOpenPanel *picker = [NSOpenPanel openPanel];
    [picker setCanChooseDirectories:YES]; [picker setCanChooseFiles:NO];
    [picker setTitle:@"Choose a signature database folder"];
    if ([picker runModal] == NSModalResponseOK) {
        NSTextField *field = [[sender superview] viewWithTag:100 + [sender tag]];
        [field setStringValue:[[picker URL] path]];
    }
}
@end

static void do_databases(CDIEApplication *delegate)
{
    NSAlert *dialog = [[[NSAlert alloc] init] autorelease];
    [dialog setMessageText:@"Database paths"];
    [dialog setInformativeText:@"Choose db, db_extra and db_custom folders. Empty paths use the bundled databases."];
    [dialog addButtonWithTitle:@"Apply"]; [dialog addButtonWithTitle:@"Cancel"];
    NSView *form = [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, 560, 155)] autorelease];
    const char *paths[] = {gui_backend_main_db_path(), gui_backend_extra_db_path(), gui_backend_custom_db_path()};
    NSArray *names = @[@"Main", @"Extra", @"Custom"];
    NSMutableArray *fields = [NSMutableArray array];
    for (NSInteger i = 0; i < 3; ++i) {
        CGFloat y = 110 - i * 50;
        NSTextField *label = [NSTextField labelWithString:names[i]];
        [label setFrame:NSMakeRect(0, y + 26, 500, 18)]; [form addSubview:label];
        NSTextField *field = [[[NSTextField alloc] initWithFrame:NSMakeRect(0, y, 455, 24)] autorelease];
        [field setStringValue:string(paths[i])]; [field setTag:100 + i];
        [form addSubview:field]; [fields addObject:field];
        NSButton *browse = [NSButton buttonWithTitle:@"Choose…" target:delegate action:@selector(chooseDatabase:)];
        [browse setFrame:NSMakeRect(465, y - 2, 95, 28)]; [browse setTag:i]; [form addSubview:browse];
    }
    [dialog setAccessoryView:form];
    if ([dialog runModal] != NSAlertFirstButtonReturn) return;
    NSArray *values = @[[fields[0] stringValue], [fields[1] stringValue], [fields[2] stringValue]];
    if (!gui_backend_set_db_paths([values[0] UTF8String], [values[1] UTF8String], [values[2] UTF8String])) {
        xxwidgets_widget_set_text(g_status, "Database load failed. Previous databases are still active.");
        return;
    }
    [[NSUserDefaults standardUserDefaults] setObject:values forKey:@"DatabasePaths"];
    {
        xxwidgets_status status = refresh_panel_tags();
        if (status != XXWIDGETS_OK) {
            report_ui_error("Refresh signature tags", status);
            return;
        }
    }
    ready();
}
static void install_menu(CDIEApplication *delegate)
{
    NSMenu *menu = [NSApp mainMenu];
    NSMenu *application = [[menu itemAtIndex:0] submenu];
    NSMenuItem *about = [[[NSMenuItem alloc] initWithTitle:@"About cdie" action:@selector(action:) keyEquivalent:@""] autorelease];
    [about setTarget:delegate]; [about setTag:ACTION_ABOUT]; [application insertItem:about atIndex:0];
    NSMenuItem *file = [[[NSMenuItem alloc] initWithTitle:@"File" action:NULL keyEquivalent:@""] autorelease];
    NSMenu *submenu = [[[NSMenu alloc] initWithTitle:@"File"] autorelease];
    NSArray *titles = @[@"Open…", @"Scan", @"Report"];
    NSArray *keys = @[@"o", @"r", @"R"];
    int actions[] = {ACTION_OPEN, ACTION_SCAN, ACTION_REPORT};
    for (NSUInteger i = 0; i < [titles count]; ++i) {
        NSMenuItem *item = [submenu addItemWithTitle:titles[i] action:@selector(action:) keyEquivalent:keys[i]];
        [item setTarget:delegate]; [item setTag:actions[i]];
    }
    [file setSubmenu:submenu]; [menu insertItem:file atIndex:1];
}

static int do_optimization(void)
{
    gui_backend_optimization_options current;
    gui_backend_optimization_capabilities features;
    xxwidgets_optimization_options options;
    xxwidgets_optimization_capabilities capabilities;
    xxwidgets_status status;
    int accepted = 0;
    if (!gui_backend_get_optimization_options(&current) ||
        !gui_backend_get_optimization_capabilities(&features)) {
        report_ui_error("Read optimization options", XXWIDGETS_PLATFORM_ERROR); return -1;
    }
    options.buffer_size = current.buffer_size;
    options.file_buffer_size = current.file_buffer_size;
    options.use_sse2 = current.use_sse2; options.use_avx2 = current.use_avx2; options.use_sse42 = current.use_sse42; options.use_aes = current.use_aes; options.use_sha = current.use_sha; options.use_vpclmulqdq = current.use_vpclmulqdq;
    options.use_neon = current.use_neon; options.use_arm_crc32 = current.use_arm_crc32; options.use_arm_crypto = current.use_arm_crypto; options.use_arm_dotprod = current.use_arm_dotprod; options.use_arm_pmull = current.use_arm_pmull;
    capabilities.sse2 = features.sse2; capabilities.avx2 = features.avx2; capabilities.sse42 = features.sse42; capabilities.aes = features.aes; capabilities.sha = features.sha; capabilities.vpclmulqdq = features.vpclmulqdq;
    capabilities.neon = features.neon; capabilities.arm_crc32 = features.arm_crc32; capabilities.arm_crypto = features.arm_crypto; capabilities.arm_dotprod = features.arm_dotprod; capabilities.arm_pmull = features.arm_pmull;
    status = xxwidgets_optimization_options_dialog(g_window, "cdie Optimization", &capabilities, &options, &accepted);
    if (status != XXWIDGETS_OK) { report_ui_error("Open optimization options", status); return -1; }
    if (accepted) {
        current.buffer_size = options.buffer_size;
        current.file_buffer_size = options.file_buffer_size;
        current.use_sse2 = options.use_sse2; current.use_avx2 = options.use_avx2; current.use_sse42 = options.use_sse42; current.use_aes = options.use_aes; current.use_sha = options.use_sha; current.use_vpclmulqdq = options.use_vpclmulqdq;
        current.use_neon = options.use_neon; current.use_arm_crc32 = options.use_arm_crc32; current.use_arm_crypto = options.use_arm_crypto; current.use_arm_dotprod = options.use_arm_dotprod; current.use_arm_pmull = options.use_arm_pmull;
        if (!gui_backend_set_optimization_options(&current)) {
            report_ui_error("Apply optimization options", XXWIDGETS_PLATFORM_ERROR); return -1;
        }
        xxwidgets_widget_set_text(g_status, "Optimization settings applied to future scans.");
    }
    return accepted;
}


static int create_controls(void)
{
    if (xxwidgets_widget_create(g_app, g_window, XXWIDGETS_EDIT, "", (xxwidgets_rect){2, 1, 75, 2}, &g_path) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, "Open…", (xxwidgets_rect){79, 1, 11, 2}, &g_browse) != XXWIDGETS_OK ||
        xxwidgets_scan_panel_create(g_window, (xxwidgets_rect){2, 4, 88, 21}, &g_panel) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, "Scan options…", (xxwidgets_rect){2, 26, 16, 2}, &g_options) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, "Database paths…", (xxwidgets_rect){20, 26, 18, 2}, &g_databases) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, "Optimization…", (xxwidgets_rect){40, 26, 18, 2}, &g_optimization) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_LABEL, "Loading databases…", (xxwidgets_rect){2, 29, 88, 1}, &g_status) != XXWIDGETS_OK) return 0;
    NSTextField *field = xxwidgets_widget_native_handle(g_path);
    [field setPlaceholderString:@"Choose or drop a file to scan"];
    [field setAccessibilityLabel:@"File to scan"];
    xxwidgets_widget_set_enabled(control(XXWIDGETS_SCAN_PANEL_FILE_TYPE), 0);
    NSButton *scan = xxwidgets_widget_native_handle(control(XXWIDGETS_SCAN_PANEL_SCAN));
    [scan setKeyEquivalent:@"\r"];
    const xxwidgets_shortcut shortcuts[] = {{"F5", ACTION_SCAN}, {"Cmd+O", ACTION_OPEN}, {"Cmd+R", ACTION_SCAN}};
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
    printf("Cocoa GUI smoke test passed: %d signatures, file drop, scan, type selection, report, missing-file recovery\n",
        gui_backend_signature_count());
    return 0;
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        BOOL smoke = argc > 1 && !strcmp(argv[1], "--smoke-test");
        NSString *startup = nil;
        if (smoke && argc > 2) startup = string(argv[2]);
        else if (!smoke && argc > 1 && strncmp(argv[1], "-psn_", 5)) startup = string(argv[1]);
        xxwidgets_config config = {XXWIDGETS_BACKEND_NATIVE, on_event, NULL};
        xxwidgets_status status = xxwidgets_app_create(&config, &g_app);
        if (status != XXWIDGETS_OK) { fprintf(stderr, "%s\n", xxwidgets_status_string(status)); return 1; }
        int result = 0;
        CDIEApplication *delegate = nil;
        if (xxwidgets_widget_create(g_app, NULL, XXWIDGETS_WINDOW, gui_backend_app_title(),
                (xxwidgets_rect){4, 3, 92, 31}, &g_window) != XXWIDGETS_OK || !create_controls()) {
            result = 1; goto finish;
        }
        xxwidgets_window_set_minimum_size(g_window, 78, 25);
        if (xxwidgets_window_set_accept_file_drops(g_window, 1) != XXWIDGETS_OK) { result = 1; goto finish; }
        NSWindow *window = xxwidgets_widget_native_handle(g_window);
        delegate = [[CDIEApplication alloc] init];
        [NSApp setDelegate:delegate];
        install_menu(delegate);
        gui_backend_init();
        if (!smoke) {
            NSArray *paths = [[NSUserDefaults standardUserDefaults] arrayForKey:@"DatabasePaths"];
            if ([paths count] == 3 && [paths[0] isKindOfClass:[NSString class]] &&
                [paths[1] isKindOfClass:[NSString class]] && [paths[2] isKindOfClass:[NSString class]])
                gui_backend_set_db_paths([paths[0] UTF8String], [paths[1] UTF8String], [paths[2] UTF8String]);
        }
        status = refresh_panel_tags();
        if (status != XXWIDGETS_OK) { report_ui_error("Load signature tags", status); result = 1; goto finish; }
        ready(); layout();
        if (smoke) { result = smoke_test(startup ?: [[NSBundle mainBundle] executablePath]); goto finish; }
        [window center]; [window makeKeyAndOrderFront:nil]; [NSApp activateIgnoringOtherApps:YES];
        xxwidgets_widget_focus(g_path);
        if (startup) open_path(startup);
        while (g_running) {
            @autoreleasepool {
                status = xxwidgets_app_poll(g_app, 30);
                if (status != XXWIDGETS_OK) { result = 1; break; }
                if (!g_running) break;
                if (g_layout) { g_layout = 0; layout(); }
                int action = g_pending; g_pending = ACTION_NONE;
                switch (action) {
                case ACTION_SCAN: do_scan(); break;
                case ACTION_OPEN: do_open(); break;
                case ACTION_REPORT:
                    status = xxwidgets_scan_panel_show_report(g_panel);
                    if (status != XXWIDGETS_OK) report_ui_error("Scan report", status);
                    break;
                case ACTION_OPTIONS: do_options(); break;
                case ACTION_DATABASES: do_databases(delegate); break;
                case ACTION_OPTIMIZATION: do_optimization(); break;
                case ACTION_ABOUT:
                    [NSApp orderFrontStandardAboutPanelWithOptions:@{NSAboutPanelOptionApplicationName: @"cdie",
                        NSAboutPanelOptionApplicationVersion: string(gui_backend_app_title()),
                        @"Copyright": @"© 2026 hors · MIT License"}];
                    break;
                default: break;
                }
            }
        }
finish:
        gui_backend_shutdown();
        [g_type_path release]; g_type_path = nil;
        if (g_panel) xxwidgets_scan_panel_destroy(g_panel);
        xxwidgets_app_destroy(g_app);
        [delegate release];
        return result;
    }
}
