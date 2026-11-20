#include <windows.h>
#include <commctrl.h>
#include "resource.h"
#include "rshell.h"

ATOM                MyRegisterClass     (HINSTANCE hInstance, LPTSTR szWindowClass);
BOOL                InitInstance        (HINSTANCE, int);
LRESULT CALLBACK    WndProc             (HWND, UINT, WPARAM, LPARAM);
BOOL                SetTaskbarVisible   (BOOL bVisible);
void                CenterWindow        (HWND hWnd);

HINSTANCE           hInst;

/* Size of the picture handed to the graph layer. */
#define RS_SCREEN_WIDTH     320
#define RS_SCREEN_HEIGHT    240

/* Desktop test window client area, large enough for either orientation. */
#define RS_WINDOW_WIDTH     640
#define RS_WINDOW_HEIGHT    480

static RSGRAPH      g_rsGraph;
static BOOL         g_bGraphReady  = FALSE;
static int          g_iOrientation = RS_DEFAULT_ORIENTATION;

#ifndef VER_PLATFORM_WIN32_CE
/* -------------------------------------------------------------------------
   Desktop only: "-o landscape|left|right" picks the screen orientation so the
   flip paths can be checked on a normal PC. Windows CE uses the real device
   orientation instead.
   ------------------------------------------------------------------------- */

static int RsCharLower(int c) {
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

/* Case insensitive compare of lpText against a NUL terminated token; the
   token in lpText must end at a separator. */
static BOOL RsTokenIs(LPCTSTR lpText, LPCTSTR lpToken, int iLen) {
    int i;

    for (i = 0; i < iLen; ++i) {
        if (!lpText[i]) {
            return FALSE;
        }
        if (RsCharLower(lpText[i]) != RsCharLower(lpToken[i])) {
            return FALSE;
        }
    }

    return lpText[iLen] == 0 || lpText[iLen] == ' ' || lpText[iLen] == '\t';
}

static int ParseOrientation(LPCTSTR lpCmdLine) {
    LPCTSTR p = lpCmdLine;
    int     iOrientation = RS_DEFAULT_ORIENTATION;

    while (p && *p) {
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        if (!*p) {
            break;
        }

        if (*p == '-' && (p[1] == 'o' || p[1] == 'O')) {
            p += 2;
            if (*p == ':' || *p == '=') {
                ++p;
            }
            while (*p == ' ' || *p == '\t') {
                ++p;
            }

            if (RsTokenIs(p, TEXT("landscape"), 9) || *p == '0') {
                iOrientation = ORIENTATION_LANDSCAPE;
            }
            else if (RsTokenIs(p, TEXT("left"), 4) || *p == '1') {
                iOrientation = ORIENTATION_PORTRAIT_L;
            }
            else if (RsTokenIs(p, TEXT("right"), 5) || *p == '2') {
                iOrientation = ORIENTATION_PORTRAIT_R;
            }
            else {
                MessageBox(
                    NULL,
                    TEXT("-o expects landscape (0), left (1) or right (2)."),
                    TEXT("RadiativeShell"),
                    MB_OK | MB_ICONWARNING
                );
                iOrientation = RS_DEFAULT_ORIENTATION;
            }
            break;
        }

        /* Skip an unrelated token */
        while (*p && *p != ' ' && *p != '\t') {
            ++p;
        }
    }

    return iOrientation;
}
#endif /* VER_PLATFORM_WIN32_CE */

#ifdef VER_PLATFORM_WIN32_CE
/* Detect the device orientation on Windows CE. Fixed portrait devices cannot
   report which way they are mounted, so a portrait device is assumed to be
   rotated counter clockwise; change the fallback below if the picture ends up
   upside down. */
static int DetectCeOrientation(HWND hWnd) {
    RECT rectClient;

    if (!GetClientRect(hWnd, &rectClient)) {
        return RS_DEFAULT_ORIENTATION;
    }

    if (rectClient.right - rectClient.left >= rectClient.bottom - rectClient.top) {
        return ORIENTATION_LANDSCAPE;
    }

    return ORIENTATION_PORTRAIT_L;
}
#endif /* VER_PLATFORM_WIN32_CE */

/* Fill an axis aligned rectangle with one grey level. */
static void FillTestRect(RSGRAPH* pRsGraph, int iX, int iY, int iW, int iH, BYTE bColor) {
    int x, y;

    for (y = 0; y < iH; ++y) {
        for (x = 0; x < iW; ++x) {
            RsGraphSetPixel(pRsGraph, iX + x, iY + y, bColor);
        }
    }
}

/* Draw enough asymmetry to tell the three orientations apart: a white block
   in the logical top left corner ends up in a different physical corner for
   each rotation, and the arrow always points towards the logical top. */
static void DrawTestPattern(RSGRAPH* pRsGraph) {
    const int iWidth  = pRsGraph->iWidth;
    const int iHeight = pRsGraph->iHeight;
    const int iBandW  = iWidth / 4;
    const int iBandH  = 24;
    int       i, x, y;

    /* Background */
    FillTestRect(pRsGraph, 0, 0, iWidth, iHeight, RS_GRAY_DARK);

    /* Four grey levels across the top */
    for (i = 0; i < 4; ++i) {
        FillTestRect(pRsGraph, i * iBandW, 0, iBandW, iBandH, (BYTE)i);
    }

    /* Logical top left marker and an opposite marker on the top right */
    FillTestRect(pRsGraph, 0, iBandH, 16, 16, RS_GRAY_WHITE);
    FillTestRect(pRsGraph, iWidth - 16, iBandH, 16, 16, RS_GRAY_BLACK);

    /* Border, drawn last so it stays visible */
    for (x = 0; x < iWidth; ++x) {
        RsGraphSetPixel(pRsGraph, x, 0, RS_GRAY_WHITE);
        RsGraphSetPixel(pRsGraph, x, iHeight - 1, RS_GRAY_WHITE);
    }
    for (y = 0; y < iHeight; ++y) {
        RsGraphSetPixel(pRsGraph, 0, y, RS_GRAY_WHITE);
        RsGraphSetPixel(pRsGraph, iWidth - 1, y, RS_GRAY_WHITE);
    }

    /* Diagonal from the top left to the bottom right */
    for (i = 0; i < iWidth && i < iHeight; ++i) {
        RsGraphSetPixel(pRsGraph, i, i, RS_GRAY_LIGHT);
    }

    /* Arrow pointing towards the logical top */
    for (i = 0; i < 20; ++i) {
        for (x = 0; x <= i; ++x) {
            RsGraphSetPixel(pRsGraph, iWidth / 2 - i + x, iHeight / 2 - 40 + i, RS_GRAY_WHITE);
            RsGraphSetPixel(pRsGraph, iWidth / 2 + i - x, iHeight / 2 - 40 + i, RS_GRAY_WHITE);
        }
    }
}

/* Build the graph, paint the test pattern and push it into the DIB. */
static BOOL InitGraph(HWND hWnd) {
    HDC             hdc;
    RSDEVICEINFO    deviceInfo;

    hdc = GetDC(hWnd);
    if (!hdc) {
        return FALSE;
    }

    GetDeviceInfo(hdc, &deviceInfo);

    if (!RsGraphInit(&g_rsGraph, hdc, RS_SCREEN_WIDTH, RS_SCREEN_HEIGHT,
                     g_iOrientation, &deviceInfo)) {
        ReleaseDC(hWnd, hdc);
        return FALSE;
    }

    if (!RsGraphCreateDC(&g_rsGraph)) {
        RsGraphDispose(&g_rsGraph);
        ReleaseDC(hWnd, hdc);
        return FALSE;
    }

    ReleaseDC(hWnd, hdc);

    /* hdcMainWin was only needed to derive the memory DC and is invalid now. */
    g_rsGraph.hdcMainWin = NULL;

    DrawTestPattern(&g_rsGraph);
    RsGraphFlip(&g_rsGraph);

    g_bGraphReady = TRUE;
    return TRUE;
}

int WINAPI WinMain( HINSTANCE hInstance,
                    HINSTANCE hPrevInstance,
                    LPTSTR    lpCmdLine,
                    int       nCmdShow) {
    MSG msg;
    HACCEL hAccelTable;

#ifndef VER_PLATFORM_WIN32_CE
    g_iOrientation = ParseOrientation(lpCmdLine);
#endif

    if (!InitInstance (hInstance, nCmdShow)) {
        return FALSE;
    }

    hAccelTable = LoadAccelerators(hInstance, (LPCTSTR)IDC_RADIATIVESHELL);

    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance, LPTSTR szWindowClass) {
    WNDCLASS    wc;

    wc.style            = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc      = (WNDPROC) WndProc;
    wc.cbClsExtra       = 0;
    wc.cbWndExtra       = 0;
    wc.hInstance        = hInstance;
    wc.hIcon            = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_RADIATIVESHELL));
    wc.hCursor          = 0;
    wc.hbrBackground    = (HBRUSH) GetStockObject(WHITE_BRUSH);
    wc.lpszMenuName     = 0;
    wc.lpszClassName    = szWindowClass;

    return RegisterClass(&wc);
}


BOOL InitInstance(HINSTANCE hInstance, int nCmdShow) {
    HWND    hWnd;
    TCHAR   szTitle[]       = TEXT("RadiativeShell");
    TCHAR   szWindowClass[] = TEXT("RS_Main_Win");
    int     iWinWidth, iWinHeight;
    DWORD   dwStyle;

    hInst = hInstance;

    MyRegisterClass(hInstance, szWindowClass);

#ifdef VER_PLATFORM_WIN32_CE
    dwStyle     = WS_VISIBLE;
    iWinWidth   = CW_USEDEFAULT;
    iWinHeight  = CW_USEDEFAULT;
#else
    {
        RECT rectWindow;

        dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

        /* Ask for a fixed client area, AdjustWindowRect adds the frame */
        rectWindow.left   = 0;
        rectWindow.top    = 0;
        rectWindow.right  = RS_WINDOW_WIDTH;
        rectWindow.bottom = RS_WINDOW_HEIGHT;
        AdjustWindowRect(&rectWindow, dwStyle, FALSE);

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

    CenterWindow(hWnd);

#ifdef VER_PLATFORM_WIN32_CE
    /* The device decides the orientation, there is no command line there */
    g_iOrientation = DetectCeOrientation(hWnd);
#endif

    /* The graph needs the final orientation, so build it here rather than in
       WM_CREATE. */
    if (InitGraph(hWnd)) {
        InvalidateRect(hWnd, NULL, TRUE);
    }

    return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    HDC hdc;
    int wmId, wmEvent;
    PAINTSTRUCT ps;
    RECT rectClient;

    switch (message) {
        case WM_COMMAND:
            wmId    = LOWORD(wParam); 
            wmEvent = HIWORD(wParam); 
            switch (wmId) {
                default:
                   return DefWindowProc(hWnd, message, wParam, lParam);
            }
            break;
        case WM_KEYDOWN:
            if (wParam == VK_TAB || wParam == VK_ESCAPE) {
                DestroyWindow(hWnd);
            }
            break;
        case WM_CREATE:
            SetTaskbarVisible(FALSE);
            break;
        case WM_PAINT:
            hdc = BeginPaint(hWnd, &ps);
            if (g_bGraphReady) {
                GetClientRect(hWnd, &rectClient);
                RsGraphPresent(
                    &g_rsGraph,
                    hdc,
                    (rectClient.right  - g_rsGraph.iBmpWidth)  / 2,
                    (rectClient.bottom - g_rsGraph.iBmpHeight) / 2
                );
            }
            EndPaint(hWnd, &ps);
            break;
        case WM_DESTROY:
            RsGraphDispose(&g_rsGraph);
            g_bGraphReady = FALSE;
            SetTaskbarVisible(TRUE);
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
   }
   return 0;
}


BOOL SetTaskbarVisible(BOOL bVisible) {
#ifdef VER_PLATFORM_WIN32_CE
    HWND hWndTaskbar = FindWindow(TEXT("HHTaskBar"), NULL);
    if (!bVisible) {
        SetWindowPos(hWndTaskbar, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_HIDEWINDOW);
    } else {
        SetWindowPos(hWndTaskbar, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    }
#endif
    return bVisible;
}

void CenterWindow(HWND hWnd) {
#ifndef VER_PLATFORM_WIN32_CE
    RECT rectWin;
    int iScrWidth, iScrHeight;

    iScrWidth = GetSystemMetrics(SM_CXSCREEN);
    iScrHeight = GetSystemMetrics(SM_CYSCREEN);

    GetWindowRect(hWnd, &rectWin);

    SetWindowPos(
        hWnd,
        HWND_TOP,
        (iScrWidth - (rectWin.right - rectWin.left)) / 2,
        (iScrHeight - (rectWin.bottom - rectWin.top)) / 2,
        0, 0,
        SWP_NOSIZE
    );
#endif
}
