#ifndef SMX_TRANSLATE_ROM_H_
#define SMX_TRANSLATE_ROM_H_

#include "smx/cpu.h"

#include <SDL3/SDL.h>

/*
 * A synthetic 64 KiB LoROM for the translator test. It holds no game bytes.
 * Bank $81 has one 8-byte slot per opcode and flag set: the opcode, three
 * operand bytes from a fixed generator, then STP. The flag sets are native
 * mode with each M and X, then emulation mode.
 */
#define SMX_TROM_SIZE 0x10000u
#define SMX_TROM_MODES 5u
#define SMX_TROM_SLOTS (256u * SMX_TROM_MODES)
#define SMX_TROM_BANK 0x81u

static Uint32 smx_trom_slot_addr(Uint32 slot) {
  return (SMX_TROM_BANK << 16) | (0x8000u + slot * 8u);
}

static void smx_trom_mode(Uint32 mode, bool *e, bool *m8, bool *x8) {
  *e = mode == 4u;
  *m8 = mode == 4u || (mode & 2u) != 0;
  *x8 = mode == 4u || (mode & 1u) != 0;
}

static Uint32 smx_trom_slot_key(Uint32 slot) {
  bool e = false;
  bool m8 = false;
  bool x8 = false;

  smx_trom_mode(slot / 256u, &e, &m8, &x8);
  return SMX_CPU_KEY(smx_trom_slot_addr(slot), e, m8, x8);
}

static Uint32 smx_trom_next(Uint32 *state) {
  *state ^= *state << 13;
  *state ^= *state >> 17;
  *state ^= *state << 5;
  return *state;
}

static void smx_trom_build(Uint8 *rom) {
  static const char title[] = "SMX TRANSLATOR TEST  ";
  Uint32 state = 0x1234567u;
  Uint32 slot = 0;
  Uint32 index = 0;

  for (; index < SMX_TROM_SIZE; index += 1) {
    rom[index] = (Uint8)smx_trom_next(&state);
  }
  for (slot = 0; slot < SMX_TROM_SLOTS; slot += 1) {
    Uint8 *at = &rom[0x8000u + slot * 8u];
    at[0] = (Uint8)(slot & 0xFFu);
    for (index = 4; index < 8u; index += 1) {
      at[index] = 0xDB;
    }
  }
  for (index = 0; index < 21u; index += 1) {
    rom[0x7FC0u + index] = (Uint8)title[index];
  }
  rom[0x7FD5] = 0x20;
  rom[0x7FD7] = 0x06;
  rom[0x7FD8] = 0x00;
  rom[0x7FDC] = 0xFF;
  rom[0x7FDD] = 0xFF;
  rom[0x7FDE] = 0x00;
  rom[0x7FDF] = 0x00;
}

#endif /* SMX_TRANSLATE_ROM_H_ */
