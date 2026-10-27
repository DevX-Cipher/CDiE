/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_STRINGS_H
#define CDIE_GUI_STRINGS_H
#include "../app/cdie_strings.h"
#include <xxwidgets/xxwidgets_strings.h>
/* UI adapter; result strings are copied by the reusable view. */
xxwidgets_status gui_strings_set_data(xxwidgets_strings *view, const cdie_strings_data *data);
xxwidgets_status gui_strings_show(xxwidgets_widget *owner, const char *path);
/* Host callback for the shared modal's accepted Save CSV destination. */
xxwidgets_status gui_strings_save_csv(const char *path, const char *text, size_t length, void *user);
#endif
