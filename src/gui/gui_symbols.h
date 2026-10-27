/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_SYMBOLS_H
#define CDIE_GUI_SYMBOLS_H
#include "../app/cdie_symbols.h"
#include <xxwidgets/xxwidgets_symbols.h>
xxwidgets_status gui_symbols_set_data(xxwidgets_symbols *, const char *path, const cdie_symbols_data *);
xxwidgets_status gui_symbols_show(xxwidgets_widget *, const char *path);
#endif
