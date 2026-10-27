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

/* The S and ? buttons DIE puts on result rows. The last scan's tree carries
 * them: S on rows found by a script, ? on every detection. From the event
 * callback, gui_tree_mark_event records a press (a CLICK from the panel's
 * results tree) and returns 1; after xxwidgets_app_poll returns,
 * gui_tree_mark_process shows it modally over owner: S the script in the
 * signature editor, whose Run executes the edited copy on the scanned file;
 * ? the bundled description, else a web search. Returns a line for the status
 * bar, or NULL. Rows of a tree not filled by gui_tree_scan do nothing. */
int gui_tree_mark_event(const xxwidgets_scan_panel *panel, const xxwidgets_event *event);
const char *gui_tree_mark_process(xxwidgets_widget *owner);
/* Releases the last scan's rows, e.g. before exit. */
void gui_tree_reset(void);
#endif
