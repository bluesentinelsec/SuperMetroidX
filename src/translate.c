#include "smx/translate.h"
#include "smx/apu.h"
#include "smx/bus.h"
#include "smx/clock.h"
#include "smx/dma.h"
#include "smx/ppu.h"

enum { SMX_P_C = 0x01, SMX_P_X = 0x10, SMX_P_M = 0x20 };

/* How the interpreter dispatches an opcode, in the order it checks. */
typedef enum SmxOpKind {
  SMX_OP_CONTROL,
  SMX_OP_BRANCH,
  SMX_OP_IMPLIED,
  SMX_OP_MOVE,
  SMX_OP_ALU
} SmxOpKind;

static const char smx_mnemonics[256][4] = {
    "BRK", "ORA", "COP", "ORA", "TSB", "ORA", "ASL", "ORA", "PHP", "ORA", "ASL", "PHD", "TSB",
    "ORA", "ASL", "ORA", "BPL", "ORA", "ORA", "ORA", "TRB", "ORA", "ASL", "ORA", "CLC", "ORA",
    "INC", "TCS", "TRB", "ORA", "ASL", "ORA", "JSR", "AND", "JSL", "AND", "BIT", "AND", "ROL",
    "AND", "PLP", "AND", "ROL", "PLD", "BIT", "AND", "ROL", "AND", "BMI", "AND", "AND", "AND",
    "BIT", "AND", "ROL", "AND", "SEC", "AND", "DEC", "TSC", "BIT", "AND", "ROL", "AND", "RTI",
    "EOR", "WDM", "EOR", "MVP", "EOR", "LSR", "EOR", "PHA", "EOR", "LSR", "PHK", "JMP", "EOR",
    "LSR", "EOR", "BVC", "EOR", "EOR", "EOR", "MVN", "EOR", "LSR", "EOR", "CLI", "EOR", "PHY",
    "TCD", "JML", "EOR", "LSR", "EOR", "RTS", "ADC", "PER", "ADC", "STZ", "ADC", "ROR", "ADC",
    "PLA", "ADC", "ROR", "RTL", "JMP", "ADC", "ROR", "ADC", "BVS", "ADC", "ADC", "ADC", "STZ",
    "ADC", "ROR", "ADC", "SEI", "ADC", "PLY", "TDC", "JMP", "ADC", "ROR", "ADC", "BRA", "STA",
    "BRL", "STA", "STY", "STA", "STX", "STA", "DEY", "BIT", "TXA", "PHB", "STY", "STA", "STX",
    "STA", "BCC", "STA", "STA", "STA", "STY", "STA", "STX", "STA", "TYA", "STA", "TXS", "TXY",
    "STZ", "STA", "STZ", "STA", "LDY", "LDA", "LDX", "LDA", "LDY", "LDA", "LDX", "LDA", "TAY",
    "LDA", "TAX", "PLB", "LDY", "LDA", "LDX", "LDA", "BCS", "LDA", "LDA", "LDA", "LDY", "LDA",
    "LDX", "LDA", "CLV", "LDA", "TSX", "TYX", "LDY", "LDA", "LDX", "LDA", "CPY", "CMP", "REP",
    "CMP", "CPY", "CMP", "DEC", "CMP", "INY", "CMP", "DEX", "WAI", "CPY", "CMP", "DEC", "CMP",
    "BNE", "CMP", "CMP", "CMP", "PEI", "CMP", "DEC", "CMP", "CLD", "CMP", "PHX", "STP", "JML",
    "CMP", "DEC", "CMP", "CPX", "SBC", "SEP", "SBC", "CPX", "SBC", "INC", "SBC", "INX", "SBC",
    "NOP", "XBA", "CPX", "SBC", "INC", "SBC", "BEQ", "SBC", "SBC", "SBC", "PEA", "SBC", "INC",
    "SBC", "SED", "SBC", "PLX", "XCE", "JSR", "SBC", "INC", "SBC",
};

/* The opcodes `smx_cpu_implied` runs. */
static bool smx_op_implied(Uint8 op) {
  switch (op) {
  case 0x18:
  case 0x38:
  case 0x58:
  case 0x78:
  case 0xB8:
  case 0xD8:
  case 0xF8:
  case 0xC2:
  case 0xE2:
  case 0xEB:
  case 0xFB:
  case 0xEA:
  case 0x42:
  case 0xCB:
  case 0xDB:
  case 0x08:
  case 0x28:
  case 0x48:
  case 0x68:
  case 0x5A:
  case 0x7A:
  case 0xDA:
  case 0xFA:
  case 0x0B:
  case 0x2B:
  case 0x4B:
  case 0x8B:
  case 0xAB:
  case 0xF4:
  case 0xD4:
  case 0x62:
  case 0xAA:
  case 0xA8:
  case 0x8A:
  case 0x98:
  case 0x9B:
  case 0xBB:
  case 0x9A:
  case 0xBA:
  case 0x1B:
  case 0x3B:
  case 0x5B:
  case 0x7B:
  case 0x0A:
  case 0x2A:
  case 0x4A:
  case 0x6A:
  case 0x1A:
  case 0x3A:
  case 0x88:
  case 0xC8:
  case 0xCA:
  case 0xE8:
    return true;
  default:
    return false;
  }
}

static SmxOpKind smx_op_kind(Uint8 op) {
  switch (op) {
  case 0x00:
  case 0x02:
  case 0x40:
  case 0x60:
  case 0x6B:
  case 0x20:
  case 0x22:
  case 0x4C:
  case 0x5C:
  case 0x6C:
  case 0x7C:
  case 0xDC:
  case 0xFC:
  case 0x82:
    return SMX_OP_CONTROL;
  default:
    break;
  }
  if ((op & 0x1F) == 0x10 || op == 0x80) {
    return SMX_OP_BRANCH;
  }
  if (smx_op_implied(op)) {
    return SMX_OP_IMPLIED;
  }
  if (op == 0x54 || op == 0x44) {
    return SMX_OP_MOVE;
  }
  return SMX_OP_ALU;
}

static bool smx_op_immediate(Uint8 op) {
  return (op & 0x1F) == 0x09 || op == 0x89 || op == 0xA0 || op == 0xA2 || op == 0xC0 || op == 0xE0;
}

static bool smx_op_index_sized(Uint8 op) {
  return op == 0xA0 || op == 0xA2 || op == 0xC0 || op == 0xE0;
}

static bool smx_op_shift(Uint8 op) {
  switch (op) {
  case 0x06:
  case 0x0E:
  case 0x16:
  case 0x1E:
  case 0x26:
  case 0x2E:
  case 0x36:
  case 0x3E:
  case 0x46:
  case 0x4E:
  case 0x56:
  case 0x5E:
  case 0x66:
  case 0x6E:
  case 0x76:
  case 0x7E:
  case 0xC6:
  case 0xCE:
  case 0xD6:
  case 0xDE:
  case 0xE6:
  case 0xEE:
  case 0xF6:
  case 0xFE:
    return true;
  default:
    return false;
  }
}

/* Operand bytes of the address modes `smx_cpu_address` resolves. */
static Uint8 smx_op_mode_bytes(Uint8 op) {
  switch (op & 0x1F) {
  case 0x0D:
  case 0x0E:
  case 0x0C:
  case 0x1C:
  case 0x19:
  case 0x1D:
  case 0x1E:
    return 2;
  case 0x0F:
  case 0x1F:
    return 3;
  default:
    return 1;
  }
}

/* Instruction length in bytes under the M and X flags. */
static Uint8 smx_op_length(Uint8 op, bool m8, bool x8) {
  switch (op) {
  case 0x40:
  case 0x60:
  case 0x6B:
    return 1;
  case 0x00:
  case 0x02:
  case 0xC2:
  case 0xE2:
  case 0x42:
  case 0xD4:
    return 2;
  case 0x20:
  case 0x4C:
  case 0x6C:
  case 0x7C:
  case 0xDC:
  case 0xFC:
  case 0x82:
  case 0xF4:
  case 0x62:
  case 0x44:
  case 0x54:
    return 3;
  case 0x22:
  case 0x5C:
    return 4;
  default:
    break;
  }
  if ((op & 0x1F) == 0x10 || op == 0x80) {
    return 2;
  }
  if (smx_op_implied(op)) {
    return 1;
  }
  if (smx_op_immediate(op)) {
    bool narrow = smx_op_index_sized(op) ? x8 : m8;
    return narrow ? 2 : 3;
  }
  return (Uint8)(1u + smx_op_mode_bytes(op));
}

/* ---- Key sets ---- */

static void smx_set_free(SmxTranslateSet *set) {
  SDL_free(set->slots);
  SDL_memset(set, 0, sizeof(*set));
}

static Uint32 smx_set_hash(Uint32 key) {
  key ^= key >> 16;
  key *= 0x7FEB352Du;
  key ^= key >> 15;
  key *= 0x846CA68Bu;
  key ^= key >> 16;
  return key;
}

static bool smx_set_has(const SmxTranslateSet *set, Uint32 key) {
  Uint32 index = 0;

  if (set->capacity == 0) {
    return false;
  }
  index = smx_set_hash(key) & (set->capacity - 1u);
  while (set->slots[index] != 0) {
    if (set->slots[index] == key + 1u) {
      return true;
    }
    index = (index + 1u) & (set->capacity - 1u);
  }
  return false;
}

static bool smx_set_put(SmxTranslateSet *set, Uint32 key);

static bool smx_set_grow(SmxTranslateSet *set) {
  SmxTranslateSet bigger;
  Uint32 index = 0;

  bigger.capacity = set->capacity == 0 ? 4096u : set->capacity * 2u;
  bigger.count = 0;
  bigger.slots = (Uint32 *)SDL_calloc(bigger.capacity, sizeof(Uint32));
  if (bigger.slots == NULL) {
    return false;
  }
  for (; index < set->capacity; index += 1) {
    if (set->slots[index] != 0) {
      smx_set_put(&bigger, set->slots[index] - 1u);
    }
  }
  SDL_free(set->slots);
  *set = bigger;
  return true;
}

/* True when `key` was not in the set. */
static bool smx_set_put(SmxTranslateSet *set, Uint32 key) {
  Uint32 index = 0;

  if ((set->count + 1u) * 2u > set->capacity && !smx_set_grow(set)) {
    return false;
  }
  index = smx_set_hash(key) & (set->capacity - 1u);
  while (set->slots[index] != 0) {
    if (set->slots[index] == key + 1u) {
      return false;
    }
    index = (index + 1u) & (set->capacity - 1u);
  }
  set->slots[index] = key + 1u;
  set->count += 1;
  return true;
}

/* ---- ROM access ---- */

static bool smx_rom_offset(const SmxTranslator *translator, Uint32 addr, Uint32 *file) {
  Uint8 bank = (Uint8)((addr >> 16) & 0x7Fu);
  Uint16 off = (Uint16)addr;

  if (off < 0x8000 || bank >= 0x7E) {
    return false;
  }
  *file = ((Uint32)bank << 15) | (Uint32)(off & 0x7FFF);
  return *file < translator->rom_size;
}

bool smx_translate_in_rom(const SmxTranslator *translator, Uint32 addr) {
  Uint32 file = 0;
  return smx_rom_offset(translator, addr, &file);
}

/* The byte `index` bytes after `addr`, wrapping within the bank as PC does. */
static bool smx_rom_byte(const SmxTranslator *translator, Uint32 addr, Uint16 index, Uint8 *value) {
  Uint32 at = (addr & 0x00FF0000u) | (Uint16)((Uint16)addr + index);
  Uint32 file = 0;

  if (!smx_rom_offset(translator, at, &file)) {
    return false;
  }
  *value = translator->rom[file];
  return true;
}

/* A decoded instruction: up to four bytes, all in ROM. */
typedef struct SmxInsn {
  Uint32 addr;
  Uint8 bytes[4];
  Uint8 length;
  Uint8 op;
} SmxInsn;

static bool smx_decode(const SmxTranslator *translator, Uint32 addr, bool m8, bool x8,
                       SmxInsn *insn) {
  Uint8 index = 0;

  insn->addr = addr & 0x00FFFFFFu;
  if (!smx_rom_byte(translator, addr, 0, &insn->op)) {
    return false;
  }
  insn->length = smx_op_length(insn->op, m8, x8);
  for (; index < insn->length; index += 1) {
    if (!smx_rom_byte(translator, addr, index, &insn->bytes[index])) {
      return false;
    }
  }
  return true;
}

static Uint16 smx_insn_word(const SmxInsn *insn) {
  return (Uint16)(insn->bytes[1] | ((Uint16)insn->bytes[2] << 8));
}

static Uint32 smx_insn_long(const SmxInsn *insn) {
  return (Uint32)smx_insn_word(insn) | ((Uint32)insn->bytes[3] << 16);
}

static Uint32 smx_insn_next(const SmxInsn *insn) {
  return (insn->addr & 0x00FF0000u) | (Uint16)((Uint16)insn->addr + insn->length);
}

/* Target of a relative branch or BRL. */
static Uint32 smx_insn_branch_target(const SmxInsn *insn) {
  Uint32 next = smx_insn_next(insn);
  Uint16 offset = insn->op == 0x82 ? smx_insn_word(insn) : (Uint16)(Sint16)(Sint8)insn->bytes[1];

  return (next & 0x00FF0000u) | (Uint16)((Uint16)next + offset);
}

/* Flags a block tracks while it runs straight through. */
typedef struct SmxMode {
  bool e;
  bool m8;
  bool x8;
  /* 0 or 1 when CLC or SEC came just before, else 2. */
  Uint8 carry;
} SmxMode;

static SmxMode smx_mode_of(Uint32 key) {
  SmxMode mode;

  mode.e = (key & 4u) != 0;
  mode.m8 = (key & 2u) != 0;
  mode.x8 = (key & 1u) != 0;
  mode.carry = 2;
  return mode;
}

static Uint32 smx_mode_key(Uint32 addr, const SmxMode *mode) {
  return SMX_CPU_KEY(addr, mode->e, mode->m8, mode->x8);
}

/*
 * Flags after `insn` when it falls through. False when they cannot be known
 * statically, which ends a block.
 */
static bool smx_mode_after(const SmxInsn *insn, SmxMode *mode) {
  Uint8 carry = 2;

  switch (insn->op) {
  case 0xC2:
    if (!mode->e) {
      mode->m8 = mode->m8 && (insn->bytes[1] & SMX_P_M) == 0;
      mode->x8 = mode->x8 && (insn->bytes[1] & SMX_P_X) == 0;
    }
    break;
  case 0xE2:
    mode->m8 = mode->m8 || (insn->bytes[1] & SMX_P_M) != 0;
    mode->x8 = mode->x8 || (insn->bytes[1] & SMX_P_X) != 0;
    break;
  case 0xFB:
    if (mode->carry == 2) {
      return false;
    }
    /* Entering native mode leaves M and X set; emulation forces them. */
    mode->e = mode->carry == 1;
    mode->m8 = true;
    mode->x8 = true;
    break;
  case 0x28:
  case 0x40:
    return false;
  case 0x18:
    carry = 0;
    break;
  case 0x38:
    carry = 1;
    break;
  default:
    break;
  }
  mode->carry = carry;
  return true;
}

/* True when control never falls through to the next instruction inside a block. */
static bool smx_insn_ends(const SmxInsn *insn) {
  switch (insn->op) {
  case 0x00:
  case 0x02:
  case 0x40:
  case 0x60:
  case 0x6B:
  case 0x20:
  case 0x22:
  case 0x4C:
  case 0x5C:
  case 0x6C:
  case 0x7C:
  case 0xDC:
  case 0xFC:
  case 0x80:
  case 0x82:
  case 0x44:
  case 0x54:
  case 0xCB:
  case 0xDB:
  case 0x28:
    return true;
  default:
    return false;
  }
}

/* ---- Exploration ---- */

void smx_translate_init(SmxTranslator *translator, const Uint8 *rom, Uint32 rom_size) {
  SDL_memset(translator, 0, sizeof(*translator));
  translator->rom = rom;
  translator->rom_size = rom_size;
}

void smx_translate_free(SmxTranslator *translator) {
  smx_set_free(&translator->entries);
  smx_set_free(&translator->visited);
  SDL_free(translator->work);
  SDL_memset(translator, 0, sizeof(*translator));
}

bool smx_translate_add(SmxTranslator *translator, Uint32 key) {
  if (!smx_translate_in_rom(translator, key >> 3)) {
    return false;
  }
  /* Emulation mode always has 8-bit A and index registers. */
  if ((key & 4u) != 0 && (key & 3u) != 3u) {
    return false;
  }
  if (!smx_set_put(&translator->entries, key)) {
    return false;
  }
  if (translator->work_count == translator->work_capacity) {
    Uint32 capacity = translator->work_capacity == 0 ? 1024u : translator->work_capacity * 2u;
    Uint32 *work = (Uint32 *)SDL_realloc(translator->work, capacity * sizeof(Uint32));
    if (work == NULL) {
      return true;
    }
    translator->work = work;
    translator->work_capacity = capacity;
  }
  translator->work[translator->work_count] = key;
  translator->work_count += 1;
  return true;
}

static Uint16 smx_rom_vector(const SmxTranslator *translator, Uint16 addr) {
  Uint8 lo = 0;
  Uint8 hi = 0;

  if (!smx_rom_byte(translator, addr, 0, &lo) || !smx_rom_byte(translator, addr, 1, &hi)) {
    return 0;
  }
  return (Uint16)(lo | ((Uint16)hi << 8));
}

void smx_translate_add_vectors(SmxTranslator *translator) {
  static const Uint16 native[] = {0xFFEA, 0xFFEE, 0xFFE6, 0xFFE4};
  Uint8 index = 0;
  Uint8 flags = 0;

  smx_translate_add(translator, SMX_CPU_KEY(smx_rom_vector(translator, 0xFFFC), true, true, true));
  for (; index < sizeof(native) / sizeof(native[0]); index += 1) {
    Uint16 target = smx_rom_vector(translator, native[index]);
    for (flags = 0; flags < 4u; flags += 1) {
      smx_translate_add(translator,
                        SMX_CPU_KEY(target, false, (flags & 2u) != 0, (flags & 1u) != 0));
    }
  }
}

/*
 * `JMP (abs,X)` and `JSR (abs,X)` index a table of addresses in the program
 * bank. Take words from the table while they point at ROM code space.
 */
#define SMX_TABLE_LIMIT 256u

static void smx_explore_table(SmxTranslator *translator, const SmxInsn *insn, const SmxMode *mode) {
  Uint32 bank = insn->addr & 0x00FF0000u;
  Uint16 base = smx_insn_word(insn);
  Uint32 index = 0;

  for (; index < SMX_TABLE_LIMIT; index += 1) {
    Uint32 at = bank | (Uint16)(base + index * 2u);
    Uint8 lo = 0;
    Uint8 hi = 0;
    Uint16 target = 0;
    if (!smx_rom_byte(translator, at, 0, &lo) || !smx_rom_byte(translator, at, 1, &hi)) {
      return;
    }
    target = (Uint16)(lo | ((Uint16)hi << 8));
    if (target < 0x8000) {
      return;
    }
    smx_translate_add(translator, smx_mode_key(bank | target, mode));
  }
}

/* Decode from `key` until control leaves, adding every target it names. */
static void smx_explore_from(SmxTranslator *translator, Uint32 key) {
  SmxMode mode = smx_mode_of(key);
  Uint32 addr = key >> 3;

  for (;;) {
    SmxInsn insn;
    Uint32 here = smx_mode_key(addr, &mode);
    if (smx_set_has(&translator->visited, here) ||
        !smx_decode(translator, addr, mode.m8, mode.x8, &insn)) {
      return;
    }
    smx_set_put(&translator->visited, here);
    switch (insn.op) {
    case 0x20:
    case 0x4C:
      smx_translate_add(translator,
                        smx_mode_key((addr & 0x00FF0000u) | smx_insn_word(&insn), &mode));
      break;
    case 0x22:
    case 0x5C:
      smx_translate_add(translator, smx_mode_key(smx_insn_long(&insn), &mode));
      break;
    case 0x80:
    case 0x82:
      smx_translate_add(translator, smx_mode_key(smx_insn_branch_target(&insn), &mode));
      break;
    case 0x7C:
    case 0xFC:
      smx_explore_table(translator, &insn, &mode);
      break;
    default:
      if ((insn.op & 0x1F) == 0x10) {
        smx_translate_add(translator, smx_mode_key(smx_insn_branch_target(&insn), &mode));
      }
      break;
    }
    /* Calls, block moves, and WAI come back to the next instruction. */
    switch (insn.op) {
    case 0x20:
    case 0x22:
    case 0xFC:
    case 0x44:
    case 0x54:
    case 0xCB:
      smx_translate_add(translator, smx_mode_key(smx_insn_next(&insn), &mode));
      break;
    default:
      break;
    }
    if (smx_insn_ends(&insn) || !smx_mode_after(&insn, &mode)) {
      return;
    }
    addr = smx_insn_next(&insn);
  }
}

void smx_translate_explore(SmxTranslator *translator) {
  while (translator->work_count != 0) {
    translator->work_count -= 1;
    smx_explore_from(translator, translator->work[translator->work_count]);
  }
}

/* ---- Key files ---- */

bool smx_translate_load_keys(SmxTranslator *translator, const char *path, const char *header,
                             Uint32 *added) {
  size_t size = 0;
  char *text = (char *)SDL_LoadFile(path, &size);
  char *line = text;
  Uint32 count = 0;

  if (text == NULL) {
    return false;
  }
  if (header != NULL && SDL_strncmp(text, header, SDL_strlen(header)) != 0) {
    SDL_free(text);
    return false;
  }
  while (line != NULL && *line != '\0') {
    char *end = SDL_strchr(line, '\n');
    if (end != NULL) {
      *end = '\0';
    }
    if (line[0] != '#' && line[0] != '\0' && line[0] != '\r' &&
        smx_translate_add(translator, (Uint32)SDL_strtoul(line, NULL, 16))) {
      count += 1;
    }
    line = end == NULL ? NULL : end + 1;
  }
  SDL_free(text);
  if (added != NULL) {
    *added = count;
  }
  return true;
}

bool smx_translate_save_keys(const SmxTranslator *translator, const char *path,
                             const char *header) {
  SDL_IOStream *out = SDL_IOFromFile(path, "wb");
  Uint32 index = 0;
  char line[32];

  if (out == NULL) {
    SDL_Log("translate: cannot write %s: %s", path, SDL_GetError());
    return false;
  }
  if (header != NULL) {
    SDL_WriteIO(out, header, SDL_strlen(header));
  }
  for (; index < translator->entries.capacity; index += 1) {
    if (translator->entries.slots[index] != 0) {
      Sint32 length = SDL_snprintf(line, sizeof(line), "%x\n",
                                   (unsigned)(translator->entries.slots[index] - 1u));
      SDL_WriteIO(out, line, (size_t)length);
    }
  }
  return SDL_CloseIO(out);
}

/* ---- Tracing ---- */

static Uint16 smx_trace_buttons(const SmxTranslatePress *presses, Uint32 count, Uint32 frame) {
  Uint16 buttons = 0;
  Uint32 index = 0;

  for (; index < count; index += 1) {
    if (frame >= presses[index].at && frame < presses[index].at + presses[index].frames) {
      buttons = (Uint16)(buttons | presses[index].buttons);
    }
  }
  return buttons;
}

bool smx_translate_trace(SmxTranslator *translator, const SmxCart *cart, Uint32 frames,
                         const SmxTranslatePress *presses, Uint32 press_count) {
  SmxBus bus;
  SmxCpu cpu;
  SmxDma dma;
  SmxPpu *ppu = (SmxPpu *)SDL_malloc(sizeof(SmxPpu));
  SmxApu *apu = (SmxApu *)SDL_malloc(sizeof(SmxApu));
  SmxClock clock;
  Uint32 frame = 0;
  Uint32 expect = 0xFFFFFFFFu;

  if (ppu == NULL || apu == NULL) {
    SDL_free(ppu);
    SDL_free(apu);
    return false;
  }
  smx_bus_init(&bus, cart->bytes, cart->size, smx_cart_sram_bytes(cart->sram_size_code));
  smx_cpu_init(&cpu, &bus);
  smx_dma_init(&dma, &bus);
  smx_ppu_init(ppu, &bus);
  smx_apu_init(apu, &bus);
  smx_clock_init(&clock, &cpu, &dma);
  clock.ppu = ppu;
  smx_cpu_reset(&cpu);
  for (; frame < frames && !cpu.stopped; frame += 1) {
    Uint64 now = clock.frame;
    clock.joy = smx_trace_buttons(presses, press_count, frame + 1u);
    while (clock.frame == now && !cpu.stopped) {
      Uint32 key = smx_cpu_key(&cpu);
      SmxInsn insn;
      SmxMode mode = smx_mode_of(key);
      if (key != expect && !cpu.waiting) {
        smx_translate_add(translator, key);
      }
      expect = 0xFFFFFFFFu;
      if (!cpu.waiting && smx_decode(translator, key >> 3, mode.m8, mode.x8, &insn) &&
          !smx_insn_ends(&insn) && smx_mode_after(&insn, &mode)) {
        expect = smx_mode_key(smx_insn_next(&insn), &mode);
      }
      smx_clock_step(&clock);
    }
  }
  smx_ppu_quit(ppu);
  smx_bus_quit(&bus);
  SDL_free(ppu);
  SDL_free(apu);
  return true;
}

/* ---- Emission ---- */

typedef struct SmxText {
  char *data;
  Uint32 length;
  Uint32 capacity;
  bool failed;
} SmxText;

static void smx_text_put(SmxText *text, const char *format, ...) {
  va_list args;
  Sint32 needed = 0;

  if (text->failed) {
    return;
  }
  for (;;) {
    Uint32 room = text->capacity - text->length;
    va_start(args, format);
    needed =
        SDL_vsnprintf(text->data == NULL ? NULL : text->data + text->length, room, format, args);
    va_end(args);
    if (needed < 0) {
      text->failed = true;
      return;
    }
    if ((Uint32)needed < room) {
      text->length += (Uint32)needed;
      return;
    }
    {
      Uint32 capacity = text->capacity == 0 ? 65536u : text->capacity * 2u;
      char *data = NULL;
      while (capacity - text->length <= (Uint32)needed) {
        capacity *= 2u;
      }
      data = (char *)SDL_realloc(text->data, capacity);
      if (data == NULL) {
        text->failed = true;
        return;
      }
      text->data = data;
      text->capacity = capacity;
    }
  }
}

/* Write `text` to `path` unless the file already holds exactly that. */
static bool smx_text_save(const SmxText *text, const char *path) {
  size_t old_size = 0;
  void *old = SDL_LoadFile(path, &old_size);
  bool same = old != NULL && old_size == (size_t)text->length &&
              SDL_memcmp(old, text->data, text->length) == 0;
  bool ok = true;

  SDL_free(old);
  if (same) {
    return true;
  }
  ok = SDL_SaveFile(path, text->data, (size_t)text->length);
  if (!ok) {
    SDL_Log("translate: cannot write %s: %s", path, SDL_GetError());
  }
  return ok;
}

static const char *smx_branch_condition(Uint8 op) {
  switch (op) {
  case 0x10:
    return "(c->p & 0x80) == 0";
  case 0x30:
    return "(c->p & 0x80) != 0";
  case 0x50:
    return "(c->p & 0x40) == 0";
  case 0x70:
    return "(c->p & 0x40) != 0";
  case 0x80:
    return "true";
  case 0x90:
    return "(c->p & 0x01) == 0";
  case 0xB0:
    return "(c->p & 0x01) != 0";
  case 0xD0:
    return "(c->p & 0x02) == 0";
  default:
    return "(c->p & 0x02) != 0";
  }
}

/* Continue at `addr` in this run: the boundary first, as for any next instruction. */
static void smx_emit_goto(SmxText *text, Uint32 addr, const char *indent) {
  smx_text_put(text, "%sif (!smx_cpu_next(c)) {\n%s  return;\n%s}\n%sgoto L_%04X;\n", indent,
               indent, indent, indent, (unsigned)(addr & 0xFFFF));
}

static void smx_emit_fetches(SmxText *text, Uint8 count) {
  for (; count != 0; count -= 1) {
    smx_text_put(text, "  smx_cpu_fetch(c);\n");
  }
}

/* The operation of an ALU-group opcode on value `v` and, for memory forms, address `ea`. */
static void smx_emit_operate(SmxText *text, Uint8 op, const SmxMode *mode) {
  const char *index_x = mode->x8 ? "(Uint16)(c->x & 0xFF)" : "c->x";
  const char *index_y = mode->x8 ? "(Uint16)(c->y & 0xFF)" : "c->y";

  if ((op & 0xE0) == 0x80 && op != 0x89) {
    if (op == 0x84 || op == 0x94 || op == 0x8C) {
      smx_text_put(text, mode->x8 ? "  smx_cpu_write8(c, ea, (Uint8)c->y);\n"
                                  : "  smx_cpu_write16(c, ea, c->y);\n");
    } else if (op == 0x86 || op == 0x96 || op == 0x8E) {
      smx_text_put(text, mode->x8 ? "  smx_cpu_write8(c, ea, (Uint8)c->x);\n"
                                  : "  smx_cpu_write16(c, ea, c->x);\n");
    } else if (op == 0x9C || op == 0x9E) {
      smx_text_put(text,
                   mode->m8 ? "  smx_cpu_write8(c, ea, 0);\n" : "  smx_cpu_write16(c, ea, 0);\n");
    } else {
      smx_text_put(text, "  smx_cpu_write_m(c, ea, c->a);\n");
    }
    return;
  }
  switch (op) {
  case 0xA1:
  case 0xA3:
  case 0xA5:
  case 0xA7:
  case 0xA9:
  case 0xAD:
  case 0xAF:
  case 0xB1:
  case 0xB2:
  case 0xB3:
  case 0xB5:
  case 0xB7:
  case 0xB9:
  case 0xBD:
  case 0xBF:
    smx_text_put(text, "  smx_cpu_set_a(c, v);\n");
    return;
  case 0xA0:
  case 0xA4:
  case 0xAC:
  case 0xB4:
  case 0xBC:
    smx_text_put(text, "  smx_cpu_set_y(c, v);\n");
    return;
  case 0xA2:
  case 0xA6:
  case 0xAE:
  case 0xB6:
  case 0xBE:
    smx_text_put(text, "  smx_cpu_set_x(c, v);\n");
    return;
  case 0xC0:
  case 0xC4:
  case 0xCC:
    smx_text_put(text, "  smx_cpu_compare(c, %s, v, %s);\n", index_y, mode->x8 ? "true" : "false");
    return;
  case 0xE0:
  case 0xE4:
  case 0xEC:
    smx_text_put(text, "  smx_cpu_compare(c, %s, v, %s);\n", index_x, mode->x8 ? "true" : "false");
    return;
  case 0x64:
  case 0x74:
    smx_text_put(text,
                 mode->m8 ? "  smx_cpu_write8(c, ea, 0);\n" : "  smx_cpu_write16(c, ea, 0);\n");
    return;
  case 0x24:
  case 0x2C:
  case 0x34:
  case 0x3C:
  case 0x89:
    smx_text_put(text, "  smx_cpu_bit(c, 0x%02X, v);\n", op);
    return;
  default:
    break;
  }
  if ((op & 0xE0) == 0x60) {
    smx_text_put(text, "  smx_cpu_adc(c, v);\n");
  } else if ((op & 0xE0) == 0xE0) {
    smx_text_put(text, "  smx_cpu_sbc(c, v);\n");
  } else if ((op & 0xE0) == 0xC0) {
    smx_text_put(text, "  smx_cpu_logic(c, 0x%02X, v);\n", op);
  } else {
    smx_text_put(text, "  smx_cpu_set_a(c, smx_cpu_logic(c, 0x%02X, v));\n", op);
  }
}

/* True when the operation reads its operand from memory, and how wide. */
static const char *smx_alu_read(Uint8 op) {
  if ((op & 0xE0) == 0x80 && op != 0x89) {
    return NULL;
  }
  if (op == 0x64 || op == 0x74) {
    return NULL;
  }
  switch (op) {
  case 0xA0:
  case 0xA4:
  case 0xAC:
  case 0xB4:
  case 0xBC:
  case 0xA2:
  case 0xA6:
  case 0xAE:
  case 0xB6:
  case 0xBE:
  case 0xC0:
  case 0xC4:
  case 0xCC:
  case 0xE0:
  case 0xE4:
  case 0xEC:
    return "smx_cpu_read_x";
  default:
    return "smx_cpu_read_m";
  }
}

/*
 * Every decoded instruction, grouped into runs: chains of instructions that
 * fall through to each other under the tracked flags. Each run becomes one C
 * function with a label per instruction, so a block can be entered at any of
 * its instructions (an interrupt returns mid-run) and branches within a run
 * stay in C. `keys` is sorted; `owner` is each key's run, `link` the next key
 * in its run or `SMX_RUN_END`, and `starts` each run's first key.
 */
#define SMX_RUN_END 0xFFFFFFFFu

typedef struct SmxRuns {
  Uint32 *keys;
  Uint32 *owner;
  Uint32 *link;
  Uint32 *starts;
  Uint32 count;
  Uint32 run_count;
} SmxRuns;

static Uint32 smx_runs_find(const SmxRuns *runs, Uint32 key) {
  Uint32 lo = 0;
  Uint32 hi = runs->count;

  while (lo < hi) {
    Uint32 mid = lo + (hi - lo) / 2u;
    if (runs->keys[mid] < key) {
      lo = mid + 1u;
    } else {
      hi = mid;
    }
  }
  return lo < runs->count && runs->keys[lo] == key ? lo : SMX_RUN_END;
}

/* True when `key` is an instruction of run `run`, so control can stay in C. */
static bool smx_runs_local(const SmxRuns *runs, Uint32 run, Uint32 key) {
  Uint32 index = 0;

  if (runs == NULL) {
    return false;
  }
  index = smx_runs_find(runs, key);
  return index != SMX_RUN_END && runs->owner[index] == run;
}

/*
 * One instruction. Returns false when control leaves the block after it;
 * the emitted code has then already returned on every path.
 */
static bool smx_emit_insn(SmxText *text, const SmxInsn *insn, const SmxMode *mode,
                          const SmxRuns *runs, Uint32 run) {
  Uint8 op = insn->op;
  Uint32 next = smx_insn_next(insn);
  const char *index_x = mode->x8 ? "(Uint16)(c->x & 0xFF)" : "c->x";
  Uint8 i = 0;

  smx_text_put(text, "  /* $%02X:%04X", (unsigned)(insn->addr >> 16),
               (unsigned)(insn->addr & 0xFFFF));
  for (i = 0; i < 4; i += 1) {
    if (i < insn->length) {
      smx_text_put(text, " %02X", insn->bytes[i]);
    } else {
      smx_text_put(text, "   ");
    }
  }
  smx_text_put(text, "  %s */\n", smx_mnemonics[op]);

  switch (smx_op_kind(op)) {
  case SMX_OP_CONTROL:
    switch (op) {
    case 0x00:
      smx_text_put(text, "  smx_cpu_fetch(c);\n  smx_cpu_interrupt(c, 0xFFE6, 0xFFFE);\n");
      break;
    case 0x02:
      smx_text_put(text, "  smx_cpu_fetch(c);\n  smx_cpu_interrupt(c, 0xFFE4, 0xFFF4);\n");
      break;
    case 0x40:
      smx_text_put(text, "  smx_cpu_fetch(c);\n  smx_cpu_idle(c);\n  smx_cpu_idle(c);\n"
                         "  smx_cpu_set_p(c, smx_cpu_pull8(c));\n  c->pc = smx_cpu_pull16(c);\n");
      if (!mode->e) {
        smx_text_put(text, "  c->pb = smx_cpu_pull8(c);\n");
      }
      break;
    case 0x60:
      smx_text_put(text, "  smx_cpu_fetch(c);\n  smx_cpu_idle(c);\n  smx_cpu_idle(c);\n"
                         "  c->pc = smx_cpu_pull16(c);\n  smx_cpu_idle(c);\n"
                         "  c->pc = (Uint16)(c->pc + 1);\n");
      break;
    case 0x6B:
      smx_text_put(text,
                   "  smx_cpu_fetch(c);\n  smx_cpu_idle(c);\n  smx_cpu_idle(c);\n"
                   "  c->pc = (Uint16)(smx_cpu_pull16(c) + 1);\n  c->pb = smx_cpu_pull8(c);\n");
      break;
    case 0x20:
      smx_emit_fetches(text, 3);
      smx_text_put(text,
                   "  smx_cpu_idle(c);\n  smx_cpu_push16(c, (Uint16)(c->pc - 1));\n"
                   "  c->pc = 0x%04X;\n",
                   smx_insn_word(insn));
      break;
    case 0x22:
      smx_text_put(text,
                   "  smx_cpu_fetch(c);\n  smx_cpu_fetch(c);\n  smx_cpu_fetch(c);\n"
                   "  smx_cpu_push8(c, c->pb);\n  smx_cpu_idle(c);\n  smx_cpu_fetch(c);\n"
                   "  smx_cpu_push16(c, (Uint16)(c->pc - 1));\n  c->pb = 0x%02X;\n"
                   "  c->pc = 0x%04X;\n",
                   insn->bytes[3], smx_insn_word(insn));
      break;
    case 0x4C:
      smx_emit_fetches(text, 3);
      smx_text_put(text, "  c->pc = 0x%04X;\n", smx_insn_word(insn));
      if (smx_runs_local(runs, run,
                         smx_mode_key((insn->addr & 0x00FF0000u) | smx_insn_word(insn), mode))) {
        smx_emit_goto(text, smx_insn_word(insn), "  ");
        return false;
      }
      break;
    case 0x5C:
      smx_emit_fetches(text, 4);
      smx_text_put(text, "  c->pb = 0x%02X;\n  c->pc = 0x%04X;\n", insn->bytes[3],
                   smx_insn_word(insn));
      break;
    case 0x6C:
      smx_emit_fetches(text, 3);
      smx_text_put(text, "  c->pc = smx_cpu_read_bank16(c, 0, 0x%04X);\n", smx_insn_word(insn));
      break;
    case 0x7C:
      smx_emit_fetches(text, 3);
      smx_text_put(text,
                   "  smx_cpu_idle(c);\n"
                   "  c->pc = smx_cpu_read_bank16(c, c->pb, (Uint16)(0x%04X + %s));\n",
                   smx_insn_word(insn), index_x);
      break;
    case 0xDC:
      smx_emit_fetches(text, 3);
      smx_text_put(text,
                   "  {\n    Uint16 target = smx_cpu_read_bank16(c, 0, 0x%04X);\n"
                   "    c->pb = smx_cpu_read8(c, 0x%04X);\n    c->pc = target;\n  }\n",
                   smx_insn_word(insn), (Uint16)(smx_insn_word(insn) + 2u));
      break;
    case 0xFC:
      /* The return address is pushed between the two operand bytes. */
      smx_emit_fetches(text, 2);
      smx_text_put(text,
                   "  smx_cpu_push16(c, c->pc);\n  smx_cpu_fetch(c);\n  smx_cpu_idle(c);\n"
                   "  c->pc = smx_cpu_read_bank16(c, c->pb, (Uint16)(0x%04X + %s));\n",
                   smx_insn_word(insn), index_x);
      break;
    default: /* BRL */
      smx_emit_fetches(text, 3);
      smx_text_put(text, "  smx_cpu_idle(c);\n  c->pc = 0x%04X;\n",
                   (unsigned)(smx_insn_branch_target(insn) & 0xFFFF));
      if (smx_runs_local(runs, run, smx_mode_key(smx_insn_branch_target(insn), mode))) {
        smx_emit_goto(text, smx_insn_branch_target(insn), "  ");
        return false;
      }
      break;
    }
    smx_text_put(text, "  return;\n");
    return false;
  case SMX_OP_BRANCH:
    smx_text_put(text, "  smx_cpu_fetch(c);\n  smx_cpu_branch(c, %s);\n", smx_branch_condition(op));
    {
      Uint32 target = smx_insn_branch_target(insn);
      bool local = smx_runs_local(runs, run, smx_mode_key(target, mode));
      if (op == 0x80) {
        if (local) {
          smx_emit_goto(text, target, "  ");
        } else {
          smx_text_put(text, "  return;\n");
        }
        return false;
      }
      smx_text_put(text, "  if (c->pc != 0x%04X) {\n", (unsigned)(next & 0xFFFF));
      if (local) {
        smx_emit_goto(text, target, "    ");
      } else {
        smx_text_put(text, "    return;\n");
      }
      smx_text_put(text, "  }\n");
    }
    return true;
  case SMX_OP_IMPLIED:
    smx_text_put(text, "  smx_cpu_fetch(c);\n  smx_cpu_implied(c, 0x%02X);\n", op);
    if (smx_insn_ends(insn)) {
      smx_text_put(text, "  return;\n");
      return false;
    }
    return true;
  case SMX_OP_MOVE:
    smx_text_put(text, "  smx_cpu_fetch(c);\n  smx_cpu_move(c, 0x%02X);\n  return;\n", op);
    return false;
  default:
    break;
  }

  smx_emit_fetches(text, insn->length);
  if (smx_op_immediate(op)) {
    Uint16 value = insn->length == 3 ? smx_insn_word(insn) : insn->bytes[1];
    smx_text_put(text, "  {\n    Uint16 v = 0x%04X;\n", value);
    smx_text_put(text, "  ");
    smx_emit_operate(text, op, mode);
    smx_text_put(text, "  }\n");
    return true;
  }
  {
    Uint32 operand = insn->bytes[1];
    const char *read = smx_alu_read(op);
    if (insn->length > 2) {
      operand |= (Uint32)insn->bytes[2] << 8;
    }
    if (insn->length > 3) {
      operand |= (Uint32)insn->bytes[3] << 16;
    }
    smx_text_put(text, "  {\n    Uint32 ea = smx_cpu_address(c, 0x%02X, 0x%06X);\n", op,
                 (unsigned)operand);
    if (op == 0x04 || op == 0x0C || op == 0x14 || op == 0x1C) {
      smx_text_put(text, "    smx_cpu_test_bits(c, ea, %s, %s);\n  }\n",
                   mode->m8 ? "smx_cpu_read8(c, ea)" : "smx_cpu_read16(c, ea)",
                   (op == 0x04 || op == 0x0C) ? "true" : "false");
      return true;
    }
    if (smx_op_shift(op)) {
      smx_text_put(text, "    smx_cpu_shift(c, 0x%02X, ea);\n  }\n", op);
      return true;
    }
    if (read != NULL) {
      smx_text_put(text, "    Uint16 v = %s(c, ea);\n", read);
    }
    smx_text_put(text, "  ");
    smx_emit_operate(text, op, mode);
    smx_text_put(text, "  }\n");
  }
  return true;
}

static int SDLCALL smx_key_order(const void *a, const void *b) {
  Uint32 left = *(const Uint32 *)a;
  Uint32 right = *(const Uint32 *)b;
  return left < right ? -1 : (left > right ? 1 : 0);
}

Uint32 smx_translate_rom_hash(const Uint8 *rom, Uint32 rom_size) {
  Uint32 hash = 2166136261u;
  Uint32 index = 0;

  for (; index < rom_size; index += 1) {
    hash = (hash ^ rom[index]) * 16777619u;
  }
  return hash;
}

static const char smx_generated_note[] =
    "/* Generated by smxrecomp from a user-supplied ROM. Never commit this file. */\n";

/* Group the visited instructions into runs. False when out of memory. */
static bool smx_runs_build(const SmxTranslator *translator, SmxRuns *runs) {
  Uint32 count = translator->visited.count;
  Uint32 index = 0;
  Uint32 at = 0;

  SDL_memset(runs, 0, sizeof(*runs));
  runs->count = count;
  runs->keys = (Uint32 *)SDL_malloc((count + 1u) * sizeof(Uint32));
  runs->owner = (Uint32 *)SDL_malloc((count + 1u) * sizeof(Uint32));
  runs->link = (Uint32 *)SDL_malloc((count + 1u) * sizeof(Uint32));
  runs->starts = (Uint32 *)SDL_malloc((count + 1u) * sizeof(Uint32));
  if (runs->keys == NULL || runs->owner == NULL || runs->link == NULL || runs->starts == NULL) {
    return false;
  }
  for (index = 0; index < translator->visited.capacity; index += 1) {
    if (translator->visited.slots[index] != 0) {
      runs->keys[at] = translator->visited.slots[index] - 1u;
      at += 1;
    }
  }
  SDL_qsort(runs->keys, count, sizeof(Uint32), smx_key_order);
  for (index = 0; index < count; index += 1) {
    runs->owner[index] = SMX_RUN_END;
    runs->link[index] = SMX_RUN_END;
  }
  /* Keys ascend by address, so each chain is claimed from its first instruction. */
  for (index = 0; index < count; index += 1) {
    Uint32 current = index;
    Uint32 run = runs->run_count;
    if (runs->owner[index] != SMX_RUN_END) {
      continue;
    }
    runs->starts[run] = index;
    runs->run_count += 1;
    runs->owner[index] = run;
    for (;;) {
      SmxMode mode = smx_mode_of(runs->keys[current]);
      SmxInsn insn;
      Uint32 next = 0;
      if (!smx_decode(translator, runs->keys[current] >> 3, mode.m8, mode.x8, &insn) ||
          smx_insn_ends(&insn) || !smx_mode_after(&insn, &mode)) {
        break;
      }
      next = smx_runs_find(runs, smx_mode_key(smx_insn_next(&insn), &mode));
      if (next == SMX_RUN_END || runs->owner[next] != SMX_RUN_END) {
        break;
      }
      runs->owner[next] = run;
      runs->link[current] = next;
      current = next;
    }
  }
  return true;
}

static void smx_runs_free(SmxRuns *runs) {
  SDL_free(runs->keys);
  SDL_free(runs->owner);
  SDL_free(runs->link);
  SDL_free(runs->starts);
  SDL_memset(runs, 0, sizeof(*runs));
}

/*
 * A run's function. The switch enters at the current instruction; each
 * later instruction first passes the boundary, then its label.
 */
static void smx_emit_run(const SmxTranslator *translator, const SmxRuns *runs, SmxText *text,
                         Uint32 run) {
  Uint32 first = runs->starts[run];
  Uint32 index = first;

  smx_text_put(text, "\nvoid smx_game_%07x(SmxCpu *c) {\n  switch (c->pc) {\n",
               (unsigned)runs->keys[first]);
  for (; index != SMX_RUN_END; index = runs->link[index]) {
    smx_text_put(text, "  case 0x%04X:\n    goto L_%04X;\n",
                 (unsigned)((runs->keys[index] >> 3) & 0xFFFF),
                 (unsigned)((runs->keys[index] >> 3) & 0xFFFF));
  }
  smx_text_put(text, "  default:\n    return;\n  }\n");
  for (index = first; index != SMX_RUN_END; index = runs->link[index]) {
    Uint32 key = runs->keys[index];
    SmxMode mode = smx_mode_of(key);
    SmxInsn insn;
    if (!smx_decode(translator, key >> 3, mode.m8, mode.x8, &insn)) {
      break;
    }
    if (index != first) {
      smx_text_put(text, "  if (!smx_cpu_next(c)) {\n    return;\n  }\n");
    }
    smx_text_put(text, "L_%04X:\n", (unsigned)((key >> 3) & 0xFFFF));
    if (!smx_emit_insn(text, &insn, &mode, runs, run)) {
      break;
    }
  }
  smx_text_put(text, "}\n");
}

bool smx_translate_write(const SmxTranslator *translator, const char *dir) {
  SmxRuns runs;
  Uint32 index = 0;
  Uint32 shard = 0;
  char path[1024];
  bool ok = smx_runs_build(translator, &runs);

  SDL_CreateDirectory(dir);
  for (shard = 0; shard < SMX_TRANSLATE_SHARDS && ok; shard += 1) {
    SmxText text;
    Uint32 first = (Uint32)((Uint64)runs.run_count * shard / SMX_TRANSLATE_SHARDS);
    Uint32 last = (Uint32)((Uint64)runs.run_count * (shard + 1u) / SMX_TRANSLATE_SHARDS);
    SDL_memset(&text, 0, sizeof(text));
    smx_text_put(&text, "%s#include \"smx/cpu.h\"\n", smx_generated_note);
    for (index = first; index < last; index += 1) {
      smx_emit_run(translator, &runs, &text, index);
    }
    SDL_snprintf(path, sizeof(path), "%s/smx_game_%02u.c", dir, (unsigned)shard);
    ok = !text.failed && smx_text_save(&text, path);
    SDL_free(text.data);
  }

  if (ok) {
    SmxText text;
    SDL_memset(&text, 0, sizeof(text));
    smx_text_put(&text, "%s#include \"smx/game.h\"\n\n", smx_generated_note);
    for (index = 0; index < runs.run_count; index += 1) {
      smx_text_put(&text, "void smx_game_%07x(SmxCpu *c);\n",
                   (unsigned)runs.keys[runs.starts[index]]);
    }
    smx_text_put(&text, "\nstatic const Uint32 smx_game_keys[] = {\n");
    for (index = 0; index < runs.count; index += 1) {
      smx_text_put(&text, "    0x%07xu,\n", (unsigned)runs.keys[index]);
    }
    if (runs.count == 0) {
      smx_text_put(&text, "    0,\n");
    }
    smx_text_put(&text, "};\n\nstatic const SmxCpuBlock smx_game_blocks[] = {\n");
    for (index = 0; index < runs.count; index += 1) {
      smx_text_put(&text, "    smx_game_%07x,\n",
                   (unsigned)runs.keys[runs.starts[runs.owner[index]]]);
    }
    if (runs.count == 0) {
      smx_text_put(&text, "    NULL,\n");
    }
    smx_text_put(&text,
                 "};\n\nconst SmxGame smx_game = {\n    {smx_game_keys, smx_game_blocks, %uu},\n"
                 "    0x%08xu,\n    %uu,\n};\n",
                 (unsigned)runs.count,
                 (unsigned)smx_translate_rom_hash(translator->rom, translator->rom_size),
                 (unsigned)translator->rom_size);
    SDL_snprintf(path, sizeof(path), "%s/smx_game.c", dir);
    ok = !text.failed && smx_text_save(&text, path);
    SDL_free(text.data);
  }
  smx_runs_free(&runs);
  return ok;
}
