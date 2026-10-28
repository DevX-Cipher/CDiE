/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "gui_settings.h"
#include "gui_backend.h"
#include "app/cdie_app.h"
#include <xxfclib/rt/xx_rt.h>
#include <xxfclib/strings/xx_string.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

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
static const char *const font_roles[XXWIDGETS_FONT_ROLE_COUNT] = {"Controls", "TableViews", "TreeViews", "TextEdits", "HexViews", "DisasmViews"};
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

/* The complete shared preference model. Keeping field definitions together
 * makes portable and native stores use the same schema and validation. */
typedef struct preference_field { const char *key; size_t offset, capacity; int boolean; } preference_field;
#define BOOL_FIELD(key,member) {key,offsetof(gui_options_values,member),0,1}
#define TEXT_FIELD(key,member) {key,offsetof(gui_options_values,member),sizeof(((gui_options_values*)0)->member),0}
static const preference_field preference_fields[] = {
    BOOL_FIELD("View/StayOnTop",appearance.stay_on_top),BOOL_FIELD("View/SingleApplication",appearance.single_application),
    TEXT_FIELD("View/Style",appearance.style),TEXT_FIELD("View/Qss",appearance.qss),TEXT_FIELD("View/Lang",appearance.language),
    BOOL_FIELD("File/SaveLastDirectory",file.save_last_directory),BOOL_FIELD("File/SaveRecentFiles",file.save_recent_files),
    BOOL_FIELD("File/SaveBackup",file.save_backup),BOOL_FIELD("File/Context",file.context_menu),BOOL_FIELD("File/SetEnvironment",file.set_environment),
    TEXT_FIELD("LastDirectory",file.last_directory),TEXT_FIELD("RecentFiles",file.recent_files),
    BOOL_FIELD("Scan/ScanAfterOpen",scan.scan_after_open),BOOL_FIELD("Scan/Flag/Recursive",scan.recursive),
    BOOL_FIELD("Scan/Log/Profiling",scan.profiling),BOOL_FIELD("Scan/Sort",scan.sort),BOOL_FIELD("Scan/UseCache",scan.use_cache),
    BOOL_FIELD("Scan/Highlight",scan.highlight),TEXT_FIELD("Scan/Engine",scan.engine),
    TEXT_FIELD("Scan/Die/Database/Main/Path",databases.main_path),TEXT_FIELD("Scan/Die/Database/Extra/Path",databases.extra_path),
    TEXT_FIELD("Scan/Die/Database/Custom/Path",databases.custom_path),TEXT_FIELD("Scan/Die/Database/Main/UpdateUrl",databases.main_update_url),
    TEXT_FIELD("Scan/Peid/Database/Path",databases.peid_path),TEXT_FIELD("Scan/Peid/Database/UpdateUrl",databases.peid_update_url),
    TEXT_FIELD("Scan/Yara/Database/Path",databases.yara_path),TEXT_FIELD("Scan/Yara/Database/UpdateUrl",databases.yara_update_url),
    TEXT_FIELD("Signatures/Path",signatures.path),TEXT_FIELD("OnlineTools/VirusTotalApi",online.virustotal_api_key),
    TEXT_FIELD("Info/Path",info.path),TEXT_FIELD("Info/Database/UpdateUrl",info.update_url),
    BOOL_FIELD("Hex/LocationColon",hex.location_colon),BOOL_FIELD("Disasm/Uppercase",disasm.uppercase),
    BOOL_FIELD("Disasm/LocationColon",disasm.location_colon),BOOL_FIELD("Disasm/Highlight",disasm.highlight)
};
static const char *const scan_color_names[XX_SCAN_COLOR_COUNT] = {
    "Unknown","Installer","Sfx","Archive","Protection","Petool","Apktool","Os","Vm","Platform","DosExtender","Format","Signtool",
    "Certificate","Licensing","Language","CorruptedData","PersonalData","Author","Virus","Trojan","Malware","Debug","DebugData","GameEngine",
    "Compiler","Compressor","Converter","Creator","Data","Database","Document","Generic","Image","InstallerData","Library","Linker","Loader",
    "Obfuscator","Overlay","Package","Player","Producer","ProtectorData","Rom","SfxData","SourceCode","Stub","Tool"
};
static void load_preferences(const xx_settings *store,gui_options_values *values)
{
    size_t i;uint64_t number;char key[192];
    for(i=0;i<sizeof(preference_fields)/sizeof(preference_fields[0]);++i){const preference_field*f=&preference_fields[i];void*v=(unsigned char*)values+f->offset;
        if(f->boolean)(void)read_bool(store,f->key,v);else(void)read_string(store,f->key,v,f->capacity);
    }
    if(!values->file.save_last_directory)memset(values->file.last_directory,0,sizeof(values->file.last_directory));
    if(!values->file.save_recent_files)memset(values->file.recent_files,0,sizeof(values->file.recent_files));
    /* Native widgets offer their platform style. Qt styles are not applied. */
    if(strcmp(values->appearance.style,"System"))strcpy(values->appearance.style,"System");
    if(read_uint(store,"Disasm/Syntax",&number)&&number<=1)values->disasm.syntax=(int)number;
    read_font(store,"Disasm/Font",&values->disasm.font);
    for(i=0;i<XX_SCAN_COLOR_COUNT;++i){xx_scan_color_record *colors[2]={&values->scan.colors.records[i],&values->scan.dark_colors.records[i]};
        for(size_t dark=0;dark<2;++dark){uint32_t*fields[2]={&colors[dark]->foreground,&colors[dark]->background};
            for(size_t part=0;part<2;++part){xx_rt_snprintf(key,sizeof(key),"Scan/Color/%s%s/%s",dark?"Dark/":"",scan_color_names[i],part?"Background":"Foreground");
                if(read_uint(store,key,&number)&&(number<=0xFFFFFFu||number==UINT32_MAX))*fields[part]=(uint32_t)number;}
        }
    }
    for(i=0;i<XXWIDGETS_DISASM_COLOR_COUNT;++i){uint32_t*fields[2]={&values->disasm.colors[i].foreground,&values->disasm.colors[i].background};
        for(size_t part=0;part<2;++part){xx_rt_snprintf(key,sizeof(key),"Disasm/Color/%s/%s",xxwidgets_disasm_color_key((xxwidgets_disasm_color_id)i),part?"Background":"Foreground");
            if(read_uint(store,key,&number)&&(number<=0xFFFFFFu||number==UINT32_MAX))*fields[part]=(uint32_t)number;}
    }
}
static void save_preferences(xx_settings *store,const gui_options_values *values,int*ok)
{
    size_t i;char key[192];
    for(i=0;i<sizeof(preference_fields)/sizeof(preference_fields[0]);++i){const preference_field*f=&preference_fields[i];const void*v=(const unsigned char*)values+f->offset;
        if(f->boolean)write_bool(store,f->key,*(const int*)v,ok);
        else if((!strcmp(f->key,"LastDirectory")&&!values->file.save_last_directory)||(!strcmp(f->key,"RecentFiles")&&!values->file.save_recent_files))write_string(store,f->key,"",ok);
        else write_string(store,f->key,v,ok);
    }
    write_uint(store,"Disasm/Syntax",(uint64_t)values->disasm.syntax,ok);write_font(store,"Disasm/Font",&values->disasm.font,ok);
    for(i=0;i<XX_SCAN_COLOR_COUNT;++i){const xx_scan_color_record *colors[2]={&values->scan.colors.records[i],&values->scan.dark_colors.records[i]};
        for(size_t dark=0;dark<2;++dark){const uint32_t fields[2]={colors[dark]->foreground,colors[dark]->background};
            for(size_t part=0;part<2;++part){xx_rt_snprintf(key,sizeof(key),"Scan/Color/%s%s/%s",dark?"Dark/":"",scan_color_names[i],part?"Background":"Foreground");write_uint(store,key,fields[part],ok);}
        }
    }
    for(i=0;i<XXWIDGETS_DISASM_COLOR_COUNT;++i){const uint32_t fields[2]={values->disasm.colors[i].foreground,values->disasm.colors[i].background};
        for(size_t part=0;part<2;++part){xx_rt_snprintf(key,sizeof(key),"Disasm/Color/%s/%s",xxwidgets_disasm_color_key((xxwidgets_disasm_color_id)i),part?"Background":"Foreground");write_uint(store,key,fields[part],ok);}
    }
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
    if (path) store = xx_settings_create_ini(path);
    else {
        char *directory = cdie_app_dir();
        store = xx_settings_create_application(directory, "NTInfo", "cdie", XX_SETTINGS_MODE_AUTO);
        xx_rt_free(directory);
    }
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
    int databases_ok = 1, stored_paths = 0, explicit_paths = 0;
    if (!store || !values || !page) return 0;
    load_preferences(store, values);
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
        if (read_string(store, database_paths[i], paths[i], size + 1)) ++explicit_paths;
        else paths[i][0] = 0;
        if (paths[i][0]) stored_paths = 1;
    }
    /* Three explicit empty values mean restore the default folders. An empty
     * store, or malformed entries alone, must preserve the current folders. */
    if (paths[0] && paths[1] && paths[2] && (stored_paths || explicit_paths == 3) && !gui_backend_db_paths_current(paths[0], paths[1], paths[2]))
        databases_ok = gui_backend_set_db_paths(paths[0], paths[1], paths[2]);
    for (i = 0; i < 3; ++i) xx_rt_free(paths[i]);
    {
        gui_backend_scan_preferences scan, previous;
        if (gui_backend_get_scan_preferences(&previous)) {
            scan.recursive = values->scan.recursive; scan.profiling = values->scan.profiling;
            scan.sort = values->scan.sort; scan.use_cache = values->scan.use_cache;
            memcpy(scan.engine, values->scan.engine, sizeof(scan.engine));
            if (!gui_backend_set_scan_preferences(&scan)) {
                values->scan.recursive = previous.recursive; values->scan.profiling = previous.profiling;
                values->scan.sort = previous.sort; values->scan.use_cache = previous.use_cache;
                memcpy(values->scan.engine, previous.engine, sizeof(values->scan.engine));
            }
        }
    }
    values->fonts.fonts[XXWIDGETS_FONT_HEX_VIEWS] = values->hex.font;
    values->fonts.fonts[XXWIDGETS_FONT_DISASM_VIEWS] = values->disasm.font;
    return databases_ok;
}

int gui_settings_save(xx_settings *store, const gui_options_values *values, gui_options_page_id page)
{
    gui_backend_optimization_options optimization;
    unsigned int modes = gui_backend_get_scan_modes();
    size_t i;
    int ok = 1;
    if (!store || !values) return 0;
    save_preferences(store, values, &ok);
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
