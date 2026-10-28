/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_DISASM_H
#define CDIE_GUI_DISASM_H
#include "../app/cdie_disasm.h"
#include <xxwidgets/xxwidgets_disasm.h>
typedef struct gui_disasm_source {
    cdie_disasm_context *context;
    xxwidgets_disasm_source source;
    const char *names[16];
    xxwidgets_disasm_record records[CDIE_DISASM_MAX_ROWS];
} gui_disasm_source;
/* Borrows path/context. The source/adapter must outlive the browser. */
void gui_disasm_source_init(gui_disasm_source *adapter, cdie_disasm_context *context, const char *path);
void gui_disasm_set_options(const xxwidgets_disasm_options *options);
void gui_disasm_get_options(xxwidgets_disasm_options *options);
xxwidgets_status gui_disasm_show(xxwidgets_widget *owner, const char *path);
#endif
