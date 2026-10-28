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

/* main_console.c - command line front end of cdie. */

#include "../global.h"
#include <xxfclib/die_engine/die_engine.h>
#include <xxfclib/fs/xx_fs.h>
#include <xxfclib/list/xx_list.h>
#include <xxfclib/rt/xx_rt.h>
#include <xxfclib/terminal/xx_terminal.h>

#include "../app/cdie_app.h"


static void print_help(void)
{
    xx_rt_printf("%s v%s\n", X_APPLICATIONDISPLAYNAME, X_APPLICATIONVERSION);
    xx_rt_printf("Detect It Easy console engine, C port\n");
    xx_rt_printf("Copyright(C) 2026 hors<horsicq@gmail.com> Web: http://ntinfo.biz\n\n");
    xx_rt_printf("Usage: cdie [options] target\n\n");
    xx_rt_printf("Options:\n");
    xx_rt_printf("  -h, --help                Displays this help.\n");
    xx_rt_printf("  -v, --version             Displays version information.\n");
    xx_rt_printf("  -r, --recursivescan       Scan directories recursively.\n");
    xx_rt_printf("  -d, --deepscan            Deep scan.\n");
    xx_rt_printf("  -u, --heuristicscan       Heuristic scan.\n");
    xx_rt_printf("  -g, --aggressivecscan     Aggressive scan.\n");
    xx_rt_printf("  -b, --verbose             Verbose output.\n");
    xx_rt_printf("  -f, --format              Format the result strings (the default).\n");
    xx_rt_printf("      --noformat            Do not format the result strings.\n");
    xx_rt_printf("      --nocolor             Disable color output (cdie never colors).\n");
    xx_rt_printf("      --sse2, --nosse2       Enable/disable SSE2 acceleration.\n");
    xx_rt_printf("      --avx2, --noavx2       Enable/disable AVX2 acceleration.\n");
    xx_rt_printf("                           Default: on when supported; unsupported keys are ignored.\n");
    xx_rt_printf("  -U, --hideunknown         Hide unknown results.\n");
    xx_rt_printf("  -M, --messages            Show engine messages.\n");
    xx_rt_printf("  -l, --profiling           Show profiling information.\n");
    xx_rt_printf("  -j, --json                Result as JSON.\n");
    xx_rt_printf("  -x, --xml                 Result as XML.\n");
    xx_rt_printf("  -c, --csv                 Result as CSV.\n");
    xx_rt_printf("  -t, --tsv                 Result as TSV.\n");
    xx_rt_printf("  -p, --plaintext           Result as plain text.\n");
    xx_rt_printf("  -D, --database <path>     Main database path.\n");
    xx_rt_printf("  -E, --extradatabase <p>   Extra database path.\n");
    xx_rt_printf("  -C, --customdatabase <p>  Custom database path.\n");
    xx_rt_printf("  -s, --showdatabase        Show the database information.\n");
    xx_rt_printf("      --createtar <path>    Create TAR archive from database.\n");
    xx_rt_printf("      --createprecompiledtar <p> Create precompiled bytecode TAR archive.\n");
    xx_rt_printf("\n");
    xx_rt_printf("Target:\n");
    xx_rt_printf("  target                    The file or directory to open.\n");
}

/* Frees the cdie_strdup'd elements and then the list itself. xx_list_t stores
 * elements by value, so a list of strings holds char* and each slot has to be
 * dereferenced through. */
static void free_string_vector(xx_list_t *pList)
{
    size_t i = 0;
    size_t nCount = xx_list_count(pList);

    for (i = 0; i < nCount; i++) {
        xx_rt_free(*(char **)xx_list_at(pList, i));
    }

    xx_list_cleanup(pList);
}

/* The element at @p i of a list of strings. */
static const char *string_at(const xx_list_t *pList, size_t i)
{
    return *(const char *const *)xx_list_at(pList, i);
}

static char *resolve_database_path(const char *pPath, const char *pDefaultName)
{
    if (pPath && pPath[0]) {
        return cdie_strdup(pPath);
    }

    {
        /* $data/<name>: next to the executable, then the current directory. */
        char *pAppDir = cdie_app_dir();
        char *pCandidate = xx_fs_path_join(pAppDir, pDefaultName);

        xx_rt_free(pAppDir);

        if (xx_fs_is_dir(pCandidate) || xx_fs_is_file(pCandidate)) {
            return pCandidate;
        }

        {
            char sTar[512];
            xx_rt_snprintf(sTar, sizeof(sTar), "%s.tar", pCandidate);
            if (xx_fs_is_file(sTar)) {
                xx_rt_free(pCandidate);
                return cdie_strdup(sTar);
            }
        }

        xx_rt_free(pCandidate);
    }

    if (xx_fs_is_dir(pDefaultName) || xx_fs_is_file(pDefaultName)) {
        return cdie_strdup(pDefaultName);
    }

    {
        char sTar[512];
        xx_rt_snprintf(sTar, sizeof(sTar), "%s.tar", pDefaultName);
        if (xx_fs_is_file(sTar)) {
            return cdie_strdup(sTar);
        }
    }

    return cdie_strdup(pDefaultName);
}

static void show_database(DBase *pDb, xx_scan_options *pOptions)
{
    /* Same list, in the same order, as XScanEngine::getSignatureStates(). */
    static const XFileType pTypes[] = {XFT_BINARY,  XFT_COM,     XFT_MSDOS, XFT_NE,     XFT_LE,       XFT_LX,     XFT_PE,      XFT_ELF,
                                       XFT_MACHO,   XFT_PDF,     XFT_CFBF,  XFT_IMAGE,  XFT_JPEG,     XFT_PNG,    XFT_RAR,     XFT_ISO9660,
                                       XFT_ARCHIVE, XFT_ZIP,     XFT_JAR,   XFT_APK,    XFT_IPA,      XFT_DEX,    XFT_NPM,     XFT_MACHOFAT,
                                       XFT_AMIGAHUNK, XFT_ATARIST, XFT_DOS16M, XFT_DOS4G};
    size_t i = 0;

    xx_rt_printf("Main database: %s\n", pOptions->main_database_path ? pOptions->main_database_path : "");
    xx_rt_printf("Extra database: %s\n", pOptions->extra_database_path ? pOptions->extra_database_path : "");
    xx_rt_printf("Custom database: %s\n", pOptions->custom_database_path ? pOptions->custom_database_path : "");

    for (i = 0; i < sizeof(pTypes) / sizeof(pTypes[0]); i++) {
        int nCount = db_count_for_type(pDb, pTypes[i]);

        if (nCount > 0) {
            xx_rt_printf("\t%s: %d\n", xft_to_string(pTypes[i]), nCount);
        }
    }
}

/* Entry point. utils_entry.c calls this: on a no-CRT Windows build from the
 * real linker entry, otherwise from main(). */
int x_main(int argc, char *argv[])
{
    xx_scan_options options;
    DBase db;
    xx_list_t vecTargets;
    xx_list_t vecFiles;
    int nResult = CR_SUCCESS;
    int i = 0;
    int bShowDatabase = 0;
    int bDatabaseLoaded = 0;
    int bEndOfOptions = 0;
    const char *pCreateTarPath = NULL;
    const char *pCreateBcTarPath = NULL;

    xx_scan_options_init(&options);
    /* diec formats the result strings by default, and printing exactly what
     * diec prints is the acceptance test, so the console front end turns the
     * option on before the command line is parsed. -f/--format stays an
     * explicit enable, --noformat is the opt-out. */
    options.format_result = 1;
    xx_rt_memset(&db, 0, sizeof(db));
    xx_list_init(&vecTargets, sizeof(char *), NULL);
    xx_list_init(&vecFiles, sizeof(char *), NULL);

    for (i = 1; i < argc; i++) {
        const char *pArg = argv[i];

        if (!bEndOfOptions && (xx_rt_strcmp(pArg, "--") == 0)) {
            bEndOfOptions = 1;
            continue;
        }

        if (!bEndOfOptions && xx_terminal_handle_option(pArg)) {
            continue;
        }

        if (!bEndOfOptions && pArg[0] == '-' && pArg[1] == '-') {
            /* Long options starting with -- */
            if (xx_rt_strcmp(pArg, "--help") == 0) {
                print_help();
                xx_scan_options_free(&options);
                free_string_vector(&vecTargets);
                free_string_vector(&vecFiles);
                return CR_SUCCESS;
            } else if (xx_rt_strcmp(pArg, "--version") == 0) {
                xx_rt_printf("%s %s\n", X_APPLICATIONDISPLAYNAME, X_APPLICATIONVERSION);
                xx_scan_options_free(&options);
                free_string_vector(&vecTargets);
                free_string_vector(&vecFiles);
                return CR_SUCCESS;
            } else if (xx_rt_strcmp(pArg, "--recursivescan") == 0) {
                options.recursive_scan = 1;
            } else if (xx_rt_strcmp(pArg, "--deepscan") == 0) {
                options.deep_scan = 1;
            } else if (xx_rt_strcmp(pArg, "--heuristicscan") == 0) {
                options.heuristic_scan = 1;
            } else if ((xx_rt_strcmp(pArg, "--aggressivecscan") == 0) || (xx_rt_strcmp(pArg, "--aggressivescan") == 0)) {
                options.aggressive_scan = 1;
            } else if (xx_rt_strcmp(pArg, "--verbose") == 0) {
                options.verbose = 1;
            } else if (xx_rt_strcmp(pArg, "--format") == 0) {
                options.format_result = 1;
            } else if (xx_rt_strcmp(pArg, "--noformat") == 0) {
                options.format_result = 0;
            } else if (xx_rt_strcmp(pArg, "--hideunknown") == 0) {
                options.hide_unknown = 1;
            } else if (xx_rt_strcmp(pArg, "--messages") == 0) {
                options.show_messages = 1;
            } else if (xx_rt_strcmp(pArg, "--profiling") == 0) {
                options.profiling = 1;
            } else if (xx_rt_strcmp(pArg, "--json") == 0) {
                options.result_as_json = 1;
            } else if (xx_rt_strcmp(pArg, "--xml") == 0) {
                options.result_as_xml = 1;
            } else if (xx_rt_strcmp(pArg, "--csv") == 0) {
                options.result_as_csv = 1;
            } else if (xx_rt_strcmp(pArg, "--tsv") == 0) {
                options.result_as_tsv = 1;
            } else if ((xx_rt_strcmp(pArg, "--plaintext") == 0)) {
                options.result_as_plain_text = 1;
            } else if (xx_rt_strcmp(pArg, "--showdatabase") == 0) {
                bShowDatabase = 1;
            } else if ((xx_rt_strcmp(pArg, "--database") == 0) && (i + 1 < argc)) {
                xx_rt_free(options.main_database_path);
                options.main_database_path = cdie_strdup(argv[++i]);
            } else if ((xx_rt_strcmp(pArg, "--extradatabase") == 0) && (i + 1 < argc)) {
                xx_rt_free(options.extra_database_path);
                options.extra_database_path = cdie_strdup(argv[++i]);
            } else if ((xx_rt_strcmp(pArg, "--customdatabase") == 0) && (i + 1 < argc)) {
                xx_rt_free(options.custom_database_path);
                options.custom_database_path = cdie_strdup(argv[++i]);
            } else if ((xx_rt_strcmp(pArg, "--createtar") == 0) && (i + 1 < argc)) {
                pCreateTarPath = argv[++i];
            } else if ((xx_rt_strcmp(pArg, "--createprecompiledtar") == 0) && (i + 1 < argc)) {
                pCreateBcTarPath = argv[++i];
            } else {
                xx_rt_fprintf(xx_rt_stderr(), "Unknown option: %s\n", pArg);
                nResult = CR_INVALIDPARAMETER;
            }
        } else if (!bEndOfOptions && pArg[0] == '-' && pArg[1] != '\0') {
            /* Short options (supports clustering like -rdu, -rd, etc.) */
            if (xx_rt_strcmp(pArg, "-he") == 0) {
                options.heuristic_scan = 1;
            } else if (xx_rt_strcmp(pArg, "-hu") == 0) {
                options.hide_unknown = 1;
            } else {
                size_t k = 0;
                for (k = 1; pArg[k] != '\0'; k++) {
                    char c = pArg[k];
                    switch (c) {
                        case 'h':
                        case '?':
                            print_help();
                            xx_scan_options_free(&options);
                            free_string_vector(&vecTargets);
                            free_string_vector(&vecFiles);
                            return CR_SUCCESS;

                        case 'v':
                            xx_rt_printf("%s %s\n", X_APPLICATIONDISPLAYNAME, X_APPLICATIONVERSION);
                            xx_scan_options_free(&options);
                            free_string_vector(&vecTargets);
                            free_string_vector(&vecFiles);
                            return CR_SUCCESS;

                        case 'r': options.recursive_scan = 1; break;
                        case 'd': options.deep_scan = 1; break;
                        case 'u': options.heuristic_scan = 1; break;
                        case 'g': options.aggressive_scan = 1; break;
                        case 'b':
                        case 'V': options.verbose = 1; break;
                        case 'f': options.format_result = 1; break;
                        case 'U': options.hide_unknown = 1; break;
                        case 'M':
                        case 'm': options.show_messages = 1; break;
                        case 'l': options.profiling = 1; break;
                        case 'j': options.result_as_json = 1; break;
                        case 'x': options.result_as_xml = 1; break;
                        case 'c': options.result_as_csv = 1; break;
                        case 't': options.result_as_tsv = 1; break;
                        case 'p':
                        case 'P': options.result_as_plain_text = 1; break;
                        case 's': bShowDatabase = 1; break;

                        case 'D': {
                            const char *pVal = NULL;
                            if (pArg[k + 1] != '\0') {
                                pVal = &pArg[k + 1];
                                k = xx_rt_strlen(pArg) - 1;
                            } else if (i + 1 < argc) {
                                pVal = argv[++i];
                            }
                            if (pVal) {
                                xx_rt_free(options.main_database_path);
                                options.main_database_path = cdie_strdup(pVal);
                            } else {
                                xx_rt_fprintf(xx_rt_stderr(), "Option -D requires an argument\n");
                                nResult = CR_INVALIDPARAMETER;
                            }
                            break;
                        }

                        case 'E': {
                            const char *pVal = NULL;
                            if (pArg[k + 1] != '\0') {
                                pVal = &pArg[k + 1];
                                k = xx_rt_strlen(pArg) - 1;
                            } else if (i + 1 < argc) {
                                pVal = argv[++i];
                            }
                            if (pVal) {
                                xx_rt_free(options.extra_database_path);
                                options.extra_database_path = cdie_strdup(pVal);
                            } else {
                                xx_rt_fprintf(xx_rt_stderr(), "Option -E requires an argument\n");
                                nResult = CR_INVALIDPARAMETER;
                            }
                            break;
                        }

                        case 'C': {
                            const char *pVal = NULL;
                            if (pArg[k + 1] != '\0') {
                                pVal = &pArg[k + 1];
                                k = xx_rt_strlen(pArg) - 1;
                            } else if (i + 1 < argc) {
                                pVal = argv[++i];
                            }
                            if (pVal) {
                                xx_rt_free(options.custom_database_path);
                                options.custom_database_path = cdie_strdup(pVal);
                            } else {
                                xx_rt_fprintf(xx_rt_stderr(), "Option -C requires an argument\n");
                                nResult = CR_INVALIDPARAMETER;
                            }
                            break;
                        }

                        default:
                            xx_rt_fprintf(xx_rt_stderr(), "Unknown option: -%c\n", c);
                            nResult = CR_INVALIDPARAMETER;
                            break;
                    }
                    if (nResult != CR_SUCCESS) {
                        break;
                    }
                }
            }
        } else {
            char *pCopy = cdie_strdup(pArg);

            if (pCopy) {
                xx_list_append(&vecTargets, &pCopy);
            }
        }
    }

    {
        char *pMain = resolve_database_path(options.main_database_path, "db");
        char *pExtra = resolve_database_path(options.extra_database_path, "db_extra");
        char *pCustom = resolve_database_path(options.custom_database_path, "db_custom");

        xx_rt_free(options.main_database_path);
        xx_rt_free(options.extra_database_path);
        xx_rt_free(options.custom_database_path);
        options.main_database_path = pMain;
        options.extra_database_path = pExtra;
        options.custom_database_path = pCustom;
    }

    if (pCreateTarPath) {
        if (!db_create_tar(options.main_database_path, pCreateTarPath)) {
            xx_rt_fprintf(xx_rt_stderr(), "Failed to create TAR database from '%s' to '%s'\n",
                          options.main_database_path, pCreateTarPath);
            nResult = CR_CANNOTFINDDATABASE;
        } else {
            xx_rt_printf("Successfully created TAR database: %s\n", pCreateTarPath);
        }
        xx_scan_options_free(&options);
        free_string_vector(&vecTargets);
        free_string_vector(&vecFiles);
        return nResult;
    }

    if (pCreateBcTarPath) {
        if (!db_create_tar_precompiled(options.main_database_path, pCreateBcTarPath)) {
            xx_rt_fprintf(xx_rt_stderr(), "Failed to create precompiled TAR database from '%s' to '%s'\n",
                          options.main_database_path, pCreateBcTarPath);
            nResult = CR_CANNOTFINDDATABASE;
        } else {
            xx_rt_printf("Successfully created precompiled TAR database: %s\n", pCreateBcTarPath);
        }
        xx_scan_options_free(&options);
        free_string_vector(&vecTargets);
        free_string_vector(&vecFiles);
        return nResult;
    }

    if ((xx_list_count(&vecTargets) == 0) && (!bShowDatabase)) {
        print_help();
        xx_scan_options_free(&options);
        free_string_vector(&vecTargets);
        free_string_vector(&vecFiles);

        /* An unknown option already set nResult; do not report success. */
        return nResult;
    }

    bDatabaseLoaded = db_load(&db, options.main_database_path, DB_MAIN);

    if (options.use_extra_database) {
        db_load(&db, options.extra_database_path, DB_EXTRA);
    }

    if (options.use_custom_database) {
        db_load(&db, options.custom_database_path, DB_CUSTOM);
    }

    db_sort(&db);

    if (!bDatabaseLoaded) {
        xx_rt_fprintf(xx_rt_stderr(), "Cannot load database: %s\n", options.main_database_path);
        nResult = CR_CANNOTFINDDATABASE;
    }

    if (bShowDatabase) {
        show_database(&db, &options);
    }

    for (i = 0; i < (int)xx_list_count(&vecTargets); i++) {
        const char *pTarget = string_at(&vecTargets, (size_t)i);

        if (cdie_path_exists(pTarget)) {
            cdie_find_files(pTarget, &vecFiles, options.recursive_scan);
        } else {
            xx_rt_printf("Cannot find: %s\n", pTarget);
            nResult = CR_CANNOTFINDFILE;
        }
    }

    for (i = 0; i < (int)xx_list_count(&vecFiles); i++) {
        const char *pFileName = string_at(&vecFiles, (size_t)i);
        xx_scan_result result;
        char *pOutput = NULL;

        if (xx_list_count(&vecFiles) > 1) {
            char *pNative = cdie_path_native(pFileName);

            xx_rt_printf("%s:\n", pNative);
            xx_rt_free(pNative);
        }

        if (!die_engine_scan_file(pFileName, &db, &options, &result)) {
            xx_rt_printf("Cannot open: %s\n", pFileName);
            nResult = CR_CANNOTOPENFILE;
            continue;
        }

        if (options.result_as_json) {
            pOutput = xx_scan_format_json(&result, &options);
        } else if (options.result_as_xml) {
            pOutput = xx_scan_format_xml(&result, &options);
        } else if (options.result_as_csv) {
            /* diec separates CSV fields with a semicolon, not a comma. */
            pOutput = xx_scan_format_csv(&result, &options, ';');
        } else if (options.result_as_tsv) {
            pOutput = xx_scan_format_csv(&result, &options, '\t');
        } else {
            pOutput = xx_scan_format_text(&result, &options);
        }

        xx_rt_printf("%s", pOutput);

        if (options.result_as_json || options.result_as_xml) {
            xx_rt_printf("\n");
        }

        xx_rt_free(pOutput);

        {
            int j = 0;

            /* diec reports script/progress errors after the detections on
             * stdout, regardless of --messages, and still exits successfully. */
            for (j = 0; j < result.nErrorCount; j++) {
                xx_rt_printf("%s\n", result.ppErrors[j]);
            }
        }

        xx_rt_printf("\n");
        xx_scan_result_free(&result);
    }

    free_string_vector(&vecTargets);
    free_string_vector(&vecFiles);
    db_free(&db);
    xx_scan_options_free(&options);

    return nResult;
}
