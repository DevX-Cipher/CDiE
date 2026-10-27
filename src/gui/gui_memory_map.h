/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_MEMORY_MAP_H
#define CDIE_GUI_MEMORY_MAP_H
#include "../app/cdie_memory_map.h"
#include <xxwidgets/xxwidgets_memory_map.h>
xxwidgets_status gui_memory_map_set_data(xxwidgets_memory_map *view, const char *path, const cdie_memory_map_data *data);
xxwidgets_status gui_memory_map_show(xxwidgets_widget *owner, const char *path);
#endif
