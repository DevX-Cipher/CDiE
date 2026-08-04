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

/* js_regexp.c - a backtracking regular expression engine.
 *
 * Supported syntax: literals, '.', character classes with ranges and
 * negation, the \d \D \w \W \s \S \b \B escapes, greedy and lazy
 * quantifiers (* + ? {n,m}), grouping, non-capturing groups, alternation,
 * anchors, back references and look-ahead assertions. Flags: g, i, m.
 *
 * Matching uses an explicit continuation chain so that quantified groups
 * backtrack correctly without needing to rewrite the AST.
 */

#include "js_internal.h"

typedef enum {
    RE_CHAR = 0, /* single literal byte             */
    RE_ANY,      /* '.'                             */
    RE_CLASS,    /* character class                 */
    RE_GROUP,    /* group with alternatives         */
    RE_BACKREF,  /* \1 .. \9                        */
    RE_BOL,      /* ^                               */
    RE_EOL,      /* $                               */
    RE_WORDB,    /* \b                              */
    RE_NWORDB,   /* \B                              */
    RE_LOOKAHEAD /* (?=...) and (?!...)             */
} RENodeType;

typedef struct RENode RENode;
typedef struct REAlt REAlt;

struct RENode {
    RENodeType type;
    unsigned char nChar;
    unsigned char pClass[32]; /* 256 bit bitmap */
    int bNegateClass;
    int nGroupIndex; /* -1 when non-capturing */
    int bNegateLook;
    REAlt *pAlts;
    int nAltCount;
    int nBackref;

    int nMin;
    int nMax; /* -1 = unbounded */
    int bLazy;

    RENode *pNext;
};

struct REAlt {
    RENode *pFirst;
};

struct JSRegExp {
    char *pSource;
    char *pFlags;
    int bGlobal;
    int bIgnoreCase;
    int bMultiline;
    int nGroups;
    REAlt *pAlts;
    int nAltCount;
};

typedef struct {
    const char *p;
    const char *pEnd;
    JSRegExp *pRegExp;
    char *pError;
    int bIgnoreCase;
} REParser;

/* ------------------------------------------------------------------ util  */

static void class_set(unsigned char *pClass, unsigned char nChar)
{
    pClass[nChar >> 3] |= (unsigned char)(1u << (nChar & 7));
}

static int class_get(const unsigned char *pClass, unsigned char nChar)
{
    return (pClass[nChar >> 3] & (1u << (nChar & 7))) ? 1 : 0;
}

static char re_lower(char nChar)
{
    if ((nChar >= 'A') && (nChar <= 'Z')) {
        return (char)(nChar - 'A' + 'a');
    }

    return nChar;
}

static int is_word_char(unsigned char nChar)
{
    return (((nChar >= 'a') && (nChar <= 'z')) || ((nChar >= 'A') && (nChar <= 'Z')) || ((nChar >= '0') && (nChar <= '9')) || (nChar == '_')) ? 1 : 0;
}

static void class_add_digit(unsigned char *pClass)
{
    int i = 0;

    for (i = '0'; i <= '9'; i++) {
        class_set(pClass, (unsigned char)i);
    }
}

static void class_add_word(unsigned char *pClass)
{
    int i = 0;

    for (i = 0; i < 256; i++) {
        if (is_word_char((unsigned char)i)) {
            class_set(pClass, (unsigned char)i);
        }
    }
}

static void class_add_space(unsigned char *pClass)
{
    static const char *pSpaces = " \t\n\r\f\v";
    int i = 0;

    for (i = 0; pSpaces[i]; i++) {
        class_set(pClass, (unsigned char)pSpaces[i]);
    }
}

static void class_invert(unsigned char *pClass)
{
    int i = 0;

    for (i = 0; i < 32; i++) {
        pClass[i] = (unsigned char)(~pClass[i]);
    }
}

/* ---------------------------------------------------------------- parser  */

static REAlt *parse_alternatives(REParser *pParser, int *pnCount);

static RENode *node_alloc(RENodeType type)
{
    RENode *pNode = (RENode *)cd_calloc(1, sizeof(RENode));

    pNode->type = type;
    pNode->nGroupIndex = -1;
    pNode->nMin = 1;
    pNode->nMax = 1;

    return pNode;
}

static void free_alts(REAlt *pAlts, int nCount);

static void free_nodes(RENode *pNode)
{
    while (pNode) {
        RENode *pNext = pNode->pNext;

        if (pNode->pAlts) {
            free_alts(pNode->pAlts, pNode->nAltCount);
        }

        cd_free(pNode);
        pNode = pNext;
    }
}

static void free_alts(REAlt *pAlts, int nCount)
{
    int i = 0;

    for (i = 0; i < nCount; i++) {
        free_nodes(pAlts[i].pFirst);
    }

    cd_free(pAlts);
}

static void parse_escape_class(REParser *pParser, unsigned char *pClass, int *pbHandled)
{
    char nChar = *pParser->p;

    *pbHandled = 1;

    switch (nChar) {
        case 'd': class_add_digit(pClass); pParser->p++; return;
        case 'D': class_add_digit(pClass); class_invert(pClass); pParser->p++; return;
        case 'w': class_add_word(pClass); pParser->p++; return;
        case 'W': class_add_word(pClass); class_invert(pClass); pParser->p++; return;
        case 's': class_add_space(pClass); pParser->p++; return;
        case 'S': class_add_space(pClass); class_invert(pClass); pParser->p++; return;
        default: *pbHandled = 0; return;
    }
}

static int hex_digit(char nChar)
{
    if ((nChar >= '0') && (nChar <= '9')) {
        return nChar - '0';
    }

    if ((nChar >= 'a') && (nChar <= 'f')) {
        return nChar - 'a' + 10;
    }

    if ((nChar >= 'A') && (nChar <= 'F')) {
        return nChar - 'A' + 10;
    }

    return -1;
}

static unsigned char parse_escape_char(REParser *pParser)
{
    char nChar = 0;

    if (pParser->p >= pParser->pEnd) {
        return '\\';
    }

    nChar = *pParser->p;
    pParser->p++;

    switch (nChar) {
        case 'n': return '\n';
        case 'r': return '\r';
        case 't': return '\t';
        case 'f': return '\f';
        case 'v': return '\v';
        case '0': return '\0';
        case 'b': return '\b'; /* only meaningful inside a class */
        case 'x': {
            int a = (pParser->p < pParser->pEnd) ? hex_digit(pParser->p[0]) : -1;
            int b = ((pParser->p + 1) < pParser->pEnd) ? hex_digit(pParser->p[1]) : -1;

            if ((a >= 0) && (b >= 0)) {
                pParser->p += 2;

                return (unsigned char)((a << 4) | b);
            }

            return 'x';
        }
        case 'u': {
            int i = 0;
            unsigned int nCode = 0;

            for (i = 0; i < 4; i++) {
                int d = ((pParser->p + i) < pParser->pEnd) ? hex_digit(pParser->p[i]) : -1;

                if (d < 0) {
                    return 'u';
                }

                nCode = nCode * 16 + (unsigned int)d;
            }

            pParser->p += 4;

            return (unsigned char)(nCode & 0xFF);
        }
        default: return (unsigned char)nChar;
    }
}

static void class_set_ci(REParser *pParser, RENode *pNode, unsigned char nChar)
{
    class_set(pNode->pClass, nChar);

    if (pParser->bIgnoreCase) {
        if ((nChar >= 'a') && (nChar <= 'z')) {
            class_set(pNode->pClass, (unsigned char)(nChar - 'a' + 'A'));
        } else if ((nChar >= 'A') && (nChar <= 'Z')) {
            class_set(pNode->pClass, (unsigned char)(nChar - 'A' + 'a'));
        }
    }
}

static RENode *parse_class(REParser *pParser)
{
    RENode *pNode = node_alloc(RE_CLASS);

    pParser->p++; /* '[' */

    if ((pParser->p < pParser->pEnd) && (*pParser->p == '^')) {
        pNode->bNegateClass = 1;
        pParser->p++;
    }

    while ((pParser->p < pParser->pEnd) && (*pParser->p != ']')) {
        unsigned char nFrom = 0;

        if (*pParser->p == '\\') {
            unsigned char sTmp[32];
            int bHandled = 0;

            x_memset(sTmp, 0, sizeof(sTmp));
            pParser->p++;

            if (pParser->p >= pParser->pEnd) {
                break;
            }

            parse_escape_class(pParser, sTmp, &bHandled);

            if (bHandled) {
                int i = 0;

                for (i = 0; i < 32; i++) {
                    pNode->pClass[i] |= sTmp[i];
                }

                continue;
            }

            nFrom = parse_escape_char(pParser);
        } else {
            nFrom = (unsigned char)*pParser->p;
            pParser->p++;
        }

        if ((pParser->p < pParser->pEnd) && (*pParser->p == '-') && ((pParser->p + 1) < pParser->pEnd) && (pParser->p[1] != ']')) {
            unsigned char nTo = 0;
            int i = 0;

            pParser->p++;

            if (*pParser->p == '\\') {
                pParser->p++;
                nTo = parse_escape_char(pParser);
            } else {
                nTo = (unsigned char)*pParser->p;
                pParser->p++;
            }

            for (i = nFrom; i <= (int)nTo; i++) {
                class_set_ci(pParser, pNode, (unsigned char)i);
            }
        } else {
            class_set_ci(pParser, pNode, nFrom);
        }
    }

    if ((pParser->p < pParser->pEnd) && (*pParser->p == ']')) {
        pParser->p++;
    }

    if (pNode->bNegateClass) {
        class_invert(pNode->pClass);
    }

    return pNode;
}

static RENode *parse_atom(REParser *pParser)
{
    char nChar = 0;

    if (pParser->p >= pParser->pEnd) {
        return NULL;
    }

    nChar = *pParser->p;

    if (nChar == '(') {
        RENode *pNode = NULL;
        int bCapture = 1;
        int bLookahead = 0;
        int bNegate = 0;

        pParser->p++;

        if (((pParser->p + 1) < pParser->pEnd) && (pParser->p[0] == '?')) {
            if (pParser->p[1] == ':') {
                bCapture = 0;
                pParser->p += 2;
            } else if (pParser->p[1] == '=') {
                bCapture = 0;
                bLookahead = 1;
                pParser->p += 2;
            } else if (pParser->p[1] == '!') {
                bCapture = 0;
                bLookahead = 1;
                bNegate = 1;
                pParser->p += 2;
            } else if (pParser->p[1] == '<') {
                pParser->p += 2;

                while ((pParser->p < pParser->pEnd) && (*pParser->p != '>')) {
                    pParser->p++;
                }

                if (pParser->p < pParser->pEnd) {
                    pParser->p++;
                }
            }
        }

        pNode = node_alloc(bLookahead ? RE_LOOKAHEAD : RE_GROUP);
        pNode->bNegateLook = bNegate;

        if (bCapture) {
            pParser->pRegExp->nGroups++;
            pNode->nGroupIndex = pParser->pRegExp->nGroups;
        }

        pNode->pAlts = parse_alternatives(pParser, &pNode->nAltCount);

        if ((pParser->p < pParser->pEnd) && (*pParser->p == ')')) {
            pParser->p++;
        }

        return pNode;
    }

    if (nChar == '[') {
        return parse_class(pParser);
    }

    if (nChar == '.') {
        pParser->p++;

        return node_alloc(RE_ANY);
    }

    if (nChar == '^') {
        pParser->p++;

        return node_alloc(RE_BOL);
    }

    if (nChar == '$') {
        pParser->p++;

        return node_alloc(RE_EOL);
    }

    if (nChar == '\\') {
        unsigned char sTmp[32];
        int bHandled = 0;

        pParser->p++;

        if (pParser->p >= pParser->pEnd) {
            return NULL;
        }

        if (*pParser->p == 'b') {
            pParser->p++;

            return node_alloc(RE_WORDB);
        }

        if (*pParser->p == 'B') {
            pParser->p++;

            return node_alloc(RE_NWORDB);
        }

        if ((*pParser->p >= '1') && (*pParser->p <= '9')) {
            RENode *pNode = node_alloc(RE_BACKREF);

            pNode->nBackref = *pParser->p - '0';
            pParser->p++;

            return pNode;
        }

        x_memset(sTmp, 0, sizeof(sTmp));
        parse_escape_class(pParser, sTmp, &bHandled);

        if (bHandled) {
            RENode *pNode = node_alloc(RE_CLASS);

            x_memcpy(pNode->pClass, sTmp, sizeof(sTmp));

            return pNode;
        }

        {
            RENode *pNode = node_alloc(RE_CHAR);

            pNode->nChar = parse_escape_char(pParser);

            return pNode;
        }
    }

    {
        RENode *pNode = node_alloc(RE_CHAR);

        pNode->nChar = (unsigned char)nChar;
        pParser->p++;

        return pNode;
    }
}

static void parse_quantifier(REParser *pParser, RENode *pNode)
{
    if (pParser->p >= pParser->pEnd) {
        return;
    }

    if (*pParser->p == '*') {
        pNode->nMin = 0;
        pNode->nMax = -1;
        pParser->p++;
    } else if (*pParser->p == '+') {
        pNode->nMin = 1;
        pNode->nMax = -1;
        pParser->p++;
    } else if (*pParser->p == '?') {
        pNode->nMin = 0;
        pNode->nMax = 1;
        pParser->p++;
    } else if (*pParser->p == '{') {
        const char *pSave = pParser->p;
        int nMin = 0;
        int nMax = -1;
        int bHasDigits = 0;

        pParser->p++;

        while ((pParser->p < pParser->pEnd) && (*pParser->p >= '0') && (*pParser->p <= '9')) {
            nMin = nMin * 10 + (*pParser->p - '0');
            bHasDigits = 1;
            pParser->p++;
        }

        if (!bHasDigits) {
            pParser->p = pSave;

            return;
        }

        if ((pParser->p < pParser->pEnd) && (*pParser->p == ',')) {
            pParser->p++;

            if ((pParser->p < pParser->pEnd) && (*pParser->p >= '0') && (*pParser->p <= '9')) {
                nMax = 0;

                while ((pParser->p < pParser->pEnd) && (*pParser->p >= '0') && (*pParser->p <= '9')) {
                    nMax = nMax * 10 + (*pParser->p - '0');
                    pParser->p++;
                }
            }
        } else {
            nMax = nMin;
        }

        if ((pParser->p < pParser->pEnd) && (*pParser->p == '}')) {
            pParser->p++;
            pNode->nMin = nMin;
            pNode->nMax = nMax;
        } else {
            pParser->p = pSave;

            return;
        }
    } else {
        return;
    }

    if ((pParser->p < pParser->pEnd) && (*pParser->p == '?')) {
        pNode->bLazy = 1;
        pParser->p++;
    }
}

static RENode *parse_sequence(REParser *pParser)
{
    RENode *pFirst = NULL;
    RENode *pLast = NULL;

    while (pParser->p < pParser->pEnd) {
        RENode *pNode = NULL;

        if ((*pParser->p == '|') || (*pParser->p == ')')) {
            break;
        }

        pNode = parse_atom(pParser);

        if (pNode == NULL) {
            break;
        }

        parse_quantifier(pParser, pNode);

        if (pLast) {
            pLast->pNext = pNode;
        } else {
            pFirst = pNode;
        }

        pLast = pNode;
    }

    return pFirst;
}

static REAlt *parse_alternatives(REParser *pParser, int *pnCount)
{
    REAlt *pAlts = NULL;
    int nCount = 0;

    for (;;) {
        RENode *pSequence = parse_sequence(pParser);

        pAlts = (REAlt *)cd_realloc(pAlts, (size_t)(nCount + 1) * sizeof(REAlt));
        pAlts[nCount].pFirst = pSequence;
        nCount++;

        if ((pParser->p < pParser->pEnd) && (*pParser->p == '|')) {
            pParser->p++;
            continue;
        }

        break;
    }

    *pnCount = nCount;

    return pAlts;
}

JSRegExp *jsregexp_compile(const char *pPattern, const char *pFlags, char **ppError)
{
    JSRegExp *pRegExp = (JSRegExp *)cd_calloc(1, sizeof(JSRegExp));
    REParser parser;

    pRegExp->pSource = cd_strdup(pPattern ? pPattern : "");
    pRegExp->pFlags = cd_strdup(pFlags ? pFlags : "");

    if (pFlags) {
        pRegExp->bGlobal = (x_strchr(pFlags, 'g') != NULL) ? 1 : 0;
        pRegExp->bIgnoreCase = (x_strchr(pFlags, 'i') != NULL) ? 1 : 0;
        pRegExp->bMultiline = (x_strchr(pFlags, 'm') != NULL) ? 1 : 0;
    }

    x_memset(&parser, 0, sizeof(parser));
    parser.p = pRegExp->pSource;
    parser.pEnd = pRegExp->pSource + x_strlen(pRegExp->pSource);
    parser.pRegExp = pRegExp;
    parser.bIgnoreCase = pRegExp->bIgnoreCase;

    pRegExp->pAlts = parse_alternatives(&parser, &pRegExp->nAltCount);

    if (parser.pError) {
        if (ppError) {
            *ppError = parser.pError;
        } else {
            cd_free(parser.pError);
        }

        jsregexp_free(pRegExp);

        return NULL;
    }

    return pRegExp;
}

void jsregexp_free(JSRegExp *pRegExp)
{
    if (pRegExp == NULL) {
        return;
    }

    free_alts(pRegExp->pAlts, pRegExp->nAltCount);
    cd_free(pRegExp->pSource);
    cd_free(pRegExp->pFlags);
    cd_free(pRegExp);
}

int jsregexp_ngroups(JSRegExp *pRegExp)
{
    return pRegExp ? pRegExp->nGroups : 0;
}

int jsregexp_global(JSRegExp *pRegExp)
{
    return pRegExp ? pRegExp->bGlobal : 0;
}

int jsregexp_ignorecase(JSRegExp *pRegExp)
{
    return pRegExp ? pRegExp->bIgnoreCase : 0;
}

int jsregexp_multiline(JSRegExp *pRegExp)
{
    return pRegExp ? pRegExp->bMultiline : 0;
}

const char *jsregexp_source(JSRegExp *pRegExp)
{
    return pRegExp ? pRegExp->pSource : "";
}

const char *jsregexp_flags(JSRegExp *pRegExp)
{
    return pRegExp ? pRegExp->pFlags : "";
}

/* --------------------------------------------------------------- matcher  */

#define RE_MAX_STEPS 2000000
/* Recursion guard: a quantified group recurses once per iteration, so a
 * pathological pattern over a long subject could otherwise exhaust the C
 * stack before the step counter fires.                                     */
#define RE_MAX_DEPTH 2000

typedef struct RECont RECont;

struct RECont {
    int nKind; /* 0 = match node chain, 1 = resume a repetition */
    RENode *pNode;
    RENode *pRepeat;
    int nDone;
    size_t nLastPos;
    RECont *pNext;
};

typedef struct {
    const char *pText;
    size_t nSize;
    JSRegExp *pRegExp;
    cd_i32 *pCaps;
    long nSteps;
    int nDepth;
} REMatcher;

static int m_cont(REMatcher *pMatcher, RECont *pCont, size_t nPos, size_t *pnEnd);
static int m_node(REMatcher *pMatcher, RENode *pNode, RECont *pCont, size_t nPos, size_t *pnEnd);

static int m_after(REMatcher *pMatcher, RENode *pNode, RECont *pCont, size_t nPos, size_t *pnEnd)
{
    RECont cont;

    cont.nKind = 0;
    cont.pNode = pNode->pNext;
    cont.pRepeat = NULL;
    cont.nDone = 0;
    cont.nLastPos = 0;
    cont.pNext = pCont;

    return m_cont(pMatcher, &cont, nPos, pnEnd);
}

static int single_match(REMatcher *pMatcher, RENode *pNode, size_t nPos)
{
    unsigned char nChar = 0;

    if (nPos >= pMatcher->nSize) {
        return 0;
    }

    nChar = (unsigned char)pMatcher->pText[nPos];

    switch (pNode->type) {
        case RE_CHAR:
            if (pMatcher->pRegExp->bIgnoreCase) {
                return (re_lower((char)nChar) == re_lower((char)pNode->nChar)) ? 1 : 0;
            }

            return (nChar == pNode->nChar) ? 1 : 0;

        case RE_ANY: return (nChar != '\n') ? 1 : 0;

        case RE_CLASS:
            if (class_get(pNode->pClass, nChar)) {
                return 1;
            }

            if (pMatcher->pRegExp->bIgnoreCase) {
                if ((nChar >= 'a') && (nChar <= 'z')) {
                    return class_get(pNode->pClass, (unsigned char)(nChar - 'a' + 'A'));
                }

                if ((nChar >= 'A') && (nChar <= 'Z')) {
                    return class_get(pNode->pClass, (unsigned char)(nChar - 'A' + 'a'));
                }
            }

            return 0;

        default: return 0;
    }
}

static int is_word_at(REMatcher *pMatcher, size_t nPos)
{
    if (nPos >= pMatcher->nSize) {
        return 0;
    }

    return is_word_char((unsigned char)pMatcher->pText[nPos]);
}

static int m_simple(REMatcher *pMatcher, RENode *pNode, RECont *pCont, size_t nPos, size_t *pnEnd)
{
    size_t nMax = (pNode->nMax < 0) ? (size_t)-1 : (size_t)pNode->nMax;
    size_t nMin = (size_t)pNode->nMin;
    size_t nCount = 0;

    if (pNode->bLazy) {
        for (nCount = 0; nCount < nMin; nCount++) {
            if (!single_match(pMatcher, pNode, nPos + nCount)) {
                return 0;
            }
        }

        for (;;) {
            if (m_after(pMatcher, pNode, pCont, nPos + nCount, pnEnd)) {
                return 1;
            }

            if (nCount >= nMax) {
                return 0;
            }

            if (!single_match(pMatcher, pNode, nPos + nCount)) {
                return 0;
            }

            nCount++;
        }
    }

    while ((nCount < nMax) && single_match(pMatcher, pNode, nPos + nCount)) {
        nCount++;
    }

    if (nCount < nMin) {
        return 0;
    }

    for (;;) {
        if (m_after(pMatcher, pNode, pCont, nPos + nCount, pnEnd)) {
            return 1;
        }

        if (nCount == nMin) {
            break;
        }

        nCount--;
    }

    return 0;
}

static int m_group(REMatcher *pMatcher, RENode *pGroup, int nDone, RECont *pExit, size_t nPos, size_t *pnEnd)
{
    size_t nMax = (pGroup->nMax < 0) ? (size_t)-1 : (size_t)pGroup->nMax;
    int i = 0;

    if (pMatcher->nSteps++ > RE_MAX_STEPS) {
        return 0;
    }

    if (pGroup->bLazy && (nDone >= pGroup->nMin)) {
        if (m_cont(pMatcher, pExit, nPos, pnEnd)) {
            return 1;
        }
    }

    if ((size_t)nDone < nMax) {
        RECont resume;

        resume.nKind = 1;
        resume.pNode = NULL;
        resume.pRepeat = pGroup;
        resume.nDone = nDone + 1;
        resume.nLastPos = nPos;
        resume.pNext = pExit;

        for (i = 0; i < pGroup->nAltCount; i++) {
            cd_i32 nSaved0 = 0;
            cd_i32 nSaved1 = 0;

            if (pGroup->nGroupIndex > 0) {
                nSaved0 = pMatcher->pCaps[pGroup->nGroupIndex * 2];
                nSaved1 = pMatcher->pCaps[pGroup->nGroupIndex * 2 + 1];
            }

            if (pGroup->pAlts[i].pFirst == NULL) {
                if (m_cont(pMatcher, &resume, nPos, pnEnd)) {
                    return 1;
                }
            } else {
                RECont body;

                body.nKind = 0;
                body.pNode = pGroup->pAlts[i].pFirst;
                body.pRepeat = NULL;
                body.nDone = 0;
                body.nLastPos = 0;
                body.pNext = &resume;

                if (m_cont(pMatcher, &body, nPos, pnEnd)) {
                    return 1;
                }
            }

            if (pGroup->nGroupIndex > 0) {
                pMatcher->pCaps[pGroup->nGroupIndex * 2] = nSaved0;
                pMatcher->pCaps[pGroup->nGroupIndex * 2 + 1] = nSaved1;
            }
        }
    }

    if ((!pGroup->bLazy) && (nDone >= pGroup->nMin)) {
        return m_cont(pMatcher, pExit, nPos, pnEnd);
    }

    return 0;
}

static int m_cont(REMatcher *pMatcher, RECont *pCont, size_t nPos, size_t *pnEnd)
{
    int bResult = 0;

    if ((pMatcher->nSteps++ > RE_MAX_STEPS) || (pMatcher->nDepth >= RE_MAX_DEPTH)) {
        return 0;
    }

    if (pCont == NULL) {
        *pnEnd = nPos;

        return 1;
    }

    pMatcher->nDepth++;

    if (pCont->nKind == 0) {
        if (pCont->pNode == NULL) {
            bResult = m_cont(pMatcher, pCont->pNext, nPos, pnEnd);
        } else {
            bResult = m_node(pMatcher, pCont->pNode, pCont->pNext, nPos, pnEnd);
        }
    } else {
        RENode *pGroup = pCont->pRepeat;

        if (pGroup->nGroupIndex > 0) {
            pMatcher->pCaps[pGroup->nGroupIndex * 2] = (cd_i32)pCont->nLastPos;
            pMatcher->pCaps[pGroup->nGroupIndex * 2 + 1] = (cd_i32)nPos;
        }

        if (nPos == pCont->nLastPos) {
            /* Zero-width iteration: stop repeating to avoid an endless loop. */
            bResult = m_cont(pMatcher, pCont->pNext, nPos, pnEnd);
        } else {
            bResult = m_group(pMatcher, pGroup, pCont->nDone, pCont->pNext, nPos, pnEnd);
        }
    }

    pMatcher->nDepth--;

    return bResult;
}

static int m_node(REMatcher *pMatcher, RENode *pNode, RECont *pCont, size_t nPos, size_t *pnEnd)
{
    if (pMatcher->nSteps++ > RE_MAX_STEPS) {
        return 0;
    }

    switch (pNode->type) {
        case RE_BOL:
            if ((nPos == 0) || (pMatcher->pRegExp->bMultiline && (pMatcher->pText[nPos - 1] == '\n'))) {
                return m_after(pMatcher, pNode, pCont, nPos, pnEnd);
            }

            return 0;

        case RE_EOL:
            if ((nPos == pMatcher->nSize) || (pMatcher->pRegExp->bMultiline && (pMatcher->pText[nPos] == '\n'))) {
                return m_after(pMatcher, pNode, pCont, nPos, pnEnd);
            }

            return 0;

        case RE_WORDB: {
            int bBefore = (nPos > 0) ? is_word_at(pMatcher, nPos - 1) : 0;
            int bAfter = is_word_at(pMatcher, nPos);

            if (bBefore != bAfter) {
                return m_after(pMatcher, pNode, pCont, nPos, pnEnd);
            }

            return 0;
        }

        case RE_NWORDB: {
            int bBefore = (nPos > 0) ? is_word_at(pMatcher, nPos - 1) : 0;
            int bAfter = is_word_at(pMatcher, nPos);

            if (bBefore == bAfter) {
                return m_after(pMatcher, pNode, pCont, nPos, pnEnd);
            }

            return 0;
        }

        case RE_BACKREF: {
            cd_i32 nStart = pMatcher->pCaps[pNode->nBackref * 2];
            cd_i32 nEnd = pMatcher->pCaps[pNode->nBackref * 2 + 1];
            size_t nLength = 0;

            if ((nStart < 0) || (nEnd < 0)) {
                return m_after(pMatcher, pNode, pCont, nPos, pnEnd);
            }

            nLength = (size_t)(nEnd - nStart);

            if (nPos + nLength > pMatcher->nSize) {
                return 0;
            }

            if (pMatcher->pRegExp->bIgnoreCase) {
                size_t i = 0;

                for (i = 0; i < nLength; i++) {
                    if (re_lower(pMatcher->pText[nPos + i]) != re_lower(pMatcher->pText[(size_t)nStart + i])) {
                        return 0;
                    }
                }
            } else if (nLength && (x_memcmp(pMatcher->pText + nPos, pMatcher->pText + nStart, nLength) != 0)) {
                return 0;
            }

            return m_after(pMatcher, pNode, pCont, nPos + nLength, pnEnd);
        }

        case RE_LOOKAHEAD: {
            size_t nDummy = 0;
            int bMatched = 0;
            int i = 0;

            for (i = 0; i < pNode->nAltCount; i++) {
                RECont body;

                if (pNode->pAlts[i].pFirst == NULL) {
                    bMatched = 1;
                    break;
                }

                body.nKind = 0;
                body.pNode = pNode->pAlts[i].pFirst;
                body.pRepeat = NULL;
                body.nDone = 0;
                body.nLastPos = 0;
                body.pNext = NULL;

                if (m_cont(pMatcher, &body, nPos, &nDummy)) {
                    bMatched = 1;
                    break;
                }
            }

            if (pNode->bNegateLook) {
                bMatched = !bMatched;
            }

            if (!bMatched) {
                return 0;
            }

            return m_after(pMatcher, pNode, pCont, nPos, pnEnd);
        }

        case RE_GROUP: {
            RECont exitCont;

            exitCont.nKind = 0;
            exitCont.pNode = pNode->pNext;
            exitCont.pRepeat = NULL;
            exitCont.nDone = 0;
            exitCont.nLastPos = 0;
            exitCont.pNext = pCont;

            return m_group(pMatcher, pNode, 0, &exitCont, nPos, pnEnd);
        }

        default: return m_simple(pMatcher, pNode, pCont, nPos, pnEnd);
    }
}

int jsregexp_exec(JSRegExp *pRegExp, const char *pText, size_t nTextSize, size_t nStart, cd_i32 *pnCaps)
{
    REMatcher matcher;
    size_t nPos = 0;

    if (pRegExp == NULL) {
        return 0;
    }

    matcher.pText = pText;
    matcher.nSize = nTextSize;
    matcher.pRegExp = pRegExp;
    matcher.pCaps = pnCaps;

    for (nPos = nStart; nPos <= nTextSize; nPos++) {
        int i = 0;

        matcher.nSteps = 0;
        matcher.nDepth = 0;

        for (i = 0; i <= pRegExp->nGroups; i++) {
            pnCaps[i * 2] = -1;
            pnCaps[i * 2 + 1] = -1;
        }

        for (i = 0; i < pRegExp->nAltCount; i++) {
            size_t nEnd = 0;
            RECont body;

            if (pRegExp->pAlts[i].pFirst == NULL) {
                pnCaps[0] = (cd_i32)nPos;
                pnCaps[1] = (cd_i32)nPos;

                return 1;
            }

            body.nKind = 0;
            body.pNode = pRegExp->pAlts[i].pFirst;
            body.pRepeat = NULL;
            body.nDone = 0;
            body.nLastPos = 0;
            body.pNext = NULL;

            if (m_cont(&matcher, &body, nPos, &nEnd)) {
                pnCaps[0] = (cd_i32)nPos;
                pnCaps[1] = (cd_i32)nEnd;

                return 1;
            }
        }
    }

    return 0;
}

/* ------------------------------------------------- JavaScript RegExp API  */

static JSVal fn_regexp_ctor(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal pattern = js_undefined();
    JSVal flags = js_undefined();
    JSVal result;

    (void)thisVal;
    (void)pUser;

    if (nArgc > 0) {
        JSRegExp *pExisting = js_get_regexp(pArgv[0]);

        if (pExisting) {
            pattern = js_str(pCtx, jsregexp_source(pExisting));
        } else {
            pattern = js_to_string(pCtx, pArgv[0]);
        }
    } else {
        pattern = js_str(pCtx, "");
    }

    flags = (nArgc > 1) ? js_to_string(pCtx, pArgv[1]) : js_str(pCtx, "");
    result = js_new_regexp_val(pCtx, js_str_data(pattern), js_str_data(flags));

    js_release(pCtx, pattern);
    js_release(pCtx, flags);

    return result;
}

static JSVal fn_regexp_test(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSRegExp *pRegExp = js_get_regexp(thisVal);
    JSVal text;
    cd_i32 *pCaps = NULL;
    int bResult = 0;

    (void)pUser;

    if (pRegExp == NULL) {
        return js_throw(pCtx, "TypeError: RegExp.prototype.test on a non-RegExp");
    }

    text = js_to_string(pCtx, (nArgc > 0) ? pArgv[0] : js_undefined());
    pCaps = (cd_i32 *)cd_malloc((size_t)(pRegExp->nGroups + 1) * 2 * sizeof(cd_i32));
    bResult = jsregexp_exec(pRegExp, js_str_data(text), js_str_len(text), 0, pCaps);
    cd_free(pCaps);
    js_release(pCtx, text);

    return js_bool(bResult);
}

static JSVal fn_regexp_exec(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSRegExp *pRegExp = js_get_regexp(thisVal);
    JSVal text;
    cd_i32 *pCaps = NULL;
    JSVal result = js_null();
    size_t nStart = 0;

    (void)pUser;

    if (pRegExp == NULL) {
        return js_throw(pCtx, "TypeError: RegExp.prototype.exec on a non-RegExp");
    }

    text = js_to_string(pCtx, (nArgc > 0) ? pArgv[0] : js_undefined());

    if (pRegExp->bGlobal) {
        JSVal lastIndex = js_get(pCtx, thisVal, "lastIndex");
        cd_i64 nLastIndex = js_to_int64(pCtx, lastIndex);

        js_release(pCtx, lastIndex);

        if (nLastIndex > 0) {
            nStart = (size_t)nLastIndex;
        }
    }

    pCaps = (cd_i32 *)cd_malloc((size_t)(pRegExp->nGroups + 1) * 2 * sizeof(cd_i32));

    if ((nStart <= js_str_len(text)) && jsregexp_exec(pRegExp, js_str_data(text), js_str_len(text), nStart, pCaps)) {
        int g = 0;

        result = js_new_array(pCtx);

        for (g = 0; g <= pRegExp->nGroups; g++) {
            if (pCaps[g * 2] >= 0) {
                js_set_index(pCtx, result, g, js_strn(pCtx, js_str_data(text) + pCaps[g * 2], (size_t)(pCaps[g * 2 + 1] - pCaps[g * 2])));
            } else {
                js_set_index(pCtx, result, g, js_undefined());
            }
        }

        result.u.o->nArrayLen = pRegExp->nGroups + 1;
        jsobj_put_hidden(pCtx, result.u.o, "index", js_num((double)pCaps[0]));
        jsobj_put_hidden(pCtx, result.u.o, "input", js_dup(text));

        if (pRegExp->bGlobal) {
            js_set(pCtx, thisVal, "lastIndex", js_num((double)pCaps[1]));
        }
    } else if (pRegExp->bGlobal) {
        js_set(pCtx, thisVal, "lastIndex", js_num(0));
    }

    cd_free(pCaps);
    js_release(pCtx, text);

    return result;
}

static JSVal fn_regexp_tostring(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSRegExp *pRegExp = js_get_regexp(thisVal);
    CDBuf buf;
    JSVal result;

    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    cdbuf_init(&buf);
    cdbuf_append_ch(&buf, '/');
    cdbuf_append_str(&buf, pRegExp ? jsregexp_source(pRegExp) : "");
    cdbuf_append_ch(&buf, '/');
    cdbuf_append_str(&buf, pRegExp ? jsregexp_flags(pRegExp) : "");

    result = js_strn(pCtx, buf.pData, buf.nSize);
    cdbuf_free(&buf);

    return result;
}

JSRegExp *js_get_regexp(JSVal value)
{
    if ((value.tag == JT_OBJ) && (value.u.o->cls == JCLASS_REGEXP)) {
        return value.u.o->pRegExp;
    }

    return NULL;
}

JSVal js_new_regexp_val(JSCtx *pCtx, const char *pPattern, const char *pFlags)
{
    JSObj *pObj = jsobj_new(pCtx, JCLASS_REGEXP, pCtx->pRegExpProto);
    char *pError = NULL;

    pObj->pRegExp = jsregexp_compile(pPattern, pFlags, &pError);

    if (pObj->pRegExp == NULL) {
        JSVal result = js_throw(pCtx, "SyntaxError: invalid regular expression /%s/: %s", pPattern, pError ? pError : "");

        cd_free(pError);
        jsobj_unref(pCtx, pObj);

        return result;
    }

    jsobj_put_hidden(pCtx, pObj, "source", js_str(pCtx, pPattern));
    jsobj_put_hidden(pCtx, pObj, "flags", js_str(pCtx, pFlags ? pFlags : ""));
    jsobj_put_hidden(pCtx, pObj, "global", js_bool(pObj->pRegExp->bGlobal));
    jsobj_put_hidden(pCtx, pObj, "ignoreCase", js_bool(pObj->pRegExp->bIgnoreCase));
    jsobj_put_hidden(pCtx, pObj, "multiline", js_bool(pObj->pRegExp->bMultiline));
    jsobj_put_hidden(pCtx, pObj, "lastIndex", js_num(0));

    return jsval_obj(pObj);
}

void js_install_regexp(JSCtx *pCtx)
{
    JSVal ctor = js_new_native(pCtx, "RegExp", fn_regexp_ctor, 2, NULL);

    jsobj_put_hidden(pCtx, pCtx->pRegExpProto, "test", js_new_native(pCtx, "test", fn_regexp_test, 1, NULL));
    jsobj_put_hidden(pCtx, pCtx->pRegExpProto, "exec", js_new_native(pCtx, "exec", fn_regexp_exec, 1, NULL));
    jsobj_put_hidden(pCtx, pCtx->pRegExpProto, "toString", js_new_native(pCtx, "toString", fn_regexp_tostring, 0, NULL));

    jsobj_put_hidden(pCtx, ctor.u.o, "prototype", jsval_obj(jsobj_ref(pCtx->pRegExpProto)));
    jsobj_put_hidden(pCtx, pCtx->pRegExpProto, "constructor", js_dup(ctor));
    jsobj_put_hidden(pCtx, pCtx->pGlobal, "RegExp", ctor);
}
