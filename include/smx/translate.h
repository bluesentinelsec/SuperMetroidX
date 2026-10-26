#ifndef SMX_TRANSLATE_H_
#define SMX_TRANSLATE_H_

#include "smx/cart.h"
#include "smx/cpu.h"

#include <SDL3/SDL.h>

/** Generated C files a translation is split into, besides the table file. */
#define SMX_TRANSLATE_SHARDS 16u

/** A held joypad state for the trace: `buttons` from frame `at` for `frames` frames. */
typedef struct SmxTranslatePress {
  Uint32 at;
  Uint32 frames;
  Uint16 buttons;
} SmxTranslatePress;

/** Open-addressed set of `SMX_CPU_KEY` values. Slots hold key + 1; 0 is empty. */
typedef struct SmxTranslateSet {
  Uint32 *slots;
  Uint32 capacity;
  Uint32 count;
} SmxTranslateSet;

/**
 * 65816-to-C translator for a LoROM image. `rom` is borrowed.
 * `entries` are the keys a block starts at: an address and the E, M, and X
 * flags it runs under. Exploration decodes straight-line code from each
 * entry, tracking REP, SEP, and CLC/SEC before XCE, and adds the targets of
 * branches, jumps, and calls, plus the return point after each call.
 * `JMP (abs,X)` and `JSR (abs,X)` add the words of their table that point
 * into ROM. `visited` holds every instruction decoded, keyed with its flags;
 * each becomes a translated entry point. Translated code is exact for the ROM
 * bytes at its key, so code that is never reached costs only space, and code
 * reached at a key that was not translated runs in the interpreter.
 */
typedef struct SmxTranslator {
  const Uint8 *rom;
  Uint32 rom_size;
  SmxTranslateSet entries;
  SmxTranslateSet visited;
  Uint32 *work;
  Uint32 work_count;
  Uint32 work_capacity;
} SmxTranslator;

void smx_translate_init(SmxTranslator *translator, const Uint8 *rom, Uint32 rom_size);
void smx_translate_free(SmxTranslator *translator);

/** True when the instruction byte at the 24-bit `addr` is ROM. */
bool smx_translate_in_rom(const SmxTranslator *translator, Uint32 addr);

/** Add an entry. Keys outside ROM are ignored. True when it is new. */
bool smx_translate_add(SmxTranslator *translator, Uint32 key);

/** Add the reset vector in emulation mode and NMI and IRQ under every M and X. */
void smx_translate_add_vectors(SmxTranslator *translator);

/** Follow control flow from every entry not yet explored. */
void smx_translate_explore(SmxTranslator *translator);

/**
 * Run the ROM in the interpreter for `frames` frames, pressing `presses`,
 * and add every key where execution did not simply fall through to the
 * next instruction. `cart` must hold the same image as `rom`.
 */
bool smx_translate_trace(SmxTranslator *translator, const SmxCart *cart, Uint32 frames,
                         const SmxTranslatePress *presses, Uint32 press_count);

/**
 * Write the translation into `dir`. Instructions that fall through to each
 * other form one C function with a label per instruction; branches within it
 * are `goto`s. `smx_game_00.c` through `smx_game_15.c` hold the functions and
 * `smx_game.c` the sorted key table and `smx_game`. A file whose contents
 * would not change is left alone, so an unchanged ROM rebuilds nothing.
 */
bool smx_translate_write(const SmxTranslator *translator, const char *dir);

/** FNV-1a over the image, used to check that a translation matches a loaded ROM. */
Uint32 smx_translate_rom_hash(const Uint8 *rom, Uint32 rom_size);

#endif /* SMX_TRANSLATE_H_ */
