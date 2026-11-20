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
    int         iColorMode;     /* GRAPH_GRAYSCALE or GRAPH_VGA_COLOR   */
    int         iOrientation;   /* one of ORIENTATION_*                 */
    int         iWidth;         /* logical width  in pixels             */
    int         iHeight;        /* logical height in pixels             */
    BYTE *      pFrameBuf;      /* one byte per pixel, value 0..3       */
    BYTE        bColors[4];     /* frame buffer value -> palette index  */

    HDC         hdcMainWin;     /* window DC the memory DC is derived from  */

    HDC         hdcMem;         /* memory DC holding the DIB section        */
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

/* Fill a rectangle with one grey level. Rectangles are clipped to the frame
   buffer, so negative coordinates and oversized sizes are safe. */
void RsGraphFillRect(RSGRAPH* pRsGraph, int iX, int iY, int iW, int iH, BYTE bColor);

/* One pixel thick horizontal and vertical lines. */
void RsGraphHLine(RSGRAPH* pRsGraph, int iX, int iY, int iW, BYTE bColor);
void RsGraphVLine(RSGRAPH* pRsGraph, int iX, int iY, int iH, BYTE bColor);

/* One pixel thick rectangle outline. */
void RsGraphFrame(RSGRAPH* pRsGraph, int iX, int iY, int iW, int iH, BYTE bColor);

/* Read the width and height out of a packed 1 bpp image header. The header is
   two bytes, width first, and every image in assets.h starts with it.
   Returns FALSE when pImage is NULL. */
BOOL RsGraphImageSize(const unsigned char* pImage, int* piWidth, int* piHeight);

/* Draw a packed 1 bpp image with its top left corner at iX, iY. The data
   starts with the two header bytes described above, followed by rows packed
   MSB first and stored top down, so the row stride is (width + 7) / 8 bytes.
   Only set bits are written, the background shows through everywhere else.
   The image is clipped to the frame buffer. */
void RsGraphDrawImage(RSGRAPH* pRsGraph, int iX, int iY,
                      const unsigned char* pImage, BYTE bColor);

/* Change the orientation at run time. Portrait left and right share the same
   physical DIB size, so this only rebinds fnFlip. Returns FALSE when the new
   orientation needs a different DIB size, which means landscape <-> portrait;
   in that case the graph is left untouched and the DC has to be recreated.
   Note: RsGraphInit stores the window DC used to derive the memory DC, but
   only RsGraphCreateDC needs it, so switching between the two portrait
   orientations works even after the caller released that DC. */
BOOL RsGraphSetOrientation(RSGRAPH* pRsGraph, int iOrientation);

/* Cell size of the built in 6x8 bitmap font, in pixels. */
#define RS_FONT_WIDTH       6
#define RS_FONT_HEIGHT      8

/* Draw one character of the built in font at iX, iY, in logical coordinates.
   The glyph box is RS_FONT_WIDTH by RS_FONT_HEIGHT, only the set pixels are
   written so the background shows through. Every byte value has a glyph, so
   there is no such thing as an unknown character. */
void RsGraphDrawChar(RSGRAPH* pRsGraph, int iX, int iY,
                     unsigned char bChar, BYTE bColor);

/* Draw a NUL terminated narrow string, one glyph every RS_FONT_WIDTH pixels,
   and return the x position just after the last glyph. This takes narrow
   bytes on purpose: even on a UNICODE build the font maps bytes, so pass a
   plain "text" literal, not a TEXT("text") one. */
int RsGraphDrawText(RSGRAPH* pRsGraph, int iX, int iY,
                    const unsigned char* pText, BYTE bColor);

/* Same as RsGraphDrawChar but every glyph pixel is expanded into an iScale by
   iScale block, which gives 2x and 3x headline text from the same 6x8 font.
   An iScale below 1 is treated as 1. */
void RsGraphDrawCharScaled(RSGRAPH* pRsGraph, int iX, int iY,
                           unsigned char bChar, BYTE bColor, int iScale);

/* Scaled text: one glyph cell every RS_FONT_WIDTH * iScale pixels, advancing
   the same way as RsGraphDrawText. Returns the x position after the last
   glyph. */
int RsGraphDrawTextScaled(RSGRAPH* pRsGraph, int iX, int iY,
                          const unsigned char* pText, BYTE bColor, int iScale);

#endif

