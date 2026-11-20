#ifndef _PIPBOY_H_
#define _PIPBOY_H_

#include "rshell.h"
#include "battery.h"

/* Tabs shown in the top bar, in display order. */
#define PB_TAB_STATS        0
#define PB_TAB_SYSTEM       1
#define PB_TAB_CLOCK        2
#define PB_TAB_LAUNCHER     3
#define PB_TAB_COUNT        4

/* Outer margin between the screen edge and the shell frame. */
#define PB_OUTER_MARGIN     2

/* Rows of the fixed chrome, measured from the top of the frame buffer. */
#define PB_TAB_TOP          4
#define PB_TAB_HEIGHT       12
#define PB_TAB_TEXT_PAD_X   5
#define PB_TAB_LEFT         6
#define PB_RULE_TAB         (PB_TAB_TOP + PB_TAB_HEIGHT + 2)

/* Height of the status bar band above the bottom frame line. */
#define PB_STATUS_HEIGHT    12

/* Branding logo shown at the right end of the tab strip. It is 15 pixels tall,
   which is exactly the gap between the frame top and the rule under the tabs,
   so it is aligned to the frame top rather than to the tab band. */
#define PB_LOGO_TOP         (PB_TAB_TOP - 1)
#define PB_LOGO_INSET_RIGHT 2
#define PB_LOGO_MIN_GAP     6

/* Widest the battery report block may get. The content column is wider than
   the report needs, so the block stays narrower than the band it sits in. */
#define PB_STATS_REPORT_MAX_W   150

/* Padding between the content frame and the stats page contents. The report is
   right aligned against it, the artwork is centred in the band that is left. */
#define PB_STATS_SIDE_MARGIN    10

/* Gap between the artwork band and the battery report. */
#define PB_STATS_GAP            12

/* Draw the complete shell: tabs, content area and status bar.
   pTime    - clock snapshot, only used by the clock tab, may be NULL.
   pSysInfo - GetSystemInfo result, only used by the system tab, may be NULL.
   pPower   - battery snapshot, only used by the stats tab, may be NULL.
   A NULL snapshot makes that tab fall back to the placeholder. */
void PbDrawScreen(RSGRAPH* pRsGraph, int iActiveTab,
                  const SYSTEMTIME* pTime, const SYSTEM_INFO* pSysInfo,
                  const RS_POWER_INFO* pPower);

/* Map a virtual key to a tab index. Accepts VK_1..VK_4 and the numeric keypad
   equivalents; returns -1 for every other key. */
int PbTabFromKey(int iVirtKey);

/* Advance to the next tab, wrapping Stats -> System -> Clock -> Launcher. */
int PbNextTab(int iActiveTab);

/* Human readable tab label, e.g. "STATS", and NULL for an unknown index. */
const unsigned char* PbTabLabel(int iTab);

#endif
