/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_DISASM_H
#define CDIE_DISASM_H
#include <xxfclib/data/xx_pd.h>
#include <xxfclib/io/xx_io.h>
#define CDIE_DISASM_MAX_ROWS 512U
#define CDIE_DISASM_PAGE_BYTES 4096U
typedef enum cdie_disasm_mode {
    CDIE_DISASM_AUTO,
    CDIE_DISASM_X86_16,
    CDIE_DISASM_X86_32,
    CDIE_DISASM_X86_64,
    CDIE_DISASM_ARM,
    CDIE_DISASM_THUMB,
    CDIE_DISASM_ARM64,
    CDIE_DISASM_ARM_BE,
    CDIE_DISASM_THUMB_BE,
    CDIE_DISASM_ARM64_BE
} cdie_disasm_mode;
typedef struct cdie_disasm_row {
    uint64_t offset, address;
    unsigned size;
    char bytes[48], instruction[256], comment[128];
} cdie_disasm_row;
typedef struct cdie_disasm_page {
    uint64_t file_size, offset, next_offset;
    size_t count;
    cdie_disasm_row rows[CDIE_DISASM_MAX_ROWS];
} cdie_disasm_page;
typedef struct cdie_disasm_info {
    uint64_t file_size, initial_offset;
    cdie_disasm_mode detected_mode;
    char description[192];
} cdie_disasm_info;
typedef struct cdie_disasm_context cdie_disasm_context;
typedef enum cdie_disasm_status {
    CDIE_DISASM_OK,
    CDIE_DISASM_UNSUPPORTED,
    CDIE_DISASM_INVALID,
    CDIE_DISASM_IO_ERROR,
    CDIE_DISASM_NO_MEMORY
} cdie_disasm_status;
/* Opens rb; optional PE/ELF/thin Mach-O maps choose mode/entry and addresses.
 * Raw/unsupported targets require a manual mode. No DataStructs or signatures.
 * Context owns its device/readers and error uses xx_rt_free. */
int cdie_disasm_open(const char *path, xx_pd_struct *progress, cdie_disasm_context **context, char **error);
void cdie_disasm_close(cdie_disasm_context *context);
/* 0 Intel, 1 AT&T for x86; ARM syntax is unaffected. Invalid input is rejected. */
int cdie_disasm_set_syntax(cdie_disasm_context *context, int syntax);
const cdie_disasm_info *cdie_disasm_get_info(const cdie_disasm_context *context);
size_t cdie_disasm_mode_count(void);
const char *cdie_disasm_mode_name(size_t mode);
/* Strict bounded read, at most 4 KiB + 15 lookahead bytes / 512 rows. Invalid
 * or truncated instructions consume one explicit db byte. Page borrows context
 * storage until the next successful load/close; failure preserves that page. */
cdie_disasm_status cdie_disasm_load(cdie_disasm_context *context, uint64_t offset, size_t mode, const cdie_disasm_page **page);
#endif
