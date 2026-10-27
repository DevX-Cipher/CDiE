/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_IMPORTS_H
#define CDIE_GUI_IMPORTS_H
#include "../app/cdie_imports.h"
#include <xxwidgets/xxwidgets_imports.h>
xxwidgets_status gui_imports_set_data(xxwidgets_imports *, const char *path, const cdie_imports_data *);
xxwidgets_status gui_imports_show(xxwidgets_widget *, const char *path);
#endif
