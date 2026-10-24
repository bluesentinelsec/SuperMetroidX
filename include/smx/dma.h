#ifndef SMX_DMA_H_
#define SMX_DMA_H_

#include "smx/bus.h"

#include <SDL3/SDL.h>

/**
 * General-purpose DMA and HDMA.
 * HDMA reloads at the start of each frame and transfers once per active line.
 */
typedef struct SmxDma {
  SmxBus *bus;
  Uint8 log[64];
  Uint32 log_count;
  bool ran;
  Uint32 hdma_addr[8];
  Uint16 hdma_indirect[8];
  Uint8 hdma_lines[8];
  bool hdma_repeat[8];
  bool hdma_do[8];
  bool hdma_done[8];
} SmxDma;

void smx_dma_init(SmxDma *dma, SmxBus *bus);

/**
 * Run the channels selected by `$420B`.
 * Each set bit starts one general-purpose transfer.
 */
void smx_dma_run(SmxDma *dma, Uint8 channels);

/** Load HDMA line counts from the channels enabled in `$420C`. */
void smx_dma_hdma_init(SmxDma *dma);

/** Run one scanline of HDMA. Channels with a repeat flag transfer again. */
void smx_dma_hdma_line(SmxDma *dma);

#endif /* SMX_DMA_H_ */
