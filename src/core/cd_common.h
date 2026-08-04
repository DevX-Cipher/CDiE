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

/* cd_common.h - basic types, allocation helpers and a growable byte buffer. */

#ifndef CD_COMMON_H
#define CD_COMMON_H

/* utils.h is the only header that reaches the C standard library; it also
 * provides size_t and NULL, so nothing else here needs a system include.  */
#include "utils.h"

#if defined(_MSC_VER)
typedef signed __int8 cd_i8;
typedef unsigned __int8 cd_u8;
typedef signed __int16 cd_i16;
typedef unsigned __int16 cd_u16;
typedef signed __int32 cd_i32;
typedef unsigned __int32 cd_u32;
typedef signed __int64 cd_i64;
typedef unsigned __int64 cd_u64;
#else
#include <stdint.h>
typedef int8_t cd_i8;
typedef uint8_t cd_u8;
typedef int16_t cd_i16;
typedef uint16_t cd_u16;
typedef int32_t cd_i32;
typedef uint32_t cd_u32;
typedef int64_t cd_i64;
typedef uint64_t cd_u64;
#endif

#ifndef CD_TRUE
#define CD_TRUE 1
#define CD_FALSE 0
#endif

/* ---------------------------------------------------------------- memory  */

void *cd_malloc(size_t nSize);
void *cd_calloc(size_t nCount, size_t nSize);
void *cd_realloc(void *pPtr, size_t nSize);
void cd_free(void *pPtr);
char *cd_strdup(const char *pString);
char *cd_strndup(const char *pString, size_t nSize);

/* ---------------------------------------------------------------- buffer  */

/* Growable byte buffer. Always keeps a terminating NUL past pData[nSize]. */
typedef struct {
    char *pData;
    size_t nSize;
    size_t nCapacity;
} CDBuf;

void cdbuf_init(CDBuf *pBuf);
void cdbuf_free(CDBuf *pBuf);
void cdbuf_reserve(CDBuf *pBuf, size_t nCapacity);
void cdbuf_clear(CDBuf *pBuf);
void cdbuf_append(CDBuf *pBuf, const void *pData, size_t nSize);
void cdbuf_append_str(CDBuf *pBuf, const char *pString);
void cdbuf_append_ch(CDBuf *pBuf, char nChar);
void cdbuf_appendf(CDBuf *pBuf, const char *pFormat, ...);
/* Detaches the buffer contents; the caller owns the returned pointer. */
char *cdbuf_detach(CDBuf *pBuf, size_t *pnSize);

/* ---------------------------------------------------------------- vector  */

/* Vector of pointers. */
typedef struct {
    void **ppData;
    size_t nSize;
    size_t nCapacity;
} CDVec;

void cdvec_init(CDVec *pVec);
void cdvec_free(CDVec *pVec);
void cdvec_push(CDVec *pVec, void *pItem);
void cdvec_insert(CDVec *pVec, size_t nIndex, void *pItem);
void cdvec_remove(CDVec *pVec, size_t nIndex);
void cdvec_clear(CDVec *pVec);

/* ------------------------------------------------------------ misc utils  */

cd_u32 cd_hash_str(const char *pString, size_t nSize);
int cd_stricmp_ascii(const char *pLeft, const char *pRight);
/* Case-insensitive ASCII compare of two counted strings. */
int cd_strnicmp_ascii(const char *pLeft, const char *pRight, size_t nSize);

#endif /* CD_COMMON_H */
