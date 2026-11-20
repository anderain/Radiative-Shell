/*
   Shell configuration: reader and writer for the .cfg format.

   The parser is deliberately strict. The layout is positional, so a file is
   either accepted as a whole or rejected with a message that names the line at
   fault. There is no partial recovery, because a half applied configuration
   would leave the shell in a state nobody can reason about.

   File access goes through the Win32 API only. There is no stdio here on
   purpose: the Windows CE CRT is minimal, and CreateFile/ReadFile/WriteFile are
   available on both targets.
*/

#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"

/* At least three theme lines plus six key lines. */
#define CFG_MIN_LINES   (RSHL_THEME_COUNT + RSHL_KEY_COUNT)

/* The format is positional, so this is the number of content lines allowed. */
#define CFG_MAX_LINES   (RSHL_THEME_COUNT + RSHL_KEY_COUNT + RSHL_SLOT_MAX)

#define CFG_FIELD_MAX   8
#define CFG_FILE_MAX    (256UL * 1024UL)

/* -------------------------------------------------------------------------
   Small string helpers, so nothing here depends on the printf family.
   ------------------------------------------------------------------------- */

static void CfgAppend(char* pDst, int iSize, const char* pSrc) {
    int iLen = (int)strlen(pDst);
    int i = 0;

    if (iLen >= iSize - 1) {
        return;
    }

    while (pSrc[i] && iLen < iSize - 1) {
        pDst[iLen++] = pSrc[i++];
    }

    pDst[iLen] = 0;
}

static void CfgAppendDec(char* pDst, int iSize, int iValue) {
    char aDigits[12];
    char aOne[2];
    int  iCount = 0;

    if (iValue < 0) {
        CfgAppend(pDst, iSize, "-");
        iValue = -iValue;
    }

    do {
        aDigits[iCount++] = (char)('0' + (iValue % 10));
        iValue /= 10;
    } while (iValue > 0 && iCount < (int)sizeof(aDigits));

    aOne[1] = 0;
    while (iCount > 0) {
        aOne[0] = aDigits[--iCount];
        CfgAppend(pDst, iSize, aOne);
    }
}

/* Compose an error message. iLineNo of 0 means "not tied to a line". */
static void CfgFail(char* pError, int iErrorSize, int iLineNo, const char* pWhat) {
    if (!pError || iErrorSize <= 0) {
        return;
    }

    pError[0] = 0;

    if (iLineNo > 0) {
        CfgAppend(pError, iErrorSize, "line ");
        CfgAppendDec(pError, iErrorSize, iLineNo);
        CfgAppend(pError, iErrorSize, ": ");
    }

    CfgAppend(pError, iErrorSize, pWhat);
}

/* Trim leading and trailing blanks in place and return the new start. */
static char* CfgTrim(char* pText) {
    char* p = pText;
    int   iLen;

    while (*p == ' ' || *p == '\t') {
        ++p;
    }

    iLen = (int)strlen(p);
    while (iLen > 0) {
        const char c = p[iLen - 1];

        if (c != ' ' && c != '\t') {
            break;
        }
        p[--iLen] = 0;
    }

    return p;
}

/* Split on '|' in place. Returns the field count, or -1 when the line holds
   more than iMax fields. */
static int CfgSplit(char* pLine, char* aFields[], int iMax) {
    int   iCount = 0;
    char* p = pLine;

    for (;;) {
        if (iCount >= iMax) {
            return -1;
        }

        aFields[iCount++] = p;

        while (*p && *p != '|') {
            ++p;
        }

        if (!*p) {
            break;
        }
        *p++ = 0;
    }

    return iCount;
}

/* Hexadecimal byte, 0..255. A leading 0x is tolerated. */
static BOOL CfgParseHex(const char* pText, int* piValue) {
    int iValue = 0;
    int iDigits = 0;

    if (!pText || !*pText) {
        return FALSE;
    }

    if (pText[0] == '0' && (pText[1] == 'x' || pText[1] == 'X')) {
        pText += 2;
    }

    while (*pText) {
        const char c = *pText;
        int iDigit;

        if (c >= '0' && c <= '9') {
            iDigit = c - '0';
        }
        else if (c >= 'a' && c <= 'f') {
            iDigit = c - 'a' + 10;
        }
        else if (c >= 'A' && c <= 'F') {
            iDigit = c - 'A' + 10;
        }
        else {
            return FALSE;
        }

        iValue = iValue * 16 + iDigit;
        if (iValue > 0xFF) {
            return FALSE;
        }

        ++iDigits;
        ++pText;
    }

    if (iDigits == 0) {
        return FALSE;
    }

    *piValue = iValue;
    return TRUE;
}

/* Decimal with an explicit range check. */
static BOOL CfgParseDec(const char* pText, int* piValue, int iMin, int iMax) {
    int iValue = 0;
    int iDigits = 0;

    if (!pText) {
        return FALSE;
    }

    while (*pText) {
        const char c = *pText;

        if (c < '0' || c > '9') {
            return FALSE;
        }

        iValue = iValue * 10 + (c - '0');
        if (iValue > iMax) {
            return FALSE;
        }

        ++iDigits;
        ++pText;
    }

    if (iDigits == 0 || iValue < iMin) {
        return FALSE;
    }

    *piValue = iValue;
    return TRUE;
}

/* -------------------------------------------------------------------------
   File access, Win32 only.
   ------------------------------------------------------------------------- */

/* Read a whole file into a NUL terminated buffer. *ppBuf is set to NULL on
   failure and *piSize tells the caller which message to use. */
static BOOL CfgReadFile(const char* pszPath, char** ppBuf, int* piSize,
                        BOOL* pbMissing) {
    HANDLE hFile;
    DWORD  dwSize;
    DWORD  dwRead = 0;
    char*  pBuf;

    *ppBuf     = NULL;
    *piSize    = 0;
    *pbMissing = FALSE;

    hFile = CreateFileA(pszPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        *pbMissing = TRUE;
        return FALSE;
    }

    dwSize = GetFileSize(hFile, NULL);
    if (dwSize == INVALID_FILE_SIZE || dwSize > CFG_FILE_MAX) {
        CloseHandle(hFile);
        return FALSE;
    }

    pBuf = (char*)malloc((size_t)dwSize + 1);
    if (!pBuf) {
        CloseHandle(hFile);
        return FALSE;
    }

    if (dwSize > 0) {
        if (!ReadFile(hFile, pBuf, dwSize, &dwRead, NULL) || dwRead != dwSize) {
            free(pBuf);
            CloseHandle(hFile);
            return FALSE;
        }
    }

    CloseHandle(hFile);

    pBuf[dwSize] = 0;
    *ppBuf  = pBuf;
    *piSize = (int)dwSize;
    return TRUE;
}

/* Append only text buffer used while writing the file. */
typedef struct {
    char* pData;
    int   iUsed;
    int   iSize;
    BOOL  bFailed;
} CFG_WRITER;

static void CfgWriterInit(CFG_WRITER* pWriter) {
    pWriter->pData   = NULL;
    pWriter->iUsed   = 0;
    pWriter->iSize   = 0;
    pWriter->bFailed = FALSE;
}

static void CfgWriterFree(CFG_WRITER* pWriter) {
    if (pWriter->pData) {
        free(pWriter->pData);
        pWriter->pData = NULL;
    }
    pWriter->iUsed = 0;
    pWriter->iSize = 0;
}

/* Make room for iNeed more bytes, doubling the buffer when it has to grow. */
static BOOL CfgWriterGrow(CFG_WRITER* pWriter, int iNeed) {
    int   iWant;
    char* pNew;

    if (pWriter->iUsed + iNeed <= pWriter->iSize) {
        return TRUE;
    }

    iWant = pWriter->iSize > 0 ? pWriter->iSize : 512;
    while (iWant < pWriter->iUsed + iNeed) {
        iWant *= 2;
    }

    pNew = (char*)realloc(pWriter->pData, (size_t)iWant);
    if (!pNew) {
        pWriter->bFailed = TRUE;
        return FALSE;
    }

    pWriter->pData = pNew;
    pWriter->iSize = iWant;
    return TRUE;
}

static void CfgWrite(CFG_WRITER* pWriter, const char* pText) {
    const int iLen = (int)strlen(pText);

    if (pWriter->bFailed || !CfgWriterGrow(pWriter, iLen)) {
        return;
    }

    memcpy(pWriter->pData + pWriter->iUsed, pText, (size_t)iLen);
    pWriter->iUsed += iLen;
}

static void CfgWriteHex(CFG_WRITER* pWriter, int iValue) {
    static const char kHex[] = "0123456789ABCDEF";
    char aBuf[3];

    aBuf[0] = kHex[(iValue >> 4) & 0x0F];
    aBuf[1] = kHex[iValue & 0x0F];
    aBuf[2] = 0;
    CfgWrite(pWriter, aBuf);
}

static void CfgWriteDec(CFG_WRITER* pWriter, int iValue) {
    char aText[12];

    aText[0] = 0;
    CfgAppendDec(aText, (int)sizeof(aText), iValue);
    CfgWrite(pWriter, aText);
}

/* -------------------------------------------------------------------------
   Field level validation.
   ------------------------------------------------------------------------- */

/* Validate and store one theme line. */
static BOOL CfgTakeTheme(char* pLine, int iLineNo, RSHL_THEME* pTheme,
                         char* pError, int iErrorSize) {
    char* aFields[CFG_FIELD_MAX];
    int   iCount;
    int   i;

    iCount = CfgSplit(pLine, aFields, CFG_FIELD_MAX);
    if (iCount < 0) {
        CfgFail(pError, iErrorSize, iLineNo, "theme has too many fields");
        return FALSE;
    }
    if (iCount != 4) {
        CfgFail(pError, iErrorSize, iLineNo, "theme needs exactly 4 fields");
        return FALSE;
    }

    for (i = 0; i < 4; ++i) {
        char* pField = CfgTrim(aFields[i]);
        int   iValue = 0;

        if (!CfgParseHex(pField, &iValue)) {
            CfgFail(pError, iErrorSize, iLineNo,
                    "theme field is not a hexadecimal byte");
            return FALSE;
        }
        pTheme->bColors[i] = (BYTE)iValue;
    }

    return TRUE;
}

/* Validate and store one key line. */
static BOOL CfgTakeKey(char* pLine, int iLineNo, int* piKey,
                       char* pError, int iErrorSize) {
    char* aFields[CFG_FIELD_MAX];
    int   iCount;
    int   iValue = 0;

    iCount = CfgSplit(pLine, aFields, CFG_FIELD_MAX);
    if (iCount < 0) {
        CfgFail(pError, iErrorSize, iLineNo, "key line has too many fields");
        return FALSE;
    }
    if (iCount != 1) {
        CfgFail(pError, iErrorSize, iLineNo, "key line needs exactly 1 field");
        return FALSE;
    }

    if (!CfgParseDec(CfgTrim(aFields[0]), &iValue, 1, 255)) {
        CfgFail(pError, iErrorSize, iLineNo,
                "key code must be a decimal number between 1 and 255");
        return FALSE;
    }

    *piKey = iValue;
    return TRUE;
}

/* Validate and store one launcher slot line. */
static BOOL CfgTakeSlot(char* pLine, int iLineNo, RSHL_SLOT* pSlot,
                        char* pError, int iErrorSize) {
    char*       aFields[CFG_FIELD_MAX];
    int         iCount;
    int         iType = 0;
    const char* pName;
    const char* pPath;

    iCount = CfgSplit(pLine, aFields, CFG_FIELD_MAX);
    if (iCount < 0) {
        CfgFail(pError, iErrorSize, iLineNo, "slot has too many fields");
        return FALSE;
    }
    if (iCount != 3) {
        CfgFail(pError, iErrorSize, iLineNo,
                "slot needs exactly 3 fields: type|name|path");
        return FALSE;
    }

    if (!CfgParseDec(CfgTrim(aFields[0]), &iType, 0, RSHL_TYPE_UTILITIES)) {
        CfgFail(pError, iErrorSize, iLineNo,
                "slot type must be a number between 0 and 9");
        return FALSE;
    }

    pName = CfgTrim(aFields[1]);
    pPath = CfgTrim(aFields[2]);

    if (!*pName) {
        CfgFail(pError, iErrorSize, iLineNo, "slot name is empty");
        return FALSE;
    }
    if (!*pPath) {
        CfgFail(pError, iErrorSize, iLineNo, "slot path is empty");
        return FALSE;
    }
    if ((int)strlen(pName) >= RSHL_NAME_MAX) {
        CfgFail(pError, iErrorSize, iLineNo, "slot name is too long");
        return FALSE;
    }
    if ((int)strlen(pPath) >= RSHL_PATH_MAX) {
        CfgFail(pError, iErrorSize, iLineNo, "slot path is too long");
        return FALSE;
    }

    pSlot->iType = iType;
    memset(pSlot->szName, 0, sizeof(pSlot->szName));
    memset(pSlot->szPath, 0, sizeof(pSlot->szPath));
    memcpy(pSlot->szName, pName, strlen(pName));
    memcpy(pSlot->szPath, pPath, strlen(pPath));

    return TRUE;
}

/* -------------------------------------------------------------------------
   Public API.
   ------------------------------------------------------------------------- */

const char* ConfigSlotTypeName(int iType) {
    switch (iType) {
        case RSHL_TYPE_DEVELOPMENT: return "Development";
        case RSHL_TYPE_EDUCATION:   return "Education";
        case RSHL_TYPE_GAMES:       return "Games";
        case RSHL_TYPE_GRAPHICS:    return "Graphics";
        case RSHL_TYPE_INTERNET:    return "Internet";
        case RSHL_TYPE_MULTIMEDIA:  return "Multimedia";
        case RSHL_TYPE_OFFICE:      return "Office";
        case RSHL_TYPE_SCIENCE:     return "Science";
        case RSHL_TYPE_SYSTEM:      return "System";
        case RSHL_TYPE_UTILITIES:   return "Utilities";
        default:                    return NULL;
    }
}

void ConfigSetDefaults(RSHL_CONFIG* pConfig) {
    if (!pConfig) {
        return;
    }

    memset(pConfig, 0, sizeof(*pConfig));

    /* Theme 0 is the palette the shell currently ships with. */
    pConfig->aThemes[0].bColors[0] = 0x10;
    pConfig->aThemes[0].bColors[1] = 0xC0;
    pConfig->aThemes[0].bColors[2] = 0x02;
    pConfig->aThemes[0].bColors[3] = 0x30;

    pConfig->aThemes[1].bColors[0] = 0x10;
    pConfig->aThemes[1].bColors[1] = 0x14;
    pConfig->aThemes[1].bColors[2] = 0x1A;
    pConfig->aThemes[1].bColors[3] = 0x1F;

    pConfig->aThemes[2].bColors[0] = 0x21;
    pConfig->aThemes[2].bColors[1] = 0x22;
    pConfig->aThemes[2].bColors[2] = 0x23;
    pConfig->aThemes[2].bColors[3] = 0x24;

    pConfig->keys.iTabSwitch   = 37;    /* VK_LEFT   */
    pConfig->keys.iFlipScreen  = 39;    /* VK_RIGHT  */
    pConfig->keys.iActionPrev  = 38;    /* VK_UP     */
    pConfig->keys.iActionNext  = 40;    /* VK_DOWN   */
    pConfig->keys.iThemeSwitch = 32;    /* VK_SPACE  */
    pConfig->keys.iConfirm     = 13;    /* VK_RETURN */

    pConfig->iSlotCount   = 0;
    pConfig->iActiveTheme = 0;
    pConfig->bValid       = FALSE;
}

BOOL ConfigLoad(const char* pszPath, RSHL_CONFIG* pConfig,
                char* pError, int iErrorSize) {
    char* pFile = NULL;
    int   iSize = 0;
    BOOL  bMissing = FALSE;
    int   iLineNo = 0;
    int   iUsed = 0;
    char* pCursor;
    BOOL  bOk = TRUE;

    if (!pConfig) {
        return FALSE;
    }

    ConfigSetDefaults(pConfig);

    if (!pszPath) {
        CfgFail(pError, iErrorSize, 0, "no file name");
        return FALSE;
    }

    if (!CfgReadFile(pszPath, &pFile, &iSize, &bMissing)) {
        CfgFail(pError, iErrorSize, 0,
                bMissing ? "cannot open the file" : "cannot read the file");
        return FALSE;
    }

    if (iSize == 0) {
        free(pFile);
        CfgFail(pError, iErrorSize, 0, "the file is empty");
        return FALSE;
    }

    pCursor = pFile;

    while (*pCursor && bOk) {
        char* pLine;
        char* pHash;
        char* pEnd;

        /* Isolate one line, whatever the line ending is. */
        pLine = pCursor;
        pEnd = pCursor;
        while (*pEnd && *pEnd != '\n') {
            ++pEnd;
        }
        if (*pEnd == '\n') {
            *pEnd = 0;
            pCursor = pEnd + 1;
        }
        else {
            pCursor = pEnd;
        }
        ++iLineNo;

        /* Drop a trailing CR from CRLF files. */
        {
            const int iLen = (int)strlen(pLine);

            if (iLen > 0 && pLine[iLen - 1] == '\r') {
                pLine[iLen - 1] = 0;
            }
        }

        /* Everything from '#' on is a comment. */
        pHash = strchr(pLine, '#');
        if (pHash) {
            *pHash = 0;
        }

        pLine = CfgTrim(pLine);
        if (!*pLine) {
            continue;               /* blank line or comment only */
        }

        if (iUsed >= CFG_MAX_LINES) {
            CfgFail(pError, iErrorSize, iLineNo, "too many content lines");
            bOk = FALSE;
            break;
        }

        if (iUsed < RSHL_THEME_COUNT) {
            bOk = CfgTakeTheme(pLine, iLineNo,
                               &pConfig->aThemes[iUsed], pError, iErrorSize);
        }
        else if (iUsed < RSHL_THEME_COUNT + RSHL_KEY_COUNT) {
            int* aKeys[RSHL_KEY_COUNT];
            int  iKey = iUsed - RSHL_THEME_COUNT;

            aKeys[0] = &pConfig->keys.iTabSwitch;
            aKeys[1] = &pConfig->keys.iFlipScreen;
            aKeys[2] = &pConfig->keys.iActionPrev;
            aKeys[3] = &pConfig->keys.iActionNext;
            aKeys[4] = &pConfig->keys.iThemeSwitch;
            aKeys[5] = &pConfig->keys.iConfirm;

            bOk = CfgTakeKey(pLine, iLineNo, aKeys[iKey], pError, iErrorSize);
        }
        else {
            bOk = CfgTakeSlot(pLine, iLineNo,
                              &pConfig->aSlots[iUsed - CFG_MIN_LINES],
                              pError, iErrorSize);
            if (bOk) {
                ++pConfig->iSlotCount;
            }
        }

        ++iUsed;
    }

    if (bOk && iUsed < CFG_MIN_LINES) {
        CfgFail(pError, iErrorSize, 0,
                "the file ends before the key section is complete");
        bOk = FALSE;
    }

    free(pFile);

    pConfig->iActiveTheme = 0;
    pConfig->bValid = bOk;

    return bOk;
}

BOOL ConfigSave(const char* pszPath, const RSHL_CONFIG* pConfig,
                char* pError, int iErrorSize) {
    CFG_WRITER writer;
    HANDLE     hFile;
    DWORD      dwWritten = 0;
    int        i, j;
    BOOL       bOk;

    if (!pszPath || !pConfig) {
        CfgFail(pError, iErrorSize, 0, "nothing to save");
        return FALSE;
    }

    if (pConfig->iSlotCount < 0 || pConfig->iSlotCount > RSHL_SLOT_MAX) {
        CfgFail(pError, iErrorSize, 0, "slot count is out of range");
        return FALSE;
    }

    CfgWriterInit(&writer);

    CfgWrite(&writer, "# RSHL Config\r\n");

    CfgWrite(&writer, "# Theme\r\n");
    for (i = 0; i < RSHL_THEME_COUNT; ++i) {
        for (j = 0; j < 4; ++j) {
            if (j > 0) {
                CfgWrite(&writer, "|");
            }
            CfgWriteHex(&writer, pConfig->aThemes[i].bColors[j]);
        }
        CfgWrite(&writer, "\r\n");
    }

    CfgWrite(&writer, "# Key Mapping\r\n");
    CfgWriteDec(&writer, pConfig->keys.iTabSwitch);   CfgWrite(&writer, "   # Tab switch\r\n");
    CfgWriteDec(&writer, pConfig->keys.iFlipScreen);  CfgWrite(&writer, "   # Flip screen\r\n");
    CfgWriteDec(&writer, pConfig->keys.iActionPrev);  CfgWrite(&writer, "   # Action prev\r\n");
    CfgWriteDec(&writer, pConfig->keys.iActionNext);  CfgWrite(&writer, "   # Action next\r\n");
    CfgWriteDec(&writer, pConfig->keys.iThemeSwitch); CfgWrite(&writer, "   # Theme switch\r\n");
    CfgWriteDec(&writer, pConfig->keys.iConfirm);     CfgWrite(&writer, "   # Confirm\r\n");

    CfgWrite(&writer, "# Launcher Slots\r\n");
    for (i = 0; i < pConfig->iSlotCount; ++i) {
        CfgWriteDec(&writer, pConfig->aSlots[i].iType);
        CfgWrite(&writer, "|");
        CfgWrite(&writer, pConfig->aSlots[i].szName);
        CfgWrite(&writer, "|");
        CfgWrite(&writer, pConfig->aSlots[i].szPath);
        CfgWrite(&writer, "\r\n");
    }

    if (writer.bFailed) {
        CfgWriterFree(&writer);
        CfgFail(pError, iErrorSize, 0, "out of memory");
        return FALSE;
    }

    hFile = CreateFileA(pszPath, GENERIC_WRITE, 0, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        CfgWriterFree(&writer);
        CfgFail(pError, iErrorSize, 0, "cannot create the file");
        return FALSE;
    }

    bOk = WriteFile(hFile, writer.pData, (DWORD)writer.iUsed, &dwWritten, NULL)
          && dwWritten == (DWORD)writer.iUsed;

    CloseHandle(hFile);
    CfgWriterFree(&writer);

    if (!bOk) {
        CfgFail(pError, iErrorSize, 0, "write failed");
        return FALSE;
    }

    return TRUE;
}
