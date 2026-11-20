#include <windows.h>
#include <commctrl.h>
#include "resource.h"
#include "rshell.h"

ATOM                MyRegisterClass     (HINSTANCE hInstance, LPTSTR szWindowClass);
BOOL                InitInstance        (HINSTANCE, int);
LRESULT CALLBACK    WndProc             (HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK    About               (HWND, UINT, WPARAM, LPARAM);

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
    TCHAR    szTitle[]          = TEXT("RadiativeShell");
    TCHAR    szWindowClass[]    = TEXT("RS_Main_Win");

    hInst = hInstance;

    MyRegisterClass(hInstance, szWindowClass);

    hWnd = CreateWindow(szWindowClass, szTitle, WS_VISIBLE,
        0, 0, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hInstance, NULL);

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
    TCHAR szHello[] = TEXT("HELLO");

    switch (message) {
        case WM_COMMAND:
            wmId    = LOWORD(wParam); 
            wmEvent = HIWORD(wParam); 
            switch (wmId) {
                default:
                   return DefWindowProc(hWnd, message, wParam, lParam);
            }
            break;
        case WM_CREATE:
            break;
        case WM_PAINT:
            RECT rt;
            hdc = BeginPaint(hWnd, &ps);
            GetClientRect(hWnd, &rt);
            DrawText(hdc, szHello, _tcslen(szHello), &rt, 
                DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            EndPaint(hWnd, &ps);
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
   }
   return 0;
}
