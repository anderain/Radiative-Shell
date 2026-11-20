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
     | RADIATIVE SHELL v1.0     1-4  ESC EXIT |  status band
     +----------------------------------------+
*/

#include "pipboy.h"

static const unsigned char* const g_pbTabLabels[PB_TAB_COUNT] = {
    (const unsigned char *)"STATS",
    (const unsigned char *)"SYSTEM",
    (const unsigned char *)"CLOCK",
    (const unsigned char *)"LAUNCHER"
};

/* Seven segment bit masks, bit 0 = a (top) through bit 6 = g (middle):
     --a--
    f     b
     --g--
    e     c
     --d--                                                            */
static const BYTE g_pbSegDigits[10] = {
    0x3F,   /* 0: a b c d e f   */
    0x06,   /* 1:   b c         */
    0x5B,   /* 2: a b   d e   g */
    0x4F,   /* 3: a b c d   g   */
    0x66,   /* 4:   b c f   g   */
    0x6D,   /* 5: a   c d f g   */
    0x7D,   /* 6: a   c d e f g */
    0x07,   /* 7: a b c         */
    0x7F,   /* 8: a b c d e f g */
    0x6F    /* 9: a b c d   f g */
};

static const unsigned char* const g_pbWeekdayNames[7] = {
    (const unsigned char *)"SUN",
    (const unsigned char *)"MON",
    (const unsigned char *)"TUE",
    (const unsigned char *)"WED",
    (const unsigned char *)"THU",
    (const unsigned char *)"FRI",
    (const unsigned char *)"SAT"
};

/* Clamp an integer into [iLo, iHi]. */
static int PbClamp(int iValue, int iLo, int iHi) {
    if (iValue < iLo) {
        return iLo;
    }
    if (iValue > iHi) {
        return iHi;
    }
    return iValue;
}

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

/* Geometry of one segment inside a digit box. iSeg is 0..6 for a..g. */
static void PbSegmentRect(int x, int y, int w, int h, int t, int iSeg,
                          int* px, int* py, int* pw, int* ph) {
    const int iMid  = y + (h - t) / 2;   /* top of the middle bar */
    const int iHalf = (h - t) / 2;       /* height of one vertical span */

    switch (iSeg) {
        case 0:  /* a: top bar */
            *px = x + t; *py = y; *pw = w - 2 * t; *ph = t;
            break;
        case 1:  /* b: upper right */
            *px = x + w - t; *py = y + t; *pw = t; *ph = iHalf - t;
            break;
        case 2:  /* c: lower right */
            *px = x + w - t; *py = iMid + t; *pw = t; *ph = h - t - (iHalf + t);
            break;
        case 3:  /* d: bottom bar */
            *px = x + t; *py = y + h - t; *pw = w - 2 * t; *ph = t;
            break;
        case 4:  /* e: lower left */
            *px = x; *py = iMid + t; *pw = t; *ph = h - t - (iHalf + t);
            break;
        case 5:  /* f: upper left */
            *px = x; *py = y + t; *pw = t; *ph = iHalf - t;
            break;
        default: /* g: middle bar */
            *px = x + t; *py = iMid; *pw = w - 2 * t; *ph = t;
            break;
    }
}

/* One digit. Every segment is drawn: the lit ones in bLit, the others faintly
   in bUnlit so the display still reads as a segmented panel. */
static void PbDrawDigit(RSGRAPH* pRsGraph, int iX, int iY, int iW, int iH, int iThick,
                        int iDigit, BYTE bLit, BYTE bUnlit) {
    const BYTE bMask = g_pbSegDigits[iDigit % 10];
    int iSeg;

    for (iSeg = 0; iSeg < 7; ++iSeg) {
        int x, y, w, h;

        PbSegmentRect(iX, iY, iW, iH, iThick, iSeg, &x, &y, &w, &h);
        RsGraphFillRect(pRsGraph, x, y, w, h,
                        (bMask & (1 << iSeg)) ? bLit : bUnlit);
    }
}

/* Two dots used as the time separator. The unlit state is not drawn. */
static void PbDrawColon(RSGRAPH* pRsGraph, int iX, int iY, int iW, int iH, int iThick,
                        BYTE bColor) {
    const int iDotX = iX + (iW - iThick) / 2;
    const int iDotA = iY + iH / 3 - iThick / 2;
    const int iDotB = iY + iH * 2 / 3 - iThick / 2;

    RsGraphFillRect(pRsGraph, iDotX, iDotA, iThick, iThick, bColor);
    RsGraphFillRect(pRsGraph, iDotX, iDotB, iThick, iThick, bColor);
}

/* Write a two digit, zero padded number and return the advanced pointer. */
static unsigned char* PbPut2(unsigned char* pOut, int iValue) {
    if (iValue < 0) {
        iValue = 0;
    }
    pOut[0] = (unsigned char)('0' + (iValue / 10) % 10);
    pOut[1] = (unsigned char)('0' + iValue % 10);
    return pOut + 2;
}

/* "HH:MM:SS", always eight bytes plus the terminator. */
static void PbFormatTime(unsigned char* pOut, const SYSTEMTIME* pTime) {
    pOut = PbPut2(pOut, pTime->wHour);
    *pOut++ = ':';
    pOut = PbPut2(pOut, pTime->wMinute);
    *pOut++ = ':';
    pOut = PbPut2(pOut, pTime->wSecond);
    *pOut = 0;
}

/* "YYYY-MM-DD DDD", always fifteen bytes plus the terminator. */
static void PbFormatDate(unsigned char* pOut, const SYSTEMTIME* pTime) {
    int iYear = (int)pTime->wYear;
    int iWeekday = (int)pTime->wDayOfWeek;

    if (iWeekday < 0 || iWeekday > 6) {
        iWeekday = 0;
    }

    pOut[0] = (unsigned char)('0' + (iYear / 1000) % 10);
    pOut[1] = (unsigned char)('0' + (iYear / 100) % 10);
    pOut[2] = (unsigned char)('0' + (iYear / 10) % 10);
    pOut[3] = (unsigned char)('0' + iYear % 10);
    pOut[4] = '-';
    pOut = PbPut2(pOut + 5, pTime->wMonth);
    pOut[0] = '-';
    pOut = PbPut2(pOut + 1, pTime->wDay);
    pOut[0] = ' ';
    pOut[1] = g_pbWeekdayNames[iWeekday][0];
    pOut[2] = g_pbWeekdayNames[iWeekday][1];
    pOut[3] = g_pbWeekdayNames[iWeekday][2];
    pOut[4] = 0;
}

/* Big seven segment clock plus title, rule and date, vertically centred as one
   block inside the content frame. */
static void PbDrawClockPage(RSGRAPH* pRsGraph, const SYSTEMTIME* pTime,
                            int iTop, int iBottom, int iLeft, int iRight) {
    unsigned char szTime[9];
    unsigned char szDate[16];
    const int iW = iRight - iLeft + 1;
    int iDigitW, iGap, iColonW, iThick;
    int iDigitH, iBlockH, iY, iTotalW, iX;
    int i, iRules;
    /* Panel layout: digit, digit, colon, digit, digit, colon, digit, digit.
       A -1 marks a colon, anything else is the index into szTime. */
    static const int aiSlots[8] = { 0, 1, -1, 3, 4, -1, 6, 7 };

    PbFormatTime(szTime, pTime);
    PbFormatDate(szDate, pTime);

    /* Pick a digit size that fits the frame with an 8 pixel side margin. */
    iDigitW = (iW - 16) * 4 / 35;
    iDigitW = PbClamp(iDigitW, 8, 40);
    iThick  = PbClamp(iDigitW / 5, 3, 8);
    iGap    = iDigitW / 4;
    if (iGap < 3) {
        iGap = 3;
    }
    iColonW = iDigitW / 2;
    if (iColonW < 6) {
        iColonW = 6;
    }
    iDigitH = iDigitW * 2;

    iTotalW = 6 * iDigitW + 2 * iColonW + 7 * iGap;

    /* Whole block: title, gap, digits, gap, rule, gap, date. */
    iBlockH = RS_FONT_HEIGHT + 10 + iDigitH + 12 + 1 + 10 + RS_FONT_HEIGHT;

    iY = iTop + (iBottom - iTop + 1 - iBlockH) / 2;
    if (iY < iTop + 1) {
        iY = iTop + 1;
    }

    PbDrawTextCentered(pRsGraph, iLeft + 1, iY, iW - 2,
                       (const unsigned char *)"LOCAL TIME", RS_GRAY_LIGHT);

    iY += RS_FONT_HEIGHT + 10;
    iX = iLeft + (iW - iTotalW) / 2;

    for (i = 0; i < 8; ++i) {
        if (aiSlots[i] < 0) {
            PbDrawColon(pRsGraph, iX, iY, iColonW, iDigitH, iThick, RS_GRAY_WHITE);
            iX += iColonW + iGap;
        }
        else {
            PbDrawDigit(pRsGraph, iX, iY, iDigitW, iDigitH, iThick,
                        szTime[aiSlots[i]] - '0',
                        RS_GRAY_WHITE, RS_GRAY_DARK);
            iX += iDigitW + iGap;
        }
    }

    iY += iDigitH + 12;

    /* Short rule under the digits, 40 percent of the frame width. */
    iRules = (iW * 2 / 5) & ~1;
    if (iRules < 8) {
        iRules = 8;
    }
    RsGraphHLine(pRsGraph, iLeft + (iW - iRules) / 2, iY, iRules, RS_GRAY_LIGHT);

    iY += 1 + 10;

    PbDrawTextCentered(pRsGraph, iLeft + 1, iY, iW - 2, szDate, RS_GRAY_LIGHT);
}

static void PbDrawContent(RSGRAPH* pRsGraph, int iActiveTab, const SYSTEMTIME* pTime,
                          int iTop, int iBottom, int iLeft, int iRight) {
    const int iW = iRight - iLeft + 1;
    const int iH = iBottom - iTop + 1;

    RsGraphFrame(pRsGraph, iLeft, iTop, iW, iH, RS_GRAY_LIGHT);
    PbDrawCorners(pRsGraph, iLeft, iTop, iW, iH, 5, RS_GRAY_WHITE);

    if (iActiveTab == PB_TAB_CLOCK && pTime) {
        PbDrawClockPage(pRsGraph, pTime, iTop, iBottom, iLeft, iRight);
        return;
    }

    PbDrawTextCentered(pRsGraph, iLeft + 1, iTop + (iH - RS_FONT_HEIGHT) / 2, iW - 2,
                       (const unsigned char *)"NO DATA", RS_GRAY_DARK);
}

/* Status bar. The right hand hint is dropped when it would collide with the
   left hand text, which is what happens on a narrow portrait screen. */
static void PbDrawStatus(RSGRAPH* pRsGraph, int iY, int iLeft, int iRight) {
    const unsigned char* pLeftText;
    const unsigned char* pRightText;
    const int iW = iRight - iLeft + 1;

    if (iW >= 300) {
        pLeftText  = (const unsigned char *)"RADIATIVE SHELL v1.0";
        pRightText = (const unsigned char *)"1-4 TABS  ESC EXIT";
    }
    else if (iW >= 200) {
        pLeftText  = (const unsigned char *)"RADIATIVE SHELL";
        pRightText = (const unsigned char *)"ESC EXIT";
    }
    else {
        pLeftText  = (const unsigned char *)"RADIATIVE SHELL";
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

void PbDrawScreen(RSGRAPH* pRsGraph, int iActiveTab, const SYSTEMTIME* pTime) {
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
        PbDrawContent(pRsGraph, iActiveTab, pTime,
                      iContentTop, iContentBottom, iInnerL + 4, iInnerR - 4);
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
