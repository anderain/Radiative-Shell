#include <windows.h>
#include <commctrl.h>
#include <tchar.h>
#include "resource.h"

HINSTANCE        	hInst;
HWND                hwndCB;

ATOM                MyRegisterClass	(HINSTANCE hInstance, LPTSTR szWindowClass);
BOOL                InitInstance	(HINSTANCE, int);
LRESULT CALLBACK    WndProc			(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK    About			(HWND, UINT, WPARAM, LPARAM);

int WINAPI WinMain (HINSTANCE hInstance,
                    HINSTANCE hPrevInstance,
                    LPTSTR    lpCmdLine,
                    int       nCmdShow) {
    MSG msg;
    HACCEL hAccelTable;

    if (!InitInstance (hInstance, nCmdShow))  {
        return FALSE;
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
    wc.hCursor        	= 0;
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
    int wmId, wmEvent;
    PAINTSTRUCT ps;
    TCHAR szHello[] = TEXT("HELLO");
    RECT rt;

    switch (message)  {
        case WM_COMMAND:
            wmId    = LOWORD(wParam); 
            wmEvent = HIWORD(wParam); 
            switch (wmId) {
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
            DrawText(hdc, szHello, _tcslen(szHello), &rt, 
                DT_SINGLELINE | DT_VCENTER | DT_CENTER);
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
