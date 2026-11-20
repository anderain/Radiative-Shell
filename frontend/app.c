#include <windows.h>
#include <commctrl.h>
#include "resource.h"

ATOM                MyRegisterClass     (HINSTANCE hInstance, LPTSTR szWindowClass);
BOOL                InitInstance        (HINSTANCE, int);
LRESULT CALLBACK    WndProc             (HWND, UINT, WPARAM, LPARAM);
BOOL                SetTaskbarVisible   (BOOL bVisible);

HINSTANCE           hInst;

int WINAPI WinMain( HINSTANCE hInstance,
                    HINSTANCE hPrevInstance,
                    LPTSTR    lpCmdLine,
                    int       nCmdShow) {
    MSG msg;
    HACCEL hAccelTable;

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
    dwStyle     = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    iWinWidth   = 500;
    iWinHeight  = 300;
#endif

    hWnd = CreateWindow(szWindowClass, szTitle, dwStyle, 0, 0, iWinWidth, iWinHeight, NULL, NULL, hInstance, NULL);

    if (!hWnd) {    
        return FALSE;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    HDC hdc;
    int wmId, wmEvent;
    PAINTSTRUCT ps;

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
            EndPaint(hWnd, &ps);
            break;
        case WM_DESTROY:
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