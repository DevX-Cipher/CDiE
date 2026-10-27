/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_SYMBOLS_H
#define CDIE_SYMBOLS_H
#include <xxfclib/formats/xx_format_streams.h>
#define CDIE_SYMBOLS_MAX_RECORDS 10000U
#define CDIE_SYMBOLS_MAX_NAME 16384U
#define CDIE_SYMBOLS_MAX_TEXT (8U * 1024U * 1024U)
typedef struct cdie_symbols_data {
    uint64_t file_size;
    char file_type[32];
    xx_symbol_record *records;
    size_t count, capacity, text_size;
} cdie_symbols_data;
/* Detached native PE/ELF/thin Mach-O symbols. Complete typed metadata and
 * UTF-8 names are copied; invalid/control bytes are escaped. No database or
 * DataStructs. Failure/cancellation returns no partial model. */
int cdie_symbols_read(const char *path, xx_pd_struct *, cdie_symbols_data **, char **error);
void cdie_symbols_free(cdie_symbols_data *);
#endif
