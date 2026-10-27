/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_OPTIONS_H
#define CDIE_GUI_OPTIONS_H

#include <xxwidgets/xxwidgets_options_pages.h>
#include "gui_scan_options.h"

/* Pages of the shared Options window, in Detect It Easy's order. */
typedef enum gui_options_page_id {
    GUI_OPTIONS_FILE = 0,
    GUI_OPTIONS_FONTS,
    GUI_OPTIONS_SCAN,
    GUI_OPTIONS_SIGNATURES,
    GUI_OPTIONS_HEX,
    GUI_OPTIONS_OPTIMIZATION,
    GUI_OPTIONS_PAGE_COUNT
} gui_options_page_id;
#define GUI_OPTIONS_PAGE(id) (1u << (id))

/* Settings the frontend owns: filled before the dialog, applied after OK. */
typedef struct gui_options_values {
    gui_scan_page_values scan; /* Flags, databases and expansion modes. */
    xxwidgets_font_options fonts;
    xxwidgets_hex_options hex;
} gui_options_values;

typedef struct gui_options_host {
    unsigned int pages;     /* GUI_OPTIONS_PAGE bits this frontend offers. */
    const char *executable; /* File page: absolute UTF-8 program path (Windows). */
    /* Folder chooser for Signatures; NULL disables Browse. Same contract as
     * xxwidgets_database_paths_callbacks.browse. */
    xxwidgets_status (*browse)(xxwidgets_widget *dialog, const char *initial, char **path, int *accepted, void *user);
    void *user;
} gui_options_host;

/* Shows Options on *page and stores the last page shown there. On OK values
 * receive the edited settings and the CPU optimization is applied to the
 * backend; the caller applies the rest. *databases_reloaded reports that the
 * signatures were reloaded, which a later page rejecting OK followed by
 * Cancel can leave in place too: refresh tags and results whenever it is set. */
xxwidgets_status gui_options_dialog(xxwidgets_widget *owner, const gui_options_host *host, gui_options_page_id *page,
                                    gui_options_values *values, int *accepted, int *databases_reloaded);

#endif
