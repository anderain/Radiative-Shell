#ifndef _PIPBOY_H_
#define _PIPBOY_H_

#include "rshell.h"
#include "battery.h"
#include "../config/config.h"

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

/* The charge bar stops this many pixels short of the report's right edge. */
#define PB_STATS_BAR_INSET_RIGHT 12

/* Headline scale, used for the page titles. */
#define PB_TITLE_SCALE          2

/* The 6x8 glyphs leave their bottom row blank, so the ink of a text block is
   one pixel shorter than its advance; a headline is two rows shorter. The
   block heights below use the ink height so the block ends up optically
   centred rather than pinned high. */
#define PB_TEXT_INK_H           (RS_FONT_HEIGHT - 1)
#define PB_TITLE_INK_H          (RS_FONT_HEIGHT * PB_TITLE_SCALE - 2)

/* System page extras: memory load bar and its inset. */
#define PB_SYS_BAR_INSET_RIGHT  12

/* Walk cycle on the stats page. Six frames, one every half second, so a full
   loop takes three seconds. */
#define PB_WALK_FRAME_COUNT     6
#define PB_WALK_FRAME_MS        500

/* Launcher page. Rows are a fixed height so the scroll maths is trivial: the
   icon is 24 pixels, the display name is drawn at double the font size and the
   path below it at the normal size, which comes to 30 pixels with the padding.
   Six of them fit the content band on a 320x240 screen. */
#define PB_SLOT_ROW_H           30
#define PB_SLOT_VISIBLE         6
#define PB_SLOT_ICON_W          24
#define PB_SLOT_ICON_H          24
#define PB_SLOT_TEXT_X          40      /* icon width plus the gap        */
#define PB_SLOT_NAME_SCALE      2
#define PB_SLOT_SEL_BAR_W       3

/* Draw the complete shell: tabs, content area and status bar.
   pTime    - clock snapshot, only used by the clock tab, may be NULL.
   pSysInfo - GetSystemInfo result, only used by the system tab, may be NULL.
   pPower   - battery snapshot, only used by the stats tab, may be NULL.
   pMem     - GlobalMemoryStatus result, only used by the system tab.
   dwUpSeconds - uptime in seconds, only used by the system tab.
   dwAnimMs - milliseconds since the stats tab was entered, drives the walk
              cycle. Only used by the stats tab, ignored elsewhere.
   pConfig  - shell configuration, only used by the launcher tab, may be NULL.
   iLauncherSel - selected launcher row, only used by the launcher tab.
   bConfigLoaded - TRUE when pConfig came from rshl.cfg rather than the built
              in defaults; a small marker is drawn in the status bar.
   A NULL snapshot makes that tab fall back to the placeholder. */
void PbDrawScreen(RSGRAPH* pRsGraph, int iActiveTab,
                  const SYSTEMTIME* pTime, const SYSTEM_INFO* pSysInfo,
                  const RS_POWER_INFO* pPower, const MEMORYSTATUS* pMem,
                  DWORD dwUpSeconds, DWORD dwAnimMs,
                  const RSHL_CONFIG* pConfig, int iLauncherSel,
                  BOOL bConfigLoaded);

/* Map a virtual key to a tab index. Accepts VK_1..VK_4 and the numeric keypad
   equivalents; returns -1 for every other key. */
int PbTabFromKey(int iVirtKey);

/* Advance to the next tab, wrapping Stats -> System -> Clock -> Launcher. */
int PbNextTab(int iActiveTab);

/* Human readable tab label, e.g. "STATS", and NULL for an unknown index. */
const unsigned char* PbTabLabel(int iTab);

#endif
