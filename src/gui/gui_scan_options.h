/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_SCAN_OPTIONS_H
#define CDIE_GUI_SCAN_OPTIONS_H

#include <xxwidgets/xxwidgets_scan_options.h>
#include <xxwidgets/xxwidgets_options_pages.h>

/* Options-dialog page: the xxwidgets scan form plus the four expansion modes.
 * OK writes both into values; Default restores the backend's initial ones. */
typedef struct gui_scan_page_values {
    xxwidgets_scan_options options;
    unsigned int modes;
} gui_scan_page_values;
xxwidgets_status gui_scan_options_page(xxwidgets_options_page *page, const char *title, gui_scan_page_values *values);

#endif
