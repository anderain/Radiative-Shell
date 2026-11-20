#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <windows.h>

/*
   The shell configuration file.

   Layout is fixed and read line by line, so the order of the sections is part
   of the format. Blank lines and lines starting with '#' are skipped, and
   everything from a '#' to the end of a line is ignored too.

       # Comment
       10|C0|02|30        three theme lines, four hex bytes each
       10|14|1A|1F
       21|22|23|24
       37                 six key lines, one virtual key code each
       39
       38
       40
       32
       13
       2|Games|\Storage Card\breakout.exe     then up to nine launcher slots
       8|System|\Application\Net.exe

   Fields are separated by '|' and surrounding blanks are trimmed. A file that
   does not match the layout is rejected as a whole.
*/

#define RSHL_THEME_COUNT        3
#define RSHL_KEY_COUNT          6
#define RSHL_SLOT_MAX           9

#define RSHL_NAME_MAX           32
#define RSHL_PATH_MAX           160

/* Buffer sizes used by callers of this module. */
#define CFG_PATH_TEXT           260
#define CFG_ERROR_TEXT          160
#define CFG_STATUS_TEXT         2048

/* Launcher slot program type. */
#define RSHL_TYPE_DEVELOPMENT   0
#define RSHL_TYPE_EDUCATION     1
#define RSHL_TYPE_GAMES         2
#define RSHL_TYPE_GRAPHICS      3
#define RSHL_TYPE_INTERNET      4
#define RSHL_TYPE_MULTIMEDIA    5
#define RSHL_TYPE_OFFICE        6
#define RSHL_TYPE_SCIENCE       7
#define RSHL_TYPE_SYSTEM        8
#define RSHL_TYPE_UTILITIES     9

/* One colour theme: four VGA palette indexes in the same order the graph
   layer keeps them (black, dark, light, white). Theme 0 is the one in use. */
typedef struct _RSHL_THEME {
    BYTE    bColors[4];
} RSHL_THEME;

/* Virtual key codes, in file order. */
typedef struct _RSHL_KEYS {
    int     iTabSwitch;         /* cycle to the next tab          */
    int     iFlipScreen;        /* flip a portrait screen         */
    int     iActionPrev;        /* previous item in the content   */
    int     iActionNext;        /* next item in the content       */
    int     iThemeSwitch;       /* cycle the colour theme         */
    int     iConfirm;           /* confirm / launch               */
} RSHL_KEYS;

typedef struct _RSHL_SLOT {
    int     iType;                          /* RSHL_TYPE_*            */
    char    szName[RSHL_NAME_MAX];
    char    szPath[RSHL_PATH_MAX];
} RSHL_SLOT;

typedef struct _RSHL_CONFIG {
    RSHL_THEME  aThemes[RSHL_THEME_COUNT];
    RSHL_KEYS   keys;
    RSHL_SLOT   aSlots[RSHL_SLOT_MAX];
    int         iSlotCount;
    int         iActiveTheme;               /* always 0 for now */
    BOOL        bValid;
} RSHL_CONFIG;

/* Fill pConfig with the built in defaults. */
void ConfigSetDefaults(RSHL_CONFIG* pConfig);

/* Parse a configuration file. Returns FALSE when the file cannot be read or
   does not match the layout; pError (optional) then holds a message that names
   the offending line. */
BOOL ConfigLoad(const char* pszPath, RSHL_CONFIG* pConfig,
                char* pError, int iErrorSize);

/* Write pConfig back out in the canonical layout, comments included. */
BOOL ConfigSave(const char* pszPath, const RSHL_CONFIG* pConfig,
                char* pError, int iErrorSize);

/* Readable name for a slot type, or NULL when the type is unknown. */
const char* ConfigSlotTypeName(int iType);

#endif
