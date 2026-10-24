#include "smx/present.h"

void smx_backdrop(Uint8 *red, Uint8 *green, Uint8 *blue) {
  *red = 12;
  *green = 18;
  *blue = 32;
}

void smx_present_fit(Sint32 src_w, Sint32 src_h, Sint32 dst_w, Sint32 dst_h, Sint32 *x, Sint32 *y,
                     Sint32 *w, Sint32 *h) {
  Sint32 scale_w = 1;
  Sint32 scale_h = 1;
  Sint32 scale = 1;
  if (src_w > 0) {
    scale_w = dst_w / src_w;
  }
  if (src_h > 0) {
    scale_h = dst_h / src_h;
  }
  scale = scale_w < scale_h ? scale_w : scale_h;
  if (scale < 1) {
    scale = 1;
  }
  *w = src_w * scale;
  *h = src_h * scale;
  *x = (dst_w - *w) / 2;
  *y = (dst_h - *h) / 2;
}
