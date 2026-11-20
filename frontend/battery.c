/*
   Battery reporting.

   The two Windows families disagree about the power API:

     desktop   GetSystemPowerStatus   - SYSTEM_POWER_STATUS, 6 fields
     Windows CE GetSystemPowerStatusEx - SYSTEM_POWER_STATUS_EX, 12 fields

   Nothing above this file may look at those structures. Both are converted
   into RS_POWER_INFO, which is what the shell draws.
*/

#include <string.h>
#include "battery.h"

/* Append a decimal number and return the advanced pointer. */
static unsigned char* PowerPutDec(unsigned char* pOut, DWORD dwValue) {
    unsigned char aDigits[12];
    int iCount = 0;

    do {
        aDigits[iCount++] = (unsigned char)('0' + (dwValue % 10));
        dwValue /= 10;
    } while (dwValue > 0);

    while (iCount > 0) {
        *pOut++ = aDigits[--iCount];
    }

    return pOut;
}

void RsQueryPower(RS_POWER_INFO* pInfo) {
    if (!pInfo) {
        return;
    }

    memset(pInfo, 0, sizeof(*pInfo));
    pInfo->bACLineStatus          = AC_LINE_UNKNOWN;
    pInfo->bBatteryFlag           = BATTERY_FLAG_UNKNOWN;
    pInfo->bBatteryLifePercent    = RS_POWER_PERCENT_UNKNOWN;
    pInfo->dwBatteryLifeTime      = RS_POWER_LIFE_UNKNOWN;
    pInfo->dwBatteryFullLifeTime  = RS_POWER_LIFE_UNKNOWN;

#if (IS_WIN_CE)
    /* Windows CE: the extended structure also carries the backup battery,
       which this shell does not show, so only the main battery is copied. */
    {
        SYSTEM_POWER_STATUS_EX ex;

        memset(&ex, 0, sizeof(ex));

        if (GetSystemPowerStatusEx(&ex, TRUE)) {
            pInfo->bValid                = TRUE;
            pInfo->bACLineStatus         = ex.ACLineStatus;
            pInfo->bBatteryFlag          = ex.BatteryFlag;
            pInfo->bBatteryLifePercent   = ex.BatteryLifePercent;
            pInfo->dwBatteryLifeTime     = ex.BatteryLifeTime;
            pInfo->dwBatteryFullLifeTime = ex.BatteryFullLifeTime;
        }
    }
#else
    /* Desktop: only the shorter structure exists. GetSystemPowerStatus works
       on machines without a battery too, it simply reports NO BATTERY. */
    {
        SYSTEM_POWER_STATUS st;

        memset(&st, 0, sizeof(st));

        if (GetSystemPowerStatus(&st)) {
            pInfo->bValid                = TRUE;
            pInfo->bACLineStatus         = st.ACLineStatus;
            pInfo->bBatteryFlag          = st.BatteryFlag;
            pInfo->bBatteryLifePercent   = st.BatteryLifePercent;
            pInfo->dwBatteryLifeTime     = st.BatteryLifeTime;
            pInfo->dwBatteryFullLifeTime = st.BatteryFullLifeTime;
        }
    }
#endif
}

const unsigned char* RsPowerStatusText(BYTE bBatteryFlag) {
    /* The flag is a bit mask and several bits can be set at once. Charging is
       reported first because that is what the user wants to know, then the
       remaining bits in order of severity. */
    if (bBatteryFlag == BATTERY_FLAG_UNKNOWN) {
        return (const unsigned char *)"UNKNOWN";
    }
    if (bBatteryFlag & BATTERY_FLAG_NO_BATTERY) {
        return (const unsigned char *)"NO BATTERY";
    }
    if (bBatteryFlag & BATTERY_FLAG_CHARGING) {
        return (const unsigned char *)"CHARGING";
    }
    if (bBatteryFlag & BATTERY_FLAG_CRITICAL) {
        return (const unsigned char *)"CRITICAL";
    }
    if (bBatteryFlag & BATTERY_FLAG_LOW) {
        return (const unsigned char *)"LOW";
    }
    if (bBatteryFlag & BATTERY_FLAG_HIGH) {
        return (const unsigned char *)"HIGH";
    }

    return (const unsigned char *)"UNKNOWN";
}

const unsigned char* RsPowerACText(BYTE bACLineStatus) {
    switch (bACLineStatus) {
        case AC_LINE_ONLINE:
            return (const unsigned char *)"ONLINE";
        case AC_LINE_OFFLINE:
            return (const unsigned char *)"OFFLINE";
        case AC_LINE_BACKUP_POWER:
            return (const unsigned char *)"BACKUP";
        default:
            return (const unsigned char *)"UNKNOWN";
    }
}

void RsPowerFormatDuration(unsigned char* pOut, DWORD dwSeconds) {
    DWORD dwHours;
    DWORD dwMinutes;

    if (!pOut) {
        return;
    }

    if (dwSeconds == RS_POWER_LIFE_UNKNOWN) {
        pOut[0] = 'N';
        pOut[1] = '/';
        pOut[2] = 'A';
        pOut[3] = 0;
        return;
    }

    dwHours   = dwSeconds / 3600;
    dwMinutes = (dwSeconds % 3600) / 60;

    if (dwHours > 0) {
        pOut = PowerPutDec(pOut, dwHours);
        *pOut++ = 'H';
        *pOut++ = ' ';
    }

    pOut = PowerPutDec(pOut, dwMinutes);
    *pOut++ = 'M';
    *pOut   = 0;
}
