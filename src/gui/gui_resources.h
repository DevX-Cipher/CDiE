/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_RESOURCES_H
#define CDIE_GUI_RESOURCES_H
#include "../app/cdie_resources.h"
#include <xxwidgets/xxwidgets_resources.h>
xxwidgets_status gui_resources_set_data(xxwidgets_resources *, const char *path, const cdie_resources_data *);
xxwidgets_status gui_resources_show(xxwidgets_widget *, const char *path);
#endif
