#include "smx/save.h"

/* Offsets in a slot. The first $60 bytes mirror WRAM $7E:09A2. */
#define SMX_SAVE_ITEMS_EQUIPPED 0x00u
#define SMX_SAVE_ITEMS_COLLECTED 0x02u
#define SMX_SAVE_BEAMS_EQUIPPED 0x04u
#define SMX_SAVE_BEAMS_COLLECTED 0x06u
#define SMX_SAVE_CONTROLS 0x08u
#define SMX_SAVE_RESERVE_MODE 0x1Eu
#define SMX_SAVE_ENERGY 0x20u
#define SMX_SAVE_MISSILES 0x24u
#define SMX_SAVE_SUPERS 0x28u
#define SMX_SAVE_POWER_BOMBS 0x2Cu
#define SMX_SAVE_RESERVE 0x32u
#define SMX_SAVE_NEW_GAME_FLAGS 0x44u
#define SMX_SAVE_LOAD_MODE 0x154u
#define SMX_SAVE_STATION 0x156u
#define SMX_SAVE_AREA 0x158u

/* Ceres has no save stations; the game saves its start with this load mode. */
#define SMX_SAVE_CERES 6u
#define SMX_SAVE_MODE_CERES 0x1Fu
#define SMX_SAVE_MODE_STATION 0x05u

/*
 * The default buttons in the order of $7E:09AA-$7E:09BE: up, down, left,
 * right, shoot (X), jump (A), dash (B), item cancel (Y), item select, angle
 * down (L), and angle up (R).
 */
static const Uint16 smx_save_controls[11] = {0x0800, 0x0400, 0x0200, 0x0100, 0x0040, 0x0080,
                                             0x8000, 0x4000, 0x2000, 0x0020, 0x0010};

#define SMX_SAVE_EARLY (SMX_SAVE_MORPH | SMX_SAVE_BOMBS)
#define SMX_SAVE_MID                                                                               \
  (SMX_SAVE_EARLY | SMX_SAVE_VARIA | SMX_SAVE_HI_JUMP | SMX_SAVE_SPEED | SMX_SAVE_GRAPPLE)
#define SMX_SAVE_ALL_ITEMS 0xF32Fu
#define SMX_SAVE_ALL_BEAMS 0x100Fu

static const SmxSaveSection smx_save_section_list[] = {
    {"ceres", "Ceres Station, as a new game starts", {SMX_SAVE_CERES, 0, 0, 0, 99, 0, 0, 0, 0}},
    {"landing", "the ship on Zebes, with nothing", {SMX_SAVE_CRATERIA, 0, 0, 0, 99, 0, 0, 0, 0}},
    {"crateria",
     "the Crateria save room, with Morph Ball and Bombs",
     {SMX_SAVE_CRATERIA, 1, SMX_SAVE_EARLY, 0, 99, 0, 10, 0, 0}},
    {"brinstar",
     "the first Brinstar save station, early equipment",
     {SMX_SAVE_BRINSTAR, 0, SMX_SAVE_EARLY, SMX_SAVE_CHARGE, 199, 0, 15, 5, 0}},
    {"norfair",
     "the first Norfair save station, with Varia",
     {SMX_SAVE_NORFAIR, 0, SMX_SAVE_MID, SMX_SAVE_CHARGE | SMX_SAVE_ICE, 399, 0, 30, 10, 10}},
    {"wrecked-ship",
     "the Wrecked Ship entrance, before Phantoon",
     {SMX_SAVE_WRECKED_SHIP, 0x10, SMX_SAVE_MID, SMX_SAVE_CHARGE | SMX_SAVE_ICE | SMX_SAVE_WAVE,
      599, 0, 40, 15, 10}},
    {"maridia",
     "the first Maridia save station, with Gravity",
     {SMX_SAVE_MARIDIA, 0, SMX_SAVE_MID | SMX_SAVE_GRAVITY | SMX_SAVE_SPACE_JUMP,
      SMX_SAVE_CHARGE | SMX_SAVE_ICE | SMX_SAVE_WAVE | SMX_SAVE_SPAZER, 799, 100, 60, 20, 15}},
    {"tourian",
     "the first Tourian save station, with everything",
     {SMX_SAVE_TOURIAN, 0, SMX_SAVE_ALL_ITEMS, SMX_SAVE_ALL_BEAMS, 1499, 400, 230, 50, 50}},
};

static const struct {
  const char *name;
  Uint16 item;
  Uint16 beam;
} smx_save_names[] = {
    {"varia", SMX_SAVE_VARIA, 0},
    {"spring", SMX_SAVE_SPRING_BALL, 0},
    {"morph", SMX_SAVE_MORPH, 0},
    {"screw", SMX_SAVE_SCREW, 0},
    {"gravity", SMX_SAVE_GRAVITY, 0},
    {"hijump", SMX_SAVE_HI_JUMP, 0},
    {"space", SMX_SAVE_SPACE_JUMP, 0},
    {"bombs", SMX_SAVE_BOMBS, 0},
    {"speed", SMX_SAVE_SPEED, 0},
    {"grapple", SMX_SAVE_GRAPPLE, 0},
    {"xray", SMX_SAVE_XRAY, 0},
    {"wave", 0, SMX_SAVE_WAVE},
    {"ice", 0, SMX_SAVE_ICE},
    {"spazer", 0, SMX_SAVE_SPAZER},
    {"plasma", 0, SMX_SAVE_PLASMA},
    {"charge", 0, SMX_SAVE_CHARGE},
    {"all", SMX_SAVE_ALL_ITEMS, SMX_SAVE_ALL_BEAMS},
};

const SmxSaveSection *smx_save_sections(Uint32 *count) {
  *count = (Uint32)(sizeof(smx_save_section_list) / sizeof(smx_save_section_list[0]));
  return smx_save_section_list;
}

const SmxSaveSection *smx_save_section(const char *name) {
  Uint32 count = 0;
  const SmxSaveSection *sections = smx_save_sections(&count);
  Uint32 index = 0;

  for (; index < count; index += 1) {
    if (SDL_strcmp(sections[index].name, name) == 0) {
      return &sections[index];
    }
  }
  return NULL;
}

bool smx_save_add_item(const char *name, Uint16 *items, Uint16 *beams) {
  Uint32 index = 0;

  for (; index < sizeof(smx_save_names) / sizeof(smx_save_names[0]); index += 1) {
    if (SDL_strcmp(smx_save_names[index].name, name) == 0) {
      *items = (Uint16)(*items | smx_save_names[index].item);
      *beams = (Uint16)(*beams | smx_save_names[index].beam);
      return true;
    }
  }
  return false;
}

static void smx_save_put16(Uint8 *at, Uint16 value) {
  at[0] = (Uint8)value;
  at[1] = (Uint8)(value >> 8);
}

static Uint16 smx_save_get16(const Uint8 *at) {
  return (Uint16)(at[0] | ((Uint16)at[1] << 8));
}

static Uint16 smx_save_sum(const Uint8 *sram, Uint32 slot) {
  const Uint8 *data = sram + 0x10u + slot * SMX_SAVE_SLOT_SIZE;
  Uint16 sum = 0;
  Uint32 at = 0;

  for (; at < SMX_SAVE_SLOT_SIZE; at += 2) {
    sum = (Uint16)(sum + smx_save_get16(data + at));
  }
  return sum;
}

void smx_save_write(Uint8 *sram, Uint32 slot, const SmxSaveLoadout *loadout) {
  Uint8 *data = sram + 0x10u + slot * SMX_SAVE_SLOT_SIZE;
  Uint16 beams = loadout->beams;
  Uint16 sum = 0;
  Uint32 index = 0;

  if ((beams & SMX_SAVE_PLASMA) != 0) {
    beams = (Uint16)(beams & ~SMX_SAVE_SPAZER);
  }
  SDL_memset(data, 0, SMX_SAVE_SLOT_SIZE);
  smx_save_put16(data + SMX_SAVE_ITEMS_EQUIPPED, loadout->items);
  smx_save_put16(data + SMX_SAVE_ITEMS_COLLECTED, loadout->items);
  smx_save_put16(data + SMX_SAVE_BEAMS_EQUIPPED, beams);
  smx_save_put16(data + SMX_SAVE_BEAMS_COLLECTED, loadout->beams);
  for (; index < 11u; index += 1) {
    smx_save_put16(data + SMX_SAVE_CONTROLS + index * 2u, smx_save_controls[index]);
  }
  /* Reserve tanks refill on their own unless set to manual. */
  smx_save_put16(data + SMX_SAVE_RESERVE_MODE, loadout->reserve != 0 ? 1u : 0u);
  smx_save_put16(data + SMX_SAVE_ENERGY, loadout->energy);
  smx_save_put16(data + SMX_SAVE_ENERGY + 2u, loadout->energy);
  smx_save_put16(data + SMX_SAVE_MISSILES, loadout->missiles);
  smx_save_put16(data + SMX_SAVE_MISSILES + 2u, loadout->missiles);
  smx_save_put16(data + SMX_SAVE_SUPERS, loadout->supers);
  smx_save_put16(data + SMX_SAVE_SUPERS + 2u, loadout->supers);
  smx_save_put16(data + SMX_SAVE_POWER_BOMBS, loadout->power_bombs);
  smx_save_put16(data + SMX_SAVE_POWER_BOMBS + 2u, loadout->power_bombs);
  smx_save_put16(data + SMX_SAVE_RESERVE, loadout->reserve);
  smx_save_put16(data + SMX_SAVE_RESERVE + 2u, loadout->reserve);
  /* The two words a new game saves as 1. */
  smx_save_put16(data + SMX_SAVE_NEW_GAME_FLAGS, 1);
  smx_save_put16(data + SMX_SAVE_NEW_GAME_FLAGS + 2u, 1);
  smx_save_put16(data + SMX_SAVE_LOAD_MODE,
                 loadout->area == SMX_SAVE_CERES ? SMX_SAVE_MODE_CERES : SMX_SAVE_MODE_STATION);
  smx_save_put16(data + SMX_SAVE_STATION, loadout->station);
  smx_save_put16(data + SMX_SAVE_AREA, loadout->area);

  sum = smx_save_sum(sram, slot);
  smx_save_put16(sram + slot * 2u, sum);
  smx_save_put16(sram + 0x0008u + slot * 2u, (Uint16)~sum);
  smx_save_put16(sram + 0x1FF0u + slot * 2u, sum);
  smx_save_put16(sram + 0x1FF8u + slot * 2u, (Uint16)~sum);
}

bool smx_save_valid(const Uint8 *sram, Uint32 slot) {
  Uint16 sum = smx_save_sum(sram, slot);
  bool first = smx_save_get16(sram + slot * 2u) == sum &&
               smx_save_get16(sram + 0x0008u + slot * 2u) == (Uint16)~sum;
  bool second = smx_save_get16(sram + 0x1FF0u + slot * 2u) == sum &&
                smx_save_get16(sram + 0x1FF8u + slot * 2u) == (Uint16)~sum;
  return first || second;
}

bool smx_save_formatted(const Uint8 *sram) {
  Uint32 at = 0x1FE0u;

  for (; at < 0x1FECu; at += 1) {
    if (sram[at] != 0) {
      return true;
    }
  }
  return false;
}
