#ifndef _SETTINGS_H_
#define _SETTINGS_H_

#include <windows.h>
#include <tchar.h>
#include "../config/config.h"

/*
   Locating and loading rshl.cfg.

   The file is looked for next to the executable first, because a shell that
   is installed on a storage card should read the configuration that travels
   with it. When that is not there the root of the boot volume is tried, which
   is where a small device usually keeps it: C:\ on the desktop, \ on Windows
   CE.

   A missing or malformed file is not an error the user should be interrupted
   for. The built in defaults are used instead, and the caller is told so it
   can flag the difference in the status bar.
*/

/* Fill pConfig from rshl.cfg. Returns TRUE when a file was found, read and
   accepted; FALSE when the built in defaults had to be used. pConfig is always
   usable. */
BOOL RsSettingsLoad(RSHL_CONFIG* pConfig);

#endif
