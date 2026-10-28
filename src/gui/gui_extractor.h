/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_EXTRACTOR_H
#define CDIE_GUI_EXTRACTOR_H
#include <xxwidgets/xxwidgets_extractor.h>
/* Open the reusable native Extractor with a read-only linked Hex view. */
xxwidgets_status gui_extractor_show(xxwidgets_widget *owner, const char *path);
#endif
