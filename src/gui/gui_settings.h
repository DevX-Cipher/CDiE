/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_SETTINGS_H
#define CDIE_GUI_SETTINGS_H

#include "gui_options.h"
#include <xxfclib/settings/xx_settings.h>

/* The Options settings between runs, under Detect It Easy's key names.
 * Store: the INI file named by CDIE_SETTINGS; else cdie.ini beside the
 * executable when a file named "portable" is there; else the platform store
 * NTInfo/cdie (HKCU registry, ~/.config/NTInfo/cdie.conf, preferences), selected
 * by xx_settings_create_application. Portable storage never falls back to native.
 * memory_only (smoke tests) never touches disk. *loaded is 0 when stored
 * settings could not be read; the frontend then continues with defaults. */
xx_settings *gui_settings_open(int memory_only, int *loaded);
void gui_settings_close(xx_settings *store);

/* Applies the stored backend settings (database folders, CPU optimization,
 * scan modes) and fills values and *page with the frontend's. Missing or
 * malformed keys keep what values and *page already hold. Returns 0 when the
 * stored database folders do not load; the defaults stay active then. */
int gui_settings_load(xx_settings *store, gui_options_values *values, gui_options_page_id *page);

/* Records values, page and the backend state, then saves. Default database
 * folders are stored empty, so moving the program keeps working. */
int gui_settings_save(xx_settings *store, const gui_options_values *values, gui_options_page_id page);

#endif
