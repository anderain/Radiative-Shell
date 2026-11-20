#include "rshell.h"
#include "vga_palette.h"

void GetDeviceInfo(HDC hdc, RSDEVICEINFO* pDeviceInfo) {
    HBITMAP         hBitmap;
    OSVERSIONINFO   osInfo;
    BITMAP          bitmap;

    /* Test bitmap */
    hBitmap = CreateCompatibleBitmap(hdc, 8, 8);
    GetObject(hBitmap, sizeof(BITMAP), &bitmap);
    DeleteObject(hBitmap);

    /* Get version info */
    GetVersionEx(&osInfo);

    pDeviceInfo->dwMajorVersion = osInfo.dwMajorVersion;
    pDeviceInfo->dwMinorVersion = osInfo.dwMinorVersion;
    pDeviceInfo->bmBitsPixel = bitmap.bmBitsPixel;
}

BOOL RsGraphInit(RSGRAPH* pRsGraph, HDC hdc, int iWidth, int iHeight, int iOrientation, const RSDEVICEINFO* pDeviceInfo) {
    int iFrameBufferSize = iWidth * iHeight;

    memset(pRsGraph, 0, sizeof(RSGRAPH));

    pRsGraph->iWidth = iWidth;
    pRsGraph->iHeight = iHeight;
    pRsGraph->hdcMainWin = hdc;

    /* Check color mode */
    if (pDeviceInfo->dwMajorVersion < 2 || pDeviceInfo->bmBitsPixel <= 2) {
        pRsGraph->iColorMode = GRAPH_GRAYSCALE;
    }
    else {
        pRsGraph->iColorMode = GRAPH_VGA_COLOR;
    }

    /* Create frame buffer */
    pRsGraph->pFrameBuf = (BYTE *)malloc(iFrameBufferSize);
    if (!pRsGraph->pFrameBuf) {
        return FALSE;
    }

    return TRUE;
}

BOOL RsGraphCreate(RSGRAPH* pRsGraph) {
    int             iPaletteSize        = 0;
    int             iPixelSize          = 0;
    BITMAPINFO*     pbmi                = NULL;
    int             iSizeWithPalette    = 0;
    int             iBitCount;
    int             i;
    const int       iWidth = pRsGraph->iWidth, iHeight = pRsGraph->iHeight;

    switch (pRsGraph->iColorMode) {
        /* 2 Bit : grapyscale */
        case GRAPH_GRAYSCALE: {
            iPaletteSize = 4;
            iPixelSize = iWidth / 4 * iHeight;   /* 4 PIXEL = 1 BYTE */
            iBitCount = 2;
            break;
        }
        /* 8 Bit: VGA Color  */
        case GRAPH_VGA_COLOR: {
            iPaletteSize = 256;
            iPixelSize = iWidth * 1 * iHeight; /* 1 BYTE = 8 BITS */
            iBitCount = 8;
            break;
        }
    }
    
    /* Header size to allocate */
    iSizeWithPalette = sizeof(BITMAPINFO) + iPaletteSize * sizeof(RGBQUAD);

    /* Create memory DC */
    pRsGraph->hdcMem = CreateCompatibleDC(pRsGraph->hdcMainWin);
    
    /* Create header */
    pbmi = (BITMAPINFO *)malloc(iSizeWithPalette);
    memset(pbmi, 0, iSizeWithPalette);

    /* Write palette */
    switch (pRsGraph->iColorMode) {
        /* 2 BIT MODE: 4 colors in palette */
        case GRAPH_GRAYSCALE: {
            // Grayscale color:
            static const BYTE grayscale[] = { 0x00, 0x80, 0xc0, 0xff };
            // Write palette
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
    pbmi->bmiHeader.biWidth         = pRsGraph->iOrientation != ORIENTATION_LANDSCAPE ? iWidth : iHeight;
    pbmi->bmiHeader.biHeight        = pRsGraph->iOrientation != ORIENTATION_LANDSCAPE ? -iHeight: -iWidth; /* from top to bottom */
    pbmi->bmiHeader.biPlanes        = 1;
    pbmi->bmiHeader.biBitCount      = iBitCount;
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
        free(pbmi);
        return FALSE;
    }
    
    /* Save old bitmap */
    pRsGraph->hBmpOld = SelectObject(pRsGraph->hdcMem, pRsGraph->hBmpBuf);

    free(pbmi);

    /* Select flip function */

    return TRUE;
}
