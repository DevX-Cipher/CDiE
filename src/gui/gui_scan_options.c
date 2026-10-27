/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "gui_scan_options.h"
#include "gui_backend.h"
#include "app/cdie_translation.h"
#include <xxfclib/rt/xx_rt.h>

xxwidgets_status gui_scan_modes_dialog(xxwidgets_widget *owner, unsigned int *modes, int *accepted)
{
    xxwidgets_option controls[] = {
        {CDIE_TR("All file types"), 0}, {CDIE_TR("Scan overlays"), 0},
        {CDIE_TR("Scan resources"), 0}, {CDIE_TR("Scan archive members (one level)"), 0}
    };
    size_t i;
    xxwidgets_status status;
    if (accepted) *accepted = 0;
    if (!modes || !accepted || (*modes & ~GUI_SCAN_MODES_MASK)) return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < 4; ++i) controls[i].value = (int)((*modes >> i) & 1u);
    status = xxwidgets_options_dialog(owner, CDIE_TR("cdie Scan modes"), controls, 4, accepted);
    if (status == XXWIDGETS_OK && *accepted) {
        *modes = 0;
        for (i = 0; i < 4; ++i) if (controls[i].value) *modes |= 1u << i;
    }
    return status;
}

xxwidgets_status gui_scan_options_dialog(xxwidgets_widget *owner, const char *title,
    xxwidgets_scan_options *options, unsigned int *modes, int *accepted)
{
    xxwidgets_option controls[] = {
        {CDIE_TR("Deep scan"), 0}, {CDIE_TR("Heuristic scan"), 0}, {CDIE_TR("Verbose results"), 0},
        {CDIE_TR("Aggressive scan"), 0}, {CDIE_TR("Hide unknown results"), 0}, {CDIE_TR("Format text report"), 0},
        {CDIE_TR("Extra database"), 0}, {CDIE_TR("Custom database"), 0},
        {CDIE_TR("All file types"), 0}, {CDIE_TR("Scan overlays"), 0},
        {CDIE_TR("Scan resources"), 0}, {CDIE_TR("Scan archive members (one level)"), 0}
    };
    size_t i;
    xxwidgets_status status;
    if (accepted) *accepted = 0;
    if (!options || !modes || !accepted || (options->flags & ~63u) ||
        (options->databases & ~3u) || (*modes & ~GUI_SCAN_MODES_MASK))
        return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < 12; ++i) {
        unsigned int bits = i < 6 ? options->flags : i < 8 ? options->databases : *modes;
        controls[i].value = (int)((bits >> (i < 6 ? i : i < 8 ? i - 6 : i - 8)) & 1u);
    }
    status = xxwidgets_options_dialog(owner, title, controls, 12, accepted);
    if (status == XXWIDGETS_OK && *accepted) {
        options->flags = options->databases = *modes = 0;
        for (i = 0; i < 12; ++i) if (controls[i].value) {
            if (i < 6) options->flags |= 1u << i;
            else if (i < 8) options->databases |= 1u << (i - 6);
            else *modes |= 1u << (i - 8);
        }
    }
    return status;
}

typedef struct scan_page {
    xxwidgets_scan_options_widget *form;
    xxwidgets_widget *modes_label, *modes[4];
    gui_scan_page_values *values;
} scan_page;

static xxwidgets_status scan_page_fill(scan_page *page, const xxwidgets_scan_options *options, unsigned int modes)
{
    xxwidgets_status status = xxwidgets_scan_options_widget_set(page->form, options);
    for (int i = 0; status == XXWIDGETS_OK && i < 4; ++i) status = xxwidgets_widget_set_value(page->modes[i], (int)((modes >> i) & 1u));
    return status;
}

static xxwidgets_status scan_page_destroy(void *state)
{
    scan_page *page = state;
    xxwidgets_status status = page->form ? xxwidgets_scan_options_widget_destroy(page->form) : XXWIDGETS_OK;
    for (int i = 0; i < 4; ++i) if (page->modes[i]) {
        xxwidgets_status cleanup = xxwidgets_widget_destroy(page->modes[i]);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    if (page->modes_label) {
        xxwidgets_status cleanup = xxwidgets_widget_destroy(page->modes_label);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    xx_rt_free(page);
    return status;
}

static xxwidgets_status scan_page_create(xxwidgets_widget *dialog, xxwidgets_rect bounds, void *user, void **out)
{
    const char *labels[4] = {CDIE_TR("All file types"), CDIE_TR("Scan overlays"), CDIE_TR("Scan resources"), CDIE_TR("Scan archive members (one level)")};
    scan_page *page = xx_rt_calloc(1, sizeof(*page));
    xxwidgets_app *app = xxwidgets_widget_app(dialog);
    int first = (bounds.width - 2) / 2;
    xxwidgets_status status;
    if (!page) return XXWIDGETS_OUT_OF_MEMORY;
    page->values = user;
    status = xxwidgets_scan_options_widget_create(dialog, (xxwidgets_rect){bounds.x, bounds.y, bounds.width, 13}, &page->form);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_create(app, dialog, XXWIDGETS_LABEL, CDIE_TR("Scan modes"),
        (xxwidgets_rect){bounds.x, bounds.y + 14, bounds.width, 1}, &page->modes_label);
    for (int i = 0; status == XXWIDGETS_OK && i < 4; ++i)
        status = xxwidgets_widget_create(app, dialog, XXWIDGETS_CHECKBOX, labels[i],
            (xxwidgets_rect){bounds.x + (i & 1) * (first + 2), bounds.y + 15 + (i / 2) * 2, i & 1 ? bounds.width - first - 2 : first, 1}, &page->modes[i]);
    if (status == XXWIDGETS_OK) status = scan_page_fill(page, &page->values->options, page->values->modes);
    if (status != XXWIDGETS_OK) { (void)scan_page_destroy(page); return status; }
    *out = page;
    return XXWIDGETS_OK;
}

static xxwidgets_status scan_page_reset(void *state)
{
    xxwidgets_scan_options defaults;
    xxwidgets_status status = xxwidgets_scan_options_init(&defaults);
    return status == XXWIDGETS_OK ? scan_page_fill(state, &defaults, GUI_SCAN_MODES_DEFAULT) : status;
}

static int scan_page_apply(void *state, char *error, size_t capacity)
{
    scan_page *page = state;
    xxwidgets_scan_options options;
    unsigned int modes = 0;
    int value = 0;
    xxwidgets_status status = xxwidgets_scan_options_widget_get(page->form, &options);
    for (int i = 0; status == XXWIDGETS_OK && i < 4; ++i) {
        status = xxwidgets_widget_get_value(page->modes[i], &value);
        if (value) modes |= 1u << i;
    }
    if (status != XXWIDGETS_OK) {
        if (error && capacity) xx_rt_snprintf(error, capacity, "%s", CDIE_TR("Cannot read the selected scan options."));
        return 0;
    }
    page->values->options = options;
    page->values->modes = modes;
    return 1;
}

xxwidgets_status gui_scan_options_page(xxwidgets_options_page *page, const char *title, gui_scan_page_values *values)
{
    if (!page || !title || !values || (values->modes & ~GUI_SCAN_MODES_MASK)) return XXWIDGETS_INVALID_ARGUMENT;
    xx_rt_memset(page, 0, sizeof(*page));
    page->title = title;
    /* Wide enough for "Scan archive members (one level)" in the second column. */
    page->width = 66;
    page->height = 18;
    page->create = scan_page_create;
    page->reset = scan_page_reset;
    page->apply = scan_page_apply;
    page->destroy = scan_page_destroy;
    page->user = values;
    return XXWIDGETS_OK;
}
