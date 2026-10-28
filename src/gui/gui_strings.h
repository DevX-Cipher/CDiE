/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_STRINGS_H
#define CDIE_GUI_STRINGS_H
#include "../app/cdie_memory_map.h"
#include "../app/cdie_strings.h"
#include <xxwidgets/xxwidgets_strings.h>
/* UI adapter; result strings are copied by the reusable view. With a memory
 * map (or NULL) each string gets the region holding it and its address. */
xxwidgets_status gui_strings_set_data(xxwidgets_strings *view, const cdie_strings_data *data, const cdie_memory_map_data *map);
xxwidgets_status gui_strings_show(xxwidgets_widget *owner, const char *path);
/* Interactive stress example: genuinely scan count * 5 virtual input bytes
 * on the worker with progress, then browse every record through 64-bit pages. */
xxwidgets_status gui_strings_show_generated(xxwidgets_widget *owner, uint64_t count);
/* Host callback for the shared modal's accepted Save CSV destination. */
xxwidgets_status gui_strings_save_csv(const char *path, const char *text, size_t length, void *user);
#endif
