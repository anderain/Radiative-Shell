#ifndef _PIPBOY_H_
#define _PIPBOY_H_

#include "rshell.h"

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

/* Draw the complete shell: tabs, content area and status bar. pTime is only
   used by the clock tab; pass NULL to show the placeholder instead. */
void PbDrawScreen(RSGRAPH* pRsGraph, int iActiveTab, const SYSTEMTIME* pTime);

/* Map a virtual key to a tab index. Accepts VK_1..VK_4 and the numeric keypad
   equivalents; returns -1 for every other key. */
int PbTabFromKey(int iVirtKey);

/* Advance to the next tab, wrapping Stats -> System -> Clock -> Launcher. */
int PbNextTab(int iActiveTab);

/* Human readable tab label, e.g. "STATS", and NULL for an unknown index. */
const unsigned char* PbTabLabel(int iTab);

#endif
