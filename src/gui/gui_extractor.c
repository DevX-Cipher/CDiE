/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "gui_extractor.h"
#include "gui_extractor_source.h"
#include "app/cdie_translation.h"
#include <xxfclib/rt/xx_rt.h>

xxwidgets_status gui_extractor_show(xxwidgets_widget *owner, const char *path)
{
    gui_extractor_source *source = NULL;
    char *error = NULL;
    xxwidgets_status status;
    if (!owner || !path || !*path) return XXWIDGETS_INVALID_ARGUMENT;
    status = gui_extractor_source_open(path, NULL, &source, &error);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_extractor_dialog(owner, CDIE_TR("Extractor"), gui_extractor_source_model(source), 1);
    else if (error)
        (void)xxwidgets_text_dialog(owner, CDIE_TR("Extractor"), error);
    gui_extractor_source_free(source);
    xx_rt_free(error);
    return status;
}
