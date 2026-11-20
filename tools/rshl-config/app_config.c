#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <tchar.h>
#include <string.h>
#include "resource.h"
#include "config.h"

HINSTANCE        	hInst;
HWND                hwndCB;

/* Configuration currently in the editor and the file it came from. */
static RSHL_CONFIG  g_cfg;
static char         g_szPath[CFG_PATH_TEXT];
static char         g_szStatus[CFG_STATUS_TEXT];

ATOM                MyRegisterClass	(HINSTANCE hInstance, LPTSTR szWindowClass);
BOOL                InitInstance	(HINSTANCE, int);
LRESULT CALLBACK    WndProc			(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK    About			(HWND, UINT, WPARAM, LPARAM);

/* -------------------------------------------------------------------------
   Text helpers. config.c speaks ANSI, the window speaks TCHAR, so the two
   conversions live here, together with the small append helpers the status
   text is built from.
   ------------------------------------------------------------------------- */

static void TCharToAnsi(const TCHAR* pSrc, char* pDst, int cbDst) {
#ifdef UNICODE
    WideCharToMultiByte(CP_ACP, 0, pSrc, -1, pDst, cbDst, NULL, NULL);
#else
    lstrcpynA(pDst, pSrc, cbDst);
#endif
}

static void StatusAppend(const char* pText) {
    const int iLen = (int)strlen(g_szStatus);
    int i = 0;

    while (pText[i] && iLen + i < (int)sizeof(g_szStatus) - 1) {
        g_szStatus[iLen + i] = pText[i];
        ++i;
    }
    g_szStatus[iLen + i] = 0;
}

static void StatusAppendDec(int iValue) {
    char aText[12];
    char aOne[2];
    int  iCount = 0;

    if (iValue < 0) {
        StatusAppend("-");
        iValue = -iValue;
    }

    do {
        aText[iCount++] = (char)('0' + (iValue % 10));
        iValue /= 10;
    } while (iValue > 0 && iCount < (int)sizeof(aText));

    aOne[1] = 0;
    while (iCount > 0) {
        aOne[0] = aText[--iCount];
        StatusAppend(aOne);
    }
}

static void StatusAppendHex(BYTE bValue) {
    static const char kHex[] = "0123456789ABCDEF";
    char aText[3];

    aText[0] = kHex[(bValue >> 4) & 0x0F];
    aText[1] = kHex[bValue & 0x0F];
    aText[2] = 0;
    StatusAppend(aText);
}

/* Rebuild the text shown in the client area from the current configuration. */
static void RefreshStatusText(void) {
    int i;

    g_szStatus[0] = 0;

    StatusAppend("RSHL Config");
    StatusAppend("\n\nFile: ");
    StatusAppend(g_szPath[0] ? g_szPath : "(none)");

    for (i = 0; i < RSHL_THEME_COUNT; ++i) {
        int j;

        StatusAppend("\n\nTheme ");
        StatusAppendDec(i);
        StatusAppend(" : ");
        for (j = 0; j < 4; ++j) {
            if (j > 0) {
                StatusAppend(" ");
            }
            StatusAppendHex(g_cfg.aThemes[i].bColors[j]);
        }
    }

    StatusAppend("\n\nKeys    : tab=");
    StatusAppendDec(g_cfg.keys.iTabSwitch);
    StatusAppend(" flip=");
    StatusAppendDec(g_cfg.keys.iFlipScreen);
    StatusAppend(" prev=");
    StatusAppendDec(g_cfg.keys.iActionPrev);
    StatusAppend(" next=");
    StatusAppendDec(g_cfg.keys.iActionNext);
    StatusAppend(" theme=");
    StatusAppendDec(g_cfg.keys.iThemeSwitch);
    StatusAppend(" ok=");
    StatusAppendDec(g_cfg.keys.iConfirm);

    StatusAppend("\n\nSlots   : ");
    StatusAppendDec(g_cfg.iSlotCount);
    StatusAppend(" of ");
    StatusAppendDec(RSHL_SLOT_MAX);

    for (i = 0; i < g_cfg.iSlotCount; ++i) {
        const char* pTypeName = ConfigSlotTypeName(g_cfg.aSlots[i].iType);

        StatusAppend("\n  ");
        StatusAppendDec(i);
        StatusAppend("  ");
        StatusAppendDec(g_cfg.aSlots[i].iType);
        StatusAppend(" ");
        StatusAppend(pTypeName ? pTypeName : "Unknown");
        StatusAppend("  ");
        StatusAppend(g_cfg.aSlots[i].szName);
    }

    if (!g_cfg.bValid) {
        StatusAppend("\n\nBuilt in defaults, nothing has been read yet.");
    }
}

/* -------------------------------------------------------------------------
   Loading and saving.
   ------------------------------------------------------------------------- */

/* Load pszPath into g_cfg. On failure the defaults are kept and the reason is
   both shown in a message box and written into the client area. */
static BOOL LoadConfigFile(HWND hWnd, const char* pszPath) {
    char szError[CFG_ERROR_TEXT];
    RSHL_CONFIG loaded;

    szError[0] = 0;

    if (!ConfigLoad(pszPath, &loaded, szError, (int)sizeof(szError))) {
        g_szPath[0] = 0;
        g_cfg.bValid = FALSE;
        g_szStatus[0] = 0;
        StatusAppend("RSHL Config");
        StatusAppend("\n\nCould not load:\n  ");
        StatusAppend(pszPath);
        StatusAppend("\n\n");
        StatusAppend(szError);

        if (hWnd) {
            MessageBoxA(hWnd, szError, "Load failed", MB_OK | MB_ICONERROR);
        }
        return FALSE;
    }

    g_cfg = loaded;
    lstrcpynA(g_szPath, pszPath, (int)sizeof(g_szPath));
    RefreshStatusText();

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

    ZeroMemory(&ofn, sizeof(ofn));
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
            MessageBoxA(hWnd, szError, "Save failed", MB_OK | MB_ICONERROR);
            return;
        }

        lstrcpynA(g_szPath, szPath, (int)sizeof(g_szPath));
        RefreshStatusText();
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
    TCHAR    szTitle[] = TEXT("RSHL Config");
    TCHAR    szWindowClass[] = TEXT("RSHL_MAIN_WIN");

    hInst = hInstance;
    MyRegisterClass(hInstance, szWindowClass);

    ConfigSetDefaults(&g_cfg);
    g_szPath[0] = 0;
    RefreshStatusText();

    hWnd = CreateWindow(szWindowClass, szTitle, WS_VISIBLE, 0, 0, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hInstance, NULL);

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

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    HDC hdc;
    PAINTSTRUCT ps;
    RECT rt;

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
#ifdef VER_PLATFORM_WIN32_CE
            hwndCB = CommandBar_Create(hInst, hWnd, 1);
            CommandBar_InsertMenubar(hwndCB, hInst, IDM_MENU, 0);
            CommandBar_AddAdornments(hwndCB, 0, 0);
#endif
            break;
        case WM_PAINT:
            hdc = BeginPaint(hWnd, &ps);
            GetClientRect(hWnd, &rt);
            DrawTextA(hdc, g_szStatus, -1, &rt,
                DT_LEFT | DT_TOP | DT_NOPREFIX | DT_EDITCONTROL);
            EndPaint(hWnd, &ps);
            break;
        case WM_DESTROY:
#ifdef VER_PLATFORM_WIN32_CE
            CommandBar_Destroy(hwndCB);
#endif
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
