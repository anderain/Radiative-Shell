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

ATOM                MyRegisterClass	(HINSTANCE hInstance, LPTSTR szWindowClass);
BOOL                InitInstance	(HINSTANCE, int);
LRESULT CALLBACK    WndProc			(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK    About			(HWND, UINT, WPARAM, LPARAM);

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

/* -------------------------------------------------------------------------
   Client area layout. The window is a fixed 240x320 on the desktop, so the
   rows are laid out top down from constants rather than measured.
   ------------------------------------------------------------------------- */

#define UI_MARGIN       8
#define UI_LINE_H       20
#define UI_SECTION_GAP  10
#define UI_SWATCH_W     30
#define UI_SWATCH_H     18
#define UI_SWATCH_GAP   6
#define UI_SWATCH_X     76
#define UI_COL2_X       124
#define UI_THEME_ROW_H  40

/* Draw one line of text top left aligned, selecting pFont for the call. */
static void DrawLine(HDC hdc, int iX, int iY, const TCHAR* pText, HFONT hFont) {
    HFONT hOld;

    if (!pText || !*pText) {
        return;
    }

    hOld = (HFONT)SelectObject(hdc, hFont);
    TextOut(hdc, iX, iY, pText, (int)_tcslen(pText));
    SelectObject(hdc, hOld);
}

/* Fetch the VGA colour for a palette index as a COLORREF. */
static COLORREF SwatchColor(BYTE bIndex) {
    const DWORD color = VGA_MODE13H_COLORPALETTE[bIndex];

    return RGB((BYTE)((color >> 16) & 0xFF),
               (BYTE)((color >> 8) & 0xFF),
               (BYTE)(color & 0xFF));
}

/* One theme row: the index, the four swatches and their hex byte values. */
static void DrawThemeRow(HDC hdc, int iY, int iIndex) {
    TCHAR    aLabel[24];
    HPEN     hPen;
    HPEN     hOldPen;
    int      j;
    const COLORREF kFrame = RGB(0x80, 0x80, 0x80);

    aLabel[0] = 0;
    AppendText(aLabel, (int)(sizeof(aLabel) / sizeof(aLabel[0])), TEXT("Theme "));
    AppendDec(aLabel, (int)(sizeof(aLabel) / sizeof(aLabel[0])), iIndex);
    DrawLine(hdc, UI_MARGIN, iY, aLabel, g_hFont);

    /* A neutral outline keeps the near black swatches visible. */
    hPen    = CreatePen(PS_SOLID, 1, kFrame);
    hOldPen = (HPEN)SelectObject(hdc, hPen ? hPen : GetStockObject(BLACK_PEN));

    for (j = 0; j < 4; ++j) {
        const int iX = UI_SWATCH_X + j * (UI_SWATCH_W + UI_SWATCH_GAP);
        HBRUSH hBrush = CreateSolidBrush(SwatchColor(g_cfg.aThemes[iIndex].bColors[j]));

        if (hBrush) {
            HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);

            Rectangle(hdc, iX, iY, iX + UI_SWATCH_W, iY + UI_SWATCH_H);

            SelectObject(hdc, hOldBrush);
            DeleteObject(hBrush);
        }
    }

    SelectObject(hdc, hOldPen);
    if (hPen) {
        DeleteObject(hPen);
    }

    /* The four palette indexes, spaced to sit under their swatches. */
    for (j = 0; j < 4; ++j) {
        static const TCHAR kHex[] = TEXT("0123456789ABCDEF");
        const BYTE  bValue = g_cfg.aThemes[iIndex].bColors[j];
        TCHAR       aHex[4];

        aHex[0] = kHex[(bValue >> 4) & 0x0F];
        aHex[1] = kHex[bValue & 0x0F];
        aHex[2] = 0;

        DrawLine(hdc, UI_SWATCH_X + j * (UI_SWATCH_W + UI_SWATCH_GAP) + 3,
                 iY + UI_SWATCH_H + 2, aHex, g_hFont);
    }
}

/* One key mapping cell: a label and its virtual key code, side by side. */
static void DrawKeyCell(HDC hdc, int iX, int iY, const TCHAR* pLabel, int iKey) {
    TCHAR aValue[16];

    DrawLine(hdc, iX, iY, pLabel, g_hFont);

    aValue[0] = 0;
    AppendDec(aValue, (int)(sizeof(aValue) / sizeof(aValue[0])), iKey);
    DrawLine(hdc, iX + 52, iY, aValue, g_hFont);
}

/* Width of pText in the UI font. */
static int TextWidth(HDC hdc, const TCHAR* pText) {
    SIZE  size;
    HFONT hOld = (HFONT)SelectObject(hdc, g_hFont);

    size.cx = 0;
    size.cy = 0;
    GetTextExtentPoint32(hdc, pText, (int)_tcslen(pText), &size);
    SelectObject(hdc, hOld);

    return size.cx;
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

        /* Ask for a fixed 240x320 client area; AdjustWindowRect adds the
           frame. bMenu is TRUE because the class attaches IDM_MENU and the
           menu bar sits in the non client area, so it has to be reserved or
           it silently takes its height out of the client. */
        rectWindow.left   = 0;
        rectWindow.top    = 0;
        rectWindow.right  = 240;
        rectWindow.bottom = 320;
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

    iY = UI_MARGIN;

    /* Theme section. */
    DrawLine(hdc, UI_MARGIN, iY, TEXT("Theme"), g_hFontBold);
    iY += UI_LINE_H;

    for (i = 0; i < RSHL_THEME_COUNT; ++i) {
        DrawThemeRow(hdc, iY, i);
        iY += UI_THEME_ROW_H;
    }

    /* Keys section, two columns of three. */
    iY += UI_SECTION_GAP;
    DrawLine(hdc, UI_MARGIN, iY, TEXT("Keys"), g_hFontBold);
    iY += UI_LINE_H;

    DrawKeyCell(hdc, UI_MARGIN, iY, TEXT("Tab"),    g_cfg.keys.iTabSwitch);
    DrawKeyCell(hdc, UI_COL2_X, iY, TEXT("Next"),   g_cfg.keys.iActionNext);
    iY += UI_LINE_H;

    DrawKeyCell(hdc, UI_MARGIN, iY, TEXT("Flip"),   g_cfg.keys.iFlipScreen);
    DrawKeyCell(hdc, UI_COL2_X, iY, TEXT("Theme"),  g_cfg.keys.iThemeSwitch);
    iY += UI_LINE_H;

    DrawKeyCell(hdc, UI_MARGIN, iY, TEXT("Prev"),   g_cfg.keys.iActionPrev);
    DrawKeyCell(hdc, UI_COL2_X, iY, TEXT("Action"), g_cfg.keys.iAction);
    iY += UI_LINE_H;

    /* Slot count. */
    iY += UI_SECTION_GAP;
    aText[0] = 0;
    AppendText(aText, (int)(sizeof(aText) / sizeof(aText[0])), TEXT("Slots: "));
    AppendDec(aText, (int)(sizeof(aText) / sizeof(aText[0])), g_cfg.iSlotCount);
    AppendText(aText, (int)(sizeof(aText) / sizeof(aText[0])), TEXT(" of "));
    AppendDec(aText, (int)(sizeof(aText) / sizeof(aText[0])), RSHL_SLOT_MAX);
    DrawLine(hdc, UI_MARGIN, iY, aText, g_hFont);
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
#endif
            break;
        case WM_PAINT:
            hdc = BeginPaint(hWnd, &ps);
            GetClientRect(hWnd, &rt);
            PaintClient(hdc, &rt);

            /* The current file name sits on the bottom edge, in its own
               column so it stays put when the rows above are reflowed. */
            DrawPathLine(hdc, UI_MARGIN, rt.bottom - UI_MARGIN - 16,
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
    RECT rt, rt1;
    int DlgWidth, DlgHeight; 
    int NewPosX, NewPosY;

    (void)lParam;

    switch (message) {
        case WM_INITDIALOG:
            if (GetWindowRect(hDlg, &rt1)) {
                GetClientRect(GetParent(hDlg), &rt);
                DlgWidth    = rt1.right - rt1.left;
                DlgHeight   = rt1.bottom - rt1.top ;
                NewPosX     = (rt.right - rt.left - DlgWidth)/2;
                NewPosY     = (rt.bottom - rt.top - DlgHeight)/2;
                
                if (NewPosX < 0) NewPosX = 0;
                if (NewPosY < 0) NewPosY = 0;
                SetWindowPos(hDlg, 0, NewPosX, NewPosY,
                    0, 0, SWP_NOZORDER | SWP_NOSIZE);
            }
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
