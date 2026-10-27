/* Copyright (c) 2026 hors<horsicq@gmail.com> · SPDX-License-Identifier: MIT */
#include "app/cdie_translation.h"
#include "../gui/gui_backend.h"
#include "../gui/gui_tree.h"
#include "../gui/gui_scan_options.h"
#include "../gui/gui_options.h"
#include "../gui/gui_about.h"
#include "../gui/gui_settings.h"
#include "global.h"
#include <xxwidgets/xxwidgets.h>
#include <xxwidgets/xxwidgets_combobox.h>
#include <xxwidgets/xxwidgets_scan_panel.h>
#include <xxfclib/strings/xx_string.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

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
#include <gtk/gtk.h>

enum { ACTION_NONE, ACTION_SCAN, ACTION_REPORT, ACTION_QUIT, ACTION_OPTIONS, ACTION_BROWSE,
    ACTION_DATABASE_PATHS, ACTION_FONTS, ACTION_ABOUT, ACTION_TOOL_BASE = 100 };
typedef struct tui_app {
    xxwidgets_app *app;
    xxwidgets_widget *window, *path, *status, *browse, *options, *about, *tools[11];
    xxwidgets_scan_panel *panel;
    char *type_path;
    gui_options_page_id options_page;
    xx_settings *settings;
    int running, pending, busy, needs_layout;
} tui_app;
typedef struct file_type { int id; wchar_t *label; } file_type;
typedef struct file_types { file_type *items; size_t count; int failed; } file_types;
static void set_status(tui_app *ui, const char *message) { (void)xxwidgets_widget_set_text(ui->status, message); }
static xxwidgets_widget *control(tui_app *ui, xxwidgets_scan_panel_control_id id)
{
    return xxwidgets_scan_panel_control(ui->panel, id);
}

static char *copy_text(const char *text)
{
    size_t length = strlen(text) + 1;
    char *copy = malloc(length);
    if (copy) memcpy(copy, text, length);
    return copy;
}

static void collect_type(int id, const char *name, void *user)
{
    file_types *types = user;
    file_type *items;
    wchar_t *label;
    if (types->failed) return;
    if (types->count >= SIZE_MAX / sizeof(*items) - 1) { types->failed = 1; return; }
    label = xx_str_utf8_to_unicode(id == 0 ? CDIE_TR("Automatic") : name);
    if (!label) { types->failed = 1; return; }
    items = realloc(types->items, (types->count + 1) * sizeof(*items));
    if (!items) { xx_str_wfree(label); types->failed = 1; return; }
    types->items = items;
    types->items[types->count++] = (file_type){id, label};
}

static int refresh_types(tui_app *ui, const char *path)
{
    file_types types = {0};
    xx_var selected = {0};
    xx_meta_string *records = NULL;
    xx_str_w_s *labels = NULL;
    xxwidgets_widget *combo = control(ui, XXWIDGETS_SCAN_PANEL_FILE_TYPE);
    char *saved_path = NULL;
    int ok = 0, index = 0;
    if (ui->type_path && !strcmp(ui->type_path, path)) (void)xxwidgets_combobox_get_current(combo, &selected);
    if (!gui_backend_file_types(path, collect_type, &types) || types.failed || !types.count) goto cleanup;
    records = calloc(types.count, sizeof(*records));
    labels = calloc(types.count, sizeof(*labels));
    saved_path = copy_text(path);
    if (!records || !labels || !saved_path) goto cleanup;
    for (size_t i = 0; i < types.count; ++i) {
        labels[i].data = types.items[i].label;
        labels[i].length = wcslen(labels[i].data);
        labels[i].capacity = labels[i].length + 1;
        labels[i].is_view = true;
        records[i].meta_string = &labels[i];
        records[i].var.type = XX_VAR_TYPE_UINT64;
        records[i].var.val.u64 = (uint64_t)types.items[i].id;
        if (selected.type == XX_VAR_TYPE_UINT64 && selected.val.u64 == records[i].var.val.u64) index = (int)i;
    }
    ok = xxwidgets_combobox_set_records(combo, records, types.count) == XXWIDGETS_OK &&
        xxwidgets_widget_set_value(combo, index) == XXWIDGETS_OK;
    if (ok) { free(ui->type_path); ui->type_path = saved_path; saved_path = NULL; }
cleanup:
    for (size_t i = 0; i < types.count; ++i) xx_str_wfree(types.items[i].label);
    free(types.items); free(records); free(labels); free(saved_path);
    return ok;
}

static char *read_path(tui_app *ui)
{
    size_t size = 0;
    char *path;
    if (xxwidgets_widget_get_text(ui->path, NULL, 0, &size) != XXWIDGETS_OK || !size) return NULL;
    path = malloc(size);
    if (!path) return NULL;
    if (xxwidgets_widget_get_text(ui->path, path, size, NULL) != XXWIDGETS_OK) { free(path); return NULL; }
    if (path[0] == '~' && (path[1] == '/' || !path[1])) {
        const char *home = getenv("HOME");
        if (home) {
            size_t home_length = strlen(home), tail_length = strlen(path + 1);
            char *expanded;
            if (home_length > SIZE_MAX - tail_length - 1) { free(path); return NULL; }
            expanded = malloc(home_length + tail_length + 1);
            if (!expanded) { free(path); return NULL; }
            memcpy(expanded, home, home_length);
            memcpy(expanded + home_length, path + 1, tail_length + 1);
            free(path); path = expanded;
        }
    }
    return path;
}

static char *read_tags(const xxwidgets_scan_panel *panel, int include)
{
    size_t size = 0;
    char *tags;
    xxwidgets_status status = include ?
        xxwidgets_scan_panel_get_include_tags(panel, NULL, 0, &size) :
        xxwidgets_scan_panel_get_exclude_tags(panel, NULL, 0, &size);
    if (status != XXWIDGETS_OK || !size) return NULL;
    tags = (char *)malloc(size);
    if (!tags) return NULL;
    status = include ?
        xxwidgets_scan_panel_get_include_tags(panel, tags, size, NULL) :
        xxwidgets_scan_panel_get_exclude_tags(panel, tags, size, NULL);
    if (status != XXWIDGETS_OK) {
        free(tags);
        return NULL;
    }
    return tags;
}

static int scan(tui_app *ui, int smoke)
{
    char message[192], *type = NULL, *path = read_path(ui);
    char *include_tags = NULL, *exclude_tags = NULL;
    xx_var selected = {0};
    unsigned int flags = 0, databases = 0;
    int cancelled = 0, ok = 0;
    if (!path) { set_status(ui, CDIE_TR("Cannot read the file path (out of memory or widget error).")); return 0; }
    if (!path[0]) { set_status(ui, CDIE_TR("Enter a file path, then press Enter or F5.")); (void)xxwidgets_widget_focus(ui->path); goto cleanup; }
    if (!gui_backend_signature_count()) { set_status(ui, CDIE_TR("No signatures loaded. Choose the main folder using Database paths.")); goto cleanup; }
    if (xxwidgets_scan_panel_clear(ui->panel) != XXWIDGETS_OK) {
        set_status(ui, CDIE_TR("Cannot clear the previous scan results.")); goto cleanup;
    }
    if (!refresh_types(ui, path)) {
        (void)xxwidgets_widget_set_enabled(control(ui, XXWIDGETS_SCAN_PANEL_FILE_TYPE), 0);
        set_status(ui, CDIE_TR("Scan failed: cannot open this file. Check its path and permissions.")); goto cleanup;
    }
    if (xxwidgets_combobox_get_current(control(ui, XXWIDGETS_SCAN_PANEL_FILE_TYPE), &selected) != XXWIDGETS_OK ||
        selected.type != XX_VAR_TYPE_UINT64 || selected.val.u64 > INT_MAX ||
        xxwidgets_scan_panel_get_flags(ui->panel, &flags) != XXWIDGETS_OK ||
        xxwidgets_scan_panel_get_databases(ui->panel, &databases) != XXWIDGETS_OK) {
        set_status(ui, CDIE_TR("Cannot read the selected scan options.")); goto cleanup;
    }
    include_tags = read_tags(ui->panel, 1);
    exclude_tags = read_tags(ui->panel, 0);
    if (!include_tags || !exclude_tags || !gui_backend_set_tag_filters(include_tags, exclude_tags)) {
        set_status(ui, CDIE_TR("Cannot apply the selected tag filters.")); goto cleanup;
    }
    gui_backend_set_flags(flags & XXWIDGETS_SCAN_DEEP, flags & XXWIDGETS_SCAN_HEURISTIC,
        flags & XXWIDGETS_SCAN_VERBOSE, flags & XXWIDGETS_SCAN_AGGRESSIVE,
        flags & XXWIDGETS_SCAN_HIDE_UNKNOWN, flags & XXWIDGETS_SCAN_FORMAT);
    gui_backend_set_databases(databases & XXWIDGETS_SCAN_DATABASE_EXTRA, databases & XXWIDGETS_SCAN_DATABASE_CUSTOM);
    set_status(ui, CDIE_TR("Scanning..."));
    ui->busy = 1;
    ok = gui_tree_scan(smoke ? NULL : ui->window, ui->panel, path, (int)selected.val.u64, &type, &cancelled);
    ui->busy = 0;
    if (ok) cdie_translation_snprintf(message, sizeof(message), CDIE_N_("Scan complete (%s). %zu detections."), type ? type : "Unknown", xxwidgets_scan_panel_count(ui->panel));
    else cdie_translation_snprintf(message, sizeof(message), CDIE_N_("%s"), cancelled ? CDIE_TR("Scan cancelled.") : CDIE_TR("Scan failed."));
    set_status(ui, message);
    if (ok) (void)xxwidgets_widget_focus(control(ui, XXWIDGETS_SCAN_PANEL_RESULTS));
    (void)xxwidgets_widget_set_enabled(control(ui, XXWIDGETS_SCAN_PANEL_FILE_TYPE), 1);
cleanup:
    gui_backend_free(type);
    free(include_tags);
    free(exclude_tags);
    free(path);
    return ok;
}


static const char *tool_names[] = {CDIE_N_("File Information"), CDIE_N_("Entropy"), CDIE_N_("Hash"), CDIE_N_("Strings"), CDIE_N_("Hex View"), CDIE_N_("Disasm"), CDIE_N_("Memory Map"), CDIE_N_("Imports"), CDIE_N_("Exports"), CDIE_N_("Resources"), CDIE_N_("Symbols")};
typedef xxwidgets_status (*tool_fn)(xxwidgets_widget *, const char *);
static const tool_fn tool_functions[] = {gui_file_information_show, gui_entropy_show, gui_hash_show, gui_strings_show, gui_hex_view_show, gui_disasm_show, gui_memory_map_show, gui_imports_show, gui_exports_show, gui_resources_show, gui_symbols_show};

static void layout(tui_app *ui)
{
    xxwidgets_rect bounds;
    if (xxwidgets_widget_get_rect(ui->window, &bounds) != XXWIDGETS_OK) return;
    int w = bounds.width, h = bounds.height;
    (void)xxwidgets_widget_set_rect(ui->path, (xxwidgets_rect){2, 1, w - 18, 2});
    (void)xxwidgets_widget_set_rect(ui->browse, (xxwidgets_rect){w - 14, 1, 12, 2});
    (void)xxwidgets_scan_panel_set_rect(ui->panel, (xxwidgets_rect){2, 4, w - 4, h - 16});
    for (int i = 0; i < 11; ++i) {
        int row = i / 6, col = i % 6;
        int x = 2 + col * (w - 4) / 6;
        int end = 2 + (col + 1) * (w - 4) / 6;
        (void)xxwidgets_widget_set_rect(ui->tools[i], (xxwidgets_rect){x, h - 11 + row * 3, end - x - 1, 2});
    }
    (void)xxwidgets_widget_set_rect(ui->options, (xxwidgets_rect){2 + 5 * (w - 4) / 6, h - 8, (w - 4) / 6 - 1, 2});
    (void)xxwidgets_widget_set_rect(ui->about, (xxwidgets_rect){2, h - 5, 16, 2});
    (void)xxwidgets_widget_set_rect(ui->status, (xxwidgets_rect){2, h - 1, w - 4, 1});
}
static void on_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    tui_app *ui = user; (void)app;
    if (event->type == XXWIDGETS_EVENT_RESIZE && event->widget == ui->window) { ui->needs_layout = 1; return; }
    if (ui->busy) return;
    if (event->type == XXWIDGETS_EVENT_CLOSE && event->widget == ui->window) ui->running = 0;
    else if (event->type == XXWIDGETS_EVENT_DROP_FILES && event->widget == ui->window && event->path_count) {
        (void)xxwidgets_widget_set_text(ui->path, event->paths[0]); ui->pending = ACTION_SCAN;
    } else if (event->type == XXWIDGETS_EVENT_SHORTCUT) {
        if (event->value == ACTION_QUIT) ui->running = 0; else ui->pending = event->value;
    } else if (event->type == XXWIDGETS_EVENT_ACTIVATE && event->widget == ui->path) ui->pending = ACTION_SCAN;
    else if (event->type == XXWIDGETS_EVENT_CHANGE && event->widget == ui->path) {
        (void)xxwidgets_scan_panel_clear(ui->panel);
        (void)xxwidgets_widget_set_enabled(control(ui, XXWIDGETS_SCAN_PANEL_FILE_TYPE), 0);
    } else if (event->type == XXWIDGETS_EVENT_SELECT && event->widget == control(ui, XXWIDGETS_SCAN_PANEL_FILE_TYPE)) ui->pending = ACTION_SCAN;
    else if (gui_tree_mark_event(ui->panel, event)) {
        /* S or ? on a result row: shown after polling returns. */
    } else if (event->type == XXWIDGETS_EVENT_CLICK) {
        if (event->widget == ui->browse) ui->pending = ACTION_BROWSE;
        else if (event->widget == ui->options) ui->pending = ACTION_OPTIONS;
        else if (event->widget == ui->about) ui->pending = ACTION_ABOUT;
        else if (event->widget == control(ui, XXWIDGETS_SCAN_PANEL_SCAN)) ui->pending = ACTION_SCAN;
        else if (event->widget == control(ui, XXWIDGETS_SCAN_PANEL_REPORT)) ui->pending = ACTION_REPORT;
        else for (int i = 0; i < 11; ++i) if (event->widget == ui->tools[i]) ui->pending = ACTION_TOOL_BASE + i;
    }
}
static int create_controls(tui_app *ui, const char *path)
{
    static const xxwidgets_shortcut shortcuts[] = {{"F5", ACTION_SCAN}, {"Ctrl+R", ACTION_REPORT}, {"Ctrl+Q", ACTION_QUIT}, {"Ctrl+O", ACTION_BROWSE}, {"F1", ACTION_ABOUT}, {"F2", ACTION_OPTIONS}, {"F3", ACTION_DATABASE_PATHS}, {"F4", ACTION_FONTS}, {"Ctrl+Shift+Y", ACTION_TOOL_BASE + 10}};
    if (xxwidgets_widget_create(ui->app, NULL, XXWIDGETS_WINDOW, gui_backend_app_title(), (xxwidgets_rect){4, 3, 132, 34}, &ui->window) != XXWIDGETS_OK ||
        xxwidgets_widget_create(ui->app, ui->window, XXWIDGETS_EDIT, path ? path : "", (xxwidgets_rect){2, 1, 114, 2}, &ui->path) != XXWIDGETS_OK ||
        xxwidgets_widget_create(ui->app, ui->window, XXWIDGETS_BUTTON, CDIE_TR("Browse"), (xxwidgets_rect){118, 1, 12, 2}, &ui->browse) != XXWIDGETS_OK ||
        xxwidgets_scan_panel_create(ui->window, (xxwidgets_rect){2, 4, 128, 21}, &ui->panel) != XXWIDGETS_OK ||
        xxwidgets_widget_create(ui->app, ui->window, XXWIDGETS_BUTTON, CDIE_TR("Options"), (xxwidgets_rect){2, 26, 18, 2}, &ui->options) != XXWIDGETS_OK ||
        xxwidgets_widget_create(ui->app, ui->window, XXWIDGETS_BUTTON, CDIE_TR("About"), (xxwidgets_rect){2, 29, 16, 2}, &ui->about) != XXWIDGETS_OK ||
        xxwidgets_widget_create(ui->app, ui->window, XXWIDGETS_LABEL, "", (xxwidgets_rect){2, 33, 128, 1}, &ui->status) != XXWIDGETS_OK) return 0;
    for (int i = 0; i < 11; ++i)
        if (xxwidgets_widget_create(ui->app, ui->window, XXWIDGETS_BUTTON, CDIE_TR(tool_names[i]), (xxwidgets_rect){2, 26, 18, 2}, &ui->tools[i]) != XXWIDGETS_OK) return 0;
    char *tags = gui_backend_used_tags();
    if (!tags) return 0;
    xxwidgets_status s = xxwidgets_scan_panel_set_available_tags(ui->panel, tags); gui_backend_free(tags);
    if (s != XXWIDGETS_OK || xxwidgets_window_set_shortcuts(ui->window, shortcuts, sizeof(shortcuts)/sizeof(shortcuts[0])) != XXWIDGETS_OK ||
        xxwidgets_scan_panel_set_flags(ui->panel, XXWIDGETS_SCAN_DEEP | XXWIDGETS_SCAN_HEURISTIC | XXWIDGETS_SCAN_VERBOSE | XXWIDGETS_SCAN_FORMAT) != XXWIDGETS_OK ||
        xxwidgets_widget_set_enabled(control(ui, XXWIDGETS_SCAN_PANEL_FILE_TYPE), 0) != XXWIDGETS_OK ||
        xxwidgets_window_set_minimum_size(ui->window, 120, 31) != XXWIDGETS_OK ||
        xxwidgets_window_set_accept_file_drops(ui->window, 1) != XXWIDGETS_OK) return 0;
    layout(ui); (void)xxwidgets_widget_focus(ui->path);
    set_status(ui, gui_backend_signature_count() ? CDIE_TR("Choose a file, drop one here, or enter its path.") :
        CDIE_TR("No signatures loaded. Choose the main folder in Options > Signatures (F3).")); return 1;
}
static xxwidgets_status browse_database(xxwidgets_widget *owner, const char *initial, char **path, int *accepted, void *user)
{
    xxwidgets_status status = XXWIDGETS_OK;
    (void)user;
    *path = NULL; *accepted = 0;
    GtkWidget *dialog = gtk_file_chooser_dialog_new(CDIE_TR("Choose signature database folder"),
        GTK_WINDOW(xxwidgets_widget_native_handle(owner)), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        CDIE_TR("Cancel"), GTK_RESPONSE_CANCEL, CDIE_TR("Select"), GTK_RESPONSE_ACCEPT, NULL);
    if (initial[0]) {
        gchar *filename = g_filename_from_utf8(initial, -1, NULL, NULL, NULL);
        if (filename) {
            gchar *cwd = g_get_current_dir();
            gchar *absolute = g_path_is_absolute(filename) ? g_strdup(filename) : g_build_filename(cwd, filename, NULL);
            (void)gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), absolute);
            g_free(absolute); g_free(cwd); g_free(filename);
        }
    }
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gchar *utf8 = filename ? g_filename_to_utf8(filename, -1, NULL, NULL, NULL) : NULL;
        if (utf8) { *path = copy_text(utf8); if (*path) *accepted = 1; else status = XXWIDGETS_OUT_OF_MEMORY; }
        else status = XXWIDGETS_INVALID_ARGUMENT;
        g_free(utf8); g_free(filename);
    }
    gtk_widget_destroy(dialog);
    return status;
}
/* The frontend's share of the Options settings, as currently applied. */
static xxwidgets_status current_options(tui_app *ui, gui_options_values *values)
{
    xxwidgets_status status;
    memset(values, 0, sizeof(*values));
    status = xxwidgets_scan_panel_get_flags(ui->panel, &values->scan.options.flags);
    if (status == XXWIDGETS_OK) status = xxwidgets_scan_panel_get_databases(ui->panel, &values->scan.options.databases);
    if (status == XXWIDGETS_OK) status = xxwidgets_app_get_font_options(ui->app, &values->fonts);
    values->scan.modes = gui_backend_get_scan_modes();
    gui_hex_view_get_options(&values->hex);
    return status;
}

/* Applies settings from Options or from the store; fonts only when changed. */
static xxwidgets_status apply_options(tui_app *ui, const gui_options_values *values)
{
    xxwidgets_font_options fonts;
    xxwidgets_status status;
    gui_backend_set_scan_modes(values->scan.modes);
    status = xxwidgets_scan_panel_set_flags(ui->panel, values->scan.options.flags);
    if (status == XXWIDGETS_OK) status = xxwidgets_scan_panel_set_databases(ui->panel, values->scan.options.databases);
    gui_hex_view_set_options(&values->hex);
    if (status == XXWIDGETS_OK) status = xxwidgets_app_get_font_options(ui->app, &fonts);
    if (status == XXWIDGETS_OK && memcmp(&values->fonts, &fonts, sizeof(fonts))) {
        status = xxwidgets_app_set_font_options(ui->app, &values->fonts);
        if (status == XXWIDGETS_OK) layout(ui);
    }
    return status;
}

static void reload_tags(tui_app *ui)
{
    char message[160];
    char *tags = gui_backend_used_tags();
    xxwidgets_status status;
    free(ui->type_path); ui->type_path = NULL;
    status = xxwidgets_scan_panel_clear(ui->panel);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(control(ui, XXWIDGETS_SCAN_PANEL_FILE_TYPE), 0);
    if (status == XXWIDGETS_OK) status = tags ? xxwidgets_scan_panel_set_available_tags(ui->panel, tags) : XXWIDGETS_OUT_OF_MEMORY;
    gui_backend_free(tags);
    cdie_translation_snprintf(message, sizeof(message), CDIE_N_("Database reloaded: %d signatures. Press F5 to scan."), gui_backend_signature_count());
    set_status(ui, status == XXWIDGETS_OK ? message : xxwidgets_status_string(status));
}

/* The shared Options window; Linux has no Explorer context menu page. */
static void options_dialog(tui_app *ui, gui_options_page_id page)
{
    gui_options_host host = {GUI_OPTIONS_PAGE(GUI_OPTIONS_FONTS) | GUI_OPTIONS_PAGE(GUI_OPTIONS_SCAN) | GUI_OPTIONS_PAGE(GUI_OPTIONS_SIGNATURES) |
                             GUI_OPTIONS_PAGE(GUI_OPTIONS_HEX) | GUI_OPTIONS_PAGE(GUI_OPTIONS_OPTIMIZATION),
                             NULL, browse_database, ui};
    gui_options_values values;
    int accepted = 0, reloaded = 0;
    xxwidgets_status status = current_options(ui, &values);
    ui->options_page = page;
    if (status == XXWIDGETS_OK) status = gui_options_dialog(ui->window, &host, &ui->options_page, &values, &accepted, &reloaded);
    if (status != XXWIDGETS_OK) { set_status(ui, xxwidgets_status_string(status)); return; }
    if (reloaded) reload_tags(ui);
    if (!accepted) return;
    status = apply_options(ui, &values);
    if (status != XXWIDGETS_OK) set_status(ui, xxwidgets_status_string(status));
    else if (!gui_settings_save(ui->settings, &values, ui->options_page))
        set_status(ui, CDIE_TR("Options applied, but they could not be saved for the next start."));
    else if (!reloaded) set_status(ui, CDIE_TR("Options applied."));
}
static void about_dialog(tui_app *ui)
{
    xxwidgets_about_dialog *about = NULL;
    xxwidgets_status status = gui_about_create(cdie_about_png, cdie_about_png_size, &about);
    if (status == XXWIDGETS_OK) status = xxwidgets_about_dialog_show(about, ui->window);
    if (about) (void)xxwidgets_about_dialog_destroy(about);
    if (status != XXWIDGETS_OK) set_status(ui, xxwidgets_status_string(status));
}
static void browse(tui_app *ui)
{
    GtkWidget *dialog = gtk_file_chooser_dialog_new(CDIE_TR("Open file"), GTK_WINDOW(xxwidgets_widget_native_handle(ui->window)), GTK_FILE_CHOOSER_ACTION_OPEN, CDIE_TR("Cancel"), GTK_RESPONSE_CANCEL, CDIE_TR("Open"), GTK_RESPONSE_ACCEPT, NULL);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (path) { (void)xxwidgets_widget_set_text(ui->path, path); ui->pending = ACTION_SCAN; g_free(path); }
    }
    gtk_widget_destroy(dialog);
}
int main(int argc, char **argv)
{
    const char *translation = NULL;
    if (!cdie_translation_arguments(&argc,argv,&translation)) { fprintf(stderr,"--translation requires a PO file\n"); return 1; }
    int smoke=0, positional=0;
    const char *path=NULL;
    for (int i=1;i<argc;++i) {
        if (!positional && !strcmp(argv[i],"--")) { positional=1; continue; }
        if (!positional && !strcmp(argv[i],"--smoke-test")) { smoke=1; continue; }
        if (path) { fprintf(stderr,"Only one initial file is supported.\n"); return 1; }
        path=argv[i];
    }
    tui_app ui = {0}; xxwidgets_config config = {XXWIDGETS_BACKEND_NATIVE, on_event, &ui};
    xxwidgets_status status; int result = 0;
    if (smoke && !path) { fprintf(stderr, "--smoke-test requires a file\n"); return 1; }
    (void)gui_backend_init();
    status = xxwidgets_app_create(&config, &ui.app);
    if (status != XXWIDGETS_OK) { fprintf(stderr, "Cannot initialize GTK: %s\n", xxwidgets_status_string(status)); result = 1; goto cleanup; }
    {
        size_t line = 0;
        status = cdie_translation_open(ui.app,translation,&line);
        if (status != XXWIDGETS_OK) { fprintf(stderr,"Cannot load translation (line %zu): %s\n",line,xxwidgets_status_string(status)); result=1; goto cleanup; }
    }
    if (smoke) (void)xxwidgets_app_set_create_hidden(ui.app, 1);
    if (!create_controls(&ui, path)) { fprintf(stderr, "Cannot create GUI controls.\n"); result = 1; goto cleanup; }
    ui.options_page = GUI_OPTIONS_SCAN;
    {
        /* Saved Options apply before the first scan; smoke tests keep them in memory. */
        gui_options_values values;
        int stored = 1, databases = 1;
        ui.settings = gui_settings_open(smoke, &stored);
        status = current_options(&ui, &values);
        if (status == XXWIDGETS_OK) databases = gui_settings_load(ui.settings, &values, &ui.options_page);
        if (status == XXWIDGETS_OK) status = apply_options(&ui, &values);
        if (status == XXWIDGETS_OK && !gui_backend_db_paths_current("", "", "")) reload_tags(&ui);
        if (status != XXWIDGETS_OK) set_status(&ui, xxwidgets_status_string(status));
        else if (!databases) set_status(&ui, CDIE_TR("The saved signature folders cannot be loaded; the default ones are active."));
        else if (!stored) set_status(&ui, CDIE_TR("Saved options cannot be read; the defaults are used."));
    }
    if (smoke) { result = scan(&ui, 1) ? 0 : 1; if (!result) printf("GTK GUI smoke: %zu detections\n", xxwidgets_scan_panel_count(ui.panel)); goto cleanup; }
    ui.running = 1; ui.pending = path ? ACTION_SCAN : ACTION_NONE;
    while (ui.running) {
        status = xxwidgets_app_poll(ui.app, 30);
        if (status != XXWIDGETS_OK) { result = 1; break; }
        if (ui.needs_layout) { ui.needs_layout = 0; layout(&ui); }
        const char *message = gui_tree_mark_process(ui.window);
        if (message) set_status(&ui, message);
        int action = ui.pending; ui.pending = ACTION_NONE;
        if (action == ACTION_SCAN) (void)scan(&ui, 0);
        else if (action == ACTION_BROWSE) browse(&ui);
        else if (action == ACTION_DATABASE_PATHS) options_dialog(&ui, GUI_OPTIONS_SIGNATURES);
        else if (action == ACTION_FONTS) options_dialog(&ui, GUI_OPTIONS_FONTS);
        else if (action == ACTION_ABOUT) about_dialog(&ui);
        else if (action == ACTION_REPORT) { status = xxwidgets_scan_panel_show_report(ui.panel); if (status != XXWIDGETS_OK) set_status(&ui, xxwidgets_status_string(status)); }
        else if (action == ACTION_OPTIONS) options_dialog(&ui, ui.options_page);
        else if (action >= ACTION_TOOL_BASE && action < ACTION_TOOL_BASE + 11) {
            char *selected = read_path(&ui);
            if (!selected || !*selected) set_status(&ui, CDIE_TR("Choose a file first."));
            else { status = tool_functions[action - ACTION_TOOL_BASE](ui.window, selected); if (status != XXWIDGETS_OK) set_status(&ui, xxwidgets_status_string(status)); }
            free(selected);
        }
    }
cleanup:
    free(ui.type_path);
    if (ui.panel) (void)xxwidgets_scan_panel_destroy(ui.panel);
    cdie_translation_close();
    if (ui.app) (void)xxwidgets_app_destroy(ui.app);
    gui_settings_close(ui.settings);
    gui_tree_reset();
    gui_backend_shutdown(); return result;
}
