#ifndef CDIE_UI_H
#define CDIE_UI_H

#include "common.h"

int  ui_setup_run(const WCHAR* dir, int allusers, int existing, const WCHAR* space);

int  ui_uninstall_confirm(const WCHAR* dir);

void ui_uninstall_progress(const WCHAR* dir, int allusers);

#endif