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

/* xpdf.c - a PDF object reader, ported from XPDF.
 *
 * PDF detection in the database is entirely key/value driven: the scripts ask
 * for "/Filter", "/Creator", "/Producer" and "/Author" values and the header
 * comment. None of that needs the page tree, encryption or stream
 * decompression, so this parser stops at reducing each object dictionary to a
 * flat list of tokens (XPART::listParts) and answering key/value queries over
 * those, exactly as the reference does.
 *
 * The tokeniser mirrors XPDF's _readPDFStringPart family byte for byte,
 * including its quirks (indirect "N 0 R" references glued into one token, the
 * simple backslash handling in "(...)" strings), because the goal is
 * identical output, not a better PDF reader.
 */

#include "xpdf.h"

/* The whole file is already in memory; work over it directly. */
typedef struct {
    const unsigned char *pData;
    cd_i64 nSize;
} PdfIO;

/* A token read from the stream: the text plus how many raw bytes it spanned. */
typedef struct {
    char *pStr;      /* heap, owned by caller  */
    cd_i64 nStrLen;
    cd_i64 nSize;    /* bytes consumed in file */
} PdfTok;

/* ------------------------------------------------------- char classes --- */

static int pdf_is_line_ending(int c)
{
    return (c == 10) || (c == 13);
}

static int pdf_is_string_terminator(int c)
{
    return (c == 0) || pdf_is_line_ending(c);
}

static int pdf_is_title_terminator(int c)
{
    return pdf_is_string_terminator(c) || (c == '<');
}

static int pdf_is_structural_delimiter(int c)
{
    return (c == '[') || (c == ']') || (c == '<') || (c == '>');
}

static int pdf_is_name_terminator(int c)
{
    return pdf_is_string_terminator(c) || pdf_is_structural_delimiter(c) || (c == ' ') || (c == '(');
}

static int pdf_is_value_terminator(int c)
{
    return pdf_is_string_terminator(c) || pdf_is_structural_delimiter(c) || (c == '/');
}

/* ------------------------------------------------------------- skips ---- */

static cd_i64 pdf_skip_ending(const PdfIO *pIO, cd_i64 nOffset)
{
    cd_i64 nStart = nOffset;

    while (nOffset < pIO->nSize) {
        int c = pIO->pData[nOffset];

        if (c == 10) {
            nOffset++;
        } else if (c == 13) {
            nOffset++;

            if ((nOffset < pIO->nSize) && (pIO->pData[nOffset] == 10)) {
                nOffset++;
            }
        } else {
            break;
        }
    }

    return nOffset - nStart;
}

static cd_i64 pdf_skip_space(const PdfIO *pIO, cd_i64 nOffset)
{
    cd_i64 nStart = nOffset;

    while ((nOffset < pIO->nSize) && (pIO->pData[nOffset] == ' ')) {
        nOffset++;
    }

    return nOffset - nStart;
}

/* ------------------------------------------------------ token readers --- */

static char *pdf_dup_range(const PdfIO *pIO, cd_i64 nOffset, cd_i64 nLen)
{
    return cd_strndup((const char *)pIO->pData + nOffset, (size_t)nLen);
}

/* _readPDFString: bytes up to a line terminator, then the terminator. */
static PdfTok pdf_read_string(const PdfIO *pIO, cd_i64 nOffset, cd_i64 nMax)
{
    PdfTok tok;
    cd_i64 nPos = 0;

    tok.pStr = NULL;
    tok.nStrLen = 0;
    tok.nSize = 0;

    if ((nOffset < 0) || (nOffset >= pIO->nSize)) {
        tok.pStr = cd_strdup("");

        return tok;
    }

    if ((nMax < 0) || (nOffset + nMax > pIO->nSize)) {
        nMax = pIO->nSize - nOffset;
    }

    while ((nPos < nMax) && (!pdf_is_string_terminator(pIO->pData[nOffset + nPos]))) {
        nPos++;
    }

    tok.pStr = pdf_dup_range(pIO, nOffset, nPos);
    tok.nStrLen = nPos;
    tok.nSize = nPos + pdf_skip_ending(pIO, nOffset + nPos);

    return tok;
}

/* _readPDFStringPart_title: like the above but '<' also terminates. */
static PdfTok pdf_read_title(const PdfIO *pIO, cd_i64 nOffset, cd_i64 nMax)
{
    PdfTok tok;
    cd_i64 nPos = 0;

    tok.pStr = NULL;
    tok.nStrLen = 0;
    tok.nSize = 0;

    if ((nOffset < 0) || (nOffset >= pIO->nSize)) {
        tok.pStr = cd_strdup("");

        return tok;
    }

    if ((nMax < 0) || (nOffset + nMax > pIO->nSize)) {
        nMax = pIO->nSize - nOffset;
    }

    while ((nPos < nMax) && (!pdf_is_title_terminator(pIO->pData[nOffset + nPos]))) {
        nPos++;
    }

    tok.pStr = pdf_dup_range(pIO, nOffset, nPos);
    tok.nStrLen = nPos;
    tok.nSize = nPos + pdf_skip_ending(pIO, nOffset + nPos);

    return tok;
}

/* _readPDFStringPart_str: a "(...)" string. The returned text keeps the outer
 * parentheses and drops one backslash from every escape, exactly as the
 * reference does; a leading FE FF marks UTF-16BE content. */
static PdfTok pdf_read_str(const PdfIO *pIO, cd_i64 nOffset)
{
    PdfTok tok;
    CDBuf buf;
    cd_i64 nPos = nOffset;
    int bStart = 0;
    int bEnd = 0;
    int bUnicode = 0;
    int bBackslash = 0;

    tok.pStr = NULL;
    tok.nStrLen = 0;
    tok.nSize = 0;

    cdbuf_init(&buf);

    while (nPos < pIO->nSize) {
        if (!bUnicode) {
            int c = pIO->pData[nPos];

            if (pdf_is_string_terminator(c)) {
                break;
            }

            if (!bStart) {
                if (c == '(') {
                    bStart = 1;

                    if ((nPos + 2 < pIO->nSize) && (pIO->pData[nPos + 1] == 0xFE) && (pIO->pData[nPos + 2] == 0xFF)) {
                        bUnicode = 1;
                        tok.nSize += 2;
                        nPos += 2;
                    }

                    cdbuf_append_ch(&buf, '(');
                } else {
                    bBackslash = 0;
                    cdbuf_append_ch(&buf, (char)c);
                }
            } else if ((c == ')') && (!bBackslash)) {
                cdbuf_append_ch(&buf, ')');
                bEnd = 1;
            } else if (c == '\\') {
                bBackslash = 1;
            } else {
                bBackslash = 0;
                cdbuf_append_ch(&buf, (char)c);
            }

            tok.nSize++;
            nPos++;
        } else {
            cd_u32 nWord = 0;

            if (nPos + 1 >= pIO->nSize) {
                break;
            }

            nWord = ((cd_u32)pIO->pData[nPos] << 8) | pIO->pData[nPos + 1];

            if (((nWord >> 8) == ')') && (!bBackslash)) {
                cdbuf_append_ch(&buf, ')');
                tok.nSize++;
                bEnd = 1;
            } else if (nWord == '\\') {
                bBackslash = 1;
                nPos += 2;
                tok.nSize += 2;

                continue;
            } else if (bBackslash && (nWord == 0x6e29)) {
                bBackslash = 0;
                cdbuf_append_ch(&buf, ')');
                tok.nSize++;
                bEnd = 1;
            } else {
                cd_u32 nUnit = nWord;

                bBackslash = 0;

                if (nUnit < 0x80) {
                    cdbuf_append_ch(&buf, (char)nUnit);
                } else if (nUnit < 0x800) {
                    cdbuf_append_ch(&buf, (char)(0xC0 | (nUnit >> 6)));
                    cdbuf_append_ch(&buf, (char)(0x80 | (nUnit & 0x3F)));
                } else {
                    cdbuf_append_ch(&buf, (char)(0xE0 | (nUnit >> 12)));
                    cdbuf_append_ch(&buf, (char)(0x80 | ((nUnit >> 6) & 0x3F)));
                    cdbuf_append_ch(&buf, (char)(0x80 | (nUnit & 0x3F)));
                }

                nPos += 2;
                tok.nSize += 2;

                continue;
            }

            nPos++;
        }

        if (bStart && bEnd) {
            break;
        }
    }

    tok.nStrLen = (cd_i64)buf.nSize;
    tok.pStr = cdbuf_detach(&buf, NULL);

    return tok;
}

/* _readPDFStringPart_hex: a "<...>" hex string, brackets included. */
static PdfTok pdf_read_hex(const PdfIO *pIO, cd_i64 nOffset)
{
    PdfTok tok;
    cd_i64 nPos = 0;

    tok.pStr = NULL;
    tok.nStrLen = 0;
    tok.nSize = 0;

    while ((nOffset + nPos) < pIO->nSize) {
        int c = pIO->pData[nOffset + nPos];

        nPos++;

        if (c == '>') {
            break;
        }
    }

    tok.pStr = pdf_dup_range(pIO, nOffset, nPos);
    tok.nStrLen = nPos;
    tok.nSize = nPos;

    return tok;
}

/* _readPDFStringPart: the token dispatcher. */
static PdfTok pdf_read_part(const PdfIO *pIO, cd_i64 nOffset)
{
    PdfTok tok;
    int nChar = 0;
    cd_i64 nTokenEnd = 0;
    int bFallback = 0;

    tok.pStr = NULL;
    tok.nStrLen = 0;
    tok.nSize = 0;

    if ((nOffset < 0) || (nOffset >= pIO->nSize)) {
        tok.pStr = cd_strdup("");

        return tok;
    }

    nChar = pIO->pData[nOffset];

    if (nChar == '/') {
        cd_i64 nPos = 0;
        int bFirst = 1;

        while ((nOffset + nPos) < pIO->nSize) {
            int c = pIO->pData[nOffset + nPos];

            if (pdf_is_name_terminator(c)) {
                break;
            }

            if ((!bFirst) && (c == '/')) {
                break;
            }

            bFirst = 0;
            nPos++;
        }

        tok.pStr = pdf_dup_range(pIO, nOffset, nPos);
        tok.nStrLen = nPos;
        nTokenEnd = nPos;
    } else if (nChar == '(') {
        tok = pdf_read_str(pIO, nOffset);
        bFallback = 1;
    } else if (nChar == '<') {
        if ((nOffset + 1 < pIO->nSize) && (pIO->pData[nOffset + 1] == '<')) {
            tok.pStr = cd_strdup("<<");
            tok.nStrLen = 2;
            nTokenEnd = 2;
        } else {
            tok = pdf_read_hex(pIO, nOffset);
            bFallback = 1;
        }
    } else if (nChar == '>') {
        if ((nOffset + 1 < pIO->nSize) && (pIO->pData[nOffset + 1] == '>')) {
            tok.pStr = cd_strdup(">>");
            tok.nStrLen = 2;
            nTokenEnd = 2;
        } else {
            tok.pStr = cd_strdup("");
            tok.nStrLen = 0;
            nTokenEnd = 0;
        }
    } else if (nChar == '[') {
        tok.pStr = cd_strdup("[");
        tok.nStrLen = 1;
        nTokenEnd = 1;
    } else if (nChar == ']') {
        tok.pStr = cd_strdup("]");
        tok.nStrLen = 1;
        nTokenEnd = 1;
    } else {
        cd_i64 nPos = 0;
        int bSpace = 0;

        while ((nOffset + nPos) < pIO->nSize) {
            int c = pIO->pData[nOffset + nPos];

            if (pdf_is_value_terminator(c)) {
                break;
            }

            if (c == ' ') {
                nPos++;
                bSpace = 1;

                break;
            }

            nPos++;
        }

        {
            cd_i64 nCopy = bSpace ? (nPos - 1) : nPos;

            tok.pStr = pdf_dup_range(pIO, nOffset, nCopy);
            tok.nStrLen = nCopy;
            nTokenEnd = nPos;
        }

        /* Glue an indirect "N 0 R" reference into one token. */
        if (bSpace) {
            if ((nOffset + nPos + 2 < pIO->nSize) && (pIO->pData[nOffset + nPos] == '0') && (pIO->pData[nOffset + nPos + 1] == ' ') && (pIO->pData[nOffset + nPos + 2] == 'R')) {
                cd_free(tok.pStr);
                tok.pStr = pdf_dup_range(pIO, nOffset, nPos + 3);
                tok.nStrLen = nPos + 3;
                nTokenEnd = nPos + 3;
            }
        }
    }

    if (bFallback) {
        cd_i64 nNew = nOffset + tok.nSize;

        tok.nSize += pdf_skip_space(pIO, nNew);
        nNew = nOffset + tok.nSize;
        tok.nSize += pdf_skip_ending(pIO, nNew);

        return tok;
    }

    {
        cd_i64 nNew = nOffset + nTokenEnd;

        nTokenEnd += pdf_skip_space(pIO, nNew);
        nNew = nOffset + nTokenEnd;
        nTokenEnd += pdf_skip_ending(pIO, nNew);
        tok.nSize = nTokenEnd;
    }

    return tok;
}

/* ---------------------------------------------------------- predicates --- */

/* _isObject: last non-space run is "obj", preceded by a space or the start. */
static int pdf_is_object(const char *pString)
{
    cd_i64 nLen = (cd_i64)x_strlen(pString);
    cd_i64 nEnd = nLen;
    cd_i64 nStart = 0;

    while ((nEnd > 0) && (pString[nEnd - 1] == ' ')) {
        nEnd--;
    }

    if (nEnd < 3) {
        return 0;
    }

    nStart = nEnd - 3;

    if (!((nStart == 0) || (pString[nStart - 1] == ' '))) {
        return 0;
    }

    return (pString[nStart] == 'o') && (pString[nStart + 1] == 'b') && (pString[nStart + 2] == 'j');
}

static int pdf_is_end_object(const char *pString)
{
    /* trimmed() == "endobj" */
    cd_i64 nStart = 0;
    cd_i64 nEnd = (cd_i64)x_strlen(pString);

    while ((nStart < nEnd) && (pString[nStart] == ' ')) {
        nStart++;
    }

    while ((nEnd > nStart) && (pString[nEnd - 1] == ' ')) {
        nEnd--;
    }

    return ((nEnd - nStart) == 6) && (x_strncmp(pString + nStart, "endobj", 6) == 0);
}

static int pdf_is_xref(const char *pString)
{
    if (x_strncmp(pString, "xref", 4) != 0) {
        return 0;
    }

    return (pString[4] == 0) || (pString[4] == ' ');
}

static int pdf_is_comment(const char *pString)
{
    return (pString[0] == '%');
}

/* getObjectID: leading optional '-' then decimal digits. */
static cd_i64 pdf_object_id(const char *pString)
{
    cd_i64 n = 0;
    int bNeg = 0;
    cd_i64 i = 0;

    if (pString[i] == '-') {
        bNeg = 1;
        i++;
    }

    while ((pString[i] >= '0') && (pString[i] <= '9')) {
        n = n * 10 + (pString[i] - '0');
        i++;
    }

    return bNeg ? -n : n;
}

/* nth space-delimited field of a string (empty for out of range). */
static char *pdf_field(const char *pString, int nField)
{
    int n = 0;
    const char *pStart = pString;
    const char *p = pString;

    for (;;) {
        if ((*p == ' ') || (*p == 0)) {
            if (n == nField) {
                return cd_strndup(pStart, (size_t)(p - pStart));
            }

            if (*p == 0) {
                return cd_strdup("");
            }

            n++;
            pStart = p + 1;
        }

        p++;
    }
}

/* QString::toLongLong: whole string (trimmed) must be an integer. */
static cd_i64 pdf_strtoll_strict(const char *pString, int *pbOk)
{
    cd_i64 n = 0;
    int bNeg = 0;
    int bAny = 0;
    const char *p = pString;

    while (*p == ' ') {
        p++;
    }

    if ((*p == '-') || (*p == '+')) {
        bNeg = (*p == '-');
        p++;
    }

    while ((*p >= '0') && (*p <= '9')) {
        n = n * 10 + (*p - '0');
        bAny = 1;
        p++;
    }

    while (*p == ' ') {
        p++;
    }

    if ((!bAny) || (*p != 0)) {
        if (pbOk) {
            *pbOk = 0;
        }

        return 0;
    }

    if (pbOk) {
        *pbOk = 1;
    }

    return bNeg ? -n : n;
}

/* --------------------------------------------------------- object walk --- */

typedef struct {
    cd_i64 nOffset;
    cd_i64 nId;
} PdfObjRef;

typedef struct {
    cd_i64 nXrefOffset;
    cd_i64 nFooterOffset;
    int bIsXref;
    int bIsObject;
} PdfStartxref;

/* handleXpart, reduced to the dictionary token list. The reference goes on to
 * measure stream sizes; nothing here needs that, and the tokens the value
 * queries read are all captured before the stream, so parsing stops once the
 * object's dictionary/array is balanced. */
static CDVec *pdf_handle_object(const PdfIO *pIO, cd_i64 nOffset)
{
    CDVec *pParts = (CDVec *)cd_malloc(sizeof(CDVec));
    PdfTok title = pdf_read_title(pIO, nOffset, 20);

    cdvec_init(pParts);

    nOffset += title.nSize;

    if (pdf_is_object(title.pStr)) {
        int nObj = 0;
        int nCol = 0;

        for (;;) {
            PdfTok part = pdf_read_part(pIO, nOffset);
            cd_i64 nStrLen = part.nStrLen;

            cdvec_push(pParts, part.pStr);
            nOffset += part.nSize;

            if (nStrLen == 0) {
                break;
            }

            if (nStrLen == 1) {
                if (part.pStr[0] == '[') {
                    nCol++;
                } else if (part.pStr[0] == ']') {
                    nCol--;
                }
            } else if (nStrLen == 2) {
                if (part.pStr[0] == '<') {
                    nObj++;
                } else if (part.pStr[0] == '>') {
                    nObj--;
                }
            }

            if ((nObj == 0) && (nCol == 0)) {
                break;
            }
        }
    }

    cd_free(title.pStr);

    return pParts;
}

/* findObjects: linear "N 0 obj ... endobj" scan. With bDeepScan set (the
 * path taken when a startxref points at an object, i.e. a cross-reference
 * stream) a non-object token does not stop the walk; instead it skips forward
 * to the next " obj" and backs up over the "N M " that precedes it. */
static void pdf_find_objects(const PdfIO *pIO, cd_i64 nOffset, cd_i64 nSize, int bDeepScan, CDVec *pRefs)
{
    cd_i64 nEnd = 0;

    if (nSize == -1) {
        nSize = pIO->nSize - nOffset;
    }

    nEnd = nOffset + nSize;

    while (nOffset < nEnd) {
        PdfTok os = pdf_read_string(pIO, nOffset, 64);

        if (pdf_is_object(os.pStr)) {
            cd_i64 nId = pdf_object_id(os.pStr);
            cd_i64 nSearchStart = nOffset + os.nSize;
            cd_i64 nEndObj = -1;

            if (nSearchStart < nEnd) {
                cd_i64 i = nSearchStart;
                cd_i64 nStop = nEnd - 6;

                for (; i <= nStop; i++) {
                    if (x_memcmp(pIO->pData + i, "endobj", 6) == 0) {
                        nEndObj = i;

                        break;
                    }
                }
            }

            if (nEndObj != -1) {
                PdfTok osEnd = pdf_read_string(pIO, nEndObj, 32);

                if (pdf_is_end_object(osEnd.pStr)) {
                    PdfObjRef *pRef = (PdfObjRef *)cd_malloc(sizeof(PdfObjRef));

                    pRef->nOffset = nOffset;
                    pRef->nId = nId;
                    cdvec_push(pRefs, pRef);

                    nOffset = nEndObj + osEnd.nSize;
                    cd_free(osEnd.pStr);
                    cd_free(os.pStr);

                    continue;
                }

                cd_free(osEnd.pStr);
                cd_free(os.pStr);

                break;
            }

            cd_free(os.pStr);

            break;
        } else if (pdf_is_comment(os.pStr)) {
            nOffset += os.nSize;
            cd_free(os.pStr);
        } else {
            int bContinue = 0;

            cd_free(os.pStr);

            if (bDeepScan) {
                cd_i64 i = nOffset;
                cd_i64 nStop = nEnd - 4;
                cd_i64 nFound = -1;

                for (; i <= nStop; i++) {
                    if (x_memcmp(pIO->pData + i, " obj", 4) == 0) {
                        nFound = i;

                        break;
                    }
                }

                if (nFound != -1) {
                    /* Back up over the "N M " that precedes " obj". */
                    while ((nFound > 0)) {
                        int nPrev = pIO->pData[nFound - 1];

                        if (!(((nPrev >= '0') && (nPrev <= '9')) || (nPrev == ' '))) {
                            break;
                        }

                        nFound--;
                    }

                    nOffset = nFound;
                    bContinue = 1;
                }
            }

            if (!bContinue) {
                break;
            }
        }
    }
}

/* getObjectsFromStartxref for a classic "xref" table. Objects come out sorted
 * by file offset (ascending), matching the reference's QMap ordering. */
static void pdf_objects_from_xref(const PdfIO *pIO, const PdfStartxref *pStart, CDVec *pRefs)
{
    cd_i64 nOffset = pStart->nXrefOffset;
    PdfTok os = pdf_read_string(pIO, nOffset, 20);

    if (!pdf_is_xref(os.pStr)) {
        cd_free(os.pStr);

        return;
    }

    nOffset += os.nSize;
    cd_free(os.pStr);

    for (;;) {
        PdfTok section = pdf_read_string(pIO, nOffset, 20);
        char *pIdField = NULL;
        char *pCountField = NULL;
        cd_i64 nId = 0;
        cd_i64 nCount = 0;
        cd_i64 i = 0;

        if (section.nStrLen == 0) {
            cd_free(section.pStr);

            break;
        }

        pIdField = pdf_field(section.pStr, 0);
        pCountField = pdf_field(section.pStr, 1);
        nId = pdf_strtoll_strict(pIdField, NULL);
        nCount = pdf_strtoll_strict(pCountField, NULL);
        cd_free(pIdField);
        cd_free(pCountField);

        nOffset += section.nSize;
        cd_free(section.pStr);

        if (nCount <= 0) {
            break;
        }

        for (i = 0; i < nCount; i++) {
            PdfTok obj = pdf_read_string(pIO, nOffset, 20);
            char *pType = pdf_field(obj.pStr, 2);

            if (x_strcmp(pType, "n") == 0) {
                char *pOff = pdf_field(obj.pStr, 0);
                cd_i64 nObjOffset = pdf_strtoll_strict(pOff, NULL);

                if ((nObjOffset > 0) && (nObjOffset < pIO->nSize)) {
                    PdfObjRef *pRef = (PdfObjRef *)cd_malloc(sizeof(PdfObjRef));

                    pRef->nOffset = nObjOffset;
                    pRef->nId = nId + i;
                    cdvec_push(pRefs, pRef);
                }

                cd_free(pOff);
            }

            cd_free(pType);
            nOffset += obj.nSize;
            cd_free(obj.pStr);
        }
    }

    /* Sort the collected offsets ascending (insertion sort; object counts are
     * modest and this keeps behaviour obvious). */
    {
        size_t a = 0;

        for (a = 1; a < pRefs->nSize; a++) {
            PdfObjRef *pKey = (PdfObjRef *)pRefs->ppData[a];
            size_t b = a;

            while ((b > 0) && (((PdfObjRef *)pRefs->ppData[b - 1])->nOffset > pKey->nOffset)) {
                pRefs->ppData[b] = pRefs->ppData[b - 1];
                b--;
            }

            pRefs->ppData[b] = pKey;
        }
    }
}

/* findStartxrefs: collect the "startxref -> xref/object" chains. */
static void pdf_find_startxrefs(const PdfIO *pIO, CDVec *pStarts)
{
    cd_i64 nOffset = 0;

    for (;;) {
        cd_i64 nStartXref = -1;
        cd_i64 nCurrent = 0;
        PdfTok osStartXref;
        PdfTok osOffset;
        PdfTok osHref;
        cd_i64 nTarget = 0;
        int bIsXref = 0;
        int bIsObject = 0;

        {
            cd_i64 i = nOffset;
            cd_i64 nStop = pIO->nSize - 9;

            for (; i <= nStop; i++) {
                if (x_memcmp(pIO->pData + i, "startxref", 9) == 0) {
                    nStartXref = i;

                    break;
                }
            }
        }

        if (nStartXref == -1) {
            break;
        }

        nCurrent = nStartXref;
        osStartXref = pdf_read_string(pIO, nCurrent, 20);
        nCurrent += osStartXref.nSize;
        cd_free(osStartXref.pStr);

        osOffset = pdf_read_string(pIO, nCurrent, 20);
        nTarget = pdf_strtoll_strict(osOffset.pStr, NULL);

        osHref = pdf_read_string(pIO, nTarget, 20);
        bIsXref = pdf_is_xref(osHref.pStr);
        bIsObject = pdf_is_object(osHref.pStr);
        cd_free(osHref.pStr);

        if ((bIsXref || bIsObject) && (nTarget < nCurrent)) {
            PdfTok osEnd;

            nCurrent += osOffset.nSize;
            osEnd = pdf_read_string(pIO, nCurrent, 20);

            if (x_strncmp(osEnd.pStr, "%%EOF", 5) == 0) {
                PdfStartxref *pRec = (PdfStartxref *)cd_malloc(sizeof(PdfStartxref));
                int bStopChain = 0;

                pRec->nXrefOffset = nTarget;
                pRec->nFooterOffset = nStartXref;
                pRec->bIsObject = bIsObject;
                pRec->bIsXref = bIsXref;
                cdvec_push(pStarts, pRec);

                nCurrent += 5;

                if ((nCurrent < pIO->nSize) && (pIO->pData[nCurrent] == 13)) {
                    nCurrent++;
                }

                if ((nCurrent < pIO->nSize) && (pIO->pData[nCurrent] == 10)) {
                    nCurrent++;
                }

                if (osEnd.nStrLen != 5) {
                    bStopChain = 1;
                } else {
                    /* An incremental update appends another section; keep
                     * chaining only while what follows still looks like one. */
                    PdfTok osAppend = pdf_read_string(pIO, nCurrent, 20);

                    if ((!pdf_is_object(osAppend.pStr)) && (!pdf_is_comment(osAppend.pStr)) && (!pdf_is_xref(osAppend.pStr))) {
                        bStopChain = 1;
                    }

                    cd_free(osAppend.pStr);
                }

                cd_free(osEnd.pStr);
                cd_free(osOffset.pStr);

                if (bStopChain) {
                    break;
                }

                nOffset = nStartXref + 10;

                continue;
            }

            cd_free(osEnd.pStr);
        }

        cd_free(osOffset.pStr);
        nOffset = nStartXref + 10;
    }
}

/* ---------------------------------------------------------- top level --- */

static void pdf_collect_object_refs(const PdfIO *pIO, CDVec *pRefs)
{
    CDVec starts;
    size_t i = 0;

    cdvec_init(&starts);
    pdf_find_startxrefs(pIO, &starts);

    if (starts.nSize > 0) {
        for (i = 0; i < starts.nSize; i++) {
            PdfStartxref *pStart = (PdfStartxref *)starts.ppData[i];

            if (pStart->bIsXref) {
                pdf_objects_from_xref(pIO, pStart, pRefs);
            } else if (pStart->bIsObject) {
                pdf_find_objects(pIO, 0, pStart->nFooterOffset, 1, pRefs);
            }
        }
    } else {
        pdf_find_objects(pIO, 0, -1, 0, pRefs);
    }

    for (i = 0; i < starts.nSize; i++) {
        cd_free(starts.ppData[i]);
    }

    cdvec_free(&starts);
}

int xpdf_parse(XBFile *pFile, XPDF *pPdf)
{
    PdfIO io;
    CDVec refs;
    size_t i = 0;

    x_memset(pPdf, 0, sizeof(XPDF));

    if (pFile == NULL) {
        return 0;
    }

    pPdf->pFile = pFile;
    cdvec_init(&pPdf->vecObjects);

    io.pData = pFile->pData;
    io.nSize = pFile->nSize;

    cdvec_init(&refs);
    pdf_collect_object_refs(&io, &refs);

    for (i = 0; i < refs.nSize; i++) {
        PdfObjRef *pRef = (PdfObjRef *)refs.ppData[i];
        CDVec *pParts = pdf_handle_object(&io, pRef->nOffset);

        cdvec_push(&pPdf->vecObjects, pParts);
        cd_free(pRef);
    }

    cdvec_free(&refs);

    pPdf->bValid = 1;

    return 1;
}

void xpdf_free(XPDF *pPdf)
{
    size_t i = 0;
    size_t j = 0;

    for (i = 0; i < pPdf->vecObjects.nSize; i++) {
        CDVec *pParts = (CDVec *)pPdf->vecObjects.ppData[i];

        for (j = 0; j < pParts->nSize; j++) {
            cd_free(pParts->ppData[j]);
        }

        cdvec_free(pParts);
        cd_free(pParts);
    }

    cdvec_free(&pPdf->vecObjects);
    pPdf->bValid = 0;
}

char *xpdf_version(XPDF *pPdf)
{
    PdfIO io;
    PdfTok tok;
    char *pResult = NULL;

    io.pData = pPdf->pFile->pData;
    io.nSize = pPdf->pFile->nSize;

    tok = pdf_read_string(&io, 5, 3);
    pResult = tok.pStr;

    return pResult;
}

/* _parseValue's classification, reduced to what the queries need: is this
 * token a string object, and what is its unwrapped text? */
static int pdf_value_is_string(const char *pToken)
{
    cd_i64 nLen = (cd_i64)x_strlen(pToken);

    return (nLen >= 2) && (pToken[0] == '(') && (pToken[nLen - 1] == ')');
}

static int pdf_value_is_pure_int(const char *pToken)
{
    int bOk = 0;
    cd_i64 n = pdf_strtoll_strict(pToken, &bOk);

    return bOk && (n != 0);
}

/* The text _parseValue would expose as var.toString(): strings lose their
 * parentheses, everything else is the token verbatim. */
static char *pdf_value_text(const char *pToken)
{
    if (pdf_value_is_string(pToken)) {
        cd_i64 nLen = (cd_i64)x_strlen(pToken);

        return cd_strndup(pToken + 1, (size_t)(nLen - 2));
    }

    return cd_strdup(pToken);
}

void xpdf_values_by_key(XPDF *pPdf, const char *pKey, int bStringsOnly, int nPartLimit, CDVec *pOut)
{
    size_t i = 0;

    for (i = 0; i < pPdf->vecObjects.nSize; i++) {
        CDVec *pParts = (CDVec *)pPdf->vecObjects.ppData[i];
        size_t nLimit = pParts->nSize;
        size_t j = 0;

        if ((nPartLimit >= 0) && ((size_t)nPartLimit < nLimit)) {
            nLimit = (size_t)nPartLimit;
        }

        for (j = 0; (j + 1) < nLimit; j++) {
            const char *pPart = (const char *)pParts->ppData[j];

            if (x_strcmp(pPart, pKey) == 0) {
                const char *pValueToken = (const char *)pParts->ppData[j + 1];

                /* getStringValuesByKey keeps only string objects; the value
                 * must additionally not be an empty (null) parse. Pure zero
                 * integers parse to a null variant in the reference and are
                 * dropped. */
                if (bStringsOnly && (!pdf_value_is_string(pValueToken))) {
                    continue;
                }

                {
                    char *pText = pdf_value_text(pValueToken);
                    int bDuplicate = 0;
                    size_t k = 0;

                    if ((pText[0] == 0) && (!pdf_value_is_pure_int(pValueToken)) && (!pdf_value_is_string(pValueToken))) {
                        /* Empty non-string value: treated as null, skip. */
                        cd_free(pText);

                        continue;
                    }

                    for (k = 0; k < pOut->nSize; k++) {
                        if (x_strcmp((const char *)pOut->ppData[k], pText) == 0) {
                            bDuplicate = 1;

                            break;
                        }
                    }

                    if (bDuplicate) {
                        cd_free(pText);
                    } else {
                        cdvec_push(pOut, pText);
                    }
                }
            }
        }
    }
}

char *xpdf_filters(XPDF *pPdf)
{
    CDVec values;
    CDBuf out;
    size_t i = 0;

    cdvec_init(&values);
    cdbuf_init(&out);

    /* getFilters uses getParts(100). */
    xpdf_values_by_key(pPdf, "/Filter", 0, 100, &values);

    for (i = 0; i < values.nSize; i++) {
        const char *pValue = (const char *)values.ppData[i];

        if (pValue[0] != 0) {
            if (out.nSize > 0) {
                cdbuf_append_str(&out, ", ");
            }

            cdbuf_append_str(&out, pValue);
        }

        cd_free(values.ppData[i]);
    }

    cdvec_free(&values);

    return cdbuf_detach(&out, NULL);
}

char *xpdf_header_comment_hex(XPDF *pPdf)
{
    PdfIO io;
    PdfTok os;
    cd_i64 nOffset = 0;
    CDBuf out;

    io.pData = pPdf->pFile->pData;
    io.nSize = pPdf->pFile->nSize;

    cdbuf_init(&out);

    os = pdf_read_string(&io, 0, 100);
    nOffset = os.nSize;
    cd_free(os.pStr);

    if ((nOffset < io.nSize) && (io.pData[nOffset] == '%')) {
        cd_i64 nMaxRead = 40;
        cd_i64 nLen = 0;

        nOffset++;

        if (io.nSize - nOffset < nMaxRead) {
            nMaxRead = io.nSize - nOffset;
        }

        while (nLen < nMaxRead) {
            if (pdf_is_string_terminator(io.pData[nOffset + nLen])) {
                break;
            }

            nLen++;
        }

        {
            cd_i64 i = 0;

            for (i = 0; i < nLen; i++) {
                cdbuf_appendf(&out, "%02x", io.pData[nOffset + i]);
            }
        }
    }

    return cdbuf_detach(&out, NULL);
}
