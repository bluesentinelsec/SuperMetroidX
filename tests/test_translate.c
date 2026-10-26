#include "smx/bus.h"
#include "smx/cpu.h"
#include "smx/game.h"
#include "smx/translate.h"
#include "translate_rom.h"

#include <SDL3/SDL.h>

/*
 * Every opcode under every flag set, translated by smxrecomp at build time,
 * must leave the CPU and memory exactly as one interpreted step does, with
 * the same master clocks.
 */

static bool g_failed = false;
static Uint32 g_failures = 0;

#define SMX_TRIALS 8u

typedef struct Machine {
  SmxBus bus;
  SmxCpu cpu;
} Machine;

static Uint32 g_seed = 0x9E3779B9u;

static Uint32 smx_cpu_key_for(Uint32 addr, bool e, bool m8, bool x8) {
  return SMX_CPU_KEY(addr, e, m8, x8);
}

/* Boundaries a block may still pass before it must return. */
static Uint32 g_budget = 0;

static Uint32 g_missed = 0;

static void count_miss(void *user, Uint32 key) {
  (void)user;
  g_missed = key;
}

static bool allow_next(void *user) {
  (void)user;
  if (g_budget == 0) {
    return false;
  }
  g_budget -= 1;
  return true;
}

static Uint32 rand32(void) {
  return smx_trom_next(&g_seed);
}

static void fail(Uint32 slot, Uint32 trial, const char *what, Uint32 interp, Uint32 native) {
  if (g_failures < 40u) {
    SDL_Log("translate: slot %u (op %02X mode %u) trial %u: %s interpreted %x translated %x",
            (unsigned)slot, (unsigned)(slot & 0xFFu), (unsigned)(slot / 256u), (unsigned)trial,
            what, (unsigned)interp, (unsigned)native);
  }
  g_failures += 1;
  g_failed = true;
}

static void compare(Uint32 slot, Uint32 trial, const Machine *a, const Machine *b,
                    Uint64 instructions) {
  const SmxCpu *x = &a->cpu;
  const SmxCpu *y = &b->cpu;

  if (x->a != y->a) {
    fail(slot, trial, "a", x->a, y->a);
  }
  if (x->x != y->x) {
    fail(slot, trial, "x", x->x, y->x);
  }
  if (x->y != y->y) {
    fail(slot, trial, "y", x->y, y->y);
  }
  if (x->s != y->s) {
    fail(slot, trial, "s", x->s, y->s);
  }
  if (x->d != y->d) {
    fail(slot, trial, "d", x->d, y->d);
  }
  if (x->pc != y->pc || x->pb != y->pb) {
    fail(slot, trial, "pc", ((Uint32)x->pb << 16) | x->pc, ((Uint32)y->pb << 16) | y->pc);
  }
  if (x->db != y->db) {
    fail(slot, trial, "db", x->db, y->db);
  }
  if (x->p != y->p || x->e != y->e) {
    fail(slot, trial, "p", x->p | (x->e ? 0x100u : 0u), y->p | (y->e ? 0x100u : 0u));
  }
  if (x->stopped != y->stopped || x->waiting != y->waiting) {
    fail(slot, trial, "stopped/waiting", x->stopped * 2u + x->waiting,
         y->stopped * 2u + y->waiting);
  }
  if (x->clocks != y->clocks) {
    fail(slot, trial, "clocks", x->clocks, y->clocks);
  }
  if (x->last_at != y->last_at) {
    fail(slot, trial, "last_at", x->last_at, y->last_at);
  }
  if (a->bus.open_bus != b->bus.open_bus) {
    fail(slot, trial, "open bus", a->bus.open_bus, b->bus.open_bus);
  }
  if (SDL_memcmp(a->bus.wram, b->bus.wram, SMX_BUS_WRAM_SIZE) != 0) {
    fail(slot, trial, "wram", 0, 0);
  }
  if (SDL_memcmp(a->bus.mmio, b->bus.mmio, sizeof(a->bus.mmio)) != 0) {
    fail(slot, trial, "mmio", 0, 0);
  }
  if (y->translated != instructions || y->interpreted != 0u) {
    fail(slot, trial, "ran translated", (Uint32)instructions, (Uint32)y->translated);
  }
}

/* A random state under the slot's flags, the same for both machines. */
static void randomize(Machine *machine, Uint32 slot, const Uint8 *wram) {
  SmxCpu *cpu = &machine->cpu;
  static const Uint8 banks[] = {0x00, 0x7E, 0x7F, 0x81};
  bool e = false;
  bool m8 = false;
  bool x8 = false;
  Uint32 addr = smx_trom_slot_addr(slot);

  smx_trom_mode(slot / 256u, &e, &m8, &x8);
  SDL_memcpy(machine->bus.wram, wram, SMX_BUS_WRAM_SIZE);
  SDL_memset(machine->bus.mmio, 0, sizeof(machine->bus.mmio));
  machine->bus.open_bus = 0;
  cpu->a = (Uint16)rand32();
  cpu->x = (Uint16)rand32();
  cpu->y = (Uint16)rand32();
  cpu->s = (Uint16)rand32();
  cpu->d = (rand32() & 1u) != 0 ? (Uint16)(rand32() & 0xFF00u) : (Uint16)rand32();
  cpu->db = banks[rand32() % sizeof(banks)];
  cpu->p = (Uint8)rand32();
  cpu->p = (Uint8)((cpu->p & 0xCFu) | (m8 ? 0x20u : 0u) | (x8 ? 0x10u : 0u));
  cpu->e = e;
  if (x8) {
    cpu->x &= 0x00FF;
    cpu->y &= 0x00FF;
  }
  if (e) {
    cpu->s = (Uint16)(0x0100u | (cpu->s & 0x00FFu));
  }
  cpu->pb = (Uint8)(addr >> 16);
  cpu->pc = (Uint16)addr;
  cpu->stopped = false;
  cpu->waiting = false;
  cpu->clocks = 0;
  cpu->last_at = 0;
  cpu->translated = 0;
  cpu->interpreted = 0;
}

int main(void) {
  static Uint8 rom[SMX_TROM_SIZE];
  Uint8 *wram = (Uint8 *)SDL_malloc(SMX_BUS_WRAM_SIZE);
  Machine *interp = (Machine *)SDL_calloc(1, sizeof(Machine));
  Machine *native = (Machine *)SDL_calloc(1, sizeof(Machine));
  Uint32 slot = 0;
  Uint32 trial = 0;
  Uint32 index = 0;
  Uint32 pairs = 0;

  smx_trom_build(rom);
  if (smx_game.rom_size != SMX_TROM_SIZE ||
      smx_game.rom_hash != smx_translate_rom_hash(rom, SMX_TROM_SIZE)) {
    SDL_Log("translate: the linked translation is not of the test ROM");
    return 1;
  }
  smx_bus_init(&interp->bus, rom, SMX_TROM_SIZE, 0);
  smx_bus_init(&native->bus, rom, SMX_TROM_SIZE, 0);
  smx_cpu_init(&interp->cpu, &interp->bus);
  smx_cpu_init(&native->cpu, &native->bus);
  native->cpu.code = &smx_game.code;
  native->cpu.next = allow_next;

  for (slot = 0; slot < SMX_TROM_SLOTS; slot += 1) {
    for (index = 0; index < SMX_BUS_WRAM_SIZE; index += 1) {
      wram[index] = (Uint8)rand32();
    }
    for (trial = 0; trial < SMX_TRIALS; trial += 1) {
      Uint32 seed = g_seed;
      randomize(interp, slot, wram);
      g_seed = seed;
      randomize(native, slot, wram);
      /* Odd trials let the block run one more instruction through `smx_cpu_next`. */
      g_budget = trial & 1u;
      smx_cpu_run(&native->cpu);
      smx_cpu_step(&interp->cpu);
      if (native->cpu.translated == 2u) {
        smx_cpu_step(&interp->cpu);
        pairs += 1;
      }
      compare(slot, trial, interp, native, (trial & 1u) != 0 ? native->cpu.translated : 1u);
    }
  }
  /* Code in WRAM is never translated: it is interpreted and reported. */
  randomize(native, 0, wram);
  native->cpu.pb = 0x7E;
  native->cpu.pc = 0x1000;
  native->bus.wram[0x1000] = 0xEA;
  native->cpu.miss = count_miss;
  smx_cpu_run(&native->cpu);
  if (g_missed != smx_cpu_key_for(0x7E1000u, false, false, false) ||
      native->cpu.interpreted != 1u || native->cpu.pc != 0x1001) {
    SDL_Log("translate: an untranslated key was not reported and interpreted");
    g_failed = true;
  }

  /* Most opcodes fall through to the STP after them, or branch back to themselves. */
  if (pairs < SMX_TROM_SLOTS) {
    SDL_Log("translate: only %u blocks ran two instructions", (unsigned)pairs);
    g_failed = true;
  }
  if (g_failures != 0) {
    SDL_Log("translate: %u mismatches", (unsigned)g_failures);
  }
  smx_bus_quit(&interp->bus);
  smx_bus_quit(&native->bus);
  SDL_free(interp);
  SDL_free(native);
  SDL_free(wram);
  return g_failed ? 1 : 0;
}
