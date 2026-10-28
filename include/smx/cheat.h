#ifndef SMX_CHEAT_H_
#define SMX_CHEAT_H_

#include <SDL3/SDL.h>

/** The cheats, in the order of the number keys that switch them. */
typedef enum SmxCheatKind {
  SMX_CHEAT_LIFE,
  SMX_CHEAT_AMMO,
  SMX_CHEAT_ITEMS,
  SMX_CHEAT_HYPER,
  SMX_CHEAT_FLY,
  SMX_CHEAT_COUNT
} SmxCheatKind;

/** The ROM, for the game's own data the cheats copy, such as HUD icons. */
typedef struct SmxCheatRom {
  const Uint8 *bytes;
  Uint32 size;
} SmxCheatRom;

/**
 * Cheats in the manner of a cheat cartridge: the shell writes Super Metroid's
 * RAM between frames and the game takes it from there. Nothing the runtime
 * emulates changes.
 *
 * Every cheat but `SMX_CHEAT_ITEMS` stays on until switched off. Items are
 * given once, at the next frame in play, and the cheat then turns itself off.
 */
typedef struct SmxCheats {
  bool on[SMX_CHEAT_COUNT];
  SmxCheatRom rom;
  /** The hyper beam is the game's, so switching it off has to clear it once. */
  bool hyper_set;
  /** The game had given the hyper beam itself, so it stays. */
  bool hyper_was;
  /** The beams equipped before the hyper beam replaced them. */
  Uint16 beams_was;
} SmxCheats;

/** No cheats on, for the LoROM image `rom` (NULL draws no HUD icons or suit colors). */
void smx_cheat_init(SmxCheats *cheats, const Uint8 *rom, Uint32 rom_size);

/** The cheat's command-line name, such as "life". */
const char *smx_cheat_name(SmxCheatKind kind);

/** The cheat's name to show, such as "Infinite life". */
const char *smx_cheat_title(SmxCheatKind kind);

/** Turn on the cheats in a comma-separated list of names, or "all". */
bool smx_cheat_parse(SmxCheats *cheats, const char *list);

/** Switch a cheat. Items are only ever switched on, to be given. */
void smx_cheat_toggle(SmxCheats *cheats, SmxCheatKind kind);

/** The game is in play, where the cheats apply. */
bool smx_cheat_playing(const Uint8 *wram);

/**
 * Apply the cheats to `wram` before a frame that reads joypad `joy`. Returns
 * true when items were given.
 */
bool smx_cheat_apply(SmxCheats *cheats, Uint8 *wram, Uint16 joy);

#endif /* SMX_CHEAT_H_ */
