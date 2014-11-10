#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <3ds.h>

#include "gfx.h"
#include "text.h"

void
gfxDrawText(gfxScreen_t screen,
            gfx3dSide_t side,
            const char  *str,
            u16         x,
            u16         y)
{
  if(!str)
    return;

  u16 fbWidth, fbHeight;
  u8  *fbAdr = gfxGetFramebuffer(screen, side, &fbWidth, &fbHeight);

  drawString(fbAdr, str, y, x-CHAR_SIZE_Y, fbHeight, fbWidth);
}

void
gfxFillColor(gfxScreen_t screen,
             gfx3dSide_t side,
             u8          rgbColor[3])
{
  u16 fbWidth, fbHeight;
  u8  *fbAdr = gfxGetFramebuffer(screen, side, &fbWidth, &fbHeight);

  //TODO : optimize; use GX command ?
  int i;
  for(i = 0; i < fbWidth*fbHeight; ++i)
  {
    *(fbAdr++) = rgbColor[2];
    *(fbAdr++) = rgbColor[1];
    *(fbAdr++) = rgbColor[0];
  }
}
