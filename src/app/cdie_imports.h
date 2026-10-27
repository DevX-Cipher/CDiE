/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_IMPORTS_H
#define CDIE_IMPORTS_H
#include <xxfclib/formats/xx_format_streams.h>
#define CDIE_IMPORTS_MAX_RECORDS 10000U
#define CDIE_IMPORTS_MAX_NAME 16384U
#define CDIE_IMPORTS_MAX_TEXT (8U * 1024U * 1024U)
typedef struct cdie_imports_data {
    uint64_t file_size;
    char file_type[32];
    xx_import_record *records;
    size_t count, capacity, text_size;
} cdie_imports_data;
/* Detached, owned snapshot. Native PE imports (including delay/ordinal), ELF
 * undefined symbols and thin Mach-O undefined external symbols. Names escape
 * non-ASCII/control bytes as \xNN. No database or DataStructs. Unsupported
 * formats, malformed input, cancellation or bounds return no partial model. */
int cdie_imports_read(const char *path, xx_pd_struct *, cdie_imports_data **, char **error);
void cdie_imports_free(cdie_imports_data *);
#endif
