#include "smx/translate.h"

bool smx_translate_ready(const SmxPpu *ppu) {
  if (ppu == NULL) {
    return false;
  }
  return ppu->picture_matches_oracle;
}
