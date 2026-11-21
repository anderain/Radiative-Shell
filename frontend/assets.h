#ifndef _ASSETS_H_
#define _ASSETS_H_

/* Packed 1 bpp images. Each array starts with two header bytes, width first,
   height second, followed by rows packed MSB first and stored top down. Use
   RsGraphImageSize / RsGraphDrawImage to decode them. */

extern const unsigned char IMAGE_WALK_BOY_F0[];
extern const unsigned char IMAGE_WALK_BOY_F1[];
extern const unsigned char IMAGE_WALK_BOY_F2[];
extern const unsigned char IMAGE_WALK_BOY_F3[];
extern const unsigned char IMAGE_WALK_BOY_F4[];
extern const unsigned char IMAGE_WALK_BOY_F5[];
extern const unsigned char IMAGE_RSHL_LOGO[];

extern const unsigned char ICON_DEV[];
extern const unsigned char ICON_EDU[];
extern const unsigned char ICON_GAME[];
extern const unsigned char ICON_GRAPH[];
extern const unsigned char ICON_MEDIA[];
extern const unsigned char ICON_INTERNET[];
extern const unsigned char ICON_OFFICE[];
extern const unsigned char ICON_SCIENCE[];
extern const unsigned char ICON_SETTING[];
extern const unsigned char ICON_UTILS[];

#endif
