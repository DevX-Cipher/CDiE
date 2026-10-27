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

/* gui_backend.h - the engine side of the native desktop GUIs.
 *
 * The scan engine headers (<windows.h>-hostile: xpe.h and friends define their
 * own PE structures) never meet <windows.h>. gui_backend.c includes the engine;
 * main_gui.c includes <windows.h>. This header is the only thing they share, so
 * it exposes plain C types alone - no engine structs, no Win32 types leak.
 * The one xxfclib type, xx_pd_struct, is plain data shared with xxwidgets. */

#ifndef CDIE_GUI_BACKEND_H
#define CDIE_GUI_BACKEND_H

#include <stddef.h>
#include <xxfclib/data/xx_pd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Resolves db/db_extra/db_custom next to the executable, then in the macOS
 * bundle's Resources directory. Loads all three once and sorts them, regardless
 * of the toggles, because the "Databases" toggles gate which databases actually
 * contribute at scan time - so changing them never needs a reload. Returns 1
 * when the main database yielded at least one signature, else 0. */
int gui_backend_init(void);

/* The resolved main / extra / custom database paths (UTF-8), for the Options
 * dialog and the "not found" message. Owned by the backend; valid until
 * gui_backend_shutdown or the next gui_backend_set_db_paths; do not free. */
const char *gui_backend_main_db_path(void);
const char *gui_backend_extra_db_path(void);
const char *gui_backend_custom_db_path(void);

/* Replaces the three database paths (UTF-8; a NULL or empty entry re-resolves
 * that slot's default next to the executable) and reloads all three. Returns 1
 * when the main database loaded at least one signature, else 0. Failure leaves
 * the previous paths and loaded database intact. */
int gui_backend_set_db_paths(const char *pMain, const char *pExtra, const char *pCustom);

/* 1 when the paths, resolved as gui_backend_set_db_paths would, equal the
 * loaded ones, so applying them needs no reload. */
int gui_backend_db_paths_current(const char *pMain, const char *pExtra, const char *pCustom);

/* 1 when database nIndex (0 main, 1 extra, 2 custom) is the default folder. */
int gui_backend_db_path_is_default(int nIndex);

/* Total number of signatures loaded across the three databases. */
int gui_backend_signature_count(void);

/* Returns the unique tags used by loaded signatures, sorted and joined with
 * semicolons. The returned UTF-8 string is allocated by the engine and must be
 * released with gui_backend_free. An initialized database with no tags yields
 * an allocated empty string. */
char *gui_backend_used_tags(void);

/* Replaces the scan-time tag filters. Both arguments are semicolon-separated
 * UTF-8 tag lists; NULL and empty strings mean no filter. The backend copies
 * the strings, so callers may release or reuse their buffers after return.
 * Allocation failure leaves both previous filters unchanged. */
int gui_backend_set_tag_filters(const char *pIncludeTags, const char *pExcludeTags);

/* "cdie <version>" (UTF-8) for the window title. Static storage; do not free. */
const char *gui_backend_app_title(void);

/* Sets the scan-flag toggles (non-zero = on). */
void gui_backend_set_flags(int bDeep, int bHeuristic, int bVerbose, int bAggressive, int bHideUnknown, int bFormat);

/* Selects which databases contribute (the main database is always used). */
void gui_backend_set_databases(int bUseExtra, int bUseCustom);

enum {
    GUI_SCAN_ALL_TYPES = 1u << 0,
    GUI_SCAN_OVERLAYS = 1u << 1,
    GUI_SCAN_RESOURCES = 1u << 2,
    GUI_SCAN_ARCHIVES = 1u << 3,
    GUI_SCAN_MODES_MASK = 15u,
    GUI_SCAN_MODES_DEFAULT = GUI_SCAN_OVERLAYS
};

/* Expansion modes apply to both the tree and its report. Overlay scanning is
 * enabled initially, preserving the interactive frontend's existing default.
 * Invalid bits leave the current settings unchanged. Call between scans. */
unsigned int gui_backend_get_scan_modes(void);
int gui_backend_set_scan_modes(unsigned int modes);

typedef struct gui_backend_optimization_options {
    size_t buffer_size;
    size_t file_buffer_size;
    int use_sse2;
    int use_avx2;
    int use_sse42;
    int use_aes;
    int use_sha;
    int use_vpclmulqdq;
    int use_neon;
    int use_arm_crc32;
    int use_arm_crypto;
    int use_arm_dotprod;
    int use_arm_pmull;
} gui_backend_optimization_options;

typedef struct gui_backend_optimization_capabilities {
    int sse2;
    int avx2;
    int sse42;
    int aes;
    int sha;
    int vpclmulqdq;
    int neon;
    int arm_crc32;
    int arm_crypto;
    int arm_dotprod;
    int arm_pmull;
} gui_backend_optimization_capabilities;

/* Process-wide settings, independent of database initialization. Sizes are in
 * bytes; zero restores the library default. Acceleration fields must be 0 or 1 and
 * unsupported acceleration is normalized to off. Invalid arguments return 0
 * without changing any settings. Getters return the effective settings; AVX2
 * support includes CPU and operating-system support. Changes affect next scans. */
int gui_backend_get_optimization_options(gui_backend_optimization_options *pOptions);
int gui_backend_get_optimization_capabilities(gui_backend_optimization_capabilities *pCapabilities);
int gui_backend_set_optimization_options(const gui_backend_optimization_options *pOptions);

/* Emits Automatic (id 0), then the preferred concrete type and the other
 * detected concrete interpretations, including Binary. IDs are backend scan
 * selectors rather than combo-box indexes. Labels are static UTF-8 strings.
 * No callback runs on an invalid path, read failure, or before initialization. */
typedef void (*gui_backend_file_type_fn)(int typeId, const char *pName, void *pUserData);
int gui_backend_file_types(const char *pUtf8Path, gui_backend_file_type_fn pTypeFn, void *pUserData);

/* Scans one file given a UTF-8 path. On success returns 1 and hands back two
 * heap strings (UTF-8): the formatted result text and the detected file-type
 * name; free each with gui_backend_free. On failure returns 0 and sets both
 * out-pointers to NULL. */
int gui_backend_scan(const char *pUtf8Path, char **ppResultText, char **ppTypeName);

/* Receives visible, sorted detection records without parsing formatted text.
 * Field strings are borrowed only for the duration of the callback. The type
 * includes the translated label and a heuristic marker when appropriate.
 * Output strings follow gui_backend_scan's ownership rules. */
typedef void (*gui_backend_result_fn)(const char *pType, const char *pName,
    const char *pVersion, const char *pInfo, void *pUserData);
int gui_backend_scan_results(const char *pUtf8Path, gui_backend_result_fn pResultFn,
    void *pUserData, char **ppResultText, char **ppTypeName);

/* Scans the current file as the selected file-type ID from file_types().
 * ID 0 is automatic; explicit IDs run only that interpretation's scan pass.
 * The automatic APIs above do not retain a previous explicit selection. */
int gui_backend_scan_results_type(const char *pUtf8Path, int nTypeId,
    gui_backend_result_fn pResultFn, void *pUserData,
    char **ppResultText, char **ppTypeName);

/* Compact result tree with explicit parent indexes. SIZE_MAX denotes a root;
 * category selects the scan-layer color pair; an empty category denotes a group.
 * Return zero from the callback to abort on an allocation/display failure.
 * progress (may be NULL) receives the engine's levels; stopping it cancels the
 * scan, which then fails with progress->is_stop set. */
typedef struct gui_backend_detection {
    const char *name;      /* what was found, e.g. "UPX"; never NULL */
    const char *signature; /* database path of the script that found it; NULL for Unknown */
} gui_backend_detection;
/* detection is NULL on group rows (the file type, Overlay, Archive member...)
 * and is borrowed for the call. */
typedef int (*gui_backend_tree_fn)(size_t parent, const char *text, const char *category,
                                   const gui_backend_detection *detection, void *user);
int gui_backend_scan_tree(const char *path, int type_id, gui_backend_tree_fn callback,
    void *user, char **report, char **type_name, xx_pd_struct *progress);

/* Source of the loaded signature a result row names (its database path),
 * NULL when no loaded signature has that path or it is compiled bytecode.
 * Free with gui_backend_free. */
char *gui_backend_signature_source(const char *signature);
/* Runs source, an edited copy of that signature, on path the way a scan of
 * type_id runs it, with the loaded _init and include helpers but no other
 * signature. *text receives what it detected followed by its _log output and
 * errors (free with gui_backend_free). Returns 0 when the file cannot be
 * scanned; *text then says why when it can. */
int gui_backend_run_signature(const char *path, int type_id, const char *signature, const char *source, char **text);
/* The bundled description of a detection name (info/html/<name>.html beside
 * the databases), as plain text; NULL when there is none. Free with
 * gui_backend_free. */
char *gui_backend_info_text(const char *name);

/* Frees a string handed back by gui_backend_scan (uses the engine allocator,
 * which on the CRT-free build is not the Win32 heap the frontend uses). */
void gui_backend_free(void *pPtr);

/* Releases the databases and options. */
void gui_backend_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* CDIE_GUI_BACKEND_H */
