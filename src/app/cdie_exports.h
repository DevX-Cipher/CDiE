/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_EXPORTS_H
#define CDIE_EXPORTS_H
#include <xxfclib/formats/xx_format_streams.h>
#define CDIE_EXPORTS_MAX_RECORDS 10000U
#define CDIE_EXPORTS_MAX_NAME 16384U
#define CDIE_EXPORTS_MAX_TEXT (8U * 1024U * 1024U)
typedef struct cdie_exports_data {
    uint64_t file_size;
    char file_type[32];
    xx_export_record *records;
    size_t count, capacity, text_size;
} cdie_exports_data;
/* Detached, owned snapshot of native PE slots, visible defined ELF global/weak
 * symbols and thin Mach-O exported symbols/tries. UINT64_MAX is an unknown
 * ordinal (ELF/Mach-O). Names/forwarders escape non-ASCII/control bytes as
 * backslash-xNN. No database or DataStructs. No partial model on failure. */
int cdie_exports_read(const char *path, xx_pd_struct *, cdie_exports_data **, char **error);
void cdie_exports_free(cdie_exports_data *);
#endif
