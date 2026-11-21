/*
   Where rshl.cfg lives, and how it is loaded.

   Only Win32 calls are used here: the Windows CE CRT is minimal and the whole
   shell avoids stdio, so the path work is done with plain loops.
*/

#include "settings.h"
#include "../config/config.h"

/* Name of the file, in both flavours. */
static const TCHAR  kSettingsFile[] = TEXT("rshl.cfg");

/* Buffer for a full path. MAX_PATH is not defined on every CE header, so the
   size comes from config.h, which both targets share. */
#define RS_PATH_BUFFER  CFG_PATH_TEXT

/* Narrow copy, since ConfigLoad speaks ANSI. The shell only ever converts
   ASCII paths, so a byte per character is enough. */
static void RsTCharToAnsi(const TCHAR* pSrc, char* pDst, int cbDst) {
    int i;

    if (!pSrc || !pDst || cbDst <= 0) {
        return;
    }

#ifdef UNICODE
    if (WideCharToMultiByte(CP_ACP, 0, pSrc, -1, pDst, cbDst, NULL, NULL) > 0) {
        return;
    }
#endif

    for (i = 0; pSrc[i] && i < cbDst - 1; ++i) {
        pDst[i] = (char)(unsigned char)pSrc[i];
    }
    pDst[i] = 0;
}

/* Append pszTail to pszPath in place, without overflowing cch buffers. */
static BOOL RsPathAppend(TCHAR* pszPath, int cchPath, const TCHAR* pszTail) {
    int iLen = 0;
    int i = 0;

    if (!pszPath || !pszTail) {
        return FALSE;
    }

    while (iLen < cchPath - 1 && pszPath[iLen]) {
        ++iLen;
    }

    while (pszTail[i] && iLen < cchPath - 1) {
        pszPath[iLen++] = pszTail[i++];
    }

    pszPath[iLen] = 0;

    return pszTail[i] == 0;
}

/* Turn a full executable path into the directory it sits in, then append the
   settings file name. Returns FALSE when there is no directory part, which is
   what happens if GetModuleFileName gave a bare name. */
static BOOL RsSettingsPathBesideExe(TCHAR* pszPath, int cchPath) {
    int i;
    int iLastSlash = -1;

    if (GetModuleFileName(NULL, pszPath, (DWORD)cchPath) == 0) {
        return FALSE;
    }

    /* GetModuleFileName may truncate; make sure the string is terminated. */
    pszPath[cchPath - 1] = 0;

    for (i = 0; pszPath[i]; ++i) {
        if (pszPath[i] == TEXT('\\') || pszPath[i] == TEXT('/')) {
            iLastSlash = i;
        }
    }

    if (iLastSlash < 0) {
        return FALSE;
    }

    pszPath[iLastSlash + 1] = 0;

    return RsPathAppend(pszPath, cchPath, kSettingsFile);
}

/* Path of the settings file at the root of the boot volume. */
static BOOL RsSettingsPathAtRoot(TCHAR* pszPath, int cchPath) {
    if (cchPath < 2) {
        return FALSE;
    }

#if (defined(VER_PLATFORM_WIN32_CE))
    pszPath[0] = TEXT('\\');
    pszPath[1] = 0;
#else
    pszPath[0] = TEXT('C');
    pszPath[1] = TEXT(':');
    pszPath[2] = TEXT('\\');
    pszPath[3] = 0;
#endif

    return RsPathAppend(pszPath, cchPath, kSettingsFile);
}

/* Try ConfigLoad on one candidate path. Errors are deliberately swallowed:
   a file that does not parse is the same to the user as no file at all. */
static BOOL RsTryLoad(const TCHAR* pszPath, RSHL_CONFIG* pConfig) {
    char aPath[RS_PATH_BUFFER];
    char aError[CFG_ERROR_TEXT];

    RsTCharToAnsi(pszPath, aPath, (int)sizeof(aPath));
    aError[0] = 0;

    return ConfigLoad(aPath, pConfig, aError, (int)sizeof(aError));
}

BOOL RsSettingsLoad(RSHL_CONFIG* pConfig) {
    TCHAR aPath[RS_PATH_BUFFER];

    if (!pConfig) {
        return FALSE;
    }

    /* The defaults are the contract: the caller always gets something it can
       draw, whether or not a file was found. */
    ConfigSetDefaults(pConfig);

    aPath[0] = 0;
    if (RsSettingsPathBesideExe(aPath, RS_PATH_BUFFER) && RsTryLoad(aPath, pConfig)) {
        return TRUE;
    }

    aPath[0] = 0;
    if (RsSettingsPathAtRoot(aPath, RS_PATH_BUFFER) && RsTryLoad(aPath, pConfig)) {
        return TRUE;
    }

    /* Nothing usable, so put the defaults back in case a failed parse wrote
       half a table before ConfigLoad rejected it. */
    ConfigSetDefaults(pConfig);

    return FALSE;
}
