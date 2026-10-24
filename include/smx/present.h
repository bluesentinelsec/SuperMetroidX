#ifndef SMX_PRESENT_H_
#define SMX_PRESENT_H_

#include <SDL3/SDL.h>

/**
 * Backdrop color drawn by the hello-world presenter.
 * Components are written as 8-bit channels in the range 0-255.
 */
void smx_backdrop(Uint8 *red, Uint8 *green, Uint8 *blue);

#endif /* SMX_PRESENT_H_ */
