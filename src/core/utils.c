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

/* utils.c - the only translation unit that talks to a platform runtime.
 *
 * On Windows the implementation uses the Win32 API: HeapAlloc for memory,
 * CreateFile/ReadFile for I/O, WriteFile to the standard handles for output,
 * lstrlenA for string length, ExitProcess, GetEnvironmentVariableA. The
 * remaining primitives (comparison, search, sorting, integer formatting) are
 * implemented here rather than taken from the CRT.
 *
 * Elsewhere the C standard library is used directly.
 *
 * The remaining pieces live next door and are equally CRT-free:
 * utils_math.c (the math functions), utils_fp.c (double <-> decimal string)
 * and utils_entry.c (startup, argv, the compiler support routines).
 *
 * Floating point conversions are deliberately absent from the formatter
 * below: the JavaScript layer calls x_dtoa_* directly, so %e/%f/%g are never
 * needed and wsprintfA's lack of them costs nothing.
 */

#include "utils.h"

#if defined(_WIN32)
#include <windows.h>

/* TinyCC's bundled <windows.h> leaves <winnls.h> commented out, so the code
 * page conversions are missing. Declare exactly what is used; the guard keeps
 * this out of the way of MSVC / MinGW, whose headers already provide them. */
#ifndef CP_UTF8
#define CP_UTF8 65001
int WINAPI MultiByteToWideChar(UINT CodePage, DWORD dwFlags, const char *lpMultiByteStr, int cbMultiByte, wchar_t *lpWideCharStr, int cchWideChar);
int WINAPI WideCharToMultiByte(UINT CodePage, DWORD dwFlags, const wchar_t *lpWideCharStr, int cchWideChar, char *lpMultiByteStr, int cbMultiByte, const char *lpDefaultChar, int *lpUsedDefaultChar);
#endif

/* Paths inside the program are UTF-8 (that is what a Unicode command line is
 * decoded to on the way in). The file APIs are therefore the wide ones, with
 * a conversion at the boundary; CreateFileA cannot even name a file outside
 * the process ANSI code page. The returned buffer is heap-allocated and the
 * caller frees it with x_free. */
void *x_utf8_to_utf16(const char *pUtf8)
{
    int nLen = 0;
    WCHAR *pResult = NULL;

    if (pUtf8 == NULL) {
        return NULL;
    }

    nLen = MultiByteToWideChar(CP_UTF8, 0, pUtf8, -1, NULL, 0);

    if (nLen <= 0) {
        return NULL;
    }

    pResult = (WCHAR *)x_malloc((size_t)nLen * sizeof(WCHAR));

    if (pResult == NULL) {
        return NULL;
    }

    if (MultiByteToWideChar(CP_UTF8, 0, pUtf8, -1, pResult, nLen) <= 0) {
        x_free(pResult);

        return NULL;
    }

    return (void *)pResult;
}

char *x_utf16_to_utf8(const void *pUtf16)
{
    const WCHAR *pWide = (const WCHAR *)pUtf16;
    int nLen = 0;
    char *pResult = NULL;

    if (pWide == NULL) {
        return NULL;
    }

    nLen = WideCharToMultiByte(CP_UTF8, 0, pWide, -1, NULL, 0, NULL, NULL);

    if (nLen <= 0) {
        return NULL;
    }

    pResult = (char *)x_malloc((size_t)nLen);

    if (pResult == NULL) {
        return NULL;
    }

    if (WideCharToMultiByte(CP_UTF8, 0, pWide, -1, pResult, nLen, NULL, NULL) <= 0) {
        x_free(pResult);

        return NULL;
    }

    return pResult;
}
#else
/* Hosted build: the standard headers back the stubs. */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#endif

#if defined(_MSC_VER)
/* Stop the compiler from rewriting the loops below into calls to itself. */
#pragma function(memset, memcpy)
#endif

/* ------------------------------------------------------------------------ */
/*  Strings                                                                  */
/* ------------------------------------------------------------------------ */

size_t x_strlen(const char *pString)
{
#if defined(_WIN32)
    return (size_t)lstrlenA(pString);
#else
    return strlen(pString);
#endif
}

/* lstrcmpA is deliberately NOT used: it compares with the user's locale via
 * CompareStringA, so "a" vs "B" and punctuation would order differently from
 * a byte comparison. The signature database is sorted with this function and
 * JavaScript relational operators rely on it, so the ordering has to be
 * plain unsigned byte order on every platform.                              */
int x_strcmp(const char *pLeft, const char *pRight)
{
#if defined(_WIN32)
    const unsigned char *pA = (const unsigned char *)pLeft;
    const unsigned char *pB = (const unsigned char *)pRight;

    while (*pA && (*pA == *pB)) {
        pA++;
        pB++;
    }

    return (int)*pA - (int)*pB;
#else
    return strcmp(pLeft, pRight);
#endif
}

int x_strncmp(const char *pLeft, const char *pRight, size_t nSize)
{
#if defined(_WIN32)
    const unsigned char *pA = (const unsigned char *)pLeft;
    const unsigned char *pB = (const unsigned char *)pRight;
    size_t i = 0;

    for (i = 0; i < nSize; i++) {
        if (pA[i] != pB[i]) {
            return (int)pA[i] - (int)pB[i];
        }

        if (pA[i] == 0) {
            break;
        }
    }

    return 0;
#else
    return strncmp(pLeft, pRight, nSize);
#endif
}

/* strncpy semantics: copy at most nSize bytes, zero-pad the remainder, and do
 * not terminate when the source is longer. lstrcpynA differs on both counts,
 * so it is not used here.                                                   */
char *x_strncpy(char *pDestination, const char *pSource, size_t nSize)
{
#if defined(_WIN32)
    size_t i = 0;

    for (i = 0; (i < nSize) && pSource[i]; i++) {
        pDestination[i] = pSource[i];
    }

    for (; i < nSize; i++) {
        pDestination[i] = 0;
    }

    return pDestination;
#else
    return strncpy(pDestination, pSource, nSize);
#endif
}

char *x_strchr(const char *pString, int nChar)
{
#if defined(_WIN32)
    char nWanted = (char)nChar;

    for (;;) {
        if (*pString == nWanted) {
            return (char *)pString;
        }

        if (*pString == 0) {
            return NULL;
        }

        pString++;
    }
#else
    return strchr(pString, nChar);
#endif
}

char *x_strrchr(const char *pString, int nChar)
{
#if defined(_WIN32)
    char nWanted = (char)nChar;
    const char *pFound = NULL;

    for (;;) {
        if (*pString == nWanted) {
            pFound = pString;
        }

        if (*pString == 0) {
            break;
        }

        pString++;
    }

    return (char *)pFound;
#else
    return strrchr(pString, nChar);
#endif
}

char *x_strstr(const char *pHaystack, const char *pNeedle)
{
#if defined(_WIN32)
    size_t nNeedle = x_strlen(pNeedle);
    size_t i = 0;

    if (nNeedle == 0) {
        return (char *)pHaystack;
    }

    for (i = 0; pHaystack[i]; i++) {
        size_t j = 0;

        while ((j < nNeedle) && (pHaystack[i + j] == pNeedle[j])) {
            j++;
        }

        if (j == nNeedle) {
            return (char *)(pHaystack + i);
        }
    }

    return NULL;
#else
    return strstr(pHaystack, pNeedle);
#endif
}

/* ------------------------------------------------------------------------ */
/*  Memory                                                                   */
/* ------------------------------------------------------------------------ */

#if defined(_WIN32)
/* The Rtl*Memory names in winnt.h are macros that expand back to the CRT, so
 * they would not remove the dependency. These copy a machine word at a time
 * to stay close to the CRT in the hot paths (signature compare, string
 * concatenation).                                                           */
typedef size_t x_word;
#endif

void *x_memcpy(void *pDestination, const void *pSource, size_t nSize)
{
#if defined(_WIN32)
    unsigned char *pDst = (unsigned char *)pDestination;
    const unsigned char *pSrc = (const unsigned char *)pSource;

    while (nSize >= sizeof(x_word)) {
        *(x_word *)pDst = *(const x_word *)pSrc;
        pDst += sizeof(x_word);
        pSrc += sizeof(x_word);
        nSize -= sizeof(x_word);
    }

    while (nSize--) {
        *pDst++ = *pSrc++;
    }

    return pDestination;
#else
    return memcpy(pDestination, pSource, nSize);
#endif
}

void *x_memmove(void *pDestination, const void *pSource, size_t nSize)
{
#if defined(_WIN32)
    unsigned char *pDst = (unsigned char *)pDestination;
    const unsigned char *pSrc = (const unsigned char *)pSource;

    if (pDst == pSrc) {
        return pDestination;
    }

    if ((pDst < pSrc) || (pDst >= pSrc + nSize)) {
        return x_memcpy(pDestination, pSource, nSize);
    }

    /* Overlapping and moving forward: copy backwards. */
    pDst += nSize;
    pSrc += nSize;

    while (nSize--) {
        *--pDst = *--pSrc;
    }

    return pDestination;
#else
    return memmove(pDestination, pSource, nSize);
#endif
}

void *x_memset(void *pDestination, int nValue, size_t nSize)
{
#if defined(_WIN32)
    unsigned char *pDst = (unsigned char *)pDestination;
    unsigned char nByte = (unsigned char)nValue;
    x_word nPattern = 0;
    size_t i = 0;

    for (i = 0; i < sizeof(x_word); i++) {
        nPattern = (nPattern << 8) | nByte;
    }

    while (nSize >= sizeof(x_word)) {
        *(x_word *)pDst = nPattern;
        pDst += sizeof(x_word);
        nSize -= sizeof(x_word);
    }

    while (nSize--) {
        *pDst++ = nByte;
    }

    return pDestination;
#else
    return memset(pDestination, nValue, nSize);
#endif
}

int x_memcmp(const void *pLeft, const void *pRight, size_t nSize)
{
#if defined(_WIN32)
    const unsigned char *pA = (const unsigned char *)pLeft;
    const unsigned char *pB = (const unsigned char *)pRight;

    /* RtlCompareMemory returns the count of equal bytes, not an ordering,
     * so it cannot stand in for memcmp.                                     */
    while (nSize >= sizeof(x_word)) {
        if (*(const x_word *)pA != *(const x_word *)pB) {
            break;
        }

        pA += sizeof(x_word);
        pB += sizeof(x_word);
        nSize -= sizeof(x_word);
    }

    while (nSize--) {
        if (*pA != *pB) {
            return (int)*pA - (int)*pB;
        }

        pA++;
        pB++;
    }

    return 0;
#else
    return memcmp(pLeft, pRight, nSize);
#endif
}

#if defined(_WIN32)
static HANDLE x_process_heap(void)
{
    static HANDLE hHeap = NULL;

    if (hHeap == NULL) {
        hHeap = GetProcessHeap();
    }

    return hHeap;
}
#endif

void *x_malloc(size_t nSize)
{
#if defined(_WIN32)
    return HeapAlloc(x_process_heap(), 0, nSize ? nSize : 1);
#else
    return malloc(nSize);
#endif
}

void *x_calloc(size_t nCount, size_t nSize)
{
#if defined(_WIN32)
    size_t nTotal = nCount * nSize;

    /* Reject a multiplication overflow rather than under-allocating. */
    if (nCount && ((nTotal / nCount) != nSize)) {
        return NULL;
    }

    return HeapAlloc(x_process_heap(), HEAP_ZERO_MEMORY, nTotal ? nTotal : 1);
#else
    return calloc(nCount, nSize);
#endif
}

void *x_realloc(void *pPtr, size_t nSize)
{
#if defined(_WIN32)
    if (pPtr == NULL) {
        return x_malloc(nSize);
    }

    return HeapReAlloc(x_process_heap(), 0, pPtr, nSize ? nSize : 1);
#else
    return realloc(pPtr, nSize);
#endif
}

void x_free(void *pPtr)
{
#if defined(_WIN32)
    if (pPtr) {
        HeapFree(x_process_heap(), 0, pPtr);
    }
#else
    free(pPtr);
#endif
}

/* ------------------------------------------------------------------------ */
/*  Process                                                                  */
/* ------------------------------------------------------------------------ */

void x_exit(int nCode)
{
#if defined(_WIN32)
    ExitProcess((UINT)nCode);
#else
    exit(nCode);
#endif
}

char *x_getenv(const char *pName)
{
#if defined(_WIN32)
    /* getenv() hands back a pointer that stays valid, so the value is kept
     * in a static buffer to preserve that contract.                         */
    static char sValue[4096];
    DWORD nSize = GetEnvironmentVariableA(pName, sValue, (DWORD)sizeof(sValue));

    if ((nSize == 0) || (nSize >= sizeof(sValue))) {
        return NULL;
    }

    return sValue;
#else
    return getenv(pName);
#endif
}

#if defined(_WIN32)
static void x_swap_bytes(char *pLeft, char *pRight, size_t nSize)
{
    size_t i = 0;

    for (i = 0; i < nSize; i++) {
        char nTemp = pLeft[i];

        pLeft[i] = pRight[i];
        pRight[i] = nTemp;
    }
}

/* Insertion sort for short runs, median-of-three quicksort above that. */
static void x_qsort_range(char *pBase, size_t nCount, size_t nSize, int (*fnCompare)(const void *, const void *))
{
    while (nCount > 12) {
        char *pLeft = pBase;
        char *pRight = pBase + (nCount - 1) * nSize;
        char *pMiddle = pBase + (nCount / 2) * nSize;
        size_t nLeftCount = 0;

        if (fnCompare(pMiddle, pLeft) < 0) {
            x_swap_bytes(pMiddle, pLeft, nSize);
        }

        if (fnCompare(pRight, pMiddle) < 0) {
            x_swap_bytes(pRight, pMiddle, nSize);

            if (fnCompare(pMiddle, pLeft) < 0) {
                x_swap_bytes(pMiddle, pLeft, nSize);
            }
        }

        /* Park the pivot at the front and partition the rest. */
        x_swap_bytes(pMiddle, pBase, nSize);
        pLeft = pBase + nSize;
        pRight = pBase + (nCount - 1) * nSize;

        for (;;) {
            while ((pLeft <= pRight) && (fnCompare(pLeft, pBase) <= 0)) {
                pLeft += nSize;
            }

            while ((pLeft <= pRight) && (fnCompare(pRight, pBase) > 0)) {
                pRight -= nSize;
            }

            if (pLeft > pRight) {
                break;
            }

            x_swap_bytes(pLeft, pRight, nSize);
            pLeft += nSize;
            pRight -= nSize;
        }

        x_swap_bytes(pBase, pRight, nSize);

        nLeftCount = (size_t)((pRight - pBase) / (ptrdiff_t)nSize);

        /* Recurse into the smaller side, loop on the larger one. */
        if (nLeftCount < nCount - nLeftCount - 1) {
            x_qsort_range(pBase, nLeftCount, nSize, fnCompare);
            pBase = pRight + nSize;
            nCount = nCount - nLeftCount - 1;
        } else {
            x_qsort_range(pRight + nSize, nCount - nLeftCount - 1, nSize, fnCompare);
            nCount = nLeftCount;
        }
    }

    {
        size_t i = 0;

        for (i = 1; i < nCount; i++) {
            size_t j = i;

            while ((j > 0) && (fnCompare(pBase + (j - 1) * nSize, pBase + j * nSize) > 0)) {
                x_swap_bytes(pBase + (j - 1) * nSize, pBase + j * nSize, nSize);
                j--;
            }
        }
    }
}
#endif

void x_qsort(void *pBase, size_t nCount, size_t nSize, int (*fnCompare)(const void *, const void *))
{
#if defined(_WIN32)
    if ((nCount > 1) && nSize) {
        x_qsort_range((char *)pBase, nCount, nSize, fnCompare);
    }
#else
    qsort(pBase, nCount, nSize, fnCompare);
#endif
}

/* ------------------------------------------------------------------------ */
/*  Conversion                                                               */
/* ------------------------------------------------------------------------ */

/* x_strtod lives in utils_fp.c, which converts without the CRT. */

static int x_is_space_char(char nChar)
{
    return ((nChar == 0x20) || (nChar == 0x09) || (nChar == 0x0A) || (nChar == 0x0D) || (nChar == 0x0C) || (nChar == 0x0B)) ? 1 : 0;
}

static int x_digit_value(char nChar, int nBase)
{
    int nValue = -1;

    if ((nChar >= '0') && (nChar <= '9')) {
        nValue = nChar - '0';
    } else if ((nChar >= 'a') && (nChar <= 'z')) {
        nValue = nChar - 'a' + 10;
    } else if ((nChar >= 'A') && (nChar <= 'Z')) {
        nValue = nChar - 'A' + 10;
    }

    if ((nValue < 0) || (nValue >= nBase)) {
        return -1;
    }

    return nValue;
}

static unsigned long long x_parse_integer(const char *pString, char **ppEnd, int nBase, int *pbNegative)
{
    const char *p = pString;
    unsigned long long nResult = 0;
    int bAny = 0;

    *pbNegative = 0;

    while (x_is_space_char(*p)) {
        p++;
    }

    if ((*p == '+') || (*p == '-')) {
        *pbNegative = (*p == '-') ? 1 : 0;
        p++;
    }

    if ((nBase == 0) || (nBase == 16)) {
        if ((p[0] == '0') && ((p[1] == 'x') || (p[1] == 'X'))) {
            p += 2;
            nBase = 16;
        } else if (nBase == 0) {
            nBase = (p[0] == '0') ? 8 : 10;
        }
    }

    for (;;) {
        int nDigit = x_digit_value(*p, nBase);

        if (nDigit < 0) {
            break;
        }

        nResult = nResult * (unsigned long long)nBase + (unsigned long long)nDigit;
        bAny = 1;
        p++;
    }

    if (ppEnd) {
        *ppEnd = (char *)(bAny ? p : pString);
    }

    return nResult;
}

long x_strtol(const char *pString, char **ppEnd, int nBase)
{
    int bNegative = 0;
    unsigned long long nValue = x_parse_integer(pString, ppEnd, nBase, &bNegative);

    return bNegative ? -(long)nValue : (long)nValue;
}

unsigned long long x_strtoull(const char *pString, char **ppEnd, int nBase)
{
    int bNegative = 0;
    unsigned long long nValue = x_parse_integer(pString, ppEnd, nBase, &bNegative);

    return bNegative ? (unsigned long long)(-(long long)nValue) : nValue;
}

int x_rand(void)
{
#if defined(_WIN32)
    /* xorshift32; Math.random() is not used by the signature database, so a
     * small deterministic generator is enough.                              */
    static unsigned int nState = 0;

    if (nState == 0) {
        nState = (unsigned int)GetTickCount() | 1u;
    }

    nState ^= nState << 13;
    nState ^= nState >> 17;
    nState ^= nState << 5;

    return (int)(nState % (X_RAND_MAX + 1));
#else
    return rand() % (X_RAND_MAX + 1);
#endif
}

/* ------------------------------------------------------------------------ */
/*  I/O                                                                      */
/* ------------------------------------------------------------------------ */

#if defined(_WIN32)

/* Stream handles are the Win32 standard handles or a CreateFile handle. */
void *x_stdout(void)
{
    return (void *)GetStdHandle(STD_OUTPUT_HANDLE);
}

void *x_stderr(void)
{
    return (void *)GetStdHandle(STD_ERROR_HANDLE);
}

static int x_write_stream(void *pStream, const char *pData, size_t nSize)
{
    DWORD nWritten = 0;

    if ((pStream == NULL) || (pStream == INVALID_HANDLE_VALUE) || (nSize == 0)) {
        return 0;
    }

    if (!WriteFile((HANDLE)pStream, pData, (DWORD)nSize, &nWritten, NULL)) {
        return -1;
    }

    return (int)nWritten;
}

void *x_fopen(const char *pFileName, const char *pMode)
{
    int bWrite = ((pMode != NULL) && ((pMode[0] == 'w') || (pMode[0] == 'a'))) ? 1 : 0;
    WCHAR *pWide = x_utf8_to_utf16(pFileName);
    HANDLE hFile = INVALID_HANDLE_VALUE;

    if (pWide == NULL) {
        return NULL;
    }

    hFile = CreateFileW(pWide, bWrite ? GENERIC_WRITE : GENERIC_READ, bWrite ? 0 : (FILE_SHARE_READ | FILE_SHARE_WRITE), NULL,
                        bWrite ? CREATE_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    x_free(pWide);

    if (hFile == INVALID_HANDLE_VALUE) {
        return NULL;
    }

    return (void *)hFile;
}

int x_fclose(void *pFile)
{
    if (pFile == NULL) {
        return -1;
    }

    return CloseHandle((HANDLE)pFile) ? 0 : -1;
}

size_t x_fread(void *pBuffer, size_t nSize, size_t nCount, void *pFile)
{
    DWORD nRead = 0;
    size_t nTotal = nSize * nCount;

    if ((pFile == NULL) || (nTotal == 0)) {
        return 0;
    }

    if (!ReadFile((HANDLE)pFile, pBuffer, (DWORD)nTotal, &nRead, NULL)) {
        return 0;
    }

    return (size_t)nRead / nSize;
}

int x_fseek(void *pFile, long nOffset, int nOrigin)
{
    LARGE_INTEGER liDistance;
    DWORD nMethod = FILE_BEGIN;

    if (pFile == NULL) {
        return -1;
    }

    if (nOrigin == X_SEEK_CUR) {
        nMethod = FILE_CURRENT;
    } else if (nOrigin == X_SEEK_END) {
        nMethod = FILE_END;
    }

    liDistance.QuadPart = nOffset;

    return SetFilePointerEx((HANDLE)pFile, liDistance, NULL, nMethod) ? 0 : -1;
}

long x_ftell(void *pFile)
{
    LARGE_INTEGER liZero;
    LARGE_INTEGER liPosition;

    if (pFile == NULL) {
        return -1;
    }

    liZero.QuadPart = 0;
    liPosition.QuadPart = 0;

    if (!SetFilePointerEx((HANDLE)pFile, liZero, &liPosition, FILE_CURRENT)) {
        return -1;
    }

    return (long)liPosition.QuadPart;
}

void x_rewind(void *pFile)
{
    x_fseek(pFile, 0, X_SEEK_SET);
}

int x_fflush(void *pStream)
{
    /* WriteFile is unbuffered, so there is nothing to flush for the standard
     * handles; a real file handle gets a genuine flush.                     */
    if ((pStream == NULL) || (pStream == x_stdout()) || (pStream == x_stderr())) {
        return 0;
    }

    return FlushFileBuffers((HANDLE)pStream) ? 0 : -1;
}

#else /* not _WIN32 */

void *x_stdout(void)
{
    return (void *)stdout;
}

void *x_stderr(void)
{
    return (void *)stderr;
}

static int x_write_stream(void *pStream, const char *pData, size_t nSize)
{
    return (int)fwrite(pData, 1, nSize, (FILE *)pStream);
}

void *x_fopen(const char *pFileName, const char *pMode)
{
    return (void *)fopen(pFileName, pMode);
}

int x_fclose(void *pFile)
{
    return fclose((FILE *)pFile);
}

size_t x_fread(void *pBuffer, size_t nSize, size_t nCount, void *pFile)
{
    return fread(pBuffer, nSize, nCount, (FILE *)pFile);
}

int x_fseek(void *pFile, long nOffset, int nOrigin)
{
    int nWhence = SEEK_SET;

    if (nOrigin == X_SEEK_CUR) {
        nWhence = SEEK_CUR;
    } else if (nOrigin == X_SEEK_END) {
        nWhence = SEEK_END;
    }

    return fseek((FILE *)pFile, nOffset, nWhence);
}

long x_ftell(void *pFile)
{
    return ftell((FILE *)pFile);
}

void x_rewind(void *pFile)
{
    rewind((FILE *)pFile);
}

int x_fflush(void *pStream)
{
    return fflush((FILE *)pStream);
}

#endif /* _WIN32 */

/* ------------------------------------------------------------------------ */
/*  Formatting                                                               */
/* ------------------------------------------------------------------------ */

#if defined(_WIN32)

/* A printf subset covering everything cdie uses: %s %c %d %i %u %o %x %X %p
 * %% with the -, +, space, 0 and # flags, a width, a precision, and the h,
 * hh, l, ll and z length modifiers.
 *
 * There is no floating point conversion on purpose - every double that cdie
 * prints goes through x_dtoa_shortest / x_dtoa_fixed / x_dtoa_precision.    */

typedef struct {
    char *pBuffer;
    size_t nCapacity;
    size_t nWritten; /* what would have been written, C99 style */
} XFormatSink;

static void fmt_put(XFormatSink *pSink, char nChar)
{
    if (pSink->nWritten + 1 < pSink->nCapacity) {
        pSink->pBuffer[pSink->nWritten] = nChar;
    }

    pSink->nWritten++;
}

static void fmt_pad(XFormatSink *pSink, char nChar, int nCount)
{
    int i = 0;

    for (i = 0; i < nCount; i++) {
        fmt_put(pSink, nChar);
    }
}

static void fmt_emit(XFormatSink *pSink, const char *pText, size_t nSize, int nWidth, int bLeft, int bZero)
{
    int nPad = (nWidth > (int)nSize) ? (nWidth - (int)nSize) : 0;
    size_t i = 0;

    if ((!bLeft) && nPad) {
        fmt_pad(pSink, bZero ? '0' : ' ', nPad);
    }

    for (i = 0; i < nSize; i++) {
        fmt_put(pSink, pText[i]);
    }

    if (bLeft && nPad) {
        fmt_pad(pSink, ' ', nPad);
    }
}

static int fmt_unsigned(char *pBuffer, unsigned long long nValue, unsigned int nBase, int bUpper)
{
    const char *pDigits = bUpper ? "0123456789ABCDEF" : "0123456789abcdef";
    char sTemp[72];
    int nCount = 0;
    int i = 0;

    if (nValue == 0) {
        sTemp[nCount++] = '0';
    }

    while (nValue) {
        sTemp[nCount++] = pDigits[nValue % nBase];
        nValue /= nBase;
    }

    for (i = 0; i < nCount; i++) {
        pBuffer[i] = sTemp[nCount - 1 - i];
    }

    return nCount;
}

int x_vsnprintf(char *pBuffer, size_t nSize, const char *pFormat, X_VA_LIST args)
{
    XFormatSink sink;
    const char *p = pFormat;

    sink.pBuffer = pBuffer;
    sink.nCapacity = nSize;
    sink.nWritten = 0;

    while (*p) {
        int bLeft = 0;
        int bZero = 0;
        int bPlus = 0;
        int bSpace = 0;
        int bAlt = 0;
        int nWidth = 0;
        int nPrecision = -1;
        int nLong = 0; /* 1 = l, 2 = ll, -1 = h, -2 = hh, 3 = z */

        if (*p != '%') {
            fmt_put(&sink, *p++);
            continue;
        }

        p++;

        for (;;) {
            if (*p == '-') {
                bLeft = 1;
            } else if (*p == '0') {
                bZero = 1;
            } else if (*p == '+') {
                bPlus = 1;
            } else if (*p == ' ') {
                bSpace = 1;
            } else if (*p == '#') {
                bAlt = 1;
            } else {
                break;
            }

            p++;
        }

        if (*p == '*') {
            nWidth = va_arg(args, int);
            p++;

            if (nWidth < 0) {
                bLeft = 1;
                nWidth = -nWidth;
            }
        } else {
            while ((*p >= '0') && (*p <= '9')) {
                nWidth = nWidth * 10 + (*p - '0');
                p++;
            }
        }

        if (*p == '.') {
            p++;
            nPrecision = 0;

            if (*p == '*') {
                nPrecision = va_arg(args, int);
                p++;
            } else {
                while ((*p >= '0') && (*p <= '9')) {
                    nPrecision = nPrecision * 10 + (*p - '0');
                    p++;
                }
            }
        }

        if ((p[0] == 'l') && (p[1] == 'l')) {
            nLong = 2;
            p += 2;
        } else if (*p == 'l') {
            nLong = 1;
            p++;
        } else if ((p[0] == 'h') && (p[1] == 'h')) {
            nLong = -2;
            p += 2;
        } else if (*p == 'h') {
            nLong = -1;
            p++;
        } else if (*p == 'z') {
            nLong = 3;
            p++;
        } else if (*p == 'j') {
            nLong = 2;
            p++;
        }

        switch (*p) {
            case 's': {
                const char *pText = va_arg(args, const char *);
                size_t nTextSize = 0;

                if (pText == NULL) {
                    pText = "(null)";
                }

                nTextSize = x_strlen(pText);

                if ((nPrecision >= 0) && ((size_t)nPrecision < nTextSize)) {
                    nTextSize = (size_t)nPrecision;
                }

                fmt_emit(&sink, pText, nTextSize, nWidth, bLeft, 0);
                break;
            }

            case 'c': {
                char nChar = (char)va_arg(args, int);

                fmt_emit(&sink, &nChar, 1, nWidth, bLeft, 0);
                break;
            }

            case 'd':
            case 'i': {
                long long nValue = 0;
                char sDigits[80];
                char sOut[84];
                int nDigits = 0;
                int nOut = 0;
                unsigned long long nMagnitude = 0;
                int bNegative = 0;

                if (nLong == 2) {
                    nValue = va_arg(args, long long);
                } else if (nLong == 1) {
                    nValue = va_arg(args, long);
                } else if (nLong == 3) {
                    nValue = (long long)va_arg(args, size_t);
                } else {
                    nValue = va_arg(args, int);
                }

                bNegative = (nValue < 0) ? 1 : 0;
                nMagnitude = bNegative ? (unsigned long long)(-(nValue + 1)) + 1ull : (unsigned long long)nValue;
                nDigits = fmt_unsigned(sDigits, nMagnitude, 10, 0);

                if (bNegative) {
                    sOut[nOut++] = '-';
                } else if (bPlus) {
                    sOut[nOut++] = '+';
                } else if (bSpace) {
                    sOut[nOut++] = ' ';
                }

                x_memcpy(sOut + nOut, sDigits, (size_t)nDigits);
                nOut += nDigits;

                fmt_emit(&sink, sOut, (size_t)nOut, nWidth, bLeft, bZero && (nPrecision < 0));
                break;
            }

            case 'u':
            case 'o':
            case 'x':
            case 'X':
            case 'p': {
                unsigned long long nValue = 0;
                char sDigits[80];
                char sOut[88];
                int nDigits = 0;
                int nOut = 0;
                unsigned int nBase = 10;
                int bUpper = (*p == 'X') ? 1 : 0;

                if (*p == 'o') {
                    nBase = 8;
                } else if ((*p == 'x') || (*p == 'X') || (*p == 'p')) {
                    nBase = 16;
                }

                if (*p == 'p') {
                    nValue = (unsigned long long)(size_t)va_arg(args, void *);
                } else if (nLong == 2) {
                    nValue = va_arg(args, unsigned long long);
                } else if (nLong == 1) {
                    nValue = va_arg(args, unsigned long);
                } else if (nLong == 3) {
                    nValue = (unsigned long long)va_arg(args, size_t);
                } else {
                    nValue = va_arg(args, unsigned int);
                }

                if (bAlt && nBase == 16 && nValue) {
                    sOut[nOut++] = '0';
                    sOut[nOut++] = bUpper ? 'X' : 'x';
                }

                nDigits = fmt_unsigned(sDigits, nValue, nBase, bUpper);

                while (nDigits < nPrecision) {
                    x_memmove(sDigits + 1, sDigits, (size_t)nDigits);
                    sDigits[0] = '0';
                    nDigits++;
                }

                x_memcpy(sOut + nOut, sDigits, (size_t)nDigits);
                nOut += nDigits;

                fmt_emit(&sink, sOut, (size_t)nOut, nWidth, bLeft, bZero && (nPrecision < 0));
                break;
            }

            case 'f':
            case 'F':
            case 'e':
            case 'E':
            case 'g':
            case 'G':
            case 'a':
            case 'A': {
                /* Not supported by design; consume the argument so the
                 * varargs cursor stays in step and emit a marker.          */
                (void)va_arg(args, double);
                fmt_emit(&sink, "<float>", 7, nWidth, bLeft, 0);
                break;
            }

            case '%': fmt_put(&sink, '%'); break;

            case 0: continue;

            default:
                fmt_put(&sink, '%');
                fmt_put(&sink, *p);
                break;
        }

        p++;
    }

    if (nSize) {
        size_t nTerminator = (sink.nWritten < nSize) ? sink.nWritten : (nSize - 1);

        pBuffer[nTerminator] = 0;
    }

    return (int)sink.nWritten;
}

#else /* not _WIN32 */

int x_vsnprintf(char *pBuffer, size_t nSize, const char *pFormat, X_VA_LIST args)
{
    return vsnprintf(pBuffer, nSize, pFormat, args);
}

#endif /* _WIN32 */

int x_snprintf(char *pBuffer, size_t nSize, const char *pFormat, ...)
{
    X_VA_LIST args;
    int nResult = 0;

    X_VA_START(args, pFormat);
    nResult = x_vsnprintf(pBuffer, nSize, pFormat, args);
    X_VA_END(args);

    return nResult;
}

/* Formatted output goes through x_vsnprintf and then one write, so the
 * Windows path never touches the CRT stdio layer.                          */
static int x_vfprintf_stream(void *pStream, const char *pFormat, X_VA_LIST args)
{
    char sStack[1024];
    X_VA_LIST copy;
    int nSize = 0;

    X_VA_COPY(copy, args);
    nSize = x_vsnprintf(sStack, sizeof(sStack), pFormat, copy);
    X_VA_END(copy);

    if (nSize < 0) {
        return -1;
    }

    if ((size_t)nSize < sizeof(sStack)) {
        return x_write_stream(pStream, sStack, (size_t)nSize);
    }

    {
        char *pHeap = (char *)x_malloc((size_t)nSize + 1);
        int nResult = 0;

        if (pHeap == NULL) {
            return -1;
        }

        x_vsnprintf(pHeap, (size_t)nSize + 1, pFormat, args);
        nResult = x_write_stream(pStream, pHeap, (size_t)nSize);
        x_free(pHeap);

        return nResult;
    }
}

int x_printf(const char *pFormat, ...)
{
    X_VA_LIST args;
    int nResult = 0;

    X_VA_START(args, pFormat);
    nResult = x_vfprintf_stream(x_stdout(), pFormat, args);
    X_VA_END(args);

    return nResult;
}

int x_fprintf(void *pStream, const char *pFormat, ...)
{
    X_VA_LIST args;
    int nResult = 0;

    X_VA_START(args, pFormat);
    nResult = x_vfprintf_stream(pStream, pFormat, args);
    X_VA_END(args);

    return nResult;
}

/* Math lives in utils_math.c; double/decimal conversion in utils_fp.c. */
