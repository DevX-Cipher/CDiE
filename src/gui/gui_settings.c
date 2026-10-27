/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "gui_settings.h"
#include "gui_backend.h"
#include "app/cdie_app.h"
#include <xxfclib/fs/xx_fs.h>
#include <xxfclib/rt/xx_rt.h>
#include <xxfclib/strings/xx_string.h>
#include <stdlib.h>
#include <string.h>

static const struct { const char *key; unsigned int bit; } scan_flags[] = {
    {"Scan/Flag/Deep", XXWIDGETS_SCAN_DEEP}, {"Scan/Flag/Heuristic", XXWIDGETS_SCAN_HEURISTIC},
    {"Scan/Flag/Verbose", XXWIDGETS_SCAN_VERBOSE}, {"Scan/Flag/Aggressive", XXWIDGETS_SCAN_AGGRESSIVE},
    {"Scan/HideUnknown", XXWIDGETS_SCAN_HIDE_UNKNOWN}, {"Scan/FormatResult", XXWIDGETS_SCAN_FORMAT}};
static const struct { const char *key; unsigned int bit; } scan_databases[] = {
    {"Scan/Die/Database/Extra/Enabled", XXWIDGETS_SCAN_DATABASE_EXTRA}, {"Scan/Die/Database/Custom/Enabled", XXWIDGETS_SCAN_DATABASE_CUSTOM}};
static const struct { const char *key; unsigned int bit; } scan_modes[] = {
    {"Scan/Flag/AllTypes", GUI_SCAN_ALL_TYPES}, {"Scan/Flag/Overlay", GUI_SCAN_OVERLAYS},
    {"Scan/Flag/Resources", GUI_SCAN_RESOURCES}, {"Scan/Flag/Archives", GUI_SCAN_ARCHIVES}};
static const char *const database_paths[3] = {"Scan/Die/Database/Main/Path", "Scan/Die/Database/Extra/Path", "Scan/Die/Database/Custom/Path"};
static const char *const font_roles[XXWIDGETS_FONT_ROLE_COUNT] = {"Controls", "TableViews", "TreeViews", "TextEdits"};
static const char *const feature_keys[11] = {"Feature/SSE2", "Feature/AVX2", "Feature/SSE42", "Feature/AES", "Feature/SHA", "Feature/VPCLMULQDQ",
                                             "Feature/NEON", "Feature/ArmCRC32", "Feature/ArmCrypto", "Feature/ArmDotProd", "Feature/ArmPMULL"};

static int *feature_field(gui_backend_optimization_options *options, size_t index)
{
    int *const fields[11] = {&options->use_sse2, &options->use_avx2, &options->use_sse42, &options->use_aes, &options->use_sha, &options->use_vpclmulqdq,
                             &options->use_neon, &options->use_arm_crc32, &options->use_arm_crypto, &options->use_arm_dotprod, &options->use_arm_pmull};
    return fields[index];
}

static int read_bool(const xx_settings *store, const char *key, int *value)
{
    const xx_settings_value *stored = xx_settings_get(store, key);
    if (!stored || stored->type != XX_SETTINGS_VALUE_BOOL) return 0;
    *value = stored->data.boolean ? 1 : 0;
    return 1;
}

static int read_uint(const xx_settings *store, const char *key, uint64_t *value)
{
    const xx_settings_value *stored = xx_settings_get(store, key);
    if (stored && stored->type == XX_SETTINGS_VALUE_UINT64) *value = stored->data.unsigned_integer;
    else if (stored && stored->type == XX_SETTINGS_VALUE_INT64 && stored->data.integer >= 0) *value = (uint64_t)stored->data.integer;
    else return 0;
    return 1;
}

/* Copies a stored string; fails for a missing key, a wrong type or a value
 * that does not fit capacity (including its NUL). */
static int read_string(const xx_settings *store, const char *key, char *buffer, size_t capacity)
{
    const xx_settings_value *stored = xx_settings_get(store, key);
    if (!stored || stored->type != XX_SETTINGS_VALUE_STRING || stored->data.buffer.size >= capacity ||
        (stored->data.buffer.size && memchr(stored->data.buffer.data, 0, stored->data.buffer.size)))
        return 0;
    memcpy(buffer, stored->data.buffer.data, stored->data.buffer.size);
    buffer[stored->data.buffer.size] = 0;
    return 1;
}

static void read_bits(const xx_settings *store, const char *key, unsigned int bit, unsigned int *bits)
{
    int value;
    if (!read_bool(store, key, &value)) return;
    if (value) *bits |= bit;
    else *bits &= ~bit;
}

static void read_font(const xx_settings *store, const char *prefix, xxwidgets_font *font)
{
    char key[96], family[sizeof(font->family)];
    uint64_t size;
    int value;
    xx_rt_snprintf(key, sizeof(key), "%s/Family", prefix);
    /* Only the text: the descriptors are compared bytewise to detect changes. */
    if (read_string(store, key, family, sizeof(family))) {
        memset(font->family, 0, sizeof(font->family));
        memcpy(font->family, family, strlen(family) + 1);
    }
    xx_rt_snprintf(key, sizeof(key), "%s/Size", prefix);
    if (read_uint(store, key, &size) && (size == 0 || (size >= 4 && size <= 96))) font->point_size = (unsigned int)size;
    xx_rt_snprintf(key, sizeof(key), "%s/Bold", prefix);
    if (read_bool(store, key, &value)) font->bold = value;
    xx_rt_snprintf(key, sizeof(key), "%s/Italic", prefix);
    if (read_bool(store, key, &value)) font->italic = value;
}

static void write_bool(xx_settings *store, const char *key, int value, int *ok)
{
    xx_settings_value stored;
    memset(&stored, 0, sizeof(stored));
    stored.type = XX_SETTINGS_VALUE_BOOL;
    stored.data.boolean = value != 0;
    if (xx_settings_set(store, key, &stored) != XXFC_OK) *ok = 0;
}

static void write_uint(xx_settings *store, const char *key, uint64_t value, int *ok)
{
    xx_settings_value stored;
    memset(&stored, 0, sizeof(stored));
    stored.type = XX_SETTINGS_VALUE_UINT64;
    stored.data.unsigned_integer = value;
    if (xx_settings_set(store, key, &stored) != XXFC_OK) *ok = 0;
}

static void write_string(xx_settings *store, const char *key, const char *value, int *ok)
{
    xx_settings_value stored;
    memset(&stored, 0, sizeof(stored));
    stored.type = XX_SETTINGS_VALUE_STRING;
    stored.data.buffer.data = value;
    stored.data.buffer.size = strlen(value);
    if (xx_settings_set(store, key, &stored) != XXFC_OK) *ok = 0;
}

static void write_font(xx_settings *store, const char *prefix, const xxwidgets_font *font, int *ok)
{
    char key[96];
    xx_rt_snprintf(key, sizeof(key), "%s/Family", prefix);
    write_string(store, key, font->family, ok);
    xx_rt_snprintf(key, sizeof(key), "%s/Size", prefix);
    write_uint(store, key, font->point_size, ok);
    xx_rt_snprintf(key, sizeof(key), "%s/Bold", prefix);
    write_bool(store, key, font->bold, ok);
    xx_rt_snprintf(key, sizeof(key), "%s/Italic", prefix);
    write_bool(store, key, font->italic, ok);
}

/* CDIE_SETTINGS names an INI file, for tests and side-by-side setups. */
static char *settings_override(void)
{
#ifdef _WIN32
    wchar_t *environment = _wgetenv(L"CDIE_SETTINGS");
    char *converted, *path;
    if (!environment || !environment[0]) return NULL;
    converted = xx_str_unicode_to_utf8(environment);
    path = converted ? cdie_strdup(converted) : NULL;
    xx_str_free(converted);
    return path;
#else
    const char *environment = getenv("CDIE_SETTINGS");
    return environment && environment[0] ? cdie_strdup(environment) : NULL;
#endif
}

xx_settings *gui_settings_open(int memory_only, int *loaded)
{
    xx_settings *store;
    char *path = NULL;
    if (loaded) *loaded = 1;
    if (memory_only) return xx_settings_create_memory();
    path = settings_override();
    if (!path) {
        /* A "portable" marker keeps the settings beside the executable, as in DIE. */
        char *directory = cdie_app_dir();
        char *marker = directory ? xx_fs_path_join(directory, "portable") : NULL;
        if (marker && xx_fs_is_file(marker)) path = xx_fs_path_join(directory, "cdie.ini");
        xx_rt_free(marker);
        xx_rt_free(directory);
    }
    store = path ? xx_settings_create_ini(path) : xx_settings_create_native("NTInfo", "cdie");
    xx_rt_free(path);
    if (store && xx_settings_load(store) == XXFC_OK) return store;
    if (loaded) *loaded = 0;
    /* Unreadable storage: keep working with defaults, but never overwrite it. */
    if (store) xx_settings_destroy(store);
    return xx_settings_create_memory();
}

void gui_settings_close(xx_settings *store)
{
    if (store) xx_settings_destroy(store);
}

int gui_settings_load(xx_settings *store, gui_options_values *values, gui_options_page_id *page)
{
    gui_backend_optimization_options optimization;
    char *paths[3] = {NULL, NULL, NULL};
    uint64_t number;
    unsigned int modes = gui_backend_get_scan_modes();
    size_t i;
    int databases_ok = 1, stored_paths = 0;
    if (!store || !values || !page) return 0;
    for (i = 0; i < sizeof(scan_flags) / sizeof(scan_flags[0]); ++i) read_bits(store, scan_flags[i].key, scan_flags[i].bit, &values->scan.options.flags);
    for (i = 0; i < sizeof(scan_databases) / sizeof(scan_databases[0]); ++i)
        read_bits(store, scan_databases[i].key, scan_databases[i].bit, &values->scan.options.databases);
    for (i = 0; i < sizeof(scan_modes) / sizeof(scan_modes[0]); ++i) read_bits(store, scan_modes[i].key, scan_modes[i].bit, &modes);
    gui_backend_set_scan_modes(modes);
    values->scan.modes = gui_backend_get_scan_modes();

    for (i = 0; i < XXWIDGETS_FONT_ROLE_COUNT; ++i) {
        char prefix[48];
        xx_rt_snprintf(prefix, sizeof(prefix), "View/Font/%s", font_roles[i]);
        read_font(store, prefix, &values->fonts.fonts[i]);
    }
    if (read_uint(store, "Hex/BytesPerRow", &number) &&
        (number == 8 || number == 16 || number == 24 || number == 32 || number == 48 || number == 64))
        values->hex.bytes_per_row = (unsigned int)number;
    {
        int bold;
        if (read_bool(store, "Hex/BoldNonZero", &bold)) values->hex.bold_nonzero = bold;
    }
    read_font(store, "Hex/Font", &values->hex.font);
    if (read_uint(store, "Options/LastPage", &number) && number < GUI_OPTIONS_PAGE_COUNT) *page = (gui_options_page_id)number;

    /* The backend validates and masks what this CPU cannot use. */
    if (gui_backend_get_optimization_options(&optimization)) {
        gui_backend_optimization_options stored = optimization;
        if (read_uint(store, "Feature/ReadBufferSize", &number) && number <= SIZE_MAX) stored.buffer_size = (size_t)number;
        if (read_uint(store, "Feature/FileBufferSize", &number) && number <= SIZE_MAX) stored.file_buffer_size = (size_t)number;
        for (i = 0; i < 11; ++i) (void)read_bool(store, feature_keys[i], feature_field(&stored, i));
        if (!gui_backend_set_optimization_options(&stored)) (void)gui_backend_set_optimization_options(&optimization);
    }

    for (i = 0; i < 3; ++i) {
        const xx_settings_value *stored = xx_settings_get(store, database_paths[i]);
        size_t size = stored && stored->type == XX_SETTINGS_VALUE_STRING ? stored->data.buffer.size : 0;
        paths[i] = (char *)xx_rt_malloc(size + 1);
        if (!paths[i]) continue;
        paths[i][0] = 0;
        if (size && !read_string(store, database_paths[i], paths[i], size + 1)) paths[i][0] = 0;
        if (paths[i][0]) stored_paths = 1;
    }
    if (paths[0] && paths[1] && paths[2] && stored_paths && !gui_backend_db_paths_current(paths[0], paths[1], paths[2]))
        databases_ok = gui_backend_set_db_paths(paths[0], paths[1], paths[2]);
    for (i = 0; i < 3; ++i) xx_rt_free(paths[i]);
    return databases_ok;
}

int gui_settings_save(xx_settings *store, const gui_options_values *values, gui_options_page_id page)
{
    gui_backend_optimization_options optimization;
    unsigned int modes = gui_backend_get_scan_modes();
    size_t i;
    int ok = 1;
    if (!store || !values) return 0;
    for (i = 0; i < sizeof(scan_flags) / sizeof(scan_flags[0]); ++i)
        write_bool(store, scan_flags[i].key, (values->scan.options.flags & scan_flags[i].bit) != 0, &ok);
    for (i = 0; i < sizeof(scan_databases) / sizeof(scan_databases[0]); ++i)
        write_bool(store, scan_databases[i].key, (values->scan.options.databases & scan_databases[i].bit) != 0, &ok);
    for (i = 0; i < sizeof(scan_modes) / sizeof(scan_modes[0]); ++i) write_bool(store, scan_modes[i].key, (modes & scan_modes[i].bit) != 0, &ok);
    write_string(store, database_paths[0], gui_backend_db_path_is_default(0) ? "" : gui_backend_main_db_path(), &ok);
    write_string(store, database_paths[1], gui_backend_db_path_is_default(1) ? "" : gui_backend_extra_db_path(), &ok);
    write_string(store, database_paths[2], gui_backend_db_path_is_default(2) ? "" : gui_backend_custom_db_path(), &ok);
    if (gui_backend_get_optimization_options(&optimization)) {
        write_uint(store, "Feature/ReadBufferSize", optimization.buffer_size, &ok);
        write_uint(store, "Feature/FileBufferSize", optimization.file_buffer_size, &ok);
        for (i = 0; i < 11; ++i) write_bool(store, feature_keys[i], *feature_field(&optimization, i), &ok);
    } else ok = 0;
    for (i = 0; i < XXWIDGETS_FONT_ROLE_COUNT; ++i) {
        char prefix[48];
        xx_rt_snprintf(prefix, sizeof(prefix), "View/Font/%s", font_roles[i]);
        write_font(store, prefix, &values->fonts.fonts[i], &ok);
    }
    write_uint(store, "Hex/BytesPerRow", values->hex.bytes_per_row, &ok);
    write_bool(store, "Hex/BoldNonZero", values->hex.bold_nonzero, &ok);
    write_font(store, "Hex/Font", &values->hex.font, &ok);
    write_uint(store, "Options/LastPage", (uint64_t)page, &ok);
    return ok && xx_settings_save(store) == XXFC_OK;
}
