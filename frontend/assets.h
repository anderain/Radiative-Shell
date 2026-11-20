#ifndef _ASSETS_H_
#define _ASSETS_H_

/* Packed 1 bpp images. Each array starts with two header bytes, width first,
   height second, followed by rows packed MSB first and stored top down. Use
   RsGraphImageSize / RsGraphDrawImage to decode them. */

extern const unsigned char IMAGE_WALK_BOY[];
extern const unsigned char IMAGE_RSHL_LOGO[];

#endif
