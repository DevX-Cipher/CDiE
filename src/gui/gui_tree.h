/* Copyright (c) 2026 hors<horsicq@gmail.com> · SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_TREE_H
#define CDIE_GUI_TREE_H
#include <xxwidgets/xxwidgets_scan_panel.h>
/* Scan and replace the complete tree transactionally. Free type with gui_backend_free.
 *
 * The engine runs on a worker thread while xxwidgets_process_dialog, owned by
 * owner, keeps the UI responsive; its window opens only for a scan that takes
 * more than a second, shows the engine's progress levels and offers Cancel.
 * A cancelled scan returns 0 with *cancelled set (cancelled may be NULL).
 * Call outside xxwidgets input callbacks and polling; there, or without an
 * owner, it waits for the worker without a dialog. The backend's options and
 * databases must not change until it returns. */
int gui_tree_scan(xxwidgets_widget *owner, xxwidgets_scan_panel *panel, const char *path, int type_id,
                  char **type, int *cancelled);
#endif
