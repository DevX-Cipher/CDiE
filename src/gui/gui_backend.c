/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* gui_backend.c - drives the scan engine for the native GUIs. It is the mirror
 * of main_console.c's scan path: same option defaults, same db resolution, same
 * die_engine_scan_file + xx_scan_format_text, so the GUI prints exactly what `cdie` does.
 *
 * This translation unit deliberately does NOT include <windows.h>: it speaks to
 * the engine only. main_gui.c is the half that speaks Win32. */

#include "gui_backend.h"
#include "gui_result_tree.h"

#include "../global.h"
#include <xxfclib/die_engine/die_engine.h>
#include <xxfclib/die_engine/die_engine_debug.h>
#include <xxfclib/js/xx_js.h>
#include <xxfclib/buf/xx_buf.h>
#include <xxfclib/formats/executable/elf/xx_elf.h>
#include <xxfclib/formats/executable/macho/xx_macho.h>
#include <xxfclib/formats/executable/pe/xx_pe.h>
/* xx_format.h aliases fileType for format code; the scan result's member is fileType. */
#undef fileType
#include <xxfclib/fs/xx_fs.h>
#include <xxfclib/io/xx_io.h>
#include <xxfclib/strings/xx_string.h>
#include <xxfclib/global/xx_global.h>
#include <xxfclib/rt/xx_rt.h>

#include "../app/cdie_app.h"


static xx_scan_options g_options;
static DBase g_db;
static int g_bInited = 0;
static int g_bMainLoaded = 0;
static char *g_pIncludeTags = NULL;
static char *g_pExcludeTags = NULL;

/* Mirrors main_console.c's resolve_database_path with no explicit path: use
 * "<exe-dir>/<name>" when it exists, else the bare relative name. */
static char *gui_resolve_db(const char *pDefaultName)
{
    char *pAppDir = cdie_app_dir();
    char *pCandidate = xx_fs_path_join(pAppDir, pDefaultName);

    if (xx_fs_is_dir(pCandidate)) {
        xx_rt_free(pAppDir);
        return pCandidate;
    }

    xx_rt_free(pCandidate);
#if defined(__APPLE__)
    /* Finder launches bundles independently of the current working directory. */
    char *pResources = xx_fs_path_join(pAppDir, "../Resources");
    pCandidate = xx_fs_path_join(pResources, pDefaultName);
    xx_rt_free(pResources);
    if (xx_fs_is_dir(pCandidate)) {
        xx_rt_free(pAppDir);
        return pCandidate;
    }
    xx_rt_free(pCandidate);
#endif
    xx_rt_free(pAppDir);

    return cdie_strdup(pDefaultName);
}

int gui_backend_init(void)
{
    if (g_bInited) {
        return g_bMainLoaded;
    }

    xx_scan_options_init(&g_options);
    xx_rt_memset(&g_db, 0, sizeof(g_db));

    /* GUI defaults follow the Detect It Easy GUI: deep + heuristic + verbose on.
     * xx_scan_options_init has already set show type/version/info, use extra/custom
     * and sort - the settings that make the output match the console. */
    g_options.deep_scan = 1;
    g_options.heuristic_scan = 1;
    g_options.verbose = 1;
    g_options.overlay_scan = 1;

    g_options.main_database_path = gui_resolve_db("db");
    g_options.extra_database_path = gui_resolve_db("db_extra");
    g_options.custom_database_path = gui_resolve_db("db_custom");

    /* Load all three once. The "Databases" toggles gate which of them run at
     * scan time (via use_extra_database / use_custom_database), so a toggle needs
     * no reload. A missing db_extra / db_custom directory simply loads nothing. */
    g_bMainLoaded = db_load(&g_db, g_options.main_database_path, DB_MAIN);
    db_load(&g_db, g_options.extra_database_path, DB_EXTRA);
    db_load(&g_db, g_options.custom_database_path, DB_CUSTOM);
    db_sort(&g_db);

    g_bInited = 1;

    return g_bMainLoaded;
}

const char *gui_backend_main_db_path(void)
{
    return g_options.main_database_path ? g_options.main_database_path : "";
}

const char *gui_backend_extra_db_path(void)
{
    return g_options.extra_database_path ? g_options.extra_database_path : "";
}

const char *gui_backend_custom_db_path(void)
{
    return g_options.custom_database_path ? g_options.custom_database_path : "";
}

int gui_backend_set_db_paths(const char *pMain, const char *pExtra, const char *pCustom)
{
    char *pNewMain;
    char *pNewExtra;
    char *pNewCustom;
    DBase newDb;

    if (!g_bInited) {
        return 0;
    }

    pNewMain = ((pMain != NULL) && (pMain[0] != 0)) ? cdie_strdup(pMain) : gui_resolve_db("db");
    pNewExtra = ((pExtra != NULL) && (pExtra[0] != 0)) ? cdie_strdup(pExtra) : gui_resolve_db("db_extra");
    pNewCustom = ((pCustom != NULL) && (pCustom[0] != 0)) ? cdie_strdup(pCustom) : gui_resolve_db("db_custom");
    xx_rt_memset(&newDb, 0, sizeof(newDb));

    if (!pNewMain || !pNewExtra || !pNewCustom || !db_load(&newDb, pNewMain, DB_MAIN)) {
        db_free(&newDb);
        xx_rt_free(pNewMain);
        xx_rt_free(pNewExtra);
        xx_rt_free(pNewCustom);
        return 0;
    }

    db_load(&newDb, pNewExtra, DB_EXTRA);
    db_load(&newDb, pNewCustom, DB_CUSTOM);
    db_sort(&newDb);
    db_free(&g_db);
    xx_rt_free(g_options.main_database_path);
    xx_rt_free(g_options.extra_database_path);
    xx_rt_free(g_options.custom_database_path);
    g_db = newDb;
    g_options.main_database_path = pNewMain;
    g_options.extra_database_path = pNewExtra;
    g_options.custom_database_path = pNewCustom;
    g_bMainLoaded = 1;
    return 1;
}

static int gui_same_db_path(const char *pPath, const char *pDefaultName, const char *pCurrent)
{
    char *pResolved = ((pPath != NULL) && (pPath[0] != 0)) ? cdie_strdup(pPath) : gui_resolve_db(pDefaultName);
    const char *pLeft = pResolved, *pRight = pCurrent ? pCurrent : "";
    int bSame;
    if (!pResolved) return 0;
    while (*pLeft && *pLeft == *pRight) { ++pLeft; ++pRight; }
    bSame = *pLeft == *pRight;
    xx_rt_free(pResolved);
    return bSame;
}

int gui_backend_db_path_is_default(int nIndex)
{
    static const char *const names[3] = {"db", "db_extra", "db_custom"};
    const char *current = nIndex == 0 ? g_options.main_database_path : nIndex == 1 ? g_options.extra_database_path : g_options.custom_database_path;
    return g_bInited && nIndex >= 0 && nIndex < 3 && gui_same_db_path("", names[nIndex], current);
}

int gui_backend_db_paths_current(const char *pMain, const char *pExtra, const char *pCustom)
{
    return g_bInited && gui_same_db_path(pMain, "db", g_options.main_database_path) &&
           gui_same_db_path(pExtra, "db_extra", g_options.extra_database_path) &&
           gui_same_db_path(pCustom, "db_custom", g_options.custom_database_path);
}

int gui_backend_signature_count(void)
{
    return g_db.nCount;
}

char *gui_backend_used_tags(void)
{
    return db_get_used_tags(g_bInited ? &g_db : NULL);
}

int gui_backend_set_tag_filters(const char *pIncludeTags, const char *pExcludeTags)
{
    char *pNewInclude = NULL;
    char *pNewExclude = NULL;

    if (!g_bInited) {
        return 0;
    }

    if ((pIncludeTags != NULL) && (pIncludeTags[0] != 0)) {
        pNewInclude = cdie_strdup(pIncludeTags);
        if (pNewInclude == NULL) {
            return 0;
        }
    }

    if ((pExcludeTags != NULL) && (pExcludeTags[0] != 0)) {
        pNewExclude = cdie_strdup(pExcludeTags);
        if (pNewExclude == NULL) {
            xx_rt_free(pNewInclude);
            return 0;
        }
    }

    xx_rt_free(g_pIncludeTags);
    xx_rt_free(g_pExcludeTags);
    g_pIncludeTags = pNewInclude;
    g_pExcludeTags = pNewExclude;
    g_options.include_tags = g_pIncludeTags;
    g_options.exclude_tags = g_pExcludeTags;

    return 1;
}

const char *gui_backend_app_title(void)
{
    return X_APPLICATIONDISPLAYNAME " " X_APPLICATIONVERSION;
}

void gui_backend_set_flags(int bDeep, int bHeuristic, int bVerbose, int bAggressive, int bHideUnknown, int bFormat)
{
    g_options.deep_scan = bDeep ? 1 : 0;
    g_options.heuristic_scan = bHeuristic ? 1 : 0;
    g_options.verbose = bVerbose ? 1 : 0;
    g_options.aggressive_scan = bAggressive ? 1 : 0;
    g_options.hide_unknown = bHideUnknown ? 1 : 0;
    g_options.format_result = bFormat ? 1 : 0;
}

void gui_backend_set_databases(int bUseExtra, int bUseCustom)
{
    g_options.use_extra_database = bUseExtra ? 1 : 0;
    g_options.use_custom_database = bUseCustom ? 1 : 0;
}

int gui_backend_get_optimization_options(gui_backend_optimization_options *pOptions)
{
    if (!pOptions) return 0;
    pOptions->buffer_size = xx_get_buffer_size();
    pOptions->file_buffer_size = xx_get_file_buffer_size();
    pOptions->use_sse2 = xx_is_sse2_enabled() ? 1 : 0;
    pOptions->use_avx2 = xx_is_avx2_enabled() ? 1 : 0;
    pOptions->use_sse42 = xx_is_sse42_enabled() ? 1 : 0;
    pOptions->use_aes = xx_is_aes_enabled() ? 1 : 0;
    pOptions->use_sha = xx_is_sha_enabled() ? 1 : 0;
    pOptions->use_vpclmulqdq = xx_is_vpclmulqdq_enabled() ? 1 : 0;
    pOptions->use_neon = xx_is_neon_enabled() ? 1 : 0;
    pOptions->use_arm_crc32 = xx_is_arm_crc32_enabled() ? 1 : 0;
    pOptions->use_arm_crypto = xx_is_arm_crypto_enabled() ? 1 : 0;
    pOptions->use_arm_dotprod = xx_is_arm_dotprod_enabled() ? 1 : 0;
    pOptions->use_arm_pmull = xx_is_arm_pmull_enabled() ? 1 : 0;
    return 1;
}

int gui_backend_get_optimization_capabilities(gui_backend_optimization_capabilities *pCapabilities)
{
    if (!pCapabilities) return 0;
    pCapabilities->sse2 = xx_has_sse2() ? 1 : 0;
    pCapabilities->avx2 = xx_has_avx2() ? 1 : 0;
    pCapabilities->sse42 = xx_has_sse42() ? 1 : 0;
    pCapabilities->aes = xx_has_aes() ? 1 : 0;
    pCapabilities->sha = xx_has_sha() ? 1 : 0;
    pCapabilities->vpclmulqdq = xx_has_vpclmulqdq() ? 1 : 0;
    pCapabilities->neon = xx_has_neon() ? 1 : 0;
    pCapabilities->arm_crc32 = xx_has_arm_crc32() ? 1 : 0;
    pCapabilities->arm_crypto = xx_has_arm_crypto() ? 1 : 0;
    pCapabilities->arm_dotprod = xx_has_arm_dotprod() ? 1 : 0;
    pCapabilities->arm_pmull = xx_has_arm_pmull() ? 1 : 0;
    return 1;
}

int gui_backend_set_optimization_options(const gui_backend_optimization_options *pOptions)
{
    gui_backend_optimization_options options;
    if (!pOptions) return 0;
    options = *pOptions;
    if ((options.use_sse2 != 0 && options.use_sse2 != 1) ||
        (options.use_avx2 != 0 && options.use_avx2 != 1) ||
        (options.use_sse42 != 0 && options.use_sse42 != 1) ||
        (options.use_aes != 0 && options.use_aes != 1) ||
        (options.use_sha != 0 && options.use_sha != 1) ||
        (options.use_vpclmulqdq != 0 && options.use_vpclmulqdq != 1) ||
        (options.use_neon != 0 && options.use_neon != 1) ||
        (options.use_arm_crc32 != 0 && options.use_arm_crc32 != 1) ||
        (options.use_arm_crypto != 0 && options.use_arm_crypto != 1) ||
        (options.use_arm_dotprod != 0 && options.use_arm_dotprod != 1) ||
        (options.use_arm_pmull != 0 && options.use_arm_pmull != 1)) return 0;
    xx_set_buffer_size(options.buffer_size);
    xx_set_file_buffer_size(options.file_buffer_size);
    xx_set_sse2_enabled(options.use_sse2 != 0);
    xx_set_avx2_enabled(options.use_avx2 != 0);
    xx_set_sse42_enabled(options.use_sse42 != 0);
    xx_set_aes_enabled(options.use_aes != 0);
    xx_set_sha_enabled(options.use_sha != 0);
    xx_set_vpclmulqdq_enabled(options.use_vpclmulqdq != 0);
    xx_set_neon_enabled(options.use_neon != 0);
    xx_set_arm_crc32_enabled(options.use_arm_crc32 != 0);
    xx_set_arm_crypto_enabled(options.use_arm_crypto != 0);
    xx_set_arm_dotprod_enabled(options.use_arm_dotprod != 0);
    xx_set_arm_pmull_enabled(options.use_arm_pmull != 0);
    return 1;
}

typedef struct gui_file_types_context {
    gui_backend_file_type_fn pTypeFn;
    void *pUserData;
    int bStarted;
} gui_file_types_context;

/* Broad database categories do not identify separate concrete format readers. */
static int gui_concrete_file_type(XFileType type)
{
    return type != XFT_UNKNOWN && type != XFT_PE && type != XFT_ELF &&
        type != XFT_MACHO && type != XFT_ARCHIVE && type != XFT_IMAGE &&
        type != XFT_CLI_ASSEMBLY;
}

static void gui_emit_file_type(XFileType type, void *pUserData)
{
    gui_file_types_context *pContext = (gui_file_types_context *)pUserData;
    if (!pContext->bStarted) {
        pContext->pTypeFn(0, "Automatic", pContext->pUserData);
        pContext->bStarted = 1;
    }
    if (gui_concrete_file_type(type))
        pContext->pTypeFn((int)type, xft_to_string(type), pContext->pUserData);
}

int gui_backend_file_types(const char *pUtf8Path, gui_backend_file_type_fn pTypeFn, void *pUserData)
{
    gui_file_types_context context;
    if (!g_bInited) return 0;
    context.pTypeFn = pTypeFn;
    context.pUserData = pUserData;
    context.bStarted = 0;
    return die_engine_detect_file_types(pUtf8Path, pTypeFn ? gui_emit_file_type : NULL, &context);
}

int gui_backend_scan(const char *pUtf8Path, char **ppResultText, char **ppTypeName)
{
    return gui_backend_scan_results(pUtf8Path, NULL, NULL, ppResultText, ppTypeName);
}

unsigned int gui_backend_get_scan_modes(void)
{
    return (g_options.all_types_scan ? GUI_SCAN_ALL_TYPES : 0u) |
        (g_options.overlay_scan ? GUI_SCAN_OVERLAYS : 0u) |
        (g_options.resources_scan ? GUI_SCAN_RESOURCES : 0u) |
        (g_options.archives_scan ? GUI_SCAN_ARCHIVES : 0u);
}

int gui_backend_set_scan_modes(unsigned int modes)
{
    if (!g_bInited || (modes & ~GUI_SCAN_MODES_MASK)) return 0;
    g_options.all_types_scan = (modes & GUI_SCAN_ALL_TYPES) != 0;
    g_options.overlay_scan = (modes & GUI_SCAN_OVERLAYS) != 0;
    g_options.resources_scan = (modes & GUI_SCAN_RESOURCES) != 0;
    g_options.archives_scan = (modes & GUI_SCAN_ARCHIVES) != 0;
    return 1;
}

int gui_backend_scan_results(const char *pUtf8Path, gui_backend_result_fn pResultFn,
    void *pUserData, char **ppResultText, char **ppTypeName)
{
    return gui_backend_scan_results_type(pUtf8Path, 0, pResultFn, pUserData, ppResultText, ppTypeName);
}

/* Bits per address for DIE's Mode field; 0 when the architecture says nothing. */
static int arch_bits(xx_arch_t arch)
{
    switch (arch) {
    case XX_ARCH_X86_64: case XX_ARCH_ARM64: case XX_ARCH_MIPS64: case XX_ARCH_PPC64: case XX_ARCH_RISCV64: case XX_ARCH_SPARC64: return 64;
    case XX_ARCH_X86: case XX_ARCH_ARM: case XX_ARCH_MIPS: case XX_ARCH_PPC: case XX_ARCH_RISCV: case XX_ARCH_SPARC:
    case XX_ARCH_M68K: case XX_ARCH_SH: case XX_ARCH_ARC: case XX_ARCH_HPPA: return 32;
    case XX_ARCH_X86_16: case XX_ARCH_Z80: return 16;
    case XX_ARCH_AVR: return 8;
    default: return 0;
    }
}

/* The names DIE gives what the parser found. */
static const char *arch_name(xx_arch_t arch)
{
    switch (arch) {
    case XX_ARCH_X86_16: return "8086";
    case XX_ARCH_X86: return "I386";
    case XX_ARCH_X86_64: return "AMD64";
    case XX_ARCH_ARM: return "ARM";
    case XX_ARCH_ARM64: return "ARM64";
    case XX_ARCH_MIPS: return "MIPS";
    case XX_ARCH_MIPS64: return "MIPS64";
    case XX_ARCH_PPC: return "PPC";
    case XX_ARCH_PPC64: return "PPC64";
    case XX_ARCH_RISCV: return "RISCV";
    case XX_ARCH_RISCV64: return "RISCV64";
    case XX_ARCH_SPARC: return "SPARC";
    case XX_ARCH_SPARC64: return "SPARC64";
    case XX_ARCH_M68K: return "68K";
    case XX_ARCH_UNKNOWN: return "";
    default: return xx_arch_to_string(arch);
    }
}

static const char *type_name(xx_format_type_t type)
{
    switch (type) {
    case XX_TYPE_GUI_APPLICATION: return "GUI";
    case XX_TYPE_CONSOLE_APPLICATION: return "Console";
    case XX_TYPE_LIBRARY: return "DLL";
    default: return xx_type_to_string(type);
    }
}

static void copy_field(char *field, size_t capacity, const char *text)
{
    xx_rt_snprintf(field, capacity, "%s", text && xx_rt_strcmp(text, "Unknown") ? text : "");
}

/* The format parser's view of the main input, as DIE shows it above the
 * tree, for the executable families the engine's readers cover. */
static void describe_input(const char *path, const xx_scan_result *result, gui_backend_scan_info *info)
{
    xx_pe pe;
    xx_elf elf;
    xx_macho macho;
    Abstractformat *format = NULL;
    xx_io_device *device;
    xx_endian_t endian;
    xx_arch_t arch;
    int bits;
    (void)result;
    if (!(device = xx_io_file_open(path, "rb"))) return;
    xx_rt_memset(&pe, 0, sizeof(pe)); xx_rt_memset(&elf, 0, sizeof(elf)); xx_rt_memset(&macho, 0, sizeof(macho));
    xx_pe_init(&pe, device, 0);
    xx_elf_init(&elf, device, 0);
    xx_macho_init(&macho, device, 0);
    {
        /* The first reader that accepts the file. */
        Abstractformat *candidates[3] = {&pe.format, &elf.format, &macho.format};
        for (int i = 0; i < 3 && !format; ++i)
            if (candidates[i]->check_is_valid && candidates[i]->check_is_valid(candidates[i], NULL)) format = candidates[i];
    }
    if (format && (!format->handle_base_info || format->handle_base_info(format, NULL))) {
        endian = format->get_endian ? format->get_endian(format) : format->endian;
        arch = format->get_arch ? format->get_arch(format) : format->arch;
        copy_field(info->endianness, sizeof(info->endianness), endian == XX_ENDIAN_LITTLE ? "LE" : endian == XX_ENDIAN_BIG ? "BE" : "");
        bits = arch_bits(arch);
        if (bits) xx_rt_snprintf(info->mode, sizeof(info->mode), "%d-bit", bits);
        copy_field(info->architecture, sizeof(info->architecture), arch_name(arch));
        copy_field(info->type, sizeof(info->type), type_name(format->get_type ? format->get_type(format) : format->format_type));
    }
    xx_pe_destroy(&pe); xx_elf_destroy(&elf); xx_macho_destroy(&macho);
    xx_io_close(device);
}

static char *join_errors(const xx_scan_result *result)
{
    xx_buf_t log;
    int i;
    xx_buf_init(&log);
    for (i = 0; i < result->nErrorCount; ++i)
        if (result->ppErrors && result->ppErrors[i] && result->ppErrors[i][0]) xx_buf_appendf(&log, "%s\n", result->ppErrors[i]);
    if (!log.size) xx_buf_append_str(&log, "");
    return xx_buf_detach(&log, NULL);
}

static int scan_results_impl(const char *pUtf8Path, int nTypeId,
    gui_backend_result_fn pResultFn, void *pUserData, char **ppResultText, char **ppTypeName, gui_backend_tree_fn tree_fn,
    gui_backend_scan_info *pInfo, xx_pd_struct *pProgress)
{
    xx_scan_result result;
    char *pText = NULL;
    char *pTypeName = NULL;
    int i;

    if (ppResultText != NULL) {
        *ppResultText = NULL;
    }

    if (ppTypeName != NULL) {
        *ppTypeName = NULL;
    }

    if ((!g_bInited) || (pUtf8Path == NULL) || (pUtf8Path[0] == 0) ||
        nTypeId < 0 || nTypeId >= XFT_COUNT ||
        (nTypeId != 0 && !gui_concrete_file_type((XFileType)nTypeId))) {
        return 0;
    }

    xx_scan_options options = g_options;
    if (!die_engine_scan_file_type_pd(pUtf8Path, &g_db, &options, (XFileType)nTypeId, &result, pProgress)) {
        xx_scan_result_free(&result);
        return 0;
    }

    if (ppResultText != NULL) {
        pText = xx_scan_format_text(&result, &g_options);
    }

    if (ppTypeName != NULL) {
        pTypeName = cdie_strdup(xft_to_string(result.fileType));
    }
    if ((ppResultText && !pText) || (ppTypeName && !pTypeName)) {
        xx_rt_free(pText);
        xx_rt_free(pTypeName);
        xx_scan_result_free(&result);
        return 0;
    }

    if (tree_fn && !gui_result_tree_emit(&result, g_options.hide_unknown, tree_fn, pUserData))
        goto scan_allocation_failed;
    if (pInfo) {
        describe_input(pUtf8Path, &result, pInfo);
        pInfo->log = join_errors(&result);
        if (!pInfo->log) goto scan_allocation_failed;
    }

    for (i = 0; pResultFn && i < result.nCount; ++i) {
        xx_scan_record *pRecord = &result.pRecords[i];
        char *pType;
        char *pDisplayType;
        const char *pMarker;
        size_t nMarkerLength;
        size_t nLength;
        if (pRecord->is_unknown && g_options.hide_unknown) continue;
        pType = xx_scan_translate_type(pRecord->type ? pRecord->type : "");
        pMarker = pRecord->is_heuristic ? "(Heur) " : pRecord->is_aggressive_heuristic ? "(A-Heur) " : "";
        if (!pType) goto scan_allocation_failed;
        nMarkerLength = xx_rt_strlen(pMarker);
        nLength = nMarkerLength + xx_rt_strlen(pType) + 1;
        pDisplayType = (char *)xx_rt_malloc(nLength);
        if (!pDisplayType) { xx_rt_free(pType); goto scan_allocation_failed; }
        xx_rt_memcpy(pDisplayType, pMarker, nMarkerLength);
        xx_rt_memcpy(pDisplayType + nMarkerLength, pType, nLength - nMarkerLength);
        pResultFn(pDisplayType, pRecord->name ? pRecord->name : "",
            pRecord->version ? pRecord->version : "", pRecord->info ? pRecord->info : "", pUserData);
        xx_rt_free(pDisplayType);
        xx_rt_free(pType);
    }

    if (ppResultText) *ppResultText = pText;
    if (ppTypeName) *ppTypeName = pTypeName;
    xx_scan_result_free(&result);
    return 1;

scan_allocation_failed:
    xx_rt_free(pText);
    xx_rt_free(pTypeName);
    xx_scan_result_free(&result);
    return 0;
}

static const DBSignature *find_signature(const char *signature)
{
    int i;
    if (!g_bInited || !signature || !signature[0]) return NULL;
    for (i = 0; i < g_db.nCount; ++i) {
        const DBSignature *record = &g_db.pRecords[i];
        const char *path = record->pFilePath && record->pFilePath[0] ? record->pFilePath : record->pName;
        if (path && !xx_rt_strcmp(path, signature)) return record;
    }
    return NULL;
}

char *gui_backend_signature_source(const char *signature)
{
    const DBSignature *record = find_signature(signature);
    char *source;
    if (!record || !record->pText || js_is_bytecode(record->pText, record->nSize)) return NULL;
    source = (char *)xx_rt_malloc(record->nSize + 1);
    if (!source) return NULL;
    xx_rt_memcpy(source, record->pText, record->nSize);
    source[record->nSize] = 0;
    return source;
}

static void append_log(const char *text, void *user)
{
    xx_buf_append_str((xx_buf_t *)user, text);
}

int gui_backend_run_signature(const char *path, int type_id, const char *signature, const char *source, char **text)
{
    xx_scan_options options;
    xx_scan_result result;
    die_engine_debug_config config;
    xx_buf_t log, out;
    char *detected;
    int ok, i;
    if (text) *text = NULL;
    if (!g_bInited || !path || !path[0] || !source || !text || type_id < 0 || type_id >= XFT_COUNT ||
        (type_id && !gui_concrete_file_type((XFileType)type_id)))
        return 0;
    xx_buf_init(&log);
    xx_buf_init(&out);
    xx_rt_memset(&config, 0, sizeof(config));
    config.size = sizeof(config);
    config.version = DIE_ENGINE_DEBUG_VERSION;
    config.log = append_log;
    config.log_user = &log;
    config.signature_source = source;
    config.signature_name = signature;
    /* One input, as the debugger requires: no child scans of its parts. */
    options = g_options;
    options.recursive_scan = false;
    options.all_types_scan = false;
    options.overlay_scan = false;
    options.resources_scan = false;
    options.archives_scan = false;
    ok = die_engine_scan_file_type_debug_pd(path, &g_db, &options, (XFileType)type_id, &result, NULL, &config);
    detected = ok ? xx_scan_format_text(&result, &options) : NULL;
    if (detected) { xx_buf_append_str(&out, detected); xx_rt_free(detected); }
    for (i = 0; i < result.nErrorCount; ++i)
        if (result.ppErrors && result.ppErrors[i]) xx_buf_appendf(&out, "%s\n", result.ppErrors[i]);
    if (log.size) {
        if (out.size && out.data[out.size - 1] != '\n') xx_buf_append_str(&out, "\n");
        xx_buf_append(&out, log.data, log.size);
    }
    xx_scan_result_free(&result);
    xx_buf_free(&log);
    *text = xx_buf_detach(&out, NULL);
    return ok;
}

/* <dir>/info/html/<name>.html for the directory the main database is in, the
 * executable's and, in a macOS bundle, Resources. */
static char *read_info(const char *dir, const char *file)
{
    char *info = xx_fs_path_join(dir, "info"), *html = info ? xx_fs_path_join(info, "html") : NULL;
    char *path = html ? xx_fs_path_join(html, file) : NULL, *data = NULL;
    if (path && xx_fs_is_file(path)) data = xx_fs_read_file(path, NULL);
    xx_rt_free(info); xx_rt_free(html); xx_rt_free(path);
    return data;
}

/* The descriptions are short HTML or plain text: drop tags, keep paragraph
 * breaks and decode the common entities. */
static char *info_plain_text(const char *html)
{
    xx_buf_t out;
    const char *s = html;
    xx_buf_init(&out);
    while (*s) {
        if (*s == '<') {
            const char *end = s + 1;
            while (*end && *end != '>') ++end;
            if ((end - s >= 3 && (s[1] == 'b' || s[1] == 'B') && (s[2] == 'r' || s[2] == 'R')) ||
                (end - s >= 2 && (s[1] == 'p' || s[1] == 'P')) || (end - s >= 3 && s[1] == '/' && (s[2] == 'p' || s[2] == 'P')))
                xx_buf_append_str(&out, "\n");
            s = *end ? end + 1 : end;
        } else if (*s == '&') {
            static const struct { const char *entity, *text; } entities[] = {
                {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&#39;", "'"}, {"&nbsp;", " "}};
            size_t k;
            for (k = 0; k < sizeof(entities) / sizeof(entities[0]); ++k) {
                size_t length = xx_rt_strlen(entities[k].entity);
                if (!xx_rt_strncmp(s, entities[k].entity, length)) { xx_buf_append_str(&out, entities[k].text); s += length; break; }
            }
            if (k == sizeof(entities) / sizeof(entities[0])) { xx_buf_append_char(&out, *s); ++s; }
        } else if (*s != '\r') {
            xx_buf_append_char(&out, *s); ++s;
        } else ++s;
    }
    return xx_buf_detach(&out, NULL);
}

char *gui_backend_info_text(const char *name)
{
    char *file, *data = NULL, *text;
    const char *s;
    if (!name || !name[0]) return NULL;
    /* A name is a file name here, never a path. */
    for (s = name; *s; ++s)
        if (*s == '/' || *s == '\\' || *s == ':' || (unsigned char)*s < 32) return NULL;
    if (!xx_rt_strcmp(name, ".") || !xx_rt_strcmp(name, "..")) return NULL;
    file = (char *)xx_rt_malloc(xx_rt_strlen(name) + 6);
    if (!file) return NULL;
    xx_rt_memcpy(file, name, xx_rt_strlen(name));
    xx_rt_memcpy(file + xx_rt_strlen(name), ".html", 6);
    if (g_options.main_database_path) {
        char *parent = xx_fs_path_join(g_options.main_database_path, "..");
        if (parent) data = read_info(parent, file);
        xx_rt_free(parent);
    }
    if (!data) {
        char *app = cdie_app_dir();
        if (app) data = read_info(app, file);
#if defined(__APPLE__)
        if (!data && app) {
            char *resources = xx_fs_path_join(app, "../Resources");
            if (resources) data = read_info(resources, file);
            xx_rt_free(resources);
        }
#endif
        xx_rt_free(app);
    }
    xx_rt_free(file);
    if (!data) return NULL;
    text = info_plain_text(data);
    xx_str_free(data);
    return text;
}

int gui_backend_scan_results_type(const char *path, int type_id, gui_backend_result_fn callback,
    void *user, char **report, char **type_name)
{
    return scan_results_impl(path, type_id, callback, user, report, type_name, NULL, NULL, NULL);
}

int gui_backend_scan_tree(const char *path, int type_id, gui_backend_tree_fn callback,
    void *user, char **report, char **type_name, gui_backend_scan_info *info, xx_pd_struct *progress)
{
    if (info) xx_rt_memset(info, 0, sizeof(*info));
    if (!callback) return 0;
    return scan_results_impl(path, type_id, NULL, user, report, type_name, callback, info, progress);
}

int gui_backend_scan_detections(const char *path, unsigned int flags, unsigned int databases,
    gui_backend_detection_fn callback, void *user)
{
    xx_scan_options options;
    xx_scan_result result;
    int i;
    if (!g_bInited || !path || !path[0] || !callback) return 0;
    options = g_options;
    options.deep_scan = (flags & 1u) != 0;
    options.heuristic_scan = (flags & 2u) != 0;
    options.verbose = (flags & 4u) != 0;
    options.aggressive_scan = (flags & 8u) != 0;
    options.hide_unknown = (flags & 16u) != 0;
    options.use_extra_database = (databases & 1u) != 0;
    options.use_custom_database = (databases & 2u) != 0;
    if (!die_engine_scan_file_type_pd(path, &g_db, &options, XFT_UNKNOWN, &result, NULL)) {
        xx_scan_result_free(&result);
        return 0;
    }
    for (i = 0; i < result.nCount; ++i) {
        const xx_scan_record *record = &result.pRecords[i];
        if (record->is_unknown && options.hide_unknown) continue;
        callback(record->type ? record->type : "", record->name ? record->name : "",
            record->version ? record->version : "", record->info ? record->info : "", user);
    }
    xx_scan_result_free(&result);
    return 1;
}

int gui_backend_signature_entry(int index, const char **display, const char **path)
{
    const DBSignature *record;
    const char *full, *base, *folder, *s;
    if (!g_bInited || index < 0 || index >= g_db.nCount) return 0;
    record = &g_db.pRecords[index];
    full = record->pFilePath && record->pFilePath[0] ? record->pFilePath : record->pName;
    if (path) *path = full;
    if (display) {
        /* The folder and file name: "PE/_Microsoft.6.sg". */
        base = folder = full;
        for (s = full; *s; ++s)
            if (*s == '/' || *s == '\\') { folder = base; base = s + 1; }
        *display = folder;
    }
    return 1;
}

void gui_backend_free(void *pPtr)
{
    xx_rt_free(pPtr);
}

void gui_backend_shutdown(void)
{
    if (g_bInited) {
        db_free(&g_db);
        xx_rt_free(g_pIncludeTags);
        xx_rt_free(g_pExcludeTags);
        g_pIncludeTags = NULL;
        g_pExcludeTags = NULL;
        g_options.include_tags = NULL;
        g_options.exclude_tags = NULL;
        xx_scan_options_free(&g_options);
        xx_rt_memset(&g_db, 0, sizeof(g_db));
        g_bInited = 0;
        g_bMainLoaded = 0;
    }
}
