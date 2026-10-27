/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_RESOURCES_H
#define CDIE_RESOURCES_H
#include <xxfclib/formats/xx_format_streams.h>
#define CDIE_RESOURCES_MAX_RECORDS 10000U
#define CDIE_RESOURCES_MAX_NAME 16384U
#define CDIE_RESOURCES_MAX_TEXT (8U * 1024U * 1024U)
typedef struct cdie_resources_data {
    uint64_t file_size;
    char file_type[32];
    xx_resource_record *records;
    size_t count, capacity, text_size;
} cdie_resources_data;
/* Detached native PE/ELF GResource metadata. IDs, code pages, physical ranges
 * and UTF-8 names are preserved; control/invalid UTF-8 bytes use backslash-xNN.
 * No database or DataStructs. Failure/cancellation returns no partial model.
 * Resource payloads are not retained in the snapshot. */
int cdie_resources_read(const char *path, xx_pd_struct *, cdie_resources_data **, char **error);
void cdie_resources_free(cdie_resources_data *);
#endif
