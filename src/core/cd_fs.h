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

/* cd_fs.h - minimal portable filesystem helpers (Windows and POSIX). */

#ifndef CD_FS_H
#define CD_FS_H

#include "cd_common.h"

typedef enum { CD_ENTRY_FILE = 0, CD_ENTRY_DIR = 1 } CDEntryType;

typedef struct {
    char *pName;      /* base name */
    char *pPath;      /* full path */
    CDEntryType type;
    cd_i64 nSize;
} CDEntry;

int cd_path_exists(const char *pPath);
int cd_path_is_dir(const char *pPath);
int cd_path_is_file(const char *pPath);

/* Reads a whole file. Returns a malloc'ed buffer (NUL terminated) or NULL. */
char *cd_read_file(const char *pPath, cd_i64 *pnSize);

/* Lists a directory. Entries are appended to pVec as CDEntry*, sorted by name.
 * "." and ".." are skipped. Returns 0 when the directory cannot be opened.  */
int cd_list_dir(const char *pPath, CDVec *pVec);
void cd_entry_free(CDEntry *pEntry);

/* Path helpers (they always return newly allocated strings). */
char *cd_path_join(const char *pLeft, const char *pRight);
char *cd_path_dir(const char *pPath);
char *cd_path_file_name(const char *pPath);
char *cd_path_base_name(const char *pPath);       /* file name without last suffix   */
char *cd_path_suffix(const char *pPath);          /* text after the last '.'         */
char *cd_path_complete_suffix(const char *pPath); /* text after the first '.'        */
char *cd_path_native(const char *pPath);          /* '/' -> '\\' on Windows          */

/* Directory of the running executable. */
char *cd_app_dir(void);

/* Recursively collects file names below pPath (or the single file itself). */
void cd_find_files(const char *pPath, CDVec *pVecNames, int bRecursive);

#endif /* CD_FS_H */
