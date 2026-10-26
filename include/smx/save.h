#ifndef SMX_SAVE_H_
#define SMX_SAVE_H_

#include <SDL3/SDL.h>

/**
 * Super Metroid's save slots, for starting play at a chosen save station.
 *
 * SRAM holds three slots of `SMX_SAVE_SLOT_SIZE` bytes from `$0010`. A slot
 * is the game's copy of WRAM `$7E:D7C0`: it starts with Samus's equipment
 * (`$7E:09A2`-`$7E:0A01`), and offsets `$154`, `$156`, and `$158` hold the
 * load mode, the save station, and the area. The game keeps the 16-bit sum
 * of a slot's words at `$0000 + 2 * slot` and its complement at
 * `$0008 + 2 * slot`, and repeats both at `$1FF0` and `$1FF8`. A slot whose
 * sums disagree reads as empty.
 *
 * The game formats SRAM on its first boot and wipes it when its signature at
 * `$1FE0` is missing, so a slot must be written into SRAM the game has
 * already formatted. Progress flags (events, bosses, items picked up, doors,
 * and maps) are written cleared.
 */
#define SMX_SAVE_SLOTS 3u
#define SMX_SAVE_SLOT_SIZE 0x065Cu
#define SMX_SAVE_SRAM_SIZE 0x2000u

/** Equipment bits, as in `$7E:09A2` (equipped) and `$7E:09A4` (collected). */
#define SMX_SAVE_VARIA 0x0001u
#define SMX_SAVE_SPRING_BALL 0x0002u
#define SMX_SAVE_MORPH 0x0004u
#define SMX_SAVE_SCREW 0x0008u
#define SMX_SAVE_GRAVITY 0x0020u
#define SMX_SAVE_HI_JUMP 0x0100u
#define SMX_SAVE_SPACE_JUMP 0x0200u
#define SMX_SAVE_BOMBS 0x1000u
#define SMX_SAVE_SPEED 0x2000u
#define SMX_SAVE_GRAPPLE 0x4000u
#define SMX_SAVE_XRAY 0x8000u

/** Beam bits, as in `$7E:09A6` (equipped) and `$7E:09A8` (collected). */
#define SMX_SAVE_WAVE 0x0001u
#define SMX_SAVE_ICE 0x0002u
#define SMX_SAVE_SPAZER 0x0004u
#define SMX_SAVE_PLASMA 0x0008u
#define SMX_SAVE_CHARGE 0x1000u

/** Areas, as in the slot and `$7E:079F`. */
#define SMX_SAVE_CRATERIA 0u
#define SMX_SAVE_BRINSTAR 1u
#define SMX_SAVE_NORFAIR 2u
#define SMX_SAVE_WRECKED_SHIP 3u
#define SMX_SAVE_MARIDIA 4u
#define SMX_SAVE_TOURIAN 5u

/**
 * Where a slot starts and what Samus carries. `station` indexes the area's
 * load stations: save stations count from 0 (Crateria's 0 is the ship), and
 * entries from `$10` are the game's other load points. Every item and beam
 * is collected and equipped, except that Spazer stays unequipped beside
 * Plasma, as the game does. Each amount is both the current and the maximum.
 */
typedef struct SmxSaveLoadout {
  Uint16 area;
  Uint16 station;
  Uint16 items;
  Uint16 beams;
  Uint16 energy;
  Uint16 reserve;
  Uint16 missiles;
  Uint16 supers;
  Uint16 power_bombs;
} SmxSaveLoadout;

/** A named starting point for play testing. */
typedef struct SmxSaveSection {
  const char *name;
  const char *description;
  SmxSaveLoadout loadout;
} SmxSaveSection;

/** The built-in sections. `count` receives how many there are. */
const SmxSaveSection *smx_save_sections(Uint32 *count);

/** The section called `name`, or NULL. */
const SmxSaveSection *smx_save_section(const char *name);

/**
 * Add the item or beam called `name` (for example `morph` or `charge`) to
 * `items` or `beams`. `all` adds everything. False for an unknown name.
 */
bool smx_save_add_item(const char *name, Uint16 *items, Uint16 *beams);

/** Write `loadout` into `slot` of formatted SRAM and seal it with the game's sums. */
void smx_save_write(Uint8 *sram, Uint32 slot, const SmxSaveLoadout *loadout);

/** True when `slot` holds data whose sums agree, as the game checks. */
bool smx_save_valid(const Uint8 *sram, Uint32 slot);

/** True when the game has formatted `sram`: its signature at `$1FE0` is present. */
bool smx_save_formatted(const Uint8 *sram);

#endif /* SMX_SAVE_H_ */
