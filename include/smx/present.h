#ifndef SMX_PRESENT_H_
#define SMX_PRESENT_H_

#include <SDL3/SDL.h>

/**
 * Backdrop color drawn when no cartridge is loaded.
 * Components are written as 8-bit channels in the range 0-255.
 */
void smx_backdrop(Uint8 *red, Uint8 *green, Uint8 *blue);

/**
 * Integer-scale a 256x224 picture into a window.
 * The destination is centered. The scale is at least 1.
 */
void smx_present_fit(Sint32 src_w, Sint32 src_h, Sint32 dst_w, Sint32 dst_h, Sint32 *x, Sint32 *y,
                     Sint32 *w, Sint32 *h);

#endif /* SMX_PRESENT_H_ */
