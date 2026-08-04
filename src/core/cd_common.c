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

#include "cd_common.h"


void *cd_malloc(size_t nSize)
{
    void *pResult = x_malloc(nSize ? nSize : 1);

    if (pResult == NULL) {
        x_fprintf(x_stderr(), "cdie: out of memory\n");
        x_exit(3);
    }

    return pResult;
}

void *cd_calloc(size_t nCount, size_t nSize)
{
    void *pResult = x_calloc(nCount ? nCount : 1, nSize ? nSize : 1);

    if (pResult == NULL) {
        x_fprintf(x_stderr(), "cdie: out of memory\n");
        x_exit(3);
    }

    return pResult;
}

void *cd_realloc(void *pPtr, size_t nSize)
{
    void *pResult = x_realloc(pPtr, nSize ? nSize : 1);

    if (pResult == NULL) {
        x_fprintf(x_stderr(), "cdie: out of memory\n");
        x_exit(3);
    }

    return pResult;
}

void cd_free(void *pPtr)
{
    x_free(pPtr);
}

char *cd_strdup(const char *pString)
{
    if (pString == NULL) {
        return NULL;
    }

    return cd_strndup(pString, x_strlen(pString));
}

char *cd_strndup(const char *pString, size_t nSize)
{
    char *pResult = (char *)cd_malloc(nSize + 1);

    if (nSize) {
        x_memcpy(pResult, pString, nSize);
    }

    pResult[nSize] = 0;

    return pResult;
}

/* ---------------------------------------------------------------- buffer  */

void cdbuf_init(CDBuf *pBuf)
{
    pBuf->pData = NULL;
    pBuf->nSize = 0;
    pBuf->nCapacity = 0;
}

void cdbuf_free(CDBuf *pBuf)
{
    cd_free(pBuf->pData);
    cdbuf_init(pBuf);
}

void cdbuf_reserve(CDBuf *pBuf, size_t nCapacity)
{
    if (nCapacity + 1 > pBuf->nCapacity) {
        size_t nNew = pBuf->nCapacity ? pBuf->nCapacity : 32;

        while (nNew < nCapacity + 1) {
            nNew *= 2;
        }

        pBuf->pData = (char *)cd_realloc(pBuf->pData, nNew);
        pBuf->nCapacity = nNew;
    }
}

void cdbuf_clear(CDBuf *pBuf)
{
    pBuf->nSize = 0;

    if (pBuf->pData) {
        pBuf->pData[0] = 0;
    }
}

void cdbuf_append(CDBuf *pBuf, const void *pData, size_t nSize)
{
    if (nSize == 0) {
        return;
    }

    cdbuf_reserve(pBuf, pBuf->nSize + nSize);
    x_memcpy(pBuf->pData + pBuf->nSize, pData, nSize);
    pBuf->nSize += nSize;
    pBuf->pData[pBuf->nSize] = 0;
}

void cdbuf_append_str(CDBuf *pBuf, const char *pString)
{
    if (pString) {
        cdbuf_append(pBuf, pString, x_strlen(pString));
    }
}

void cdbuf_append_ch(CDBuf *pBuf, char nChar)
{
    cdbuf_reserve(pBuf, pBuf->nSize + 1);
    pBuf->pData[pBuf->nSize++] = nChar;
    pBuf->pData[pBuf->nSize] = 0;
}

void cdbuf_appendf(CDBuf *pBuf, const char *pFormat, ...)
{
    char sStack[512];
    X_VA_LIST args;
    int nCount;

    X_VA_START(args, pFormat);
    nCount = x_vsnprintf(sStack, sizeof(sStack), pFormat, args);
    X_VA_END(args);

    if (nCount < 0) {
        return;
    }

    if ((size_t)nCount < sizeof(sStack)) {
        cdbuf_append(pBuf, sStack, (size_t)nCount);
    } else {
        char *pHeap = (char *)cd_malloc((size_t)nCount + 1);

        X_VA_START(args, pFormat);
        x_vsnprintf(pHeap, (size_t)nCount + 1, pFormat, args);
        X_VA_END(args);

        cdbuf_append(pBuf, pHeap, (size_t)nCount);
        cd_free(pHeap);
    }
}

char *cdbuf_detach(CDBuf *pBuf, size_t *pnSize)
{
    char *pResult = pBuf->pData;

    if (pResult == NULL) {
        pResult = (char *)cd_malloc(1);
        pResult[0] = 0;
    }

    if (pnSize) {
        *pnSize = pBuf->nSize;
    }

    cdbuf_init(pBuf);

    return pResult;
}

/* ---------------------------------------------------------------- vector  */

void cdvec_init(CDVec *pVec)
{
    pVec->ppData = NULL;
    pVec->nSize = 0;
    pVec->nCapacity = 0;
}

void cdvec_free(CDVec *pVec)
{
    cd_free(pVec->ppData);
    cdvec_init(pVec);
}

static void cdvec_grow(CDVec *pVec, size_t nNeeded)
{
    if (nNeeded > pVec->nCapacity) {
        size_t nNew = pVec->nCapacity ? pVec->nCapacity : 8;

        while (nNew < nNeeded) {
            nNew *= 2;
        }

        pVec->ppData = (void **)cd_realloc(pVec->ppData, nNew * sizeof(void *));
        pVec->nCapacity = nNew;
    }
}

void cdvec_push(CDVec *pVec, void *pItem)
{
    cdvec_grow(pVec, pVec->nSize + 1);
    pVec->ppData[pVec->nSize++] = pItem;
}

void cdvec_insert(CDVec *pVec, size_t nIndex, void *pItem)
{
    if (nIndex > pVec->nSize) {
        nIndex = pVec->nSize;
    }

    cdvec_grow(pVec, pVec->nSize + 1);
    x_memmove(pVec->ppData + nIndex + 1, pVec->ppData + nIndex, (pVec->nSize - nIndex) * sizeof(void *));
    pVec->ppData[nIndex] = pItem;
    pVec->nSize++;
}

void cdvec_remove(CDVec *pVec, size_t nIndex)
{
    if (nIndex >= pVec->nSize) {
        return;
    }

    x_memmove(pVec->ppData + nIndex, pVec->ppData + nIndex + 1, (pVec->nSize - nIndex - 1) * sizeof(void *));
    pVec->nSize--;
}

void cdvec_clear(CDVec *pVec)
{
    pVec->nSize = 0;
}

/* ------------------------------------------------------------ misc utils  */

cd_u32 cd_hash_str(const char *pString, size_t nSize)
{
    /* FNV-1a */
    cd_u32 nHash = 2166136261u;
    size_t i = 0;

    for (i = 0; i < nSize; i++) {
        nHash ^= (cd_u8)pString[i];
        nHash *= 16777619u;
    }

    return nHash;
}

static char cd_lower_ascii(char nChar)
{
    if ((nChar >= 'A') && (nChar <= 'Z')) {
        return (char)(nChar - 'A' + 'a');
    }

    return nChar;
}

int cd_stricmp_ascii(const char *pLeft, const char *pRight)
{
    while (*pLeft && *pRight) {
        char a = cd_lower_ascii(*pLeft);
        char b = cd_lower_ascii(*pRight);

        if (a != b) {
            return (int)(unsigned char)a - (int)(unsigned char)b;
        }

        pLeft++;
        pRight++;
    }

    return (int)(unsigned char)cd_lower_ascii(*pLeft) - (int)(unsigned char)cd_lower_ascii(*pRight);
}

int cd_strnicmp_ascii(const char *pLeft, const char *pRight, size_t nSize)
{
    size_t i = 0;

    for (i = 0; i < nSize; i++) {
        char a = cd_lower_ascii(pLeft[i]);
        char b = cd_lower_ascii(pRight[i]);

        if (a != b) {
            return (int)(unsigned char)a - (int)(unsigned char)b;
        }

        if (a == 0) {
            break;
        }
    }

    return 0;
}
