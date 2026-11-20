#ifndef _RSHELL_H_
#define _RSHELL_H_

#include <windows.h>

#ifdef VER_PLATFORM_WIN32_CE
#   define IS_WIN_CE    1
#else
#   define IS_WIN_CE    0
#endif

/* Device information gathered at run time. Used to pick the colour mode
   of the memory DC: old Windows CE devices only have a 2 bit display. */
typedef struct _RSDEVICEINFO {
  DWORD     dwMajorVersion;
  DWORD     dwMinorVersion;
  WORD      bmBitsPixel;
} RSDEVICEINFO;

/* Fill pDeviceInfo from the given device context. */
void GetDeviceInfo(HDC hdc, RSDEVICEINFO* pDeviceInfo);

/* Colour modes of the memory DC. */
#define GRAPH_GRAYSCALE         1   /* 2 bit per pixel, 4 grey levels */
#define GRAPH_VGA_COLOR         2   /* 8 bit per pixel, VGA palette  */

/* Logical orientation of the picture inside the memory DC.
   PORTRAIT_L rotates the picture counter clockwise, PORTRAIT_R clockwise. */
#define ORIENTATION_LANDSCAPE   0
#define ORIENTATION_PORTRAIT_L  1
#define ORIENTATION_PORTRAIT_R  2

/* Grey levels stored in one frame buffer byte. */
#define RS_GRAY_BLACK           0
#define RS_GRAY_DARK            1
#define RS_GRAY_LIGHT           2
#define RS_GRAY_WHITE           3

/* Orientation used when nothing else is known. */
#define RS_DEFAULT_ORIENTATION  ORIENTATION_LANDSCAPE

/* The graph is drawn in two layers:
     frame buffer  --(RsGraphFlip)-->  memory DC DIB  --(RsGraphPresent)-->  window
   The frame buffer always holds one byte per pixel, the value is 0..3 and
   describes one of four grey levels. The memory DC itself is either a 2 bit
   or an 8 bit DIB; in the 8 bit case bColors maps the frame buffer value to
   an index of the VGA palette. */
typedef struct _RSGRAPH {
    int         iColorMode;     /* GRAPH_GRAYSCALE or GRAPH_VGA_COLOR */
    int         iOrientation;   /* one of ORIENTATION_*               */
    int         iWidth;         /* logical width  in pixels           */
    int         iHeight;        /* logical height in pixels           */
    BYTE *      pFrameBuf;      /* one byte per pixel, value 0..3     */
    BYTE        bColors[4];     /* frame buffer value -> palette index */

    HDC         hdcMainWin;     /* window DC the memory DC is derived from */

    HDC         hdcMem;         /* memory DC holding the DIB section       */
    HBITMAP     hBmpBuf;        /* DIB section, owned by hdcMem             */
    BYTE *      pBmpPixels;     /* raw pixels of the DIB section            */
    HBITMAP     hBmpOld;        /* bitmap that was selected before hBmpBuf  */

    int         iBmpWidth;      /* physical DIB width, swapped on portrait  */
    int         iBmpHeight;     /* physical DIB height                      */
    int         iBmpStride;     /* bytes per physical DIB row               */

    void        (*fnFlip)(struct _RSGRAPH *pRsGraph);   /* bound by CreateDC */
} RSGRAPH;

/* Allocate the frame buffer and choose the colour mode. iOrientation is one
   of ORIENTATION_*, an invalid value falls back to landscape. */
BOOL RsGraphInit(RSGRAPH* pRsGraph, HDC hdc, int iWidth, int iHeight,
                 int iOrientation, const RSDEVICEINFO* pDeviceInfo);

/* Create the memory DC and the DIB section, then bind fnFlip for the current
   combination of colour mode and orientation. */
BOOL RsGraphCreateDC(RSGRAPH* pRsGraph);

/* Release the memory DC, the DIB section and the saved bitmap. The frame
   buffer is left untouched so the DC can be created again. */
void RsGraphDeleteDC(RSGRAPH* pRsGraph);

/* Release everything, including the frame buffer, and zero the structure. */
void RsGraphDispose(RSGRAPH* pRsGraph);

/* Write one pixel into the frame buffer. Coordinates are logical and out of
   range coordinates are silently ignored. Only the low two bits of bColor are
   used, so 0..3 selects one of the four grey levels. */
void RsGraphSetPixel(RSGRAPH* pRsGraph, int iX, int iY, BYTE bColor);

/* Copy the frame buffer into the DIB, honouring bColors and the orientation.
   This only touches the DIB, call RsGraphPresent to show the result. */
void RsGraphFlip(RSGRAPH* pRsGraph);

/* Blit the DIB to the given device context at iX, iY. */
void RsGraphPresent(RSGRAPH* pRsGraph, HDC hdc, int iX, int iY);

#endif
