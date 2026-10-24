#ifndef SMX_TRANSLATE_H_
#define SMX_TRANSLATE_H_

#include "smx/ppu.h"

#include <SDL3/SDL.h>

/**
 * The 65816-to-C translator is a separate tool.
 * It stays disabled until a rendered frame has matched an NTSC oracle.
 */
bool smx_translate_ready(const SmxPpu *ppu);

#endif /* SMX_TRANSLATE_H_ */
