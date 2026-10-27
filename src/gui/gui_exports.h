/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_EXPORTS_H
#define CDIE_GUI_EXPORTS_H
#include "../app/cdie_exports.h"
#include <xxwidgets/xxwidgets_exports.h>
xxwidgets_status gui_exports_set_data(xxwidgets_exports *, const char *path, const cdie_exports_data *);
xxwidgets_status gui_exports_show(xxwidgets_widget *, const char *path);
#endif
