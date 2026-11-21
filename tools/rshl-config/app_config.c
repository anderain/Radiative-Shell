#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <tchar.h>
#include <string.h>
#include "resource.h"
#include "../../config/config.h"
#include "../../frontend/vga_palette.h"

HINSTANCE        	hInst;
HWND                hwndCB;

/* Configuration currently in the editor and the file it came from. */
static RSHL_CONFIG  g_cfg;
static char         g_szPath[CFG_PATH_TEXT];
static HFONT        g_hFont;
static HFONT        g_hFontBold;
static int          g_iCbHeight = 0; /* Commandbar height, only for CE */

ATOM                MyRegisterClass	(HINSTANCE hInstance, LPTSTR szWindowClass);
BOOL                InitInstance	(HINSTANCE, int);
LRESULT CALLBACK    WndProc			(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK    About			(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK    ThemeDlgProc	(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK    KeyDlgProc		(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK    KeyCaptureDlgProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK    SlotDlgProc		(HWND, UINT, WPARAM, LPARAM);

/* -------------------------------------------------------------------------
   Text helpers. config.c speaks ANSI, the window speaks TCHAR, so the two
   conversions live here, together with the small append helpers the status
   text is built from.

   Nothing here calls an -A entry point: Windows CE only exports the Unicode
   ones, and the narrow builds get a plain loop instead.
   ------------------------------------------------------------------------- */

static void AnsiToTChar(const char* pSrc, TCHAR* pDst, int cchDst) {
    int i;

    if (!pSrc || !pDst || cchDst <= 0) {
        return;
    }

#ifdef UNICODE
    if (MultiByteToWideChar(CP_ACP, 0, pSrc, -1, pDst, cchDst) > 0) {
        return;
    }
#endif

    for (i = 0; pSrc[i] && i < cchDst - 1; ++i) {
        pDst[i] = (TCHAR)(unsigned char)pSrc[i];
    }
    pDst[i] = 0;
}

static void TCharToAnsi(const TCHAR* pSrc, char* pDst, int cbDst) {
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

/* Bounded ANSI copy, the job the -A string functions used to do. */
static void CopyAnsi(char* pDst, int cbDst, const char* pSrc) {
    int i;

    if (!pDst || cbDst <= 0) {
        return;
    }

    for (i = 0; pSrc && pSrc[i] && i < cbDst - 1; ++i) {
        pDst[i] = pSrc[i];
    }
    pDst[i] = 0;
}

/* Message box with an ANSI body, so callers can pass config.c text straight
   through without an -A entry point. */
static void ShowAnsiMessage(HWND hWnd, const char* pText, const TCHAR* pCaption,
                            UINT uType) {
    TCHAR szText[CFG_ERROR_TEXT];

    AnsiToTChar(pText, szText, CFG_ERROR_TEXT);
    MessageBox(hWnd, szText, pCaption, uType);
}

/* -------------------------------------------------------------------------
   Small text helpers used by the paint code.
   ------------------------------------------------------------------------- */

/* Bounded TCHAR copy and append, so the labels need no printf family. */
static void CopyText(TCHAR* pDst, int cchDst, const TCHAR* pSrc) {
    int i;

    if (!pDst || cchDst <= 0) {
        return;
    }

    for (i = 0; pSrc && pSrc[i] && i < cchDst - 1; ++i) {
        pDst[i] = pSrc[i];
    }
    pDst[i] = 0;
}

static void AppendText(TCHAR* pDst, int cchDst, const TCHAR* pSrc) {
    int iLen = 0;
    int i = 0;

    while (iLen < cchDst - 1 && pDst[iLen]) {
        ++iLen;
    }

    while (pSrc && pSrc[i] && iLen < cchDst - 1) {
        pDst[iLen++] = pSrc[i++];
    }

    pDst[iLen] = 0;
}

static void AppendDec(TCHAR* pDst, int cchDst, int iValue) {
    TCHAR aText[12];
    TCHAR aOne[2];
    int   iCount = 0;

    if (iValue < 0) {
        AppendText(pDst, cchDst, TEXT("-"));
        iValue = -iValue;
    }

    do {
        aText[iCount++] = (TCHAR)(TEXT('0') + (iValue % 10));
        iValue /= 10;
    } while (iValue > 0 && iCount < (int)(sizeof(aText) / sizeof(aText[0])));

    aOne[1] = 0;
    while (iCount > 0) {
        aOne[0] = aText[--iCount];
        AppendText(pDst, cchDst, aOne);
    }
}

/* Two upper case hexadecimal digits, the shape the cfg file uses. */
static void FormatHexByte(TCHAR* pDst, int cchDst, BYTE bValue) {
    static const TCHAR kHex[] = TEXT("0123456789ABCDEF");
    TCHAR aText[3];

    if (!pDst || cchDst < 3) {
        return;
    }

    aText[0] = kHex[(bValue >> 4) & 0x0F];
    aText[1] = kHex[bValue & 0x0F];
    aText[2] = 0;

    CopyText(pDst, cchDst, aText);
}

/* One or two hexadecimal digits, with or without a 0x prefix. Rejects
   anything else, including a value that does not fit in a byte. */
static BOOL ParseHexByte(const TCHAR* pText, BYTE* pbValue) {
    int iValue = 0;
    int iDigits = 0;

    if (!pText || !pbValue) {
        return FALSE;
    }

    while (*pText == TEXT(' ') || *pText == TEXT('\t')) {
        ++pText;
    }

    if (pText[0] == TEXT('0') && (pText[1] == TEXT('x') || pText[1] == TEXT('X'))) {
        pText += 2;
    }

    while (*pText) {
        const TCHAR c = *pText;
        int iDigit;

        if (c >= TEXT('0') && c <= TEXT('9')) {
            iDigit = (int)(c - TEXT('0'));
        }
        else if (c >= TEXT('a') && c <= TEXT('f')) {
            iDigit = (int)(c - TEXT('a')) + 10;
        }
        else if (c >= TEXT('A') && c <= TEXT('F')) {
            iDigit = (int)(c - TEXT('A')) + 10;
        }
        else if (c == TEXT(' ') || c == TEXT('\t')) {
            ++pText;
            continue;
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

    *pbValue = (BYTE)iValue;
    return TRUE;
}

/* Decimal virtual key code, 1..255. Leading and trailing blanks are allowed,
   anything else is rejected. */
static BOOL ParseKeyCode(const TCHAR* pText, int* piKey) {
    int iValue = 0;
    int iDigits = 0;

    if (!pText || !piKey) {
        return FALSE;
    }

    while (*pText == TEXT(' ') || *pText == TEXT('\t')) {
        ++pText;
    }

    while (*pText) {
        const TCHAR c = *pText;

        if (c < TEXT('0') || c > TEXT('9')) {
            return FALSE;
        }

        iValue = iValue * 10 + (int)(c - TEXT('0'));
        if (iValue > 255) {
            return FALSE;
        }

        ++iDigits;
        ++pText;
    }

    if (iDigits == 0 || iValue < 1) {
        return FALSE;
    }

    *piKey = iValue;
    return TRUE;
}

/* Center a dialog over its owner. The same calls the About box has always
   used, kept deliberately: GetDesktopWindow and friends are not declared by
   the Windows CE headers this tool also targets. */
static void CenterDialog(HWND hDlg) {
    RECT rt, rt1;
    int DlgWidth, DlgHeight;
    int NewPosX, NewPosY;

    if (GetWindowRect(hDlg, &rt1)) {
        GetClientRect(GetParent(hDlg), &rt);
        DlgWidth    = rt1.right - rt1.left;
        DlgHeight   = rt1.bottom - rt1.top;
        NewPosX     = (rt.right - rt.left - DlgWidth) / 2;
        NewPosY     = (rt.bottom - rt.top - DlgHeight) / 2;

        if (NewPosX < 0) NewPosX = 0;
        if (NewPosY < 0) NewPosY = 0;
        SetWindowPos(hDlg, 0, NewPosX, NewPosY,
                     0, 0, SWP_NOZORDER | SWP_NOSIZE);
    }
}

/* -------------------------------------------------------------------------
   Client area layout. The target is a 320x240 landscape screen, so every row
   is placed from constants and the whole picture has to fit in 240 pixels of
   height. The desktop window asks for the same client size, which makes the
   local build look like the device.
   ------------------------------------------------------------------------- */

#define UI_MARGIN       4
#define UI_TITLE_H      18
#define UI_THEME_ROW_H  20
#define UI_KEY_ROW_H    16
#define UI_SECTION_GAP  6
#define UI_SWATCH_W     30
#define UI_SWATCH_H     16
#define UI_SWATCH_GAP   6
#define UI_SWATCH_X     32
#define UI_KEY_VALUE_DX 52
#define UI_COL2_X       124

/* DrawText is used instead of TextOut because the Windows CE build this tool
   also targets does not export TextOut. */
static void DrawLine(HDC hdc, int iX, int iY, const TCHAR* pText, HFONT hFont) {
    RECT  rt;
    HFONT hOld;

    if (!pText || !*pText) {
        return;
    }

    rt.left   = iX;
    rt.top    = iY;
    rt.right  = iX + 1024;
    rt.bottom = iY + UI_TITLE_H;

    hOld = (HFONT)SelectObject(hdc, hFont);
    DrawText(hdc, pText, -1, &rt, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_SINGLELINE);
    SelectObject(hdc, hOld);
}

/* Fetch the VGA colour for a palette index as a COLORREF. */
static COLORREF SwatchColor(BYTE bIndex) {
    const DWORD color = VGA_MODE13H_COLORPALETTE[bIndex];

    return RGB((BYTE)((color >> 16) & 0xFF),
               (BYTE)((color >> 8) & 0xFF),
               (BYTE)(color & 0xFF));
}

/* One theme row: the index and the four swatches for that theme. */
static void DrawThemeRow(HDC hdc, int iY, int iIndex) {
    TCHAR    aLabel[24];
    HPEN     hPen;
    HPEN     hOldPen;
    int      j;
    const COLORREF kFrame = RGB(0x80, 0x80, 0x80);

    aLabel[0] = 0;
    AppendText(aLabel, (int)(sizeof(aLabel) / sizeof(aLabel[0])), TEXT("#"));
    AppendDec(aLabel, (int)(sizeof(aLabel) / sizeof(aLabel[0])), iIndex);
    DrawLine(hdc, UI_MARGIN, iY + 4, aLabel, g_hFont);

    /* A neutral outline keeps the near black swatches visible. */
    hPen    = CreatePen(PS_SOLID, 1, kFrame);
    hOldPen = (HPEN)SelectObject(hdc, hPen ? hPen : GetStockObject(BLACK_PEN));

    for (j = 0; j < 4; ++j) {
        const int iX = UI_SWATCH_X + j * (UI_SWATCH_W + UI_SWATCH_GAP);
        const int iTop = iY + (UI_THEME_ROW_H - UI_SWATCH_H) / 2;
        HBRUSH    hBrush = CreateSolidBrush(SwatchColor(g_cfg.aThemes[iIndex].bColors[j]));

        if (hBrush) {
            HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);

            Rectangle(hdc, iX, iTop, iX + UI_SWATCH_W, iTop + UI_SWATCH_H);

            SelectObject(hdc, hOldBrush);
            DeleteObject(hBrush);
        }
    }

    SelectObject(hdc, hOldPen);
    if (hPen) {
        DeleteObject(hPen);
    }
}

/* One key mapping cell: a label and its virtual key code, side by side. */
static void DrawKeyCell(HDC hdc, int iX, int iY, const TCHAR* pLabel, int iKey) {
    TCHAR aValue[16];

    DrawLine(hdc, iX, iY, pLabel, g_hFont);

    aValue[0] = 0;
    AppendDec(aValue, (int)(sizeof(aValue) / sizeof(aValue[0])), iKey);
    DrawLine(hdc, iX + UI_KEY_VALUE_DX, iY, aValue, g_hFont);
}

/* Width of pText in the UI font, measured with the same call that draws it. */
static int TextWidth(HDC hdc, const TCHAR* pText) {
    RECT  rt;
    HFONT hOld;

    rt.left   = 0;
    rt.top    = 0;
    rt.right  = 0;
    rt.bottom = 0;

    hOld = (HFONT)SelectObject(hdc, g_hFont);
    DrawText(hdc, pText, -1, &rt, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, hOld);

    return rt.right - rt.left;
}

/* The current file name along the bottom edge. A path that is too wide is
   trimmed from the front, because the file name is the useful part. */
static void DrawPathLine(HDC hdc, int iX, int iY, int iWidth) {
    TCHAR aPath[CFG_PATH_TEXT];
    TCHAR aText[CFG_PATH_TEXT + 8];
    int   iStart = 0;
    int   iLen;
    int   iRoom;

    AnsiToTChar(g_szPath, aPath, CFG_PATH_TEXT);
    if (!aPath[0]) {
        CopyText(aPath, CFG_PATH_TEXT, TEXT("(no file)"));
    }

    CopyText(aText, (int)(sizeof(aText) / sizeof(aText[0])), aPath);
    iLen = (int)_tcslen(aPath);

    /* The leading dots take room of their own, so the tail is measured
       against what is left once they are accounted for. */
    iRoom = iWidth - TextWidth(hdc, TEXT("..."));

    while (iStart < iLen && TextWidth(hdc, aPath + iStart) > iRoom) {
        ++iStart;
    }

    if (iStart > 0) {
        aText[0] = TEXT('.');
        aText[1] = TEXT('.');
        aText[2] = TEXT('.');
        CopyText(aText + 3, (int)(sizeof(aText) / sizeof(aText[0])) - 3,
                 aPath + iStart);
    }

    DrawLine(hdc, iX, iY, aText, g_hFont);
}

/* -------------------------------------------------------------------------
   Loading and saving.
   ------------------------------------------------------------------------- */

/* Load pszPath into g_cfg. A file that does not parse leaves the current
   configuration alone and reports the reason in a message box: the client
   area has no room for an error block, and the fixed layout is meant to show
   a valid configuration only. */
static BOOL LoadConfigFile(HWND hWnd, const char* pszPath) {
    char        szError[CFG_ERROR_TEXT];
    RSHL_CONFIG loaded;

    szError[0] = 0;

    if (!ConfigLoad(pszPath, &loaded, szError, (int)sizeof(szError))) {
        if (hWnd) {
            ShowAnsiMessage(hWnd, szError, TEXT("Load failed"),
                            MB_OK | MB_ICONERROR);
        }
        return FALSE;
    }

    g_cfg = loaded;
    CopyAnsi(g_szPath, (int)sizeof(g_szPath), pszPath);

    if (hWnd) {
        InvalidateRect(hWnd, NULL, TRUE);
    }
    return TRUE;
}

/* Shared body of Open and Save As. bSave picks the dialog. */
static void DoFileDialog(HWND hWnd, BOOL bSave) {
    OPENFILENAME ofn;
    TCHAR        szFile[CFG_PATH_TEXT] = TEXT("");
    char         szPath[CFG_PATH_TEXT];
    char         szError[CFG_ERROR_TEXT];

    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hWnd;
    ofn.lpstrFilter = TEXT("Config files (*.cfg)\0*.cfg\0All files (*.*)\0*.*\0\0");
    ofn.lpstrFile   = szFile;
    ofn.nMaxFile    = CFG_PATH_TEXT;
    ofn.lpstrDefExt = TEXT("cfg");
    ofn.Flags       = OFN_EXPLORER | OFN_PATHMUSTEXIST;

    if (bSave) {
        ofn.Flags |= OFN_OVERWRITEPROMPT;
    }
    else {
        ofn.Flags |= OFN_FILEMUSTEXIST;
    }

    if (bSave) {
        if (!GetSaveFileName(&ofn)) {
            return;
        }
    }
    else {
        if (!GetOpenFileName(&ofn)) {
            return;
        }
    }

    TCharToAnsi(szFile, szPath, (int)sizeof(szPath));

    if (bSave) {
        szError[0] = 0;

        if (!ConfigSave(szPath, &g_cfg, szError, (int)sizeof(szError))) {
            ShowAnsiMessage(hWnd, szError, TEXT("Save failed"),
                            MB_OK | MB_ICONERROR);
            return;
        }

        CopyAnsi(g_szPath, (int)sizeof(g_szPath), szPath);
        InvalidateRect(hWnd, NULL, TRUE);
    }
    else {
        LoadConfigFile(hWnd, szPath);
    }
}

/* -------------------------------------------------------------------------
   Window plumbing.
   ------------------------------------------------------------------------- */

int WINAPI WinMain (HINSTANCE hInstance,
                    HINSTANCE hPrevInstance,
                    LPTSTR    lpCmdLine,
                    int       nCmdShow) {
    MSG msg;
    HACCEL hAccelTable;

    (void)hPrevInstance;

    if (!InitInstance (hInstance, nCmdShow))  {
        return FALSE;
    }

    /* A file name on the command line is loaded straight away, which also
       makes the tool scriptable. */
    if (lpCmdLine && *lpCmdLine) {
        char szPath[CFG_PATH_TEXT];

        TCharToAnsi(lpCmdLine, szPath, (int)sizeof(szPath));

        /* Strip the quotes the shell may have wrapped around the name. */
        {
            const int iLen = (int)strlen(szPath);

            if (iLen >= 2 && szPath[0] == '"' && szPath[iLen - 1] == '"') {
                szPath[iLen - 1] = 0;
                memmove(szPath, szPath + 1, strlen(szPath));
            }
        }

        LoadConfigFile(FindWindow(NULL, TEXT("RSHL Config")), szPath);
    }

    hAccelTable = LoadAccelerators(hInstance, (LPCTSTR)IDC_RSHLCONFIG);

    while (GetMessage(&msg, NULL, 0, 0))  {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))  {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance, LPTSTR szWindowClass) {
    WNDCLASS    wc;

    wc.style          	= CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc     	= (WNDPROC) WndProc;
    wc.cbClsExtra     	= 0;
    wc.cbWndExtra     	= 0;
    wc.hInstance        = hInstance;
    wc.hIcon            = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_RSHLCONFIG));
    wc.hCursor        	= LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground    = (HBRUSH) GetStockObject(WHITE_BRUSH);
#ifdef VER_PLATFORM_WIN32_CE
    /* Windows CE puts the menu in a CommandBar, created in WM_CREATE. */
    wc.lpszMenuName    	= 0;
#else
    /* The desktop has no CommandBar API, so the menu is a normal window menu. */
    wc.lpszMenuName    	= (LPCTSTR)IDM_MENU;
#endif
    wc.lpszClassName    = szWindowClass;

    return RegisterClass(&wc);
}


BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    HWND    hWnd;
    TCHAR   szTitle[] = TEXT("RSHL Config");
    TCHAR   szWindowClass[] = TEXT("RSHL_MAIN_WIN");
    int     iWinWidth, iWinHeight;
    DWORD   dwStyle;

    hInst = hInstance;
    MyRegisterClass(hInstance, szWindowClass);

    ConfigSetDefaults(&g_cfg);
    g_szPath[0] = 0;

#ifdef VER_PLATFORM_WIN32_CE
    {
        dwStyle     = WS_VISIBLE;
        iWinWidth   = GetSystemMetrics(SM_CXSCREEN);
        iWinHeight  = GetSystemMetrics(SM_CYSCREEN);
    }
#else
    {
        RECT rectWindow;

        dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

        /* Ask for the target screen's client area, 320x240 landscape;
           AdjustWindowRect adds the frame. bMenu is TRUE because the class
           attaches IDM_MENU and the menu bar sits in the non client area, so
           it has to be reserved or it silently takes its height out of the
           client. */
        rectWindow.left   = 0;
        rectWindow.top    = 0;
        rectWindow.right  = 320;
        rectWindow.bottom = 240;
        AdjustWindowRect(&rectWindow, dwStyle, TRUE);

        iWinWidth  = rectWindow.right - rectWindow.left;
        iWinHeight = rectWindow.bottom - rectWindow.top;
    }
#endif

    hWnd = CreateWindow(szWindowClass, szTitle, dwStyle, 0, 0, iWinWidth, iWinHeight, NULL, NULL, hInstance, NULL);

    if (!hWnd) {    
        return FALSE;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
#ifdef VER_PLATFORM_WIN32_CE
    if (hwndCB) {
        CommandBar_Show(hwndCB, TRUE);
    }
#endif

    return TRUE;
}

/* -------------------------------------------------------------------------
   Font. Tahoma at 8 points, the same face the About dialog asks the loader
   for, so the client area and the dialog match.

   CreateFontIndirect is used rather than CreateFont because the Windows CE
   build this tool also targets does not export CreateFont at all.
   ------------------------------------------------------------------------- */

static HFONT CreateUiFont(int iWeight) {
    HFONT   hFont;
    LOGFONT logfont;

    memset(&logfont, 0, sizeof(logfont));

    logfont.lfHeight         = 16;
    logfont.lfWidth          = 0;
    logfont.lfEscapement     = 0;
    logfont.lfOrientation    = 0;
    logfont.lfWeight         = iWeight;
    logfont.lfItalic         = FALSE;
    logfont.lfUnderline      = FALSE;
    logfont.lfStrikeOut      = FALSE;
    logfont.lfCharSet        = DEFAULT_CHARSET;
    logfont.lfOutPrecision   = OUT_DEFAULT_PRECIS;
    logfont.lfClipPrecision  = CLIP_DEFAULT_PRECIS;
    logfont.lfQuality        = DEFAULT_QUALITY;
    logfont.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    _tcscpy(logfont.lfFaceName, TEXT("Tahoma"));

    hFont = CreateFontIndirect(&logfont);

    return hFont;
}

/* Repaint the whole client area from g_cfg. Rows are positioned from the
   constants above, so the picture does not depend on measuring text. */
static void PaintClient(HDC hdc, const RECT* pRect) {
    TCHAR aText[64];
    int   iY;
    int   i;

    FillRect(hdc, pRect, (HBRUSH)GetStockObject(WHITE_BRUSH));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 0, 0));

    iY = UI_MARGIN + g_iCbHeight;

    /* Theme section. */
    DrawLine(hdc, UI_MARGIN, iY, TEXT("Theme"), g_hFontBold);
    iY += UI_TITLE_H;

    for (i = 0; i < RSHL_THEME_COUNT; ++i) {
        DrawThemeRow(hdc, iY, i);
        iY += UI_THEME_ROW_H;
    }

    /* Keys section, two columns of three. */
    iY += UI_SECTION_GAP;
    DrawLine(hdc, UI_MARGIN, iY, TEXT("Keys"), g_hFontBold);
    iY += UI_TITLE_H;

    DrawKeyCell(hdc, UI_MARGIN, iY, TEXT("Tab"),    g_cfg.keys.iTabSwitch);
    DrawKeyCell(hdc, UI_COL2_X, iY, TEXT("Next"),   g_cfg.keys.iActionNext);
    iY += UI_KEY_ROW_H;

    DrawKeyCell(hdc, UI_MARGIN, iY, TEXT("Flip"),   g_cfg.keys.iFlipScreen);
    DrawKeyCell(hdc, UI_COL2_X, iY, TEXT("Theme"),  g_cfg.keys.iThemeSwitch);
    iY += UI_KEY_ROW_H;

    DrawKeyCell(hdc, UI_MARGIN, iY, TEXT("Prev"),   g_cfg.keys.iActionPrev);
    DrawKeyCell(hdc, UI_COL2_X, iY, TEXT("Action"), g_cfg.keys.iAction);
    iY += UI_KEY_ROW_H;

    /* Slot count. */
    iY += UI_SECTION_GAP;
    aText[0] = 0;
    AppendText(aText, (int)(sizeof(aText) / sizeof(aText[0])), TEXT("Slots: "));
    AppendDec(aText, (int)(sizeof(aText) / sizeof(aText[0])), g_cfg.iSlotCount);
    AppendText(aText, (int)(sizeof(aText) / sizeof(aText[0])), TEXT(" of "));
    AppendDec(aText, (int)(sizeof(aText) / sizeof(aText[0])), RSHL_SLOT_MAX);
    DrawLine(hdc, UI_MARGIN, iY, aText, g_hFont);
}

/* -------------------------------------------------------------------------
   Theme editor. The dialog works on a copy of the theme table, so Cancel
   leaves the live configuration untouched; initialise costs nothing and the
   caller only copies the table back when the dialog returns TRUE.
   ------------------------------------------------------------------------- */

/* Control ID of the n-th theme byte. The resource file lays the boxes out in
   reading order, which this mirrors. */
static int ThemeEditId(int iTheme, int iByte) {
    return IDC_THEME_0_0 + iTheme * 4 + iByte;
}

/* Dialog state, needed because the controls are addressed by ID only. */
static RSHL_CONFIG* g_pThemeEdit;

/* Read and validate all twelve boxes. Returns the first failing control, or
   NULL when every box holds a valid byte. */
static HWND ThemeReadControls(HWND hDlg) {
    int iTheme;

    for (iTheme = 0; iTheme < RSHL_THEME_COUNT; ++iTheme) {
        int iByte;

        for (iByte = 0; iByte < 4; ++iByte) {
            const HWND  hEdit = GetDlgItem(hDlg, ThemeEditId(iTheme, iByte));
            TCHAR       aText[16];
            BYTE        bValue = 0;

            if (!hEdit) {
                continue;
            }

            aText[0] = 0;
            GetWindowText(hEdit, aText, (int)(sizeof(aText) / sizeof(aText[0])));

            if (!ParseHexByte(aText, &bValue)) {
                return hEdit;
            }

            g_pThemeEdit->aThemes[iTheme].bColors[iByte] = bValue;
        }
    }

    return NULL;
}

LRESULT CALLBACK ThemeDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_INITDIALOG: {
            int iTheme;

            g_pThemeEdit = (RSHL_CONFIG*)lParam;

            /* Never NULL: the caller always passes its scratch copy. */
            if (!g_pThemeEdit) {
                EndDialog(hDlg, FALSE);
                return TRUE;
            }

            for (iTheme = 0; iTheme < RSHL_THEME_COUNT; ++iTheme) {
                int iByte;

                for (iByte = 0; iByte < 4; ++iByte) {
                    const HWND hEdit = GetDlgItem(hDlg, ThemeEditId(iTheme, iByte));
                    TCHAR      aText[8];

                    if (!hEdit) {
                        continue;
                    }

                    FormatHexByte(aText, (int)(sizeof(aText) / sizeof(aText[0])),
                                  g_pThemeEdit->aThemes[iTheme].bColors[iByte]);

                    SetWindowText(hEdit, aText);
                    /* Two characters: one byte in hexadecimal, nothing else
                       fits the field. */
                    SendMessage(hEdit, EM_LIMITTEXT, 2, 0);
                    SendMessage(hEdit, EM_SETSEL, 0, -1);
                }
            }

            /* Focus the first box so typing starts where the eye does. */
            SetFocus(GetDlgItem(hDlg, ThemeEditId(0, 0)));

            CenterDialog(hDlg);
            return FALSE;               /* focus already set explicitly */
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK: {
                    HWND hBad = ThemeReadControls(hDlg);

                    if (hBad) {
                        TCHAR aMessage[128];

                        aMessage[0] = 0;
                        AppendText(aMessage,
                                   (int)(sizeof(aMessage) / sizeof(aMessage[0])),
                                   TEXT("Enter a hexadecimal byte (00 to FF)."));
                        MessageBox(hDlg, aMessage, TEXT("Invalid Value"),
                                   MB_OK | MB_ICONWARNING);
                        SetFocus(hBad);
                        SendMessage(hBad, EM_SETSEL, 0, -1);
                        return TRUE;    /* keep the dialog open */
                    }

                    EndDialog(hDlg, TRUE);
                    return TRUE;
                }

                case IDCANCEL:
                    EndDialog(hDlg, FALSE);
                    return TRUE;

                default:
                    break;
            }
            break;

        default:
            break;
    }

    return FALSE;
}

/* Open the theme editor and copy the result back into g_cfg. */
static void ShowThemeEditor(HWND hWnd) {
    RSHL_CONFIG scratch = g_cfg;

    if (DialogBoxParam(hInst, (LPCTSTR)IDD_THEMEEDIT, hWnd,
                       (DLGPROC)ThemeDlgProc, (LPARAM)&scratch)) {
        int i;

        for (i = 0; i < RSHL_THEME_COUNT; ++i) {
            g_cfg.aThemes[i] = scratch.aThemes[i];
        }

        InvalidateRect(hWnd, NULL, TRUE);
    }
}

/* -------------------------------------------------------------------------
   Key editor. Same copy-then-commit approach as the theme editor: the dialog
   edits a scratch copy of the key table and the result only reaches g_cfg when
   it returns TRUE.
   ------------------------------------------------------------------------- */

/* The six keys in dialog order, with the label each row shows. */
typedef struct {
    const TCHAR* pLabel;
    int          iValueId;
    int          iSetId;
    int          iIndex;        /* which RSHL_KEYS field this row edits */
} RSHL_KEYROW;

static RSHL_KEYROW g_aKeyRows[RSHL_KEY_COUNT];

static void InitKeyRows(void) {
    g_aKeyRows[0].pLabel   = TEXT("Tab");
    g_aKeyRows[0].iValueId = IDC_KEY_VAL_TAB;
    g_aKeyRows[0].iSetId   = IDC_KEY_SET_TAB;
    g_aKeyRows[0].iIndex   = 0;

    g_aKeyRows[1].pLabel   = TEXT("Flip");
    g_aKeyRows[1].iValueId = IDC_KEY_VAL_FLIP;
    g_aKeyRows[1].iSetId   = IDC_KEY_SET_FLIP;
    g_aKeyRows[1].iIndex   = 1;

    g_aKeyRows[2].pLabel   = TEXT("Prev");
    g_aKeyRows[2].iValueId = IDC_KEY_VAL_PREV;
    g_aKeyRows[2].iSetId   = IDC_KEY_SET_PREV;
    g_aKeyRows[2].iIndex   = 2;

    g_aKeyRows[3].pLabel   = TEXT("Next");
    g_aKeyRows[3].iValueId = IDC_KEY_VAL_NEXT;
    g_aKeyRows[3].iSetId   = IDC_KEY_SET_NEXT;
    g_aKeyRows[3].iIndex   = 3;

    g_aKeyRows[4].pLabel   = TEXT("Theme");
    g_aKeyRows[4].iValueId = IDC_KEY_VAL_THEME;
    g_aKeyRows[4].iSetId   = IDC_KEY_SET_THEME;
    g_aKeyRows[4].iIndex   = 4;

    g_aKeyRows[5].pLabel   = TEXT("Action");
    g_aKeyRows[5].iValueId = IDC_KEY_VAL_ACTION;
    g_aKeyRows[5].iSetId   = IDC_KEY_SET_ACTION;
    g_aKeyRows[5].iIndex   = 5;
}

/* Point pKeys at the six fields in the order the rows expect. */
static int* KeyField(RSHL_KEYS* pKeys, int iIndex) {
    switch (iIndex) {
        case 0:  return &pKeys->iTabSwitch;
        case 1:  return &pKeys->iFlipScreen;
        case 2:  return &pKeys->iActionPrev;
        case 3:  return &pKeys->iActionNext;
        case 4:  return &pKeys->iThemeSwitch;
        default: return &pKeys->iAction;
    }
}

/* Scratch copy the key dialogs work on. The dialog edits this through a
   pointer, so the caller can copy it back when the dialog returns TRUE. */
static RSHL_KEYS* g_pKeyEdit;

/* Put *g_pKeyEdit back into the six boxes. */
static void KeyRefreshBoxes(HWND hDlg) {
    int i;

    if (!g_pKeyEdit) {
        return;
    }

    for (i = 0; i < RSHL_KEY_COUNT; ++i) {
        TCHAR aText[16];

        aText[0] = 0;
        AppendDec(aText, (int)(sizeof(aText) / sizeof(aText[0])),
                  *KeyField(g_pKeyEdit, g_aKeyRows[i].iIndex));
        SetDlgItemText(hDlg, g_aKeyRows[i].iValueId, aText);
    }
}

/* Read the six boxes into *g_pKeyEdit. Returns the first box that does not
   hold a valid key code, or NULL when all six are fine. Nothing is written
   back until every box has parsed, so a half typed value cannot leave the
   table in a state that was never shown. */
static HWND KeyReadBoxes(HWND hDlg) {
    int i;

    if (!g_pKeyEdit) {
        return NULL;
    }

    for (i = 0; i < RSHL_KEY_COUNT; ++i) {
        const HWND hEdit = GetDlgItem(hDlg, g_aKeyRows[i].iValueId);
        TCHAR      aText[16];
        int        iKey = 0;

        if (!hEdit) {
            continue;
        }

        aText[0] = 0;
        GetWindowText(hEdit, aText, (int)(sizeof(aText) / sizeof(aText[0])));

        if (!ParseKeyCode(aText, &iKey)) {
            return hEdit;
        }

        *KeyField(g_pKeyEdit, g_aKeyRows[i].iIndex) = iKey;
    }

    return NULL;
}

/* ------------------------------------------------------------------ capture */

/* Set while the capture dialog is up: the key the user pressed, or 0. */
static int    g_iCaptureKey;
static WNDPROC g_pfnCaptureOld;

/* Draw "Key 37" or "Press any key to assign it." in the capture dialog. */
static void KeyCaptureShow(HWND hDlg) {
    TCHAR aText[64];

    aText[0] = 0;

    if (g_iCaptureKey > 0) {
        AppendText(aText, (int)(sizeof(aText) / sizeof(aText[0])),
                   TEXT("Key "));
        AppendDec(aText, (int)(sizeof(aText) / sizeof(aText[0])), g_iCaptureKey);
    }
    else {
        AppendText(aText, (int)(sizeof(aText) / sizeof(aText[0])),
                   TEXT("Press any key to assign it."));
    }

    SetDlgItemText(hDlg, IDC_KEYCAP_HINT, aText);
}

/* Every key press is wanted, including Tab, Enter and Escape, otherwise the
   dialog manager would use them for navigation and Action could never be set
   to VK_RETURN. */
static LRESULT CALLBACK KeyCaptureSink(HWND hWnd, UINT message,
                                       WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS | DLGC_WANTCHARS;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if ((int)wParam > 0 && (int)wParam <= 255) {
                g_iCaptureKey = (int)wParam;
                KeyCaptureShow(GetParent(hWnd));
            }
            return 0;

        default:
            break;
    }

    return CallWindowProc(g_pfnCaptureOld, hWnd, message, wParam, lParam);
}

LRESULT CALLBACK KeyCaptureDlgProc(HWND hDlg, UINT message,
                                   WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_INITDIALOG: {
            const HWND hHint = GetDlgItem(hDlg, IDC_KEYCAP_HINT);

            (void)lParam;

            g_iCaptureKey = 0;
            KeyCaptureShow(hDlg);

            CenterDialog(hDlg);

            if (hHint) {
                g_pfnCaptureOld = (WNDPROC)SetWindowLong(
                    hHint, GWL_WNDPROC, (LONG)KeyCaptureSink);
                SetFocus(hHint);
            }

            return FALSE;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK:
                    if (g_iCaptureKey <= 0) {
                        MessageBox(hDlg,
                                   TEXT("Press a key first, then choose OK."),
                                   TEXT("No Key Pressed"),
                                   MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }
                    EndDialog(hDlg, g_iCaptureKey);
                    return TRUE;

                case IDCANCEL:
                    EndDialog(hDlg, 0);
                    return TRUE;

                default:
                    break;
            }
            break;

        case WM_DESTROY: {
            const HWND hHint = GetDlgItem(hDlg, IDC_KEYCAP_HINT);

            if (hHint && g_pfnCaptureOld) {
                SetWindowLong(hHint, GWL_WNDPROC, (LONG)g_pfnCaptureOld);
                g_pfnCaptureOld = NULL;
            }
            break;
        }

        default:
            break;
    }

    return FALSE;
}

/* Ask the user for one virtual key code. Returns 0 when cancelled. */
static int KeyCaptureRun(HWND hParent, int iCurrent) {
    return (int)DialogBoxParam(hInst, (LPCTSTR)IDD_KEYCAPTURE, hParent,
                               (DLGPROC)KeyCaptureDlgProc, (LPARAM)iCurrent);
}

/* ------------------------------------------------------------------- dialog */

LRESULT CALLBACK KeyDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_INITDIALOG: {
            int i;

            g_pKeyEdit = (RSHL_KEYS*)lParam;
            InitKeyRows();
            KeyRefreshBoxes(hDlg);

            /* Three characters is enough for 255; the boxes are typed into
               as well as filled by the capture dialog. */
            for (i = 0; i < RSHL_KEY_COUNT; ++i) {
                const HWND hEdit = GetDlgItem(hDlg, g_aKeyRows[i].iValueId);

                if (hEdit) {
                    SendMessage(hEdit, EM_LIMITTEXT, 3, 0);
                }
            }

            CenterDialog(hDlg);
            return TRUE;
        }

        case WM_COMMAND: {
            const int iId = LOWORD(wParam);
            int       i;

            /* A Set button on one of the six rows. */
            for (i = 0; i < RSHL_KEY_COUNT; ++i) {
                if (iId == g_aKeyRows[i].iSetId) {
                    const int iKey =
                        KeyCaptureRun(hDlg, 0);

                    if (iKey > 0) {
                        *KeyField(g_pKeyEdit, g_aKeyRows[i].iIndex) = iKey;
                        KeyRefreshBoxes(hDlg);
                    }
                    return TRUE;
                }
            }

            switch (iId) {
                case IDOK: {
                    HWND hBad = KeyReadBoxes(hDlg);

                    /* The boxes can be typed into, so they have to be parsed
                       before the conflict check means anything. */
                    if (hBad) {
                        MessageBox(hDlg,
                                   TEXT("Enter a key code between 1 and 255."),
                                   TEXT("Invalid Key"),
                                   MB_OK | MB_ICONWARNING);
                        SetFocus(hBad);
                        SendMessage(hBad, EM_SETSEL, 0, -1);
                        return TRUE;
                    }

                    /* Two rows may not share a key, otherwise the shell cannot
                       tell which command the user meant. */
                    for (i = 0; i < RSHL_KEY_COUNT; ++i) {
                        int j;

                        for (j = i + 1; j < RSHL_KEY_COUNT; ++j) {
                            const int iLeft  = *KeyField(g_pKeyEdit, i);
                            const int iRight = *KeyField(g_pKeyEdit, j);

                            if (iLeft == iRight) {
                                TCHAR aMessage[160];

                                aMessage[0] = 0;
                                AppendText(aMessage,
                                           (int)(sizeof(aMessage) / sizeof(aMessage[0])),
                                           g_aKeyRows[i].pLabel);
                                AppendText(aMessage,
                                           (int)(sizeof(aMessage) / sizeof(aMessage[0])),
                                           TEXT(" and "));
                                AppendText(aMessage,
                                           (int)(sizeof(aMessage) / sizeof(aMessage[0])),
                                           g_aKeyRows[j].pLabel);
                                AppendText(aMessage,
                                           (int)(sizeof(aMessage) / sizeof(aMessage[0])),
                                           TEXT(" use the same key "));
                                AppendDec(aMessage,
                                          (int)(sizeof(aMessage) / sizeof(aMessage[0])),
                                          iLeft);
                                AppendText(aMessage,
                                           (int)(sizeof(aMessage) / sizeof(aMessage[0])),
                                           TEXT("."));

                                MessageBox(hDlg, aMessage,
                                           TEXT("Duplicate Key"),
                                           MB_OK | MB_ICONWARNING);
                                return TRUE;
                            }
                        }
                    }

                    EndDialog(hDlg, TRUE);
                    return TRUE;
                }

                case IDCANCEL:
                    EndDialog(hDlg, FALSE);
                    return TRUE;

                default:
                    break;
            }
            break;
        }

        default:
            break;
    }

    return FALSE;
}

/* Open the key editor and copy the result back into g_cfg. */
static void ShowKeyEditor(HWND hWnd) {
    RSHL_KEYS scratch = g_cfg.keys;

    if (DialogBoxParam(hInst, (LPCTSTR)IDD_KEYEDIT, hWnd,
                       (DLGPROC)KeyDlgProc, (LPARAM)&scratch)) {
        g_cfg.keys = scratch;
        InvalidateRect(hWnd, NULL, TRUE);
    }
}

/* -------------------------------------------------------------------------
   Slot editor. Nine rows of type, name, path and a Select button. The rows are
   numbered, so the panel is a fixed grid: control ids count up by row and the
   resource file lays the rows out the same way this code walks them.
   ------------------------------------------------------------------------- */

/* Field numbers, in the order the row control ids are spaced. */
#define SLOT_FIELD_TYPE     0
#define SLOT_FIELD_NAME     1
#define SLOT_FIELD_PATH     2
#define SLOT_FIELD_SELECT   3

static int SlotRowId(int iRow, int iField) {
    switch (iField) {
        case SLOT_FIELD_NAME:   return IDC_SLOT_NAME_0 + iRow;
        case SLOT_FIELD_PATH:   return IDC_SLOT_PATH_0 + iRow;
        case SLOT_FIELD_SELECT: return IDC_SLOT_SEL_0 + iRow;
        default:                return IDC_SLOT_TYPE_0 + iRow;
    }
}

/* Scratch copy the dialog edits. */
static RSHL_CONFIG* g_pSlotEdit;

/* Fill one combo with the ten type names; the item index is the type number. */
static void SlotFillCombo(HWND hDlg, int iRow) {
    const HWND hCombo = GetDlgItem(hDlg, SlotRowId(iRow, SLOT_FIELD_TYPE));
    int        i;

    if (!hCombo) {
        return;
    }

    for (i = 0; i <= RSHL_TYPE_UTILITIES; ++i) {
        const char* pszName = ConfigSlotTypeName(i);
        TCHAR       aText[32];

        AnsiToTChar(pszName ? pszName : "Unknown", aText,
                    (int)(sizeof(aText) / sizeof(aText[0])));
        SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)aText);
    }
}

/* Put g_pSlotEdit back into the nine rows. */
static void SlotRefresh(HWND hDlg) {
    int i;

    if (!g_pSlotEdit) {
        return;
    }

    for (i = 0; i < RSHL_SLOT_MAX; ++i) {
        const HWND hCombo = GetDlgItem(hDlg, SlotRowId(i, SLOT_FIELD_TYPE));
        TCHAR      aName[RSHL_NAME_MAX];
        TCHAR      aPath[RSHL_PATH_MAX];

        AnsiToTChar(g_pSlotEdit->aSlots[i].szName, aName, RSHL_NAME_MAX);
        AnsiToTChar(g_pSlotEdit->aSlots[i].szPath, aPath, RSHL_PATH_MAX);

        if (hCombo) {
            SendMessage(hCombo, CB_SETCURSEL,
                        (WPARAM)g_pSlotEdit->aSlots[i].iType, 0);
        }

        SetDlgItemText(hDlg, SlotRowId(i, SLOT_FIELD_NAME), aName);
        SetDlgItemText(hDlg, SlotRowId(i, SLOT_FIELD_PATH), aPath);
    }
}

/* Ask for a program, then fill the path and a default name into the row. */
static void SlotPickFile(HWND hDlg, int iRow) {
    OPENFILENAME ofn;
    TCHAR        aFile[CFG_PATH_TEXT];
    TCHAR        aName[RSHL_NAME_MAX];
    TCHAR*       pBase;
    TCHAR*       pDot;
    TCHAR*       pSlash;
    int          i;

    aFile[0] = 0;

    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hDlg;
    ofn.lpstrFilter = TEXT("Programs (*.exe)\0*.exe\0All files (*.*)\0*.*\0\0");
    ofn.lpstrFile   = aFile;
    ofn.nMaxFile    = CFG_PATH_TEXT;
    ofn.Flags       = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (!GetOpenFileName(&ofn)) {
        return;
    }

    SetDlgItemText(hDlg, SlotRowId(iRow, SLOT_FIELD_PATH), aFile);

    /* Display name: the file name without its directory or extension. */
    pBase = aFile;
    pSlash = _tcsrchr(aFile, TEXT('\\'));
    if (pSlash) {
        pBase = pSlash + 1;
    }
    {
        TCHAR* pForward = _tcsrchr(aFile, TEXT('/'));

        if (pForward && pForward > pBase) {
            pBase = pForward + 1;
        }
    }

    pDot = _tcsrchr(pBase, TEXT('.'));
    for (i = 0; pBase[i] && i < (int)(sizeof(aName) / sizeof(aName[0])) - 1; ++i) {
        if (pDot && (pBase + i) == pDot) {
            break;
        }
        aName[i] = pBase[i];
    }
    aName[i] = 0;

    SetDlgItemText(hDlg, SlotRowId(iRow, SLOT_FIELD_NAME), aName);
}

/* Read one row into pSlot. A row with nothing in it ends the list. */
static BOOL SlotReadRow(HWND hDlg, int iRow, RSHL_SLOT* pSlot, BOOL* pbEmpty) {
    const HWND hCombo = GetDlgItem(hDlg, SlotRowId(iRow, SLOT_FIELD_TYPE));
    TCHAR      aName[RSHL_NAME_MAX];
    TCHAR      aPath[RSHL_PATH_MAX];

    aName[0] = 0;
    aPath[0] = 0;
    GetDlgItemText(hDlg, SlotRowId(iRow, SLOT_FIELD_NAME), aName,
                   (int)(sizeof(aName) / sizeof(aName[0])));
    GetDlgItemText(hDlg, SlotRowId(iRow, SLOT_FIELD_PATH), aPath,
                   (int)(sizeof(aPath) / sizeof(aPath[0])));

    *pbEmpty = (!aName[0] && !aPath[0]);
    if (*pbEmpty) {
        return TRUE;
    }

    /* A half filled row is a mistake, not a deliberate blank. */
    if (!aName[0] || !aPath[0]) {
        return FALSE;
    }

    memset(pSlot, 0, sizeof(*pSlot));

    pSlot->iType = 0;
    if (hCombo) {
        const LRESULT iSel = SendMessage(hCombo, CB_GETCURSEL, 0, 0);

        if (iSel != CB_ERR) {
            pSlot->iType = (int)iSel;
        }
    }

    TCharToAnsi(aName, pSlot->szName, (int)sizeof(pSlot->szName));
    TCharToAnsi(aPath, pSlot->szPath, (int)sizeof(pSlot->szPath));

    return TRUE;
}

/* Reason a row failed, so the message can say what is actually wrong. */
#define SLOT_BAD_NONE       0
#define SLOT_BAD_PARTIAL    1   /* name or path missing            */
#define SLOT_BAD_AFTER_GAP  2   /* filled row after a blank one    */

/* Read all nine rows. Returns TRUE and sets the count on success, or FALSE
   with the offending row number and reason. */
static BOOL SlotReadAll(HWND hDlg, RSHL_SLOT* aSlots, int* piCount,
                        int* piBadRow, int* piBadReason) {
    int  iCount = 0;
    BOOL bEnded = FALSE;
    int  i;

    *piBadRow    = 0;
    *piBadReason = SLOT_BAD_NONE;

    for (i = 0; i < RSHL_SLOT_MAX; ++i) {
        RSHL_SLOT slot;
        BOOL      bEmpty = FALSE;

        if (!SlotReadRow(hDlg, i, &slot, &bEmpty)) {
            *piBadRow    = i;
            *piBadReason = SLOT_BAD_PARTIAL;
            return FALSE;
        }

        if (bEmpty) {
            bEnded = TRUE;
            continue;
        }

        /* Once a blank row appears the list is over; a filled row after it
           would silently move up on the next save. */
        if (bEnded) {
            *piBadRow    = i;
            *piBadReason = SLOT_BAD_AFTER_GAP;
            return FALSE;
        }

        aSlots[iCount++] = slot;
    }

    *piCount = iCount;
    return TRUE;
}

LRESULT CALLBACK SlotDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_INITDIALOG: {
            int i;

            g_pSlotEdit = (RSHL_CONFIG*)lParam;

            for (i = 0; i < RSHL_SLOT_MAX; ++i) {
                SlotFillCombo(hDlg, i);
            }

            SlotRefresh(hDlg);
            CenterDialog(hDlg);
            return TRUE;
        }

        case WM_COMMAND: {
            const int iId = LOWORD(wParam);
            int       i;

            if (iId == IDOK) {
                int  iCount = 0;
                int  iBadRow = 0;
                int  iBadReason = SLOT_BAD_NONE;
                TCHAR aMessage[192];

                if (!SlotReadAll(hDlg, g_pSlotEdit->aSlots, &iCount,
                                 &iBadRow, &iBadReason)) {
                    aMessage[0] = 0;
                    AppendText(aMessage,
                               (int)(sizeof(aMessage) / sizeof(aMessage[0])),
                               TEXT("Row "));
                    AppendDec(aMessage,
                              (int)(sizeof(aMessage) / sizeof(aMessage[0])),
                              iBadRow + 1);

                    if (iBadReason == SLOT_BAD_AFTER_GAP) {
                        AppendText(aMessage,
                                   (int)(sizeof(aMessage) / sizeof(aMessage[0])),
                                   TEXT(" is filled but an earlier row is blank. "
                                        "Fill the rows from the top."));
                        MessageBox(hDlg, aMessage, TEXT("Row Out Of Order"),
                                   MB_OK | MB_ICONWARNING);
                    }
                    else {
                        AppendText(aMessage,
                                   (int)(sizeof(aMessage) / sizeof(aMessage[0])),
                                   TEXT(" needs both a name and a path, or "
                                        "neither."));
                        MessageBox(hDlg, aMessage, TEXT("Incomplete Row"),
                                   MB_OK | MB_ICONWARNING);
                    }
                    return TRUE;
                }

                g_pSlotEdit->iSlotCount = iCount;
                EndDialog(hDlg, TRUE);
                return TRUE;
            }

            if (iId == IDCANCEL) {
                EndDialog(hDlg, FALSE);
                return TRUE;
            }

            /* A Select button on one of the nine rows. */
            for (i = 0; i < RSHL_SLOT_MAX; ++i) {
                if (iId == SlotRowId(i, SLOT_FIELD_SELECT)) {
                    SlotPickFile(hDlg, i);
                    return TRUE;
                }
            }
            break;
        }

        default:
            break;
    }

    return FALSE;
}

/* Open the slot editor and copy the result back into g_cfg. */
static void ShowSlotEditor(HWND hWnd) {
    RSHL_CONFIG scratch = g_cfg;

    if (DialogBoxParam(hInst, (LPCTSTR)IDD_SLOTEDIT, hWnd,
                       (DLGPROC)SlotDlgProc, (LPARAM)&scratch)) {
        g_cfg.iSlotCount = scratch.iSlotCount;
        memcpy(g_cfg.aSlots, scratch.aSlots, sizeof(g_cfg.aSlots));
        InvalidateRect(hWnd, NULL, TRUE);
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    HDC         hdc;
    PAINTSTRUCT ps;
    RECT        rt;

    switch (message)  {
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDM_FILE_OPEN:
                   DoFileDialog(hWnd, FALSE);
                   break;
                case IDM_FILE_SAVEAS:
                   DoFileDialog(hWnd, TRUE);
                   break;
                case IDM_HELP_ABOUT:
                   DialogBox(hInst, (LPCTSTR)IDD_ABOUTBOX, hWnd, (DLGPROC)About);
                   break;
                case IDM_EDIT_THEME:
                   ShowThemeEditor(hWnd);
                   break;
                case IDM_EDIT_KEY:
                   ShowKeyEditor(hWnd);
                   break;
                case IDM_EDIT_SLOTS:
                   ShowSlotEditor(hWnd);
                   break;
                case IDM_FILE_EXIT:
                   DestroyWindow(hWnd);
                   break;
                default:
                   return DefWindowProc(hWnd, message, wParam, lParam);
            }
            break;
        case WM_CREATE:
            g_hFont     = CreateUiFont(FW_NORMAL);
            g_hFontBold = CreateUiFont(FW_BOLD);
#ifdef VER_PLATFORM_WIN32_CE
            hwndCB = CommandBar_Create(hInst, hWnd, 1);
            CommandBar_InsertMenubar(hwndCB, hInst, IDM_MENU, 0);
            CommandBar_AddAdornments(hwndCB, 0, 0);
            /* The CommandBar is a child of a full screen window, but it still
               covers the top of the client area, so its height is subtracted
               from the layout origin when the rows are drawn. */
            {
                RECT rcBar;

                GetWindowRect(hwndCB, &rcBar);
                g_iCbHeight = rcBar.bottom - rcBar.top;
            }
#else
            g_iCbHeight = 0;
#endif
            break;
        case WM_PAINT:
            hdc = BeginPaint(hWnd, &ps);
            GetClientRect(hWnd, &rt);
            PaintClient(hdc, &rt);

            /* The current file name rides on the bottom edge, so it stays put
               whatever the rows above do. */
            DrawPathLine(hdc, UI_MARGIN, rt.bottom - UI_MARGIN - UI_TITLE_H,
                         rt.right - 2 * UI_MARGIN);
            EndPaint(hWnd, &ps);
            break;
        case WM_DESTROY:
#ifdef VER_PLATFORM_WIN32_CE
            CommandBar_Destroy(hwndCB);
#endif
            if (g_hFont) {
                DeleteObject(g_hFont);
                g_hFont = NULL;
            }
            if (g_hFontBold) {
                DeleteObject(g_hFontBold);
                g_hFontBold = NULL;
            }
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
   }
   return 0;
}

LRESULT CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    (void)lParam;

    switch (message) {
        case WM_INITDIALOG:
            CenterDialog(hDlg);
            return TRUE;

        case WM_COMMAND:
            if ((LOWORD(wParam) == IDOK) || (LOWORD(wParam) == IDCANCEL)) {
                EndDialog(hDlg, LOWORD(wParam));
                return TRUE;
            }
            break;
    }
    return FALSE;
}
