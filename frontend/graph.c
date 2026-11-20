#include <stdlib.h>
#include <string.h>
#include "rshell.h"
#include "vga_palette.h"
#include "hybird_6x8.h"

/* Number of bytes one DIB row occupies. Rows are DWORD aligned. */
static int RsGraphStride(int iWidth, int iBitCount) {
    return ((iWidth * iBitCount + 31) / 32) * 4;
}

/* -------------------------------------------------------------------------
   8 bit flips: one destination byte per pixel, the frame buffer value is
   mapped to a palette index through bColors.
   ------------------------------------------------------------------------- */

static void RsFlip8Landscape(RSGRAPH* pRsGraph) {
    int x, y;

    for (y = 0; y < pRsGraph->iHeight; ++y) {
        const BYTE* pSrc = pRsGraph->pFrameBuf + (size_t)y * pRsGraph->iWidth;
        BYTE*       pDst = pRsGraph->pBmpPixels + (size_t)y * pRsGraph->iBmpStride;

        for (x = 0; x < pRsGraph->iWidth; ++x) {
            pDst[x] = pRsGraph->bColors[pSrc[x] & 3];
        }
    }
}

/* Logical (x, y) becomes physical (y, iWidth - 1 - x). */
static void RsFlip8PortraitL(RSGRAPH* pRsGraph) {
    const int iWidth = pRsGraph->iWidth;
    int x, y;

    for (x = 0; x < iWidth; ++x) {
        BYTE* pDst = pRsGraph->pBmpPixels + (size_t)(iWidth - 1 - x) * pRsGraph->iBmpStride;

        for (y = 0; y < pRsGraph->iHeight; ++y) {
            pDst[y] = pRsGraph->bColors[pRsGraph->pFrameBuf[(size_t)y * iWidth + x] & 3];
        }
    }
}

/* Logical (x, y) becomes physical (iHeight - 1 - y, x). */
static void RsFlip8PortraitR(RSGRAPH* pRsGraph) {
    const int iWidth = pRsGraph->iWidth, iHeight = pRsGraph->iHeight;
    int x, y;

    for (x = 0; x < iWidth; ++x) {
        BYTE* pDst = pRsGraph->pBmpPixels + (size_t)x * pRsGraph->iBmpStride;

        for (y = 0; y < iHeight; ++y) {
            pDst[iHeight - 1 - y] =
                pRsGraph->bColors[pRsGraph->pFrameBuf[(size_t)y * iWidth + x] & 3];
        }
    }
}

/* -------------------------------------------------------------------------
   2 bit flips: four pixels share one byte, the leftmost pixel lives in the
   low bits. The destination is cleared once per flip: portrait writes only
   touch part of a destination byte, and reusing a dirty buffer would let
   old bits survive a flip.
   ------------------------------------------------------------------------- */

static void RsFlip2Landscape(RSGRAPH* pRsGraph) {
    int x, y;

    memset(pRsGraph->pBmpPixels, 0, (size_t)pRsGraph->iBmpStride * pRsGraph->iBmpHeight);

    for (y = 0; y < pRsGraph->iHeight; ++y) {
        const BYTE* pSrc = pRsGraph->pFrameBuf + (size_t)y * pRsGraph->iWidth;
        BYTE*       pDst = pRsGraph->pBmpPixels + (size_t)y * pRsGraph->iBmpStride;

        for (x = 0; x < pRsGraph->iWidth; ++x) {
            pDst[x >> 2] |= (BYTE)((pSrc[x] & 3) << ((x & 3) * 2));
        }
    }
}

static void RsFlip2PortraitL(RSGRAPH* pRsGraph) {
    const int iWidth = pRsGraph->iWidth;
    int x, y;

    memset(pRsGraph->pBmpPixels, 0, (size_t)pRsGraph->iBmpStride * pRsGraph->iBmpHeight);

    for (x = 0; x < iWidth; ++x) {
        BYTE* pDst = pRsGraph->pBmpPixels + (size_t)(iWidth - 1 - x) * pRsGraph->iBmpStride;

        for (y = 0; y < pRsGraph->iHeight; ++y) {
            pDst[y >> 2] |= (BYTE)((pRsGraph->pFrameBuf[(size_t)y * iWidth + x] & 3)
                                   << ((y & 3) * 2));
        }
    }
}

static void RsFlip2PortraitR(RSGRAPH* pRsGraph) {
    const int iWidth = pRsGraph->iWidth, iHeight = pRsGraph->iHeight;
    int x, y;

    memset(pRsGraph->pBmpPixels, 0, (size_t)pRsGraph->iBmpStride * pRsGraph->iBmpHeight);

    for (x = 0; x < iWidth; ++x) {
        BYTE* pDst = pRsGraph->pBmpPixels + (size_t)x * pRsGraph->iBmpStride;

        for (y = 0; y < iHeight; ++y) {
            pDst[(iHeight - 1 - y) >> 2] |=
                (BYTE)((pRsGraph->pFrameBuf[(size_t)y * iWidth + x] & 3)
                       << (((iHeight - 1 - y) & 3) * 2));
        }
    }
}

/* -------------------------------------------------------------------------
   Device and graph life cycle.
   ------------------------------------------------------------------------- */

void GetDeviceInfo(HDC hdc, RSDEVICEINFO* pDeviceInfo) {
    HBITMAP         hBitmap;
    OSVERSIONINFO   osInfo;
    BITMAP          bitmap;

    pDeviceInfo->dwMajorVersion = 0;
    pDeviceInfo->dwMinorVersion = 0;
    pDeviceInfo->bmBitsPixel    = 0;

    /* Test bitmap */
    hBitmap = CreateCompatibleBitmap(hdc, 8, 8);
    if (hBitmap) {
        if (GetObject(hBitmap, sizeof(BITMAP), &bitmap)) {
            pDeviceInfo->bmBitsPixel = bitmap.bmBitsPixel;
        }
        DeleteObject(hBitmap);
    }

    /* Get version info */
    memset(&osInfo, 0, sizeof(osInfo));
    osInfo.dwOSVersionInfoSize = sizeof(osInfo);
    if (GetVersionEx(&osInfo)) {
        pDeviceInfo->dwMajorVersion = osInfo.dwMajorVersion;
        pDeviceInfo->dwMinorVersion = osInfo.dwMinorVersion;
    }
}

BOOL RsGraphInit(RSGRAPH* pRsGraph, HDC hdc, int iWidth, int iHeight,
                 int iOrientation, const RSDEVICEINFO* pDeviceInfo) {
    size_t iFrameBufferSize;

    if (!pRsGraph || !pDeviceInfo || iWidth <= 0 || iHeight <= 0) {
        return FALSE;
    }

    memset(pRsGraph, 0, sizeof(RSGRAPH));

    pRsGraph->iWidth       = iWidth;
    pRsGraph->iHeight      = iHeight;
    pRsGraph->hdcMainWin   = hdc;
    pRsGraph->iOrientation = iOrientation;

    /* Reject unknown orientations instead of silently misplacing pixels */
    if (iOrientation != ORIENTATION_LANDSCAPE &&
        iOrientation != ORIENTATION_PORTRAIT_L &&
        iOrientation != ORIENTATION_PORTRAIT_R) {
        pRsGraph->iOrientation = RS_DEFAULT_ORIENTATION;
    }

    /* Check color mode.
       Desktop GDI cannot create a 2 bit DIB section at all, so that mode is
       reserved for old Windows CE devices which really do have a 2 bit
       display. */
#if IS_WIN_CE
    if (pDeviceInfo->dwMajorVersion < 2 || pDeviceInfo->bmBitsPixel <= 2) {
        pRsGraph->iColorMode = GRAPH_GRAYSCALE;
    }
    else {
        pRsGraph->iColorMode = GRAPH_VGA_COLOR;
    }
#else
    pRsGraph->iColorMode = GRAPH_VGA_COLOR;
#endif

    /* Create frame buffer, one byte per pixel */
    iFrameBufferSize = (size_t)iWidth * (size_t)iHeight;
    pRsGraph->pFrameBuf = (BYTE *)malloc(iFrameBufferSize);
    if (!pRsGraph->pFrameBuf) {
        return FALSE;
    }
    memset(pRsGraph->pFrameBuf, 0, iFrameBufferSize);

    /* Frame buffer value -> VGA palette index, inverted Matrix theme: a black
       background with a green ramp, so RS_GRAY_BLACK is the darkest and
       RS_GRAY_WHITE the brightest green. Only the 8 bit mode uses this map;
       the 2 bit mode keeps its fixed grey palette. */
    pRsGraph->bColors[RS_GRAY_BLACK] = 0x10;   /* 0x000000 black      */
    pRsGraph->bColors[RS_GRAY_DARK]  = 0x78;   /* 0x007100 dark green */
    pRsGraph->bColors[RS_GRAY_LIGHT] = 0x60;   /* 0xBAFFBA pale green */
    pRsGraph->bColors[RS_GRAY_WHITE] = 0x30;   /* 0x00FF00 green      */

    return TRUE;
}

BOOL RsGraphCreateDC(RSGRAPH* pRsGraph) {
    int             iPaletteSize        = 0;
    BITMAPINFO*     pbmi                = NULL;
    int             iSizeWithPalette    = 0;
    int             iBitCount           = 0;
    int             i;
    const int       iWidth  = pRsGraph->iWidth;
    const int       iHeight = pRsGraph->iHeight;

    switch (pRsGraph->iColorMode) {
        /* 2 Bit : grayscale */
        case GRAPH_GRAYSCALE: {
            iPaletteSize = 4;
            iBitCount    = 2;
            break;
        }
        /* 8 Bit: VGA Color */
        case GRAPH_VGA_COLOR: {
            iPaletteSize = 256;
            iBitCount    = 8;
            break;
        }
        default: {
            return FALSE;
        }
    }

    /* Physical size of the DIB follows the orientation, the frame buffer
       always stays in logical orientation. */
    if (pRsGraph->iOrientation == ORIENTATION_LANDSCAPE) {
        pRsGraph->iBmpWidth  = iWidth;
        pRsGraph->iBmpHeight = iHeight;
    }
    else {
        pRsGraph->iBmpWidth  = iHeight;
        pRsGraph->iBmpHeight = iWidth;
    }
    pRsGraph->iBmpStride = RsGraphStride(pRsGraph->iBmpWidth, iBitCount);

    /* Header size to allocate */
    iSizeWithPalette = sizeof(BITMAPINFO) + iPaletteSize * sizeof(RGBQUAD);

    /* Create memory DC */
    pRsGraph->hdcMem = CreateCompatibleDC(pRsGraph->hdcMainWin);
    if (!pRsGraph->hdcMem) {
        return FALSE;
    }

    /* Create header */
    pbmi = (BITMAPINFO *)malloc(iSizeWithPalette);
    if (!pbmi) {
        DeleteDC(pRsGraph->hdcMem);
        pRsGraph->hdcMem = NULL;
        return FALSE;
    }
    memset(pbmi, 0, iSizeWithPalette);

    /* Write palette */
    switch (pRsGraph->iColorMode) {
        /* 2 BIT MODE: 4 colors in palette */
        case GRAPH_GRAYSCALE: {
            static const BYTE grayscale[] = { 0x00, 0x80, 0xc0, 0xff };
            /* Write palette */
            for (i = 0; i < iPaletteSize; ++i) {
                pbmi->bmiColors[i].rgbRed       = grayscale[i];
                pbmi->bmiColors[i].rgbGreen     = grayscale[i];
                pbmi->bmiColors[i].rgbBlue      = grayscale[i];
                pbmi->bmiColors[i].rgbReserved  = 0;
            }
            break;
        }
        /* 8 BIT MODE: VGA colors */
        case GRAPH_VGA_COLOR: {
            for (i = 0; i < iPaletteSize; ++i) {
                const DWORD color = VGA_MODE13H_COLORPALETTE[i];
                pbmi->bmiColors[i].rgbRed       = (BYTE)((color & 0xff0000) >> 16);
                pbmi->bmiColors[i].rgbGreen     = (BYTE)((color & 0x00ff00) >> 8);
                pbmi->bmiColors[i].rgbBlue      = (BYTE)((color & 0x0000ff));
                pbmi->bmiColors[i].rgbReserved  = 0;
            }
            break;
        }
    }

    pbmi->bmiHeader.biSize          = sizeof(BITMAPINFOHEADER);
    pbmi->bmiHeader.biWidth         = pRsGraph->iBmpWidth;
    pbmi->bmiHeader.biHeight        = -pRsGraph->iBmpHeight; /* from top to bottom */
    pbmi->bmiHeader.biPlanes        = 1;
    pbmi->bmiHeader.biBitCount      = (WORD)iBitCount;
    pbmi->bmiHeader.biSizeImage     = 0;
    pbmi->bmiHeader.biCompression   = 0;
    pbmi->bmiHeader.biXPelsPerMeter = 0;
    pbmi->bmiHeader.biYPelsPerMeter = 0;
    pbmi->bmiHeader.biClrImportant  = 0;
    /* Note: when creating 2bit DIBsection: biClrUsed should be set to 0 */
    pbmi->bmiHeader.biClrUsed       = iBitCount == 2 ? 0 : iPaletteSize;

    pRsGraph->hBmpBuf = CreateDIBSection(
        pRsGraph->hdcMem,
        pbmi,
        DIB_RGB_COLORS,
        (void**)&(pRsGraph->pBmpPixels),
        NULL,
        0
    );

    if (!pRsGraph->hBmpBuf || !pRsGraph->pBmpPixels) {
        DeleteDC(pRsGraph->hdcMem);
        pRsGraph->hdcMem = NULL;
        pRsGraph->hBmpBuf = NULL;
        pRsGraph->pBmpPixels = NULL;
        free(pbmi);
        return FALSE;
    }

    /* Save old bitmap */
    pRsGraph->hBmpOld = SelectObject(pRsGraph->hdcMem, pRsGraph->hBmpBuf);

    free(pbmi);

    /* Select flip function */
    if (pRsGraph->iColorMode == GRAPH_GRAYSCALE) {
        switch (pRsGraph->iOrientation) {
            case ORIENTATION_PORTRAIT_L: pRsGraph->fnFlip = RsFlip2PortraitL; break;
            case ORIENTATION_PORTRAIT_R: pRsGraph->fnFlip = RsFlip2PortraitR; break;
            default:                     pRsGraph->fnFlip = RsFlip2Landscape; break;
        }
    }
    else {
        switch (pRsGraph->iOrientation) {
            case ORIENTATION_PORTRAIT_L: pRsGraph->fnFlip = RsFlip8PortraitL; break;
            case ORIENTATION_PORTRAIT_R: pRsGraph->fnFlip = RsFlip8PortraitR; break;
            default:                     pRsGraph->fnFlip = RsFlip8Landscape; break;
        }
    }

    return TRUE;
}

void RsGraphDeleteDC(RSGRAPH* pRsGraph) {
    if (!pRsGraph) {
        return;
    }

    if (pRsGraph->hdcMem) {
        if (pRsGraph->hBmpOld) {
            SelectObject(pRsGraph->hdcMem, pRsGraph->hBmpOld);
        }

        /* The bitmap must leave the DC before it can be deleted. */
        if (pRsGraph->hBmpBuf) {
            DeleteObject(pRsGraph->hBmpBuf);
        }

        DeleteDC(pRsGraph->hdcMem);
    }
    else if (pRsGraph->hBmpBuf) {
        DeleteObject(pRsGraph->hBmpBuf);
    }

    pRsGraph->hdcMem     = NULL;
    pRsGraph->hBmpBuf    = NULL;
    pRsGraph->pBmpPixels = NULL;
    pRsGraph->hBmpOld    = NULL;
    pRsGraph->fnFlip     = NULL;
}

void RsGraphDispose(RSGRAPH* pRsGraph) {
    if (!pRsGraph) {
        return;
    }

    RsGraphDeleteDC(pRsGraph);

    if (pRsGraph->pFrameBuf) {
        free(pRsGraph->pFrameBuf);
        pRsGraph->pFrameBuf = NULL;
    }

    memset(pRsGraph, 0, sizeof(RSGRAPH));
}

void RsGraphSetPixel(RSGRAPH* pRsGraph, int iX, int iY, BYTE bColor) {
    if (!pRsGraph || !pRsGraph->pFrameBuf) {
        return;
    }
    if (iX < 0 || iX >= pRsGraph->iWidth || iY < 0 || iY >= pRsGraph->iHeight) {
        return;
    }

    pRsGraph->pFrameBuf[(size_t)iY * pRsGraph->iWidth + iX] = (BYTE)(bColor & 3);
}

void RsGraphFlip(RSGRAPH* pRsGraph) {
    if (!pRsGraph || !pRsGraph->fnFlip || !pRsGraph->pBmpPixels) {
        return;
    }

    pRsGraph->fnFlip(pRsGraph);
}

/* -------------------------------------------------------------------------
   Text drawing with the built in 6x8 bitmap font.
   ------------------------------------------------------------------------- */

void RsGraphDrawChar(RSGRAPH* pRsGraph, int iX, int iY,
                     unsigned char bChar, BYTE bColor) {
    const unsigned char* pGlyph = FONT_HYBIRD_6x8 + (size_t)bChar * RS_FONT_HEIGHT;
    int x, y;

    if (!pRsGraph) {
        return;
    }

    /* Bit 7 is the leftmost pixel, the glyphs only use the top six bits. */
    for (y = 0; y < RS_FONT_HEIGHT; ++y) {
        const unsigned char bRow = pGlyph[y];

        for (x = 0; x < RS_FONT_WIDTH; ++x) {
            if (bRow & (0x80 >> x)) {
                RsGraphSetPixel(pRsGraph, iX + x, iY + y, bColor);
            }
        }
    }
}

int RsGraphDrawText(RSGRAPH* pRsGraph, int iX, int iY,
                    const unsigned char* pText, BYTE bColor) {
    int x = iX;

    if (!pRsGraph || !pText) {
        return iX;
    }

    while (*pText) {
        RsGraphDrawChar(pRsGraph, x, iY, *pText, bColor);
        x += RS_FONT_WIDTH;
        ++pText;
    }

    return x;
}

void RsGraphPresent(RSGRAPH* pRsGraph, HDC hdc, int iX, int iY) {
    if (!pRsGraph || !pRsGraph->hdcMem || !hdc) {
        return;
    }

    BitBlt(hdc, iX, iY, pRsGraph->iBmpWidth, pRsGraph->iBmpHeight,
           pRsGraph->hdcMem, 0, 0, SRCCOPY);
}
