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

/* xapk.c - APK (ZIP + Android binary XML) just far enough to read the
 * manifest attributes the database queries.
 *
 * Two format layers:
 *   1. ZIP: scan the End Of Central Directory record from the tail, walk the
 *      central directory to find AndroidManifest.xml, then read its local
 *      header to reach the compressed bytes and inflate them.
 *   2. Android binary XML (AXML): a string pool followed by a stream of XML
 *      chunks. This decodes the start-element attributes into text of the
 *      form `name="value"`, which is what APK_Script::getAndroidManifestRecord
 *      runs its regex over.
 */

#include "xapk.h"
#include "inflate.h"
#include "../core/cd_common.h"

/* -------------------------------------------------------------------- ZIP */

/* Reads the whole ZIP member "AndroidManifest.xml" into pOut. Returns 1 on
 * success. Only STORE (0) and DEFLATE (8) are handled; that is all an APK
 * uses for the manifest. */
static int zip_read_manifest(XBFile *pFile, CDBuf *pOut)
{
    cd_i64 nSize = pFile->nSize;
    const unsigned char *pData = pFile->pData;
    cd_i64 nEocd = -1;
    cd_i64 i = 0;
    cd_i64 nCentralOffset = 0;
    cd_u32 nEntries = 0;
    cd_i64 nScanStart = 0;

    if (nSize < 22) {
        return 0;
    }

    /* The EOCD record (PK\x05\x06) sits within the last 64 KB + 22 bytes. */
    nScanStart = nSize - 22;

    for (i = nScanStart; i >= 0; i--) {
        if ((nSize - i) > (65536 + 22)) {
            break;
        }

        if ((pData[i] == 0x50) && (pData[i + 1] == 0x4B) && (pData[i + 2] == 0x05) && (pData[i + 3] == 0x06)) {
            nEocd = i;

            break;
        }
    }

    if (nEocd < 0) {
        return 0;
    }

    nEntries = xb_u16(pFile, nEocd + 10, 0);
    nCentralOffset = xb_u32(pFile, nEocd + 16, 0);

    for (i = 0; (cd_u32)i < nEntries; i++) {
        cd_u16 nMethod = 0;
        cd_u32 nCompSize = 0;
        cd_u32 nUncompSize = 0;
        cd_u16 nNameLen = 0;
        cd_u16 nExtraLen = 0;
        cd_u16 nCommentLen = 0;
        cd_u32 nLocalOffset = 0;
        cd_i64 nNameOffset = 0;

        /* Central directory header: PK\x01\x02 */
        if ((nCentralOffset + 46) > nSize) {
            return 0;
        }

        if (!((pData[nCentralOffset] == 0x50) && (pData[nCentralOffset + 1] == 0x4B) && (pData[nCentralOffset + 2] == 0x01) && (pData[nCentralOffset + 3] == 0x02))) {
            return 0;
        }

        nMethod = xb_u16(pFile, nCentralOffset + 10, 0);
        nCompSize = xb_u32(pFile, nCentralOffset + 20, 0);
        nUncompSize = xb_u32(pFile, nCentralOffset + 24, 0);
        nNameLen = xb_u16(pFile, nCentralOffset + 28, 0);
        nExtraLen = xb_u16(pFile, nCentralOffset + 30, 0);
        nCommentLen = xb_u16(pFile, nCentralOffset + 32, 0);
        nLocalOffset = xb_u32(pFile, nCentralOffset + 42, 0);
        nNameOffset = nCentralOffset + 46;

        if ((nNameOffset + nNameLen) > nSize) {
            return 0;
        }

        if ((nNameLen == 19) && (x_memcmp(pData + nNameOffset, "AndroidManifest.xml", 19) == 0)) {
            /* Local header: skip its own (possibly different) name and extra
             * field lengths to reach the compressed data.                   */
            cd_u16 nLocalNameLen = 0;
            cd_u16 nLocalExtraLen = 0;
            cd_i64 nDataOffset = 0;

            if ((nLocalOffset + 30) > nSize) {
                return 0;
            }

            if (!((pData[nLocalOffset] == 0x50) && (pData[nLocalOffset + 1] == 0x4B) && (pData[nLocalOffset + 2] == 0x03) && (pData[nLocalOffset + 3] == 0x04))) {
                return 0;
            }

            nLocalNameLen = xb_u16(pFile, nLocalOffset + 26, 0);
            nLocalExtraLen = xb_u16(pFile, nLocalOffset + 28, 0);
            nDataOffset = (cd_i64)nLocalOffset + 30 + nLocalNameLen + nLocalExtraLen;

            if ((nDataOffset + (cd_i64)nCompSize) > nSize) {
                return 0;
            }

            if (nMethod == 0) {
                cdbuf_append(pOut, pData + nDataOffset, nCompSize);

                return 1;
            }

            if (nMethod == 8) {
                return inflate_raw(pData + nDataOffset, nCompSize, nUncompSize, pOut);
            }

            return 0;
        }

        nCentralOffset += 46 + nNameLen + nExtraLen + nCommentLen;
    }

    return 0;
}

/* ------------------------------------------------------------------- AXML */

/* Android binary XML chunk types (subset). */
#define AXML_RES_STRING_POOL 0x0001
#define AXML_RES_XML 0x0003
#define AXML_RES_XML_START_NAMESPACE 0x0100
#define AXML_RES_XML_END_NAMESPACE 0x0101
#define AXML_RES_XML_START_ELEMENT 0x0102
#define AXML_RES_XML_END_ELEMENT 0x0103
#define AXML_RES_XML_RESOURCE_MAP 0x0180

/* A parsed AXML string pool: strings live in a single joined buffer, indexed
 * through the offset array. */
typedef struct {
    const unsigned char *pData;
    size_t nSize;
    cd_u32 nStringCount;
    cd_u32 nFlags;
    size_t nOffsetsBase; /* start of the u32 offset array */
    size_t nStringsBase; /* start of the string data       */
} AxmlPool;

static cd_u16 rd16(const unsigned char *pData, size_t nSize, size_t nOffset)
{
    if (nOffset + 2 > nSize) {
        return 0;
    }

    return (cd_u16)(pData[nOffset] | (pData[nOffset + 1] << 8));
}

static cd_u32 rd32(const unsigned char *pData, size_t nSize, size_t nOffset)
{
    if (nOffset + 4 > nSize) {
        return 0;
    }

    return (cd_u32)pData[nOffset] | ((cd_u32)pData[nOffset + 1] << 8) | ((cd_u32)pData[nOffset + 2] << 16) | ((cd_u32)pData[nOffset + 3] << 24);
}

/* Appends the pool string at nIndex to pOut as UTF-8. Out-of-range indices
 * (including 0xFFFFFFFF for "no string") append nothing. */
static void axml_append_string(const AxmlPool *pPool, cd_u32 nIndex, CDBuf *pOut)
{
    size_t nStrOffset = 0;
    cd_u32 nStrOffsetRel = 0;
    cd_u16 nLen = 0;
    cd_u16 k = 0;

    if (nIndex >= pPool->nStringCount) {
        return;
    }

    nStrOffsetRel = rd32(pPool->pData, pPool->nSize, pPool->nOffsetsBase + (size_t)nIndex * 4);
    nStrOffset = pPool->nStringsBase + nStrOffsetRel;
    nLen = rd16(pPool->pData, pPool->nSize, nStrOffset);

    if (pPool->nFlags) {
        /* UTF-8 pool: copy the bytes as Latin-1, matching the reference's
         * read_ansiString. Each byte becomes one UTF-8 code point.          */
        for (k = 0; k < nLen; k++) {
            size_t nAt = nStrOffset + 2 + k;
            unsigned char nByte = 0;

            if (nAt >= pPool->nSize) {
                break;
            }

            nByte = pPool->pData[nAt];

            if (nByte < 0x80) {
                cdbuf_append_ch(pOut, (char)nByte);
            } else {
                cdbuf_append_ch(pOut, (char)(0xC0 | (nByte >> 6)));
                cdbuf_append_ch(pOut, (char)(0x80 | (nByte & 0x3F)));
            }
        }

        return;
    }

    /* UTF-16LE pool: decode to UTF-8, handling the surrogate pair range. */
    for (k = 0; k < nLen; k++) {
        size_t nAt = nStrOffset + 2 + (size_t)k * 2;
        cd_u32 nUnit = 0;

        if (nAt + 2 > pPool->nSize) {
            break;
        }

        nUnit = (cd_u32)pPool->pData[nAt] | ((cd_u32)pPool->pData[nAt + 1] << 8);

        if ((nUnit >= 0xD800) && (nUnit <= 0xDBFF) && ((size_t)(k + 1) < nLen)) {
            cd_u32 nLow = rd16(pPool->pData, pPool->nSize, nStrOffset + 2 + (size_t)(k + 1) * 2);

            if ((nLow >= 0xDC00) && (nLow <= 0xDFFF)) {
                nUnit = 0x10000 + ((nUnit - 0xD800) << 10) + (nLow - 0xDC00);
                k++;
            }
        }

        if (nUnit < 0x80) {
            cdbuf_append_ch(pOut, (char)nUnit);
        } else if (nUnit < 0x800) {
            cdbuf_append_ch(pOut, (char)(0xC0 | (nUnit >> 6)));
            cdbuf_append_ch(pOut, (char)(0x80 | (nUnit & 0x3F)));
        } else if (nUnit < 0x10000) {
            cdbuf_append_ch(pOut, (char)(0xE0 | (nUnit >> 12)));
            cdbuf_append_ch(pOut, (char)(0x80 | ((nUnit >> 6) & 0x3F)));
            cdbuf_append_ch(pOut, (char)(0x80 | (nUnit & 0x3F)));
        } else {
            cdbuf_append_ch(pOut, (char)(0xF0 | (nUnit >> 18)));
            cdbuf_append_ch(pOut, (char)(0x80 | ((nUnit >> 12) & 0x3F)));
            cdbuf_append_ch(pOut, (char)(0x80 | ((nUnit >> 6) & 0x3F)));
            cdbuf_append_ch(pOut, (char)(0x80 | (nUnit & 0x3F)));
        }
    }
}

/* Appends an attribute value as text into pOut, XML-escaping it so the output
 * matches the reference's QXmlStreamWriter. Escaping keeps the regex results
 * identical when a value contains &, <, >, or ". */
static void axml_append_escaped(const char *pData, size_t nSize, CDBuf *pOut)
{
    size_t i = 0;

    for (i = 0; i < nSize; i++) {
        char c = pData[i];

        switch (c) {
            case '&': cdbuf_append_str(pOut, "&amp;"); break;
            case '<': cdbuf_append_str(pOut, "&lt;"); break;
            case '>': cdbuf_append_str(pOut, "&gt;"); break;
            case '"': cdbuf_append_str(pOut, "&quot;"); break;
            default: cdbuf_append_ch(pOut, c); break;
        }
    }
}

/* Namespace map: uri string index -> prefix string index, filled from the
 * start-namespace chunks. The manifest declares just one (android), but a
 * small fixed table covers any file. */
#define AXML_MAX_NS 16

typedef struct {
    cd_u32 nUri[AXML_MAX_NS];
    cd_u32 nPrefix[AXML_MAX_NS];
    int nCount;
} AxmlNamespaces;

static void axml_write_attr_name(const AxmlPool *pPool, const AxmlNamespaces *pNs, cd_u32 nNsIndex, cd_u32 nNameIndex, CDBuf *pOut)
{
    int i = 0;

    if (nNsIndex != 0xFFFFFFFF) {
        for (i = 0; i < pNs->nCount; i++) {
            if (pNs->nUri[i] == nNsIndex) {
                axml_append_string(pPool, pNs->nPrefix[i], pOut);
                cdbuf_append_ch(pOut, ':');

                break;
            }
        }
    }

    axml_append_string(pPool, nNameIndex, pOut);
}

/* Decodes the AXML in pData into element/attribute text. */
static char *axml_decode(const unsigned char *pData, size_t nSize)
{
    CDBuf out;
    AxmlPool pool;
    AxmlNamespaces ns;
    size_t nOffset = 0;
    int bHavePool = 0;

    x_memset(&pool, 0, sizeof(pool));
    x_memset(&ns, 0, sizeof(ns));
    cdbuf_init(&out);

    pool.pData = pData;
    pool.nSize = nSize;

    /* Top chunk must be RES_XML. */
    if ((nSize < 8) || (rd16(pData, nSize, 0) != AXML_RES_XML)) {
        cdbuf_free(&out);

        return NULL;
    }

    nOffset = 8;

    while (nOffset + 8 <= nSize) {
        cd_u16 nType = rd16(pData, nSize, nOffset);
        cd_u16 nHeaderSize = rd16(pData, nSize, nOffset + 2);
        cd_u32 nChunkSize = rd32(pData, nSize, nOffset + 4);

        if ((nChunkSize == 0) || (nOffset + nChunkSize > nSize) || (nChunkSize < nHeaderSize)) {
            break;
        }

        if (nType == AXML_RES_STRING_POOL) {
            pool.nStringCount = rd32(pData, nSize, nOffset + 8);
            pool.nFlags = rd32(pData, nSize, nOffset + 16);
            pool.nOffsetsBase = nOffset + nHeaderSize;
            pool.nStringsBase = nOffset + rd32(pData, nSize, nOffset + 20);
            bHavePool = 1;
        } else if (nType == AXML_RES_XML_START_NAMESPACE) {
            if ((bHavePool) && (ns.nCount < AXML_MAX_NS)) {
                ns.nPrefix[ns.nCount] = rd32(pData, nSize, nOffset + 16);
                ns.nUri[ns.nCount] = rd32(pData, nSize, nOffset + 20);
                ns.nCount++;
            }
        } else if (nType == AXML_RES_XML_START_ELEMENT) {
            cd_u32 nName = rd32(pData, nSize, nOffset + 20);
            cd_u16 nAttrCount = rd16(pData, nSize, nOffset + 28);
            size_t nAttrOffset = nOffset + 36; /* sizeof(HEADER_XML_START) */
            cd_u16 a = 0;

            cdbuf_append_ch(&out, '<');
            axml_append_string(&pool, nName, &out);

            for (a = 0; a < nAttrCount; a++) {
                size_t nAt = nAttrOffset + (size_t)a * 20;
                cd_u32 nAttrNs = 0;
                cd_u32 nAttrName = 0;
                cd_u8 nDataType = 0;
                cd_u32 nAttrData = 0;

                if (nAt + 20 > nSize) {
                    break;
                }

                nAttrNs = rd32(pData, nSize, nAt + 0);
                nAttrName = rd32(pData, nSize, nAt + 4);
                nDataType = pData[nAt + 15]; /* HEADER_XML_ATTRIBUTE.dataType */
                nAttrData = rd32(pData, nSize, nAt + 16);

                cdbuf_append_ch(&out, ' ');
                axml_write_attr_name(&pool, &ns, nAttrNs, nAttrName, &out);
                cdbuf_append_str(&out, "=\"");

                if (nDataType == 1) {
                    /* Reference: @hex. */
                    cdbuf_appendf(&out, "@%x", (unsigned)nAttrData);
                } else if (nDataType == 3) {
                    /* String: escape so the text matches the reference. */
                    CDBuf value;

                    cdbuf_init(&value);
                    axml_append_string(&pool, nAttrData, &value);
                    axml_append_escaped(value.pData ? value.pData : "", value.nSize, &out);
                    cdbuf_free(&value);
                } else if (nDataType == 16) {
                    cdbuf_appendf(&out, "%d", (int)nAttrData);
                } else if (nDataType == 17) {
                    cdbuf_appendf(&out, "0x%x", (unsigned)nAttrData);
                } else if (nDataType == 18) {
                    cdbuf_append_str(&out, (nAttrData == 0xFFFFFFFF) ? "true" : "false");
                }

                cdbuf_append_ch(&out, '"');
            }

            cdbuf_append_ch(&out, '>');
            cdbuf_append_ch(&out, '\n');
        }

        nOffset += nChunkSize;
    }

    return cdbuf_detach(&out, NULL);
}

/* ------------------------------------------------------------------ entry */

int xapk_parse(XBFile *pFile, XAPK *pApk)
{
    CDBuf manifest;

    x_memset(pApk, 0, sizeof(XAPK));

    if (pFile == NULL) {
        return 0;
    }

    cdbuf_init(&manifest);

    if (!zip_read_manifest(pFile, &manifest)) {
        cdbuf_free(&manifest);

        return 0;
    }

    pApk->pManifestText = axml_decode((const unsigned char *)manifest.pData, manifest.nSize);
    cdbuf_free(&manifest);

    if (pApk->pManifestText == NULL) {
        return 0;
    }

    pApk->bValid = 1;

    return 1;
}

void xapk_free(XAPK *pApk)
{
    if (pApk->pManifestText) {
        cd_free(pApk->pManifestText);
        pApk->pManifestText = NULL;
    }

    pApk->bValid = 0;
}

char *xapk_manifest_record(XAPK *pApk, const char *pKey)
{
    const char *pText = pApk->pManifestText;
    size_t nKeyLen = 0;
    const char *p = NULL;

    if ((pText == NULL) || (pKey == NULL)) {
        return cd_strdup("");
    }

    nKeyLen = x_strlen(pKey);
    p = pText;

    /* Find the first `pKey="`, then capture up to the next quote. Mirrors the
     * reference regex sRecord + "=\"(.*?)\"". */
    while (*p) {
        if ((x_strncmp(p, pKey, nKeyLen) == 0) && (p[nKeyLen] == '=') && (p[nKeyLen + 1] == '"')) {
            const char *pStart = p + nKeyLen + 2;
            const char *pEnd = pStart;

            while (*pEnd && (*pEnd != '"')) {
                pEnd++;
            }

            return cd_strndup(pStart, (size_t)(pEnd - pStart));
        }

        p++;
    }

    return cd_strdup("");
}
