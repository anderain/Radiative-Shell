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
#include "battery.h"
#include "assets.h"

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

/* dwProcessorType values and the name shown on the system page. The numbers
   are spelled out instead of using the PROCESSOR_* macros, because the old
   Windows CE SDKs and the desktop SDKs disagree about which ones exist. Note
   that the Hitachi SH3/SH4 and the SHx SH3/SH4 are two distinct encodings. */
typedef struct {
    DWORD                  dwType;
    const unsigned char*   pName;
} PB_CPUTYPE;

static const PB_CPUTYPE g_pbCpuTypes[] = {
    { 386,    (const unsigned char *)"I386"        },
    { 486,    (const unsigned char *)"I486"        },
    { 586,    (const unsigned char *)"PENTIUM"     },
    { 860,    (const unsigned char *)"I860"        },
    { 2000,   (const unsigned char *)"MIPS R2000"  },
    { 3000,   (const unsigned char *)"MIPS R3000"  },
    { 4000,   (const unsigned char *)"MIPS R4000"  },
    { 10003,  (const unsigned char *)"HITACHI SH3" },
    { 10005,  (const unsigned char *)"HITACHI SH4" },
    { 21064,  (const unsigned char *)"ALPHA 21064" },
    { 601,    (const unsigned char *)"PPC 601"     },
    { 603,    (const unsigned char *)"PPC 603"     },
    { 604,    (const unsigned char *)"PPC 604"     },
    { 620,    (const unsigned char *)"PPC 620"     },
    { 821,    (const unsigned char *)"PPC 821"     },
    { 103,    (const unsigned char *)"SH3"         },
    { 104,    (const unsigned char *)"SH4"         },
    { 2577,   (const unsigned char *)"STRONGARM"   },
    { 1824,   (const unsigned char *)"ARM720"      },
    { 2080,   (const unsigned char *)"ARM820"      },
    { 2336,   (const unsigned char *)"ARM920"      }
};

/* wProcessorArchitecture values. Anything outside the table reads UNKNOWN. */
static const unsigned char* PbArchName(WORD wArch) {
    switch (wArch) {
        case 0:  return (const unsigned char *)"INTEL";
        case 1:  return (const unsigned char *)"MIPS";
        case 2:  return (const unsigned char *)"ALPHA";
        case 3:  return (const unsigned char *)"PPC";
        case 4:  return (const unsigned char *)"SHX";
        case 5:  return (const unsigned char *)"ARM";
        case 6:  return (const unsigned char *)"IA64";
        case 7:  return (const unsigned char *)"ALPHA64";
        case 8:  return (const unsigned char *)"MSIL";
        case 9:  return (const unsigned char *)"AMD64";
        case 10: return (const unsigned char *)"IA32ONWIN64";
        default: return (const unsigned char *)"UNKNOWN";
    }
}

/* Name for a dwProcessorType value, or NULL when it is not in the table. */
static const unsigned char* PbCpuTypeName(DWORD dwType) {
    int i;
    const int iCount = (int)(sizeof(g_pbCpuTypes) / sizeof(g_pbCpuTypes[0]));

    for (i = 0; i < iCount; ++i) {
        if (g_pbCpuTypes[i].dwType == dwType) {
            return g_pbCpuTypes[i].pName;
        }
    }

    return NULL;
}

/* Number of digits needed to print a value, at least one. */
static int PbHexDigits(DWORD dwValue) {
    int iDigits = 1;

    while (dwValue >= 16) {
        dwValue /= 16;
        ++iDigits;
    }

    return iDigits;
}

static int PbDecDigits(DWORD dwValue) {
    int iDigits = 1;

    while (dwValue >= 10) {
        dwValue /= 10;
        ++iDigits;
    }

    return iDigits;
}

/* Fixed width, zero padded, upper case hexadecimal. When the value needs more
   digits than iDigits the field simply grows, so nothing is ever truncated. */
static unsigned char* PbPutHex(unsigned char* pOut, DWORD dwValue, int iDigits) {
    static const unsigned char kHex[] = "0123456789ABCDEF";
    int iTotal = PbHexDigits(dwValue);
    int i;

    if (iDigits < 1) {
        iDigits = 1;
    }
    if (iTotal < iDigits) {
        iTotal = iDigits;
    }
    if (iTotal > 16) {
        iTotal = 16;
    }

    /* Fill from the right: the least significant nibble goes last. */
    for (i = iTotal - 1; i >= 0; --i) {
        pOut[i] = kHex[dwValue % 16];
        dwValue /= 16;
    }

    return pOut + iTotal;
}

/* Fixed width decimal, zero padded, growing when the value needs it. */
static unsigned char* PbPutDec(unsigned char* pOut, DWORD dwValue, int iDigits) {
    int iTotal = PbDecDigits(dwValue);
    int i;

    if (iDigits < 1) {
        iDigits = 1;
    }
    if (iTotal < iDigits) {
        iTotal = iDigits;
    }
    if (iTotal > 16) {
        iTotal = 16;
    }

    for (i = iTotal - 1; i >= 0; --i) {
        pOut[i] = (unsigned char)('0' + (dwValue % 10));
        dwValue /= 10;
    }

    return pOut + iTotal;
}

/* Append a NUL terminated string and return the advanced pointer. */
static unsigned char* PbPutStr(unsigned char* pOut, const unsigned char* pText) {
    while (*pText) {
        *pOut++ = *pText++;
    }

    return pOut;
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
   switching tabs never moves the other labels around. iRight is the exclusive
   limit the slots may reach, which leaves room for the logo. */
static void PbDrawTabBar(RSGRAPH* pRsGraph, int iActiveTab, int iLeft, int iRight) {
    int i, x = iLeft;

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

/* Branding logo, right aligned in the tab strip. Returns the exclusive x limit
   the tab slots may reach, so the logo is never overdrawn. */
static int PbDrawLogo(RSGRAPH* pRsGraph, int iRight) {
    int iImgW, iImgH, iX;

    if (!RsGraphImageSize(IMAGE_RSHL_LOGO, &iImgW, &iImgH)) {
        return iRight + 1;
    }

    iX = iRight - PB_LOGO_INSET_RIGHT - iImgW + 1;
    if (iX <= PB_LOGO_INSET_RIGHT) {
        /* Not enough room, drop the logo rather than crushing the tabs. */
        return iRight + 1;
    }

    RsGraphDrawImage(pRsGraph, iX, PB_LOGO_TOP, IMAGE_RSHL_LOGO, RS_GRAY_LIGHT);

    return iX - PB_LOGO_MIN_GAP;
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

/* Stats page: the vault boy artwork, centred in the content frame. The image
   is drawn at its native size, the canvas (not the visible pixels) is what
   gets centred. */
/* Frame plus a continuous fill, iPercent 0..100. An unknown percentage leaves
   the bar empty so only the frame shows. */
static void PbDrawBatteryBar(RSGRAPH* pRsGraph, int iX, int iY, int iW, int iH,
                             int iPercent) {
    int iFill;

    RsGraphFrame(pRsGraph, iX, iY, iW, iH, RS_GRAY_LIGHT);

    if (iPercent <= 0 || iPercent > 100) {
        return;
    }

    iFill = (iW - 2) * iPercent / 100;
    if (iFill < 1) {
        iFill = 1;                    /* never hide a non zero charge */
    }

    RsGraphFillRect(pRsGraph, iX + 1, iY + 1, iFill, iH - 2, RS_GRAY_WHITE);
}

/* Right hand column of the stats page: a battery report with a charge bar.
   The label column is sized to the longest label plus a small gap, so the
   values start close to the labels instead of after a wide empty band. */
static void PbDrawBatteryReport(RSGRAPH* pRsGraph, const RS_POWER_INFO* pPower,
                                int iLeft, int iRight, int iTop, int iBottom) {
    unsigned char aPercent[8];
    unsigned char aLife[16];
    unsigned char aFull[16];
    const int iW = iRight - iLeft + 1;
    /* "BATTERY" is the longest label at 7 characters, two spaces of gap. */
    const int iLabelW = 9 * RS_FONT_WIDTH;
    const int iValueX = iLeft + iLabelW;
    const int iBarH = 10;
    const int iRowH = RS_FONT_HEIGHT;
    const int iRowGap = 4;
    const int iBlockH = RS_FONT_HEIGHT + 6 + 1 + 8 + iRowH + 4 + iBarH + 8
                      + iRowH + iRowGap + iRowH + iRowGap + iRowH + iRowGap + iRowH;
    int iY, iRuleW;
    int iPercent;

    /* Percentage text, "N/A" when the platform does not know. */
    if (pPower->bBatteryLifePercent == RS_POWER_PERCENT_UNKNOWN) {
        aPercent[0] = 'N';
        aPercent[1] = '/';
        aPercent[2] = 'A';
        aPercent[3] = 0;
        iPercent = -1;
    }
    else {
        iPercent = (int)pPower->bBatteryLifePercent;
        if (iPercent > 100) {
            iPercent = 100;
        }

        {
            unsigned char* p = aPercent;

            if (iPercent >= 100) {
                *p++ = '1';
                *p++ = '0';
                *p++ = '0';
            }
            else if (iPercent >= 10) {
                *p++ = (unsigned char)('0' + iPercent / 10);
                *p++ = (unsigned char)('0' + iPercent % 10);
            }
            else {
                *p++ = (unsigned char)('0' + iPercent);
            }
            *p++ = '%';
            *p = 0;
        }
    }

    RsPowerFormatDuration(aLife, pPower->dwBatteryLifeTime);
    RsPowerFormatDuration(aFull, pPower->dwBatteryFullLifeTime);

    iY = iTop + (iBottom - iTop + 1 - iBlockH) / 2;
    if (iY < iTop + 1) {
        iY = iTop + 1;
    }

    PbDrawTextCentered(pRsGraph, iLeft, iY, iW,
                       (const unsigned char *)"POWER", RS_GRAY_LIGHT);

    iY += RS_FONT_HEIGHT + 6;

    iRuleW = (iW * 2 / 5) & ~1;
    if (iRuleW < 8) {
        iRuleW = 8;
    }
    RsGraphHLine(pRsGraph, iLeft + (iW - iRuleW) / 2, iY, iRuleW, RS_GRAY_LIGHT);

    iY += 1 + 8;

    RsGraphDrawText(pRsGraph, iLeft, iY, (const unsigned char *)"BATTERY", RS_GRAY_LIGHT);
    RsGraphDrawText(pRsGraph, iValueX, iY, aPercent, RS_GRAY_WHITE);

    iY += iRowH + 4;

    PbDrawBatteryBar(pRsGraph, iValueX, iY, iRight - iValueX + 1, iBarH, iPercent);

    iY += iBarH + 8;

    RsGraphDrawText(pRsGraph, iLeft, iY, (const unsigned char *)"STATUS", RS_GRAY_LIGHT);
    RsGraphDrawText(pRsGraph, iValueX, iY,
                    RsPowerStatusText(pPower->bBatteryFlag), RS_GRAY_WHITE);

    iY += iRowH + iRowGap;

    RsGraphDrawText(pRsGraph, iLeft, iY, (const unsigned char *)"AC", RS_GRAY_LIGHT);
    RsGraphDrawText(pRsGraph, iValueX, iY,
                    RsPowerACText(pPower->bACLineStatus), RS_GRAY_WHITE);

    iY += iRowH + iRowGap;

    RsGraphDrawText(pRsGraph, iLeft, iY, (const unsigned char *)"LIFE", RS_GRAY_LIGHT);
    RsGraphDrawText(pRsGraph, iValueX, iY, aLife, RS_GRAY_WHITE);

    iY += iRowH + iRowGap;

    RsGraphDrawText(pRsGraph, iLeft, iY, (const unsigned char *)"FULL", RS_GRAY_LIGHT);
    RsGraphDrawText(pRsGraph, iValueX, iY, aFull, RS_GRAY_WHITE);
}

/* Stats page: the battery report hugs the right side of the frame, the artwork
   is centred in the band that is left over. Both are vertically centred. */
static void PbDrawStatsPage(RSGRAPH* pRsGraph, const RS_POWER_INFO* pPower,
                            int iTop, int iBottom, int iLeft, int iRight) {
    int iImgW = 0, iImgH = 0;
    int iBandRight = iRight;         /* right edge of the artwork band */
    int iImgX;

    /* The report is right aligned, so reserve its width plus the side margin
       first and let the artwork use whatever is left. */
    if (pPower) {
        const int iAvail = iRight - PB_STATS_SIDE_MARGIN - iLeft + 1;
        int iReportW = iAvail;
        int iReportRight;

        if (iReportW > PB_STATS_REPORT_MAX_W) {
            iReportW = PB_STATS_REPORT_MAX_W;
        }

        iReportRight = iRight - PB_STATS_SIDE_MARGIN;
        PbDrawBatteryReport(pRsGraph, pPower, iReportRight - iReportW + 1,
                            iReportRight, iTop, iBottom);

        iBandRight = iReportRight - iReportW - PB_STATS_GAP;
    }

    /* Artwork centred in the remaining band. */
    if (!RsGraphImageSize(IMAGE_WALK_BOY, &iImgW, &iImgH)) {
        return;
    }

    iImgX = iLeft + (iBandRight - iLeft + 1 - iImgW) / 2;
    if (iImgX < iLeft) {
        iImgX = iLeft;               /* narrow screen: fall back to the left */
    }

    RsGraphDrawImage(pRsGraph, iImgX,
                     iTop + (iBottom - iTop + 1 - iImgH) / 2,
                     IMAGE_WALK_BOY, RS_GRAY_WHITE);
}

/* System page: a labelled list of the values GetSystemInfo reported. The
   block is vertically centred, the label column has a fixed width so the
   values line up. */
static void PbDrawSystemPage(RSGRAPH* pRsGraph, const SYSTEM_INFO* pSysInfo,
                             int iTop, int iBottom, int iLeft, int iRight) {
    static const unsigned char* const kLabels[8] = {
        (const unsigned char *)"ARCH",
        (const unsigned char *)"TYPE",
        (const unsigned char *)"LEVEL",
        (const unsigned char *)"REVISION",
        (const unsigned char *)"CPUS",
        (const unsigned char *)"MASK",
        (const unsigned char *)"PAGE",
        (const unsigned char *)"GRAN"
    };
    unsigned char aValues[8][24];
    const int iW = iRight - iLeft + 1;
    const int iRowH = RS_FONT_HEIGHT + 2;
    const int iRowsH = 7 * iRowH + RS_FONT_HEIGHT;
    const int iBlockH = RS_FONT_HEIGHT + 6 + 1 + 8 + iRowsH;
    const int iLabelX = iLeft + 8;
    const int iValueX = iLabelX + 10 * RS_FONT_WIDTH;
    int i, iY, iRuleW;
    unsigned char* p;

    /* Column 1: architecture name. */
    p = PbPutStr(aValues[0], PbArchName(pSysInfo->wProcessorArchitecture));
    *p = 0;

    /* Column 2: processor type name, hex fallback for unknown values. */
    {
        const unsigned char* pName = PbCpuTypeName(pSysInfo->dwProcessorType);

        if (pName) {
            p = PbPutStr(aValues[1], pName);
        }
        else {
            /* Unknown value: show it as 0xNNNN so it is still informative. */
            aValues[1][0] = '0';
            aValues[1][1] = 'x';
            p = PbPutHex(aValues[1] + 2, pSysInfo->dwProcessorType, 4);
        }
        *p = 0;
    }

    p = PbPutDec(aValues[2], (DWORD)pSysInfo->wProcessorLevel, 1);
    *p = 0;

    p = PbPutHex(aValues[3], (DWORD)pSysInfo->wProcessorRevision, 4);
    *p = 0;

    p = PbPutDec(aValues[4], pSysInfo->dwNumberOfProcessors, 1);
    *p = 0;

    p = PbPutHex(aValues[5], pSysInfo->dwActiveProcessorMask, 8);
    *p = 0;

    p = PbPutHex(aValues[6], pSysInfo->dwPageSize, 8);
    *p = 0;

    p = PbPutHex(aValues[7], pSysInfo->dwAllocationGranularity, 8);
    *p = 0;

    iY = iTop + (iBottom - iTop + 1 - iBlockH) / 2;
    if (iY < iTop + 1) {
        iY = iTop + 1;
    }

    PbDrawTextCentered(pRsGraph, iLeft + 1, iY, iW - 2,
                       (const unsigned char *)"SYSTEM INFO", RS_GRAY_LIGHT);

    iY += RS_FONT_HEIGHT + 6;

    iRuleW = (iW * 2 / 5) & ~1;
    if (iRuleW < 8) {
        iRuleW = 8;
    }
    RsGraphHLine(pRsGraph, iLeft + (iW - iRuleW) / 2, iY, iRuleW, RS_GRAY_LIGHT);

    iY += 1 + 8;

    for (i = 0; i < 8; ++i) {
        RsGraphDrawText(pRsGraph, iLabelX, iY, kLabels[i], RS_GRAY_LIGHT);
        RsGraphDrawText(pRsGraph, iValueX, iY, aValues[i], RS_GRAY_WHITE);
        iY += iRowH;
    }
}

static void PbDrawContent(RSGRAPH* pRsGraph, int iActiveTab, const SYSTEMTIME* pTime,
                          const SYSTEM_INFO* pSysInfo, const RS_POWER_INFO* pPower,
                          int iTop, int iBottom, int iLeft, int iRight) {
    const int iW = iRight - iLeft + 1;
    const int iH = iBottom - iTop + 1;

    RsGraphFrame(pRsGraph, iLeft, iTop, iW, iH, RS_GRAY_LIGHT);
    PbDrawCorners(pRsGraph, iLeft, iTop, iW, iH, 5, RS_GRAY_WHITE);

    if (iActiveTab == PB_TAB_STATS) {
        PbDrawStatsPage(pRsGraph, pPower, iTop, iBottom, iLeft, iRight);
        return;
    }

    if (iActiveTab == PB_TAB_SYSTEM && pSysInfo) {
        PbDrawSystemPage(pRsGraph, pSysInfo, iTop, iBottom, iLeft, iRight);
        return;
    }

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

void PbDrawScreen(RSGRAPH* pRsGraph, int iActiveTab,
                  const SYSTEMTIME* pTime, const SYSTEM_INFO* pSysInfo,
                  const RS_POWER_INFO* pPower) {
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
    int       iTabLimit;

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

    /* Logo first: it reports how far right the tab slots may go. */
    iTabLimit = PbDrawLogo(pRsGraph, iInnerR);

    PbDrawTabBar(pRsGraph, iActiveTab, PB_TAB_LEFT, iTabLimit);

    if (iContentBottom > iContentTop) {
        PbDrawContent(pRsGraph, iActiveTab, pTime, pSysInfo, pPower,
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
