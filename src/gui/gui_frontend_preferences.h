/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_FRONTEND_PREFERENCES_H
#define CDIE_GUI_FRONTEND_PREFERENCES_H
#include "gui_options.h"

void gui_frontend_get_preferences(gui_options_values *values);
xxwidgets_status gui_frontend_apply_preferences(xxwidgets_app *app, const gui_options_values *values);
int gui_frontend_scan_after_open(void);
void gui_frontend_remember_file(const char *path);
xxwidgets_status gui_frontend_recent_dialog(xxwidgets_widget *owner, char *path, size_t capacity, int *accepted);
char *gui_frontend_stored_language(int memory_only); /* free() by caller; NULL = System */
char *gui_frontend_executable_directory(void); /* free() by caller */
/* Single-application setting applies at startup. Zero means another instance
 * owns this installation. A negative return means the instance lock failed. */
int gui_frontend_instance_start(xxwidgets_widget *window, const char *startup_file);
void gui_frontend_instance_close(void);
#define GUI_FRONTEND_COPYDATA_ID 0x43444945u
/* Windows user PATH: inspect/apply the exact application-directory component.
 * No process is launched and unrelated PATH components are preserved. */
int gui_frontend_environment_contains(const char *directory);
xxwidgets_status gui_frontend_environment_apply(const char *directory, int enabled);
#endif
