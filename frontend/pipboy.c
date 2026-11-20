/*
   Pipboy style shell chrome.

   The layout is built from a handful of fixed bands so that the content area
   can grow on taller screens without moving the tabs or the status bar:

     +----------------------------------------+  outer frame
     | [STATS] SYSTEM  CLOCK  LAUNCHER         |  tab band
     |----------------------------------------|  rule
     | +------------------------------------+  |
     | |                                    |  |  content frame
     | |              NO DATA               |  |
     | +------------------------------------+  |
     |----------------------------------------|  rule
     | RADIATIVE OS v1.0        1-4  ESC EXIT |  status band
     +----------------------------------------+
*/

#include "pipboy.h"

static const unsigned char* const g_pbTabLabels[PB_TAB_COUNT] = {
    (const unsigned char *)"STATS",
    (const unsigned char *)"SYSTEM",
    (const unsigned char *)"CLOCK",
    (const unsigned char *)"LAUNCHER"
};

/* Width in pixels of a narrow string in the built in font. */
static int PbTextWidth(const unsigned char* pText) {
    int iLen = 0;

    while (pText[iLen]) {
        ++iLen;
    }

    return iLen * RS_FONT_WIDTH;
}

/* Draw text centred horizontally inside [iX, iX + iW). Vertical centring of
   the 8 pixel glyph box inside a band of iH pixels is done by the caller. */
static void PbDrawTextCentered(RSGRAPH* pRsGraph, int iX, int iY, int iW,
                               const unsigned char* pText, BYTE bColor) {
    int iTextW = PbTextWidth(pText);

    if (iTextW > iW) {
        iTextW = iW;
    }

    RsGraphDrawText(pRsGraph, iX + (iW - iTextW) / 2, iY, pText, bColor);
}

/* L shaped corner bracket. iDX and iDY are +1 or -1 and say which corner of
   the box is being drawn. */
static void PbDrawCorner(RSGRAPH* pRsGraph, int iX, int iY, int iDX, int iDY,
                         int iArm, BYTE bColor) {
    RsGraphHLine(pRsGraph, iDX > 0 ? iX : iX - iArm + 1, iY, iArm, bColor);
    RsGraphVLine(pRsGraph, iX, iDY > 0 ? iY : iY - iArm + 1, iArm, bColor);
}

/* Four corner brackets inset by one pixel from the given box. */
static void PbDrawCorners(RSGRAPH* pRsGraph, int iX, int iY, int iW, int iH,
                          int iArm, BYTE bColor) {
    PbDrawCorner(pRsGraph, iX + 1, iY + 1, 1, 1, iArm, bColor);
    PbDrawCorner(pRsGraph, iX + iW - 2, iY + 1, -1, 1, iArm, bColor);
    PbDrawCorner(pRsGraph, iX + 1, iY + iH - 2, 1, -1, iArm, bColor);
    PbDrawCorner(pRsGraph, iX + iW - 2, iY + iH - 2, -1, -1, iArm, bColor);
}

const unsigned char* PbTabLabel(int iTab) {
    if (iTab < 0 || iTab >= PB_TAB_COUNT) {
        return NULL;
    }

    return g_pbTabLabels[iTab];
}

/* Tab bar. Slots have a fixed width derived from the label length so that
   switching tabs never moves the other labels around. */
static void PbDrawTabBar(RSGRAPH* pRsGraph, int iActiveTab, int iRight) {
    int i, x = PB_TAB_LEFT;

    for (i = 0; i < PB_TAB_COUNT; ++i) {
        const unsigned char* pLabel = g_pbTabLabels[i];
        const int iSlotW = PbTextWidth(pLabel) + 2 * PB_TAB_TEXT_PAD_X;
        const int iTextY = PB_TAB_TOP + (PB_TAB_HEIGHT - RS_FONT_HEIGHT) / 2;

        if (x + iSlotW > iRight) {
            break;
        }

        if (i == iActiveTab) {
            /* Inverted block: bright fill, dark glyphs. */
            RsGraphFillRect(pRsGraph, x, PB_TAB_TOP, iSlotW, PB_TAB_HEIGHT, RS_GRAY_WHITE);
            RsGraphDrawText(pRsGraph, x + PB_TAB_TEXT_PAD_X, iTextY, pLabel, RS_GRAY_BLACK);
        }
        else {
            RsGraphDrawText(pRsGraph, x + PB_TAB_TEXT_PAD_X, iTextY, pLabel, RS_GRAY_LIGHT);
        }

        x += iSlotW + 2;
    }
}

/* Content frame plus the placeholder text, both vertically centred in the
   available band. */
static void PbDrawContent(RSGRAPH* pRsGraph, int iTop, int iBottom, int iLeft, int iRight) {
    const int iW = iRight - iLeft + 1;
    const int iH = iBottom - iTop + 1;
    const int iTextY = iTop + (iH - RS_FONT_HEIGHT) / 2;

    RsGraphFrame(pRsGraph, iLeft, iTop, iW, iH, RS_GRAY_LIGHT);
    PbDrawCorners(pRsGraph, iLeft, iTop, iW, iH, 5, RS_GRAY_WHITE);

    PbDrawTextCentered(pRsGraph, iLeft + 1, iTextY, iW - 2,
                       (const unsigned char *)"NO DATA", RS_GRAY_DARK);
}

/* Status bar. The right hand hint is dropped when it would collide with the
   left hand text, which is what happens on a narrow portrait screen. */
static void PbDrawStatus(RSGRAPH* pRsGraph, int iY, int iLeft, int iRight) {
    const unsigned char* pLeftText;
    const unsigned char* pRightText;
    const int iW = iRight - iLeft + 1;

    if (iW >= 300) {
        pLeftText  = (const unsigned char *)"RADIATIVE OS v1.0";
        pRightText = (const unsigned char *)"1-4 TABS  ESC EXIT";
    }
    else if (iW >= 200) {
        pLeftText  = (const unsigned char *)"RADIATIVE OS";
        pRightText = (const unsigned char *)"ESC EXIT";
    }
    else {
        pLeftText  = (const unsigned char *)"RADIATIVE OS";
        pRightText = NULL;
    }

    RsGraphDrawText(pRsGraph, iLeft, iY, pLeftText, RS_GRAY_LIGHT);

    if (pRightText) {
        const int iTextW = PbTextWidth(pRightText);
        const int iLeftW = PbTextWidth(pLeftText);

        /* Keep at least one blank cell between the two labels. */
        if (iLeftW + RS_FONT_WIDTH + iTextW <= iW) {
            RsGraphDrawText(pRsGraph, iLeft + iW - iTextW, iY, pRightText, RS_GRAY_LIGHT);
        }
    }
}

void PbDrawScreen(RSGRAPH* pRsGraph, int iActiveTab) {
    const int iW = pRsGraph->iWidth;
    const int iH = pRsGraph->iHeight;
    const int iFrameX  = PB_OUTER_MARGIN;
    const int iFrameY  = PB_OUTER_MARGIN;
    const int iFrameW  = iW - 2 * PB_OUTER_MARGIN;
    const int iFrameH  = iH - 2 * PB_OUTER_MARGIN;
    const int iInnerL  = iFrameX + 1;
    const int iInnerR  = iFrameX + iFrameW - 2;
    const int iStatusRuleY = iFrameY + iFrameH - 1 - PB_STATUS_HEIGHT - 1;
    const int iStatusTextY = iStatusRuleY + 1
                           + (PB_STATUS_HEIGHT - RS_FONT_HEIGHT) / 2;
    const int iContentTop = PB_RULE_TAB + 2;
    const int iContentBottom = iStatusRuleY - 2;

    if (iActiveTab < 0 || iActiveTab >= PB_TAB_COUNT) {
        iActiveTab = PB_TAB_STATS;
    }

    /* Background and chrome. */
    RsGraphFillRect(pRsGraph, 0, 0, iW, iH, RS_GRAY_BLACK);

    RsGraphFrame(pRsGraph, iFrameX, iFrameY, iFrameW, iFrameH, RS_GRAY_LIGHT);
    PbDrawCorners(pRsGraph, iFrameX, iFrameY, iFrameW, iFrameH, 8, RS_GRAY_WHITE);

    /* Small tick marks on the two long edges, a Pipboy-ish HUD detail. */
    RsGraphHLine(pRsGraph, iFrameX, iFrameY + iFrameH / 2, 3, RS_GRAY_WHITE);
    RsGraphHLine(pRsGraph, iFrameX + iFrameW - 3, iFrameY + iFrameH / 2, 3, RS_GRAY_WHITE);

    /* Separator under the tabs and above the status bar. */
    RsGraphHLine(pRsGraph, iFrameX + 1, PB_RULE_TAB, iFrameW - 2, RS_GRAY_LIGHT);
    RsGraphHLine(pRsGraph, iFrameX + 1, iStatusRuleY, iFrameW - 2, RS_GRAY_LIGHT);

    PbDrawTabBar(pRsGraph, iActiveTab, iInnerR);

    if (iContentBottom > iContentTop) {
        PbDrawContent(pRsGraph, iContentTop, iContentBottom, iInnerL + 4, iInnerR - 4);
    }

    PbDrawStatus(pRsGraph, iStatusTextY, iInnerL + 3, iInnerR - 3);
}

int PbTabFromKey(int iVirtKey) {
    /* VK_1..VK_4 are numbered in order, same for the numeric keypad block. */
    if (iVirtKey >= '1' && iVirtKey <= '4') {
        return iVirtKey - '1';
    }
    if (iVirtKey >= VK_NUMPAD1 && iVirtKey <= VK_NUMPAD4) {
        return iVirtKey - VK_NUMPAD1;
    }

    return -1;
}

int PbNextTab(int iActiveTab) {
    if (iActiveTab < 0 || iActiveTab >= PB_TAB_COUNT) {
        return PB_TAB_STATS;
    }

    return (iActiveTab + 1) % PB_TAB_COUNT;
}
