#ifndef SMX_ICON_H_
#define SMX_ICON_H_

#include <SDL3/SDL.h>

/** How the icon sits in its image. */
typedef enum SmxIconStyle {
  /** The tile fills the image: Windows, Linux, and the window's own icon. */
  SMX_ICON_FULL,
  /** The tile sits inside Apple's icon grid margin, with a shadow: macOS. */
  SMX_ICON_MACOS
} SmxIconStyle;

/**
 * Draw the game's icon, a `size` x `size` image, as straight-alpha RGBA8
 * rows into `rgba` (`size * size * 4` bytes). The icon is drawn from shapes
 * at any size, so every size is sharp.
 */
void smx_icon_draw(Uint32 size, SmxIconStyle style, Uint8 *rgba);

/**
 * The icon as a surface for SDL_SetWindowIcon, with smaller and larger
 * alternate images for the system to choose from. NULL on failure.
 */
SDL_Surface *smx_icon_surface(void);

#endif /* SMX_ICON_H_ */
