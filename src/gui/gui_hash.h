/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_HASH_H
#define CDIE_GUI_HASH_H
#include "../app/cdie_hash.h"
#include <xxwidgets/xxwidgets_hash.h>
/* UI adapter for results from cdie_hash_read; widgets copy every entry. */
xxwidgets_status gui_hash_set_data(xxwidgets_hash *view, const char *path, const cdie_hash_data *data);
xxwidgets_status gui_hash_show(xxwidgets_widget *owner, const char *path);
#endif
