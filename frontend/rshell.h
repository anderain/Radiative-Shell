#ifndef _RSHELL_H_
#define _RSHELL_H_

#include <windows.h>

#ifdef VER_PLATFORM_WIN32_CE
#   define IS_WIN_CE    1
#else
#   define IS_WIN_CE    0
#endif

typedef struct _RSDEVICEINFO {
  DWORD     dwMajorVersion;
  DWORD     dwMinorVersion;
  WORD      bmBitsPixel;
} RSDEVICEINFO;

void GetDeviceInfo(HDC hdc, RSDEVICEINFO* pDeviceInfo);

#define GRAPH_GRAYSCALE         1
#define GRAPH_VGA_COLOR         2

#define ORIENTATION_LANDSCAPE   0
#define ORIENTATION_PORTRAIT_L  1
#define ORIENTATION_PORTRAIT_R  2

typedef struct _RSGRAPH {
    int         iColorMode;
    int         iOrientation;
    int         iWidth;
    int         iHeight;
    BYTE *      pFrameBuf;

    HDC         hdcMainWin;

    HDC         hdcMem;
    HBITMAP     hBmpBuf;
    BYTE *      pBmpPixels;
    HBITMAP     hBmpOld;

    void        (*fnFlip)();
} RSGRAPH;

#endif