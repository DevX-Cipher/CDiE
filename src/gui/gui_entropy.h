/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_ENTROPY_H
#define CDIE_GUI_ENTROPY_H
#include "../app/cdie_entropy.h"
#include <xxwidgets/xxwidgets_entropy.h>
/* UI adapter: installs a copied snapshot. No parser or file I/O in widgets. */
xxwidgets_status gui_entropy_set_data(xxwidgets_entropy *view, const cdie_entropy_data *data);
xxwidgets_status gui_entropy_show(xxwidgets_widget *owner, const char *path);
#endif
