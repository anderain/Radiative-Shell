#ifndef _BATTERY_H_
#define _BATTERY_H_

#include "rshell.h"

/* Sentinel values, matching the ones the Windows headers use. */
#define RS_POWER_PERCENT_UNKNOWN    0xFF
#define RS_POWER_LIFE_UNKNOWN       0xFFFFFFFFUL

/* Platform neutral battery snapshot. GetSystemPowerStatus (desktop) and
   GetSystemPowerStatusEx (Windows CE) are both mapped onto this, so the rest
   of the shell never has to care which one the platform provides. */
typedef struct _RS_POWER_INFO {
    BOOL   bValid;               /* FALSE when the platform refused to answer */
    BYTE   bACLineStatus;        /* AC_LINE_*                                 */
    BYTE   bBatteryFlag;         /* BATTERY_FLAG_* bit mask                   */
    BYTE   bBatteryLifePercent;  /* 0..100, or RS_POWER_PERCENT_UNKNOWN       */
    DWORD  dwBatteryLifeTime;    /* seconds remaining, or RS_POWER_LIFE_UNKNOWN */
    DWORD  dwBatteryFullLifeTime;/* seconds when full,  or RS_POWER_LIFE_UNKNOWN */
} RS_POWER_INFO;

/* Fill pInfo with the current battery state. The structure is always fully
   initialised, even when the platform has no battery or no such API. */
void RsQueryPower(RS_POWER_INFO* pInfo);

/* Name for a BATTERY_FLAG_* combination. */
const unsigned char* RsPowerStatusText(BYTE bBatteryFlag);

/* Name for an AC_LINE_* value. */
const unsigned char* RsPowerACText(BYTE bACLineStatus);

/* Seconds to "2H 35M" / "35M" / "0M", or "N/A" for RS_POWER_LIFE_UNKNOWN.
   The output buffer must hold at least 12 bytes. */
void RsPowerFormatDuration(unsigned char* pOut, DWORD dwSeconds);

#endif
