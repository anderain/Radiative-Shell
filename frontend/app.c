#include <windows.h>
#include <commctrl.h>
#include "resource.h"
#include "rshell.h"
#include "pipboy.h"

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
static int          g_iActiveTab   = PB_TAB_STATS;

/* Time snapshot shown on the clock tab. It is captured when the tab is entered
   and kept until the tab is left, so the page does not tick by itself. */
static SYSTEMTIME   g_stClock;
static BOOL         g_bClockValid  = FALSE;

/* System information. It does not change during a session, so it is read once
   at startup. */
static SYSTEM_INFO  g_stSysInfo;
static BOOL         g_bSysInfoValid = FALSE;

/* Memory status and the boot time, read on the same occasion as the system
   information. The uptime is derived from the tick count at draw time. */
static MEMORYSTATUS g_stMemStatus;
static BOOL         g_bMemValid = FALSE;
static DWORD        g_dwBootTick = 0;

/* Battery snapshot, refreshed every time the stats tab is entered. */
static RS_POWER_INFO g_stPower;
static BOOL          g_bPowerValid = FALSE;

/* Timers. The clock ticks once a second while the clock tab is visible, the
   walk cycle ticks four times a second while the stats tab is visible. Neither
   runs on the other tabs, and both stop while the window is minimised. */
#define RS_TIMER_CLOCK   1
#define RS_TIMER_ANIM    2

static DWORD        g_dwAnimBase   = 0;    /* tick when the walk cycle started */
static BOOL         g_bAnimActive  = FALSE;
static BOOL         g_bMinimised   = FALSE;

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

#if (IS_WIN_CE)
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

/* Take a fresh clock snapshot when the tab is entered, and drop it when the
   tab is left so the next visit reads the clock again. */
static void RefreshClockIfNeeded(void) {
    if (g_iActiveTab != PB_TAB_CLOCK) {
        g_bClockValid = FALSE;
        return;
    }

    if (!g_bClockValid) {
        GetLocalTime(&g_stClock);
        g_bClockValid = TRUE;
    }
}

/* Battery level changes over time, so it is re-read whenever the stats tab is
   entered and dropped when the tab is left. */
static void RefreshPowerIfNeeded(void) {
    if (g_iActiveTab != PB_TAB_STATS) {
        g_bPowerValid = FALSE;
        return;
    }

    if (!g_bPowerValid) {
        RsQueryPower(&g_stPower);
        g_bPowerValid = TRUE;
    }
}

/* Draw the whole shell into the frame buffer and push the result into the
   DIB. Safe to call when the graph is not ready yet. */
static void RedrawScreen(void) {
    if (!g_bGraphReady) {
        return;
    }

    RefreshClockIfNeeded();
    RefreshPowerIfNeeded();

    /* The walk cycle restarts every time the stats tab is entered, so the
       animation always begins at the first frame. */
    if (g_iActiveTab == PB_TAB_STATS) {
        if (!g_bAnimActive) {
            g_dwAnimBase  = GetTickCount();
            g_bAnimActive = TRUE;
        }
    }
    else {
        g_bAnimActive = FALSE;
    }

    PbDrawScreen(&g_rsGraph, g_iActiveTab,
                 g_bClockValid ? &g_stClock : NULL,
                 g_bSysInfoValid ? &g_stSysInfo : NULL,
                 g_bPowerValid ? &g_stPower : NULL,
                 g_bMemValid ? &g_stMemStatus : NULL,
                 (GetTickCount() - g_dwBootTick) / 1000,
                 g_bAnimActive ? (GetTickCount() - g_dwAnimBase) : 0);
    RsGraphFlip(&g_rsGraph);
}

/* Arm or disarm the timers for the current tab. Only one tab needs a timer at
   a time, and nothing needs one while the window is minimised. */
static void SyncTimers(HWND hWnd) {
    const BOOL bWantClock = (!g_bMinimised && g_iActiveTab == PB_TAB_CLOCK);
    const BOOL bWantAnim  = (!g_bMinimised && g_iActiveTab == PB_TAB_STATS);

    if (bWantClock) {
        UINT uiDelay = 1000;

        /* Aim at the next whole second so the display never lags the system
           clock, and fall back to a plain second while the snapshot is empty. */
        if (g_bClockValid) {
            uiDelay = 1000 - g_stClock.wMilliseconds;
            if (uiDelay < 100) {
                uiDelay = 100;
            }
        }
        SetTimer(hWnd, RS_TIMER_CLOCK, uiDelay, NULL);
    }
    else {
        KillTimer(hWnd, RS_TIMER_CLOCK);
        if (!g_bMinimised) {
            g_bClockValid = FALSE;
        }
    }

    if (bWantAnim) {
        SetTimer(hWnd, RS_TIMER_ANIM, PB_WALK_FRAME_MS, NULL);
    }
    else {
        KillTimer(hWnd, RS_TIMER_ANIM);
        g_bAnimActive = FALSE;
    }
}

/* Change tab, then bring the timers and the picture in line. */
static void SwitchTab(HWND hWnd, int iTab) {
    g_iActiveTab = iTab;
    g_bAnimActive = FALSE;      /* the next redraw restarts the walk cycle */
    SyncTimers(hWnd);
    RedrawScreen();
    InvalidateRect(hWnd, NULL, FALSE);
}

/* Direction down while in a portrait orientation mirrors the picture. The
   physical DIB size does not change between the two portrait rotations, so
   the memory DC can be kept. Landscape ignores this key. */
static void TogglePortraitOrientation(void) {
    int iNew;

    if (!g_bGraphReady || g_iOrientation == ORIENTATION_LANDSCAPE) {
        return;
    }

    iNew = (g_iOrientation == ORIENTATION_PORTRAIT_L)
         ? ORIENTATION_PORTRAIT_R
         : ORIENTATION_PORTRAIT_L;

    if (RsGraphSetOrientation(&g_rsGraph, iNew)) {
        g_iOrientation = iNew;
    }
}

/* Build the graph, draw the shell and push it into the DIB. */
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

    /* hdcMainWin was only needed to derive the memory DC and is invalid now.
       Switching between the two portrait rotations only rebinds fnFlip, which
       never touches hdcMainWin, so it stays NULL on purpose. */
    g_rsGraph.hdcMainWin = NULL;

    /* System information is static for the lifetime of the process. */
    GetSystemInfo(&g_stSysInfo);
    g_bSysInfoValid = TRUE;

    memset(&g_stMemStatus, 0, sizeof(g_stMemStatus));
    g_stMemStatus.dwLength = sizeof(g_stMemStatus);
    GlobalMemoryStatus(&g_stMemStatus);
    g_bMemValid = TRUE;

    /* Tick count wraps after about 49 days, which is fine for an uptime
       display: the wrap only makes the figure jump back to zero. */
    g_dwBootTick = GetTickCount();

    g_bGraphReady = TRUE;
    RedrawScreen();

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

#if (IS_WIN_CE)
    {
        dwStyle     = WS_VISIBLE;
        iWinWidth   = GetSystemMetrics(SM_CXSCREEN);
        iWinHeight  = GetSystemMetrics(SM_CYSCREEN);
    }
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
    SetTaskbarVisible(FALSE);
    UpdateWindow(hWnd);
    CenterWindow(hWnd);

#if (IS_WIN_CE)
    /* The device decides the orientation, there is no command line there */
    g_iOrientation = DetectCeOrientation(hWnd);
#endif

    /* The graph needs the final orientation, so build it here rather than in
       WM_CREATE. */
    if (InitGraph(hWnd)) {
        InvalidateRect(hWnd, NULL, FALSE);
        SyncTimers(hWnd);
    }

    return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    HDC hdc;
    int wmId;
    PAINTSTRUCT ps;
    RECT rectClient;

    switch (message) {
        case WM_COMMAND:
            wmId = LOWORD(wParam);
            switch (wmId) {
                case IDM_FILE_EXIT:
                    DestroyWindow(hWnd);
                    return 0;
                default:
                    return DefWindowProc(hWnd, message, wParam, lParam);
            }
        case WM_KEYDOWN:
            switch (wParam) {
                case VK_ESCAPE:
                case VK_RETURN:
                    KillTimer(hWnd, RS_TIMER_CLOCK);
                    KillTimer(hWnd, RS_TIMER_ANIM);
                    DestroyWindow(hWnd);
                    break;
                case VK_UP:
                    SwitchTab(hWnd, PbNextTab(g_iActiveTab));
                    break;
                case VK_DOWN:
                    /* Auto repeat would flip the picture dozens of times per
                       second, so only react to the initial key press. */
                    if (!(lParam & 0x40000000)) {
                        TogglePortraitOrientation();
                        RedrawScreen();
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                    break;
                default: {
                    const int iTab = PbTabFromKey((int)wParam);

                    if (iTab >= 0 && iTab != g_iActiveTab) {
                        SwitchTab(hWnd, iTab);
                    }
                    break;
                }
            }
            break;
        case WM_TIMER:
            if (wParam == RS_TIMER_CLOCK) {
                SYSTEMTIME stNow;

                GetLocalTime(&stNow);

                /* Only repaint when the second really changed; a timer may
                   fire a few milliseconds early. Either way the timer is
                   rearmed, so a skipped tick never accumulates. */
                if (!g_bClockValid || stNow.wSecond != g_stClock.wSecond) {
                    g_stClock     = stNow;
                    g_bClockValid = TRUE;
                    RedrawScreen();
                    InvalidateRect(hWnd, NULL, FALSE);
                }

                SyncTimers(hWnd);
            }
            else if (wParam == RS_TIMER_ANIM) {
                RedrawScreen();
                InvalidateRect(hWnd, NULL, FALSE);
            }
            break;
        case WM_SIZE:
            /* Nothing needs to animate or tick while minimised. */
            if (wParam == SIZE_MINIMIZED) {
                if (!g_bMinimised) {
                    g_bMinimised = TRUE;
                    SyncTimers(hWnd);
                }
            }
            else if (g_bMinimised) {
                g_bMinimised = FALSE;
                SyncTimers(hWnd);
                RedrawScreen();
                InvalidateRect(hWnd, NULL, FALSE);
            }
            break;
        case WM_CREATE:
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
            KillTimer(hWnd, RS_TIMER_CLOCK);
            KillTimer(hWnd, RS_TIMER_ANIM);
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
#if (IS_WIN_CE)
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
