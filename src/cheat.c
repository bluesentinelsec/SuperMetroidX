#include "smx/cheat.h"

/* Super Metroid's RAM, in bank $7E. */
#define SMX_CHEAT_GAME_STATE 0x0998u
#define SMX_CHEAT_ITEMS_EQUIPPED 0x09A2u
#define SMX_CHEAT_ITEMS_COLLECTED 0x09A4u
#define SMX_CHEAT_BEAMS_EQUIPPED 0x09A6u
#define SMX_CHEAT_BEAMS_COLLECTED 0x09A8u
#define SMX_CHEAT_JUMP_BUTTON 0x09B4u
#define SMX_CHEAT_RESERVE_MODE 0x09C0u
#define SMX_CHEAT_ENERGY 0x09C2u
#define SMX_CHEAT_MISSILES 0x09C6u
#define SMX_CHEAT_SUPERS 0x09CAu
#define SMX_CHEAT_POWER_BOMBS 0x09CEu
#define SMX_CHEAT_RESERVE 0x09D4u
/* Damage owed to Samus, taken from her energy early in the next frame. */
#define SMX_CHEAT_DAMAGE 0x0A50u
#define SMX_CHEAT_HYPER_BEAM 0x0A76u
#define SMX_CHEAT_BEAM_SOUND 0x0DC0u
/* The game's queue of transfers to VRAM at the next vblank: 7-byte entries at $D0. */
#define SMX_CHEAT_VRAM_QUEUE 0x00D0u
#define SMX_CHEAT_VRAM_QUEUE_END 0x0330u
#define SMX_CHEAT_Y_SUBSPEED 0x0B2Cu
#define SMX_CHEAT_Y_SPEED 0x0B2Eu
#define SMX_CHEAT_Y_DIRECTION 0x0B36u
/* Palette effect objects: eight slots of IDs, then their state. */
#define SMX_CHEAT_FX_IDS 0x1E7Du
#define SMX_CHEAT_FX_COLOR 0x1E8Du
#define SMX_CHEAT_FX_PRE 0x1EADu
#define SMX_CHEAT_FX_LIST 0x1EBDu
#define SMX_CHEAT_FX_TIMER 0x1ECDu
#define SMX_CHEAT_FX_VAR 0x1EDDu
#define SMX_CHEAT_FX_SLOTS 8u
/* The palette the game copies to CGRAM, and the HUD's tilemap. */
#define SMX_CHEAT_PALETTES 0xC000u
#define SMX_CHEAT_HUD 0xC608u

/* `$0998` in play. */
#define SMX_CHEAT_STATE_PLAY 0x08u

/* Every tank and expansion in the game. */
#define SMX_CHEAT_MAX_ENERGY 1499u
#define SMX_CHEAT_MAX_RESERVE 400u
#define SMX_CHEAT_MAX_MISSILES 230u
#define SMX_CHEAT_MAX_SUPERS 50u
#define SMX_CHEAT_MAX_POWER_BOMBS 50u

/* Infinite ammo before the first expansion of a kind. */
#define SMX_CHEAT_AMMO_START 99u

/* `$09A2` and `$09A6` bits. */
#define SMX_CHEAT_ALL_ITEMS 0xF32Fu
#define SMX_CHEAT_ALL_BEAMS 0x100Fu
#define SMX_CHEAT_SPAZER 0x0004u
#define SMX_CHEAT_GRAPPLE 0x4000u
#define SMX_CHEAT_XRAY 0x8000u

/*
 * From the ROM. The suit palettes, by `$0A74` (0 power, 2 varia, 4 gravity),
 * point into bank $9B. The HUD icons are drawn as the game's own routines
 * draw them on pickup: missiles at $80:99CF, the rest at $80:9A0E-$80:9A3E.
 * Beam graphics and colors load as $90:AC8D loads them for `$09A6`: tiles
 * from bank $9A to VRAM $6300, and colors from bank $90 to sprite palette 6.
 * The hyper beam is given as Mother Brain's fight gives it at $91:E5F0:
 * Charge, Wave, and Plasma equipped and loaded, the rainbow palette effect,
 * and the hyper flag.
 */
#define SMX_CHEAT_SUIT_PALETTES 0x91D727u
#define SMX_CHEAT_SUIT_GRAVITY 4u
#define SMX_CHEAT_SUIT_BANK 0x9Bu
#define SMX_CHEAT_SAMUS_COLORS 0x180u
#define SMX_CHEAT_MISSILE_ICON 0x8099A3u
#define SMX_CHEAT_HUD_BLANK 0x000Fu
#define SMX_CHEAT_HYPER_FX 0xE1F0u
#define SMX_CHEAT_HYPER_FX_BANK 0x8Du
#define SMX_CHEAT_FX_PRE_DEFAULT 0xC526u
#define SMX_CHEAT_BEAM_TILES 0x90C3B1u
#define SMX_CHEAT_BEAM_COLORS 0x90C3C9u
#define SMX_CHEAT_BEAM_TILES_BANK 0x9Au
#define SMX_CHEAT_BEAM_COLORS_BANK 0x90u
#define SMX_CHEAT_BEAM_TILES_SIZE 0x0100u
#define SMX_CHEAT_BEAM_VRAM 0x6300u
#define SMX_CHEAT_BEAM_PALETTE 0x1C0u
#define SMX_CHEAT_HYPER_BEAMS 0x1009u

/* Fly mode's rise, in pixels per frame. */
#define SMX_CHEAT_FLY_SPEED 4u

static const struct {
  const char *name;
  const char *title;
} smx_cheat_names[SMX_CHEAT_COUNT] = {
    {"life", "Infinite life"}, {"ammo", "Infinite ammo"}, {"items", "All items"},
    {"hyper", "Hyper beam"},   {"fly", "Fly mode"},
};

static const struct {
  Uint32 percent;
  const char *name;
} smx_cheat_turbo_speeds[SMX_CHEAT_TURBO_SPEEDS] = {
    {100, "Off"}, {150, "1.5x"}, {200, "2x"}, {300, "3x"}, {400, "4x"},
};

/* The 2x2 HUD icons: where their tiles are in bank $80, and where they go in the HUD. */
static const struct {
  Uint16 tiles;
  Uint16 at;
} smx_cheat_icons[] = {
    {0x99AF, 0x1C}, /* super missiles */
    {0x99B7, 0x22}, /* power bombs */
    {0x99BF, 0x28}, /* grapple */
    {0x99C7, 0x2E}, /* x-ray */
};

static Uint16 smx_cheat_get(const Uint8 *wram, Uint32 at) {
  return (Uint16)(wram[at] | ((Uint16)wram[at + 1u] << 8));
}

static void smx_cheat_put(Uint8 *wram, Uint32 at, Uint16 value) {
  wram[at] = (Uint8)value;
  wram[at + 1u] = (Uint8)(value >> 8);
}

/* A word of a LoROM `rom` at bank:address `address`, or 0 past its end. */
static Uint16 smx_cheat_rom(const SmxCheatRom *rom, Uint32 address) {
  Uint32 at = ((address >> 16) & 0x7Fu) * 0x8000u + (address & 0x7FFFu);

  if (rom->bytes == NULL || at + 1u >= rom->size) {
    return 0;
  }
  return (Uint16)(rom->bytes[at] | ((Uint16)rom->bytes[at + 1u] << 8));
}

void smx_cheat_init(SmxCheats *cheats, const Uint8 *rom, Uint32 rom_size) {
  SDL_memset(cheats, 0, sizeof(*cheats));
  cheats->rom.bytes = rom;
  cheats->rom.size = rom_size;
}

const char *smx_cheat_name(SmxCheatKind kind) {
  return smx_cheat_names[kind].name;
}

const char *smx_cheat_title(SmxCheatKind kind) {
  return smx_cheat_names[kind].title;
}

Uint32 smx_cheat_turbo_percent(Uint32 turbo) {
  return smx_cheat_turbo_speeds[turbo < SMX_CHEAT_TURBO_SPEEDS ? turbo : 0].percent;
}

const char *smx_cheat_turbo_name(Uint32 turbo) {
  return smx_cheat_turbo_speeds[turbo < SMX_CHEAT_TURBO_SPEEDS ? turbo : 0].name;
}

void smx_cheat_turbo_next(SmxCheats *cheats) {
  cheats->turbo = (cheats->turbo + 1u) % SMX_CHEAT_TURBO_SPEEDS;
}

bool smx_cheat_parse(SmxCheats *cheats, const char *list) {
  char name[16];
  const char *at = list;

  while (*at != '\0') {
    Uint32 length = 0;
    Uint32 kind = 0;
    while (at[length] != '\0' && at[length] != ',') {
      length += 1;
    }
    if (length == 0 || length >= sizeof(name)) {
      return false;
    }
    SDL_memcpy(name, at, length);
    name[length] = '\0';
    if (SDL_strcmp(name, "all") == 0) {
      for (; kind < SMX_CHEAT_COUNT; kind += 1) {
        cheats->on[kind] = true;
      }
    } else {
      while (kind < SMX_CHEAT_COUNT && SDL_strcmp(name, smx_cheat_names[kind].name) != 0) {
        kind += 1;
      }
      if (kind == SMX_CHEAT_COUNT) {
        return false;
      }
      cheats->on[kind] = true;
    }
    at += length;
    if (*at == ',') {
      at += 1;
    }
  }
  return true;
}

void smx_cheat_toggle(SmxCheats *cheats, SmxCheatKind kind) {
  cheats->on[kind] = kind == SMX_CHEAT_ITEMS ? true : !cheats->on[kind];
}

bool smx_cheat_playing(const Uint8 *wram) {
  return smx_cheat_get(wram, SMX_CHEAT_GAME_STATE) == SMX_CHEAT_STATE_PLAY;
}

/* Draw a HUD icon from tiles in bank $80, `width` tiles by 2, unless it is drawn. */
static void smx_cheat_icon(const SmxCheats *cheats, Uint8 *wram, Uint16 tiles, Uint32 at,
                           Uint32 width) {
  Uint32 column = 0;

  if ((smx_cheat_get(wram, SMX_CHEAT_HUD + at) & 0x03FFu) != SMX_CHEAT_HUD_BLANK) {
    return;
  }
  for (; column < width; column += 1) {
    Uint32 top = 0x800000u | (Uint32)(tiles + column * 2u);
    smx_cheat_put(wram, SMX_CHEAT_HUD + at + column * 2u, smx_cheat_rom(&cheats->rom, top));
    smx_cheat_put(wram, SMX_CHEAT_HUD + 0x40u + at + column * 2u,
                  smx_cheat_rom(&cheats->rom, top + width * 2u));
  }
}

/* The HUD icons for the ammo and items Samus has, as pickups draw them. */
static void smx_cheat_icons_draw(const SmxCheats *cheats, Uint8 *wram) {
  Uint16 items = smx_cheat_get(wram, SMX_CHEAT_ITEMS_COLLECTED);
  bool has[4];
  Uint32 index = 0;

  if (smx_cheat_get(wram, SMX_CHEAT_MISSILES + 2u) != 0) {
    smx_cheat_icon(cheats, wram, (Uint16)SMX_CHEAT_MISSILE_ICON, 0x14, 3);
  }
  has[0] = smx_cheat_get(wram, SMX_CHEAT_SUPERS + 2u) != 0;
  has[1] = smx_cheat_get(wram, SMX_CHEAT_POWER_BOMBS + 2u) != 0;
  has[2] = (items & SMX_CHEAT_GRAPPLE) != 0;
  has[3] = (items & SMX_CHEAT_XRAY) != 0;
  for (; index < 4u; index += 1) {
    if (has[index]) {
      smx_cheat_icon(cheats, wram, smx_cheat_icons[index].tiles, smx_cheat_icons[index].at, 2);
    }
  }
}

/* A current and maximum pair: both to `most`. */
static void smx_cheat_fill(Uint8 *wram, Uint32 at, Uint16 most) {
  smx_cheat_put(wram, at, most);
  smx_cheat_put(wram, at + 2u, most);
}

/* Ammo of a kind back to its maximum, with some before its first expansion. */
static void smx_cheat_refill(Uint8 *wram, Uint32 at) {
  Uint16 most = smx_cheat_get(wram, at + 2u);

  if (most == 0) {
    most = SMX_CHEAT_AMMO_START;
    smx_cheat_put(wram, at + 2u, most);
  }
  smx_cheat_put(wram, at, most);
}

/* Load the graphics and colors of `beams`, as the game does when they change. */
static void smx_cheat_beam_load(const SmxCheats *cheats, Uint8 *wram, Uint16 beams) {
  Uint32 index = (Uint32)(beams & 0x0FFFu) * 2u;
  Uint16 tiles = smx_cheat_rom(&cheats->rom, SMX_CHEAT_BEAM_TILES + index);
  Uint16 colors = smx_cheat_rom(&cheats->rom, SMX_CHEAT_BEAM_COLORS + index);
  Uint32 at = SMX_CHEAT_VRAM_QUEUE + smx_cheat_get(wram, SMX_CHEAT_VRAM_QUEUE_END);
  Uint32 color = 0;

  if (tiles == 0 || colors == 0) {
    return;
  }
  smx_cheat_put(wram, at, SMX_CHEAT_BEAM_TILES_SIZE);
  smx_cheat_put(wram, at + 2u, tiles);
  wram[at + 4u] = SMX_CHEAT_BEAM_TILES_BANK;
  smx_cheat_put(wram, at + 5u, SMX_CHEAT_BEAM_VRAM);
  smx_cheat_put(wram, SMX_CHEAT_VRAM_QUEUE_END, (Uint16)(at + 7u - SMX_CHEAT_VRAM_QUEUE));
  for (; color < 16u; color += 1) {
    Uint32 from = ((Uint32)SMX_CHEAT_BEAM_COLORS_BANK << 16) | (Uint32)(colors + color * 2u);
    smx_cheat_put(wram, SMX_CHEAT_PALETTES + SMX_CHEAT_BEAM_PALETTE + color * 2u,
                  smx_cheat_rom(&cheats->rom, from));
  }
}

static void smx_cheat_give(SmxCheats *cheats, Uint8 *wram) {
  Uint16 beams = SMX_CHEAT_ALL_BEAMS & (Uint16)~SMX_CHEAT_SPAZER;
  Uint16 palette = smx_cheat_rom(&cheats->rom, SMX_CHEAT_SUIT_PALETTES + SMX_CHEAT_SUIT_GRAVITY);
  Uint32 color = 0;

  smx_cheat_put(wram, SMX_CHEAT_ITEMS_EQUIPPED, SMX_CHEAT_ALL_ITEMS);
  smx_cheat_put(wram, SMX_CHEAT_ITEMS_COLLECTED, SMX_CHEAT_ALL_ITEMS);
  /*
   * Every beam but Spazer, which the game will not equip with Plasma, loaded
   * as equipping them does. The hyper beam, while on, stays in their place.
   */
  smx_cheat_put(wram, SMX_CHEAT_BEAMS_COLLECTED, SMX_CHEAT_ALL_BEAMS);
  if (cheats->hyper_set && cheats->hyper_was == 0) {
    cheats->beams_was = beams;
  } else {
    smx_cheat_put(wram, SMX_CHEAT_BEAMS_EQUIPPED, beams);
    smx_cheat_beam_load(cheats, wram, beams);
  }
  smx_cheat_fill(wram, SMX_CHEAT_ENERGY, SMX_CHEAT_MAX_ENERGY);
  smx_cheat_fill(wram, SMX_CHEAT_MISSILES, SMX_CHEAT_MAX_MISSILES);
  smx_cheat_fill(wram, SMX_CHEAT_SUPERS, SMX_CHEAT_MAX_SUPERS);
  smx_cheat_fill(wram, SMX_CHEAT_POWER_BOMBS, SMX_CHEAT_MAX_POWER_BOMBS);
  smx_cheat_fill(wram, SMX_CHEAT_RESERVE, SMX_CHEAT_MAX_RESERVE);
  if (smx_cheat_get(wram, SMX_CHEAT_RESERVE_MODE) == 0) {
    smx_cheat_put(wram, SMX_CHEAT_RESERVE_MODE, 1);
  }
  /* The suit's colors, which the game sets only when a suit is picked up. */
  if (palette != 0) {
    for (; color < 16u; color += 1) {
      Uint32 from = ((Uint32)SMX_CHEAT_SUIT_BANK << 16) | (Uint32)(palette + color * 2u);
      smx_cheat_put(wram, SMX_CHEAT_PALETTES + SMX_CHEAT_SAMUS_COLORS + color * 2u,
                    smx_cheat_rom(&cheats->rom, from));
    }
  }
  smx_cheat_icons_draw(cheats, wram);
}

/* The hyper beam's palette effect slot, or SMX_CHEAT_FX_SLOTS without one. */
static Uint32 smx_cheat_fx_find(const Uint8 *wram, Uint16 id) {
  Uint32 slot = 0;

  while (slot < SMX_CHEAT_FX_SLOTS && smx_cheat_get(wram, SMX_CHEAT_FX_IDS + slot * 2u) != id) {
    slot += 1;
  }
  return slot;
}

/* Start the rainbow as $8D:C4E9 spawns a palette effect, in the last free slot. */
static void smx_cheat_fx_spawn(const SmxCheats *cheats, Uint8 *wram) {
  Uint32 slot = SMX_CHEAT_FX_SLOTS;
  Uint32 header = ((Uint32)SMX_CHEAT_HYPER_FX_BANK << 16) | SMX_CHEAT_HYPER_FX;
  Uint16 list = smx_cheat_rom(&cheats->rom, header + 2u);

  if (list == 0) {
    return;
  }
  while (slot > 0) {
    Uint32 at = (slot - 1u) * 2u;
    slot -= 1u;
    if (smx_cheat_get(wram, SMX_CHEAT_FX_IDS + at) == 0) {
      smx_cheat_put(wram, SMX_CHEAT_FX_IDS + at, SMX_CHEAT_HYPER_FX);
      smx_cheat_put(wram, SMX_CHEAT_FX_COLOR + at, 0);
      smx_cheat_put(wram, SMX_CHEAT_FX_PRE + at, SMX_CHEAT_FX_PRE_DEFAULT);
      smx_cheat_put(wram, SMX_CHEAT_FX_LIST + at, list);
      smx_cheat_put(wram, SMX_CHEAT_FX_TIMER + at, 1);
      smx_cheat_put(wram, SMX_CHEAT_FX_VAR + at, 0);
      return;
    }
  }
}

static void smx_cheat_hyper(SmxCheats *cheats, Uint8 *wram) {
  Uint32 slot = smx_cheat_fx_find(wram, SMX_CHEAT_HYPER_FX);

  if (cheats->on[SMX_CHEAT_HYPER]) {
    if (!cheats->hyper_set) {
      cheats->hyper_was = smx_cheat_get(wram, SMX_CHEAT_HYPER_BEAM);
      cheats->beams_was = smx_cheat_get(wram, SMX_CHEAT_BEAMS_EQUIPPED);
      cheats->hyper_set = true;
      if (cheats->hyper_was == 0) {
        smx_cheat_put(wram, SMX_CHEAT_BEAMS_EQUIPPED, SMX_CHEAT_HYPER_BEAMS);
        smx_cheat_beam_load(cheats, wram, SMX_CHEAT_HYPER_BEAMS);
        smx_cheat_put(wram, SMX_CHEAT_BEAM_SOUND, 0);
      }
    }
    smx_cheat_put(wram, SMX_CHEAT_HYPER_BEAM, 0x8000u);
    if (slot == SMX_CHEAT_FX_SLOTS) {
      smx_cheat_fx_spawn(cheats, wram);
    }
  } else if (cheats->hyper_set) {
    cheats->hyper_set = false;
    if (cheats->hyper_was != 0) {
      return;
    }
    smx_cheat_put(wram, SMX_CHEAT_HYPER_BEAM, 0);
    if (slot < SMX_CHEAT_FX_SLOTS) {
      smx_cheat_put(wram, SMX_CHEAT_FX_IDS + slot * 2u, 0);
    }
    smx_cheat_put(wram, SMX_CHEAT_BEAMS_EQUIPPED, cheats->beams_was);
    smx_cheat_beam_load(cheats, wram, cheats->beams_was);
  }
}

bool smx_cheat_apply(SmxCheats *cheats, Uint8 *wram, Uint16 joy) {
  bool gave = false;

  if (!smx_cheat_playing(wram)) {
    return false;
  }
  if (cheats->on[SMX_CHEAT_ITEMS]) {
    smx_cheat_give(cheats, wram);
    cheats->on[SMX_CHEAT_ITEMS] = false;
    gave = true;
  }
  if (cheats->on[SMX_CHEAT_LIFE]) {
    Uint16 most = smx_cheat_get(wram, SMX_CHEAT_ENERGY + 2u);
    smx_cheat_put(wram, SMX_CHEAT_ENERGY, most);
    smx_cheat_put(wram, SMX_CHEAT_RESERVE + 2u, smx_cheat_get(wram, SMX_CHEAT_RESERVE));
    /* A hit bigger than full energy is taken before the next refill; leave one. */
    if (most > 0 && smx_cheat_get(wram, SMX_CHEAT_DAMAGE) >= most) {
      smx_cheat_put(wram, SMX_CHEAT_DAMAGE, (Uint16)(most - 1u));
    }
  }
  if (cheats->on[SMX_CHEAT_AMMO]) {
    smx_cheat_refill(wram, SMX_CHEAT_MISSILES);
    smx_cheat_refill(wram, SMX_CHEAT_SUPERS);
    smx_cheat_refill(wram, SMX_CHEAT_POWER_BOMBS);
    smx_cheat_icons_draw(cheats, wram);
  }
  smx_cheat_hyper(cheats, wram);
  if (cheats->on[SMX_CHEAT_FLY] && (joy & smx_cheat_get(wram, SMX_CHEAT_JUMP_BUTTON)) != 0) {
    smx_cheat_put(wram, SMX_CHEAT_Y_DIRECTION, 1);
    smx_cheat_put(wram, SMX_CHEAT_Y_SPEED, SMX_CHEAT_FLY_SPEED);
    smx_cheat_put(wram, SMX_CHEAT_Y_SUBSPEED, 0);
  }
  return gave;
}
