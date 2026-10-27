/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "gui_options.h"
#include "gui_backend.h"
#include "app/cdie_translation.h"
#include <xxfclib/rt/xx_rt.h>

typedef struct options_session {
    const gui_options_host *host;
    int reloaded;
} options_session;

static xxwidgets_status browse_database(xxwidgets_widget *dialog, const char *initial, char **path, int *accepted, void *user)
{
    const options_session *session = user;
    return session->host->browse(dialog, initial, path, accepted, session->host->user);
}

/* OK reloads the signatures only when the folders actually change. */
static int apply_databases(const char *const paths[3], char *error, size_t capacity, void *user)
{
    options_session *session = user;
    if (gui_backend_db_paths_current(paths[0], paths[1], paths[2])) return 1;
    if (gui_backend_set_db_paths(paths[0], paths[1], paths[2])) {
        session->reloaded = 1;
        return 1;
    }
    xx_rt_snprintf(error, capacity, "%s", CDIE_TR("Cannot load the main signature database. Check its folder and try again."));
    return 0;
}

static void optimization_to_page(const gui_backend_optimization_options *from, const gui_backend_optimization_capabilities *features,
                                 xxwidgets_optimization_page_values *to)
{
    to->options.buffer_size = from->buffer_size;
    to->options.file_buffer_size = from->file_buffer_size;
    to->options.use_sse2 = from->use_sse2; to->options.use_avx2 = from->use_avx2; to->options.use_sse42 = from->use_sse42;
    to->options.use_aes = from->use_aes; to->options.use_sha = from->use_sha; to->options.use_vpclmulqdq = from->use_vpclmulqdq;
    to->options.use_neon = from->use_neon; to->options.use_arm_crc32 = from->use_arm_crc32; to->options.use_arm_crypto = from->use_arm_crypto;
    to->options.use_arm_dotprod = from->use_arm_dotprod; to->options.use_arm_pmull = from->use_arm_pmull;
    to->capabilities.sse2 = features->sse2; to->capabilities.avx2 = features->avx2; to->capabilities.sse42 = features->sse42;
    to->capabilities.aes = features->aes; to->capabilities.sha = features->sha; to->capabilities.vpclmulqdq = features->vpclmulqdq;
    to->capabilities.neon = features->neon; to->capabilities.arm_crc32 = features->arm_crc32; to->capabilities.arm_crypto = features->arm_crypto;
    to->capabilities.arm_dotprod = features->arm_dotprod; to->capabilities.arm_pmull = features->arm_pmull;
}

static void optimization_from_page(const xxwidgets_optimization_options *from, gui_backend_optimization_options *to)
{
    to->buffer_size = from->buffer_size;
    to->file_buffer_size = from->file_buffer_size;
    to->use_sse2 = from->use_sse2; to->use_avx2 = from->use_avx2; to->use_sse42 = from->use_sse42;
    to->use_aes = from->use_aes; to->use_sha = from->use_sha; to->use_vpclmulqdq = from->use_vpclmulqdq;
    to->use_neon = from->use_neon; to->use_arm_crc32 = from->use_arm_crc32; to->use_arm_crypto = from->use_arm_crypto;
    to->use_arm_dotprod = from->use_arm_dotprod; to->use_arm_pmull = from->use_arm_pmull;
}

xxwidgets_status gui_options_dialog(xxwidgets_widget *owner, const gui_options_host *host, gui_options_page_id *page,
                                    gui_options_values *values, int *accepted, int *databases_reloaded)
{
    xxwidgets_options_page pages[GUI_OPTIONS_PAGE_COUNT];
    gui_options_page_id ids[GUI_OPTIONS_PAGE_COUNT];
    options_session session;
    gui_options_values edited;
    xxwidgets_database_paths_page_values databases;
    xxwidgets_hex_page_values hex;
    xxwidgets_optimization_page_values optimization;
    gui_backend_optimization_options current;
    gui_backend_optimization_capabilities features;
    xxwidgets_context_config context = {"cdie", "Scan with cdie", "*", NULL, XXWIDGETS_CONTEXT_CURRENT_USER};
    xxwidgets_status status = XXWIDGETS_OK;
    size_t count = 0, shown = 0, i;
    if (accepted) *accepted = 0;
    if (databases_reloaded) *databases_reloaded = 0;
    if (!owner || !host || !page || !values || !accepted || !databases_reloaded) return XXWIDGETS_INVALID_ARGUMENT;
    if (!gui_backend_get_optimization_options(&current) || !gui_backend_get_optimization_capabilities(&features)) return XXWIDGETS_PLATFORM_ERROR;
    session.host = host;
    session.reloaded = 0;
    edited = *values;
    databases.paths[0] = gui_backend_main_db_path();
    databases.paths[1] = gui_backend_extra_db_path();
    databases.paths[2] = gui_backend_custom_db_path();
    databases.callbacks.browse = host->browse ? browse_database : NULL;
    databases.callbacks.apply = apply_databases;
    databases.user = &session;
    hex.options = edited.hex;
    hex.show_editable = 0;
    optimization_to_page(&current, &features, &optimization);

    for (i = 0; status == XXWIDGETS_OK && i < GUI_OPTIONS_PAGE_COUNT; ++i) {
        gui_options_page_id id = (gui_options_page_id)i;
        if (!(host->pages & GUI_OPTIONS_PAGE(id))) continue;
        switch (id) {
            case GUI_OPTIONS_FILE:
                /* The context menu exists only where the platform has one. */
                context.executable = host->executable;
                if (!host->executable || xxwidgets_options_page_context(&pages[count], CDIE_TR("File"), &context) != XXWIDGETS_OK) continue;
                break;
            case GUI_OPTIONS_FONTS: status = xxwidgets_options_page_fonts(&pages[count], CDIE_TR("Fonts"), &edited.fonts); break;
            case GUI_OPTIONS_SCAN: status = gui_scan_options_page(&pages[count], CDIE_TR("Scan"), &edited.scan); break;
            case GUI_OPTIONS_SIGNATURES: status = xxwidgets_options_page_database_paths(&pages[count], CDIE_TR("Signatures"), &databases); break;
            case GUI_OPTIONS_HEX: status = xxwidgets_options_page_hex(&pages[count], CDIE_TR("Hex"), &hex); break;
            case GUI_OPTIONS_OPTIMIZATION: status = xxwidgets_options_page_optimization(&pages[count], CDIE_TR("Optimization"), &optimization); break;
            default: continue;
        }
        if (status == XXWIDGETS_OK) {
            if (id == *page) shown = count;
            ids[count++] = id;
        }
    }
    if (status == XXWIDGETS_OK && !count) status = XXWIDGETS_INVALID_ARGUMENT;
    if (status == XXWIDGETS_OK) status = xxwidgets_options_pages_dialog(owner, CDIE_TR("Options"), pages, count, &shown, accepted);
    *databases_reloaded = session.reloaded;
    if (status != XXWIDGETS_OK) return status;
    *page = ids[shown];
    if (!*accepted) return XXWIDGETS_OK;
    optimization_from_page(&optimization.options, &current);
    if (!gui_backend_set_optimization_options(&current)) {
        *accepted = 0;
        return XXWIDGETS_PLATFORM_ERROR;
    }
    edited.hex = hex.options;
    *values = edited;
    return XXWIDGETS_OK;
}
