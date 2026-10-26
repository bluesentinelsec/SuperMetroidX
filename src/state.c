#include "smx/state.h"

static const Uint8 smx_state_magic[8] = {'S', 'M', 'X', 'S', 'T', 'A', 'T', 'E'};

/* Header: magic, version, layout, ROM hash. */
#define SMX_STATE_HEADER (8u + 4u + 4u + 4u)

/* A layout check over the sizes of everything a state copies. */
static Uint32 smx_state_layout(void) {
  Uint32 sizes[7];
  Uint32 hash = 2166136261u;
  Uint32 index = 0;

  sizes[0] = (Uint32)sizeof(SmxBus);
  sizes[1] = (Uint32)SMX_BUS_WRAM_SIZE;
  sizes[2] = (Uint32)sizeof(SmxCpu);
  sizes[3] = (Uint32)sizeof(SmxDma);
  sizes[4] = (Uint32)sizeof(SmxPpu);
  sizes[5] = (Uint32)sizeof(SmxApu);
  sizes[6] = (Uint32)sizeof(SmxClock);
  for (; index < 7u; index += 1) {
    hash = (hash ^ sizes[index]) * 16777619u;
  }
  return hash;
}

Uint32 smx_state_size(void) {
  return SMX_STATE_HEADER + (Uint32)sizeof(SmxBus) + SMX_BUS_WRAM_SIZE + (Uint32)sizeof(SmxCpu) +
         (Uint32)sizeof(SmxDma) + (Uint32)sizeof(SmxPpu) + (Uint32)sizeof(SmxApu) +
         (Uint32)sizeof(SmxClock);
}

static void smx_state_put32(Uint8 *out, Uint32 value) {
  out[0] = (Uint8)value;
  out[1] = (Uint8)(value >> 8);
  out[2] = (Uint8)(value >> 16);
  out[3] = (Uint8)(value >> 24);
}

static Uint32 smx_state_get32(const Uint8 *in) {
  return (Uint32)in[0] | ((Uint32)in[1] << 8) | ((Uint32)in[2] << 16) | ((Uint32)in[3] << 24);
}

void smx_state_capture(const SmxMachine *machine, Uint32 rom_hash, Uint8 *out) {
  Uint8 *at = out + SMX_STATE_HEADER;

  SDL_memcpy(out, smx_state_magic, sizeof(smx_state_magic));
  smx_state_put32(out + 8, SMX_STATE_VERSION);
  smx_state_put32(out + 12, smx_state_layout());
  smx_state_put32(out + 16, rom_hash);
  SDL_memcpy(at, machine->bus, sizeof(SmxBus));
  at += sizeof(SmxBus);
  SDL_memcpy(at, machine->bus->wram, SMX_BUS_WRAM_SIZE);
  at += SMX_BUS_WRAM_SIZE;
  SDL_memcpy(at, machine->cpu, sizeof(SmxCpu));
  at += sizeof(SmxCpu);
  SDL_memcpy(at, machine->dma, sizeof(SmxDma));
  at += sizeof(SmxDma);
  SDL_memcpy(at, machine->ppu, sizeof(SmxPpu));
  at += sizeof(SmxPpu);
  SDL_memcpy(at, machine->apu, sizeof(SmxApu));
  at += sizeof(SmxApu);
  SDL_memcpy(at, machine->clock, sizeof(SmxClock));
}

/*
 * Each restore copies the saved structure over the live one, then puts back
 * what belongs to this process: pointers, hooks, and the cartridge.
 */
static void smx_state_restore_bus(SmxBus *bus, const Uint8 *in) {
  SmxBus live = *bus;

  SDL_memcpy(bus, in, sizeof(SmxBus));
  bus->rom = live.rom;
  bus->rom_size = live.rom_size;
  bus->wram = live.wram;
  bus->sram = live.sram;
  bus->sram_size = live.sram_size;
  bus->mmio_write = live.mmio_write;
  bus->mmio_read = live.mmio_read;
  bus->mmio_user = live.mmio_user;
  bus->apu_write = live.apu_write;
  bus->apu_user = live.apu_user;
  bus->ppu_sync = live.ppu_sync;
  bus->apu_sync = live.apu_sync;
  bus->reg_read = live.reg_read;
  bus->reg_write = live.reg_write;
  bus->sync_user = live.sync_user;
}

static void smx_state_restore_cpu(SmxCpu *cpu, const Uint8 *in) {
  SmxCpu live = *cpu;

  SDL_memcpy(cpu, in, sizeof(SmxCpu));
  cpu->bus = live.bus;
  cpu->code = live.code;
  cpu->next = live.next;
  cpu->next_user = live.next_user;
  cpu->miss = live.miss;
  cpu->miss_user = live.miss_user;
  cpu->cycle_begin = live.cycle_begin;
  cpu->cycle_advance = live.cycle_advance;
  cpu->interrupt_soon = live.interrupt_soon;
  cpu->cycle_user = live.cycle_user;
  cpu->translated = live.translated;
  cpu->interpreted = live.interpreted;
}

static void smx_state_restore_dma(SmxDma *dma, const Uint8 *in) {
  SmxDma live = *dma;

  SDL_memcpy(dma, in, sizeof(SmxDma));
  dma->bus = live.bus;
  dma->advance = live.advance;
  dma->now = live.now;
  dma->user = live.user;
}

static void smx_state_restore_ppu(SmxPpu *ppu, const Uint8 *in) {
  SmxBus *bus = ppu->bus;
  Uint8 *pixels = ppu->pixels;
  Uint8 *picture = ppu->picture;
  const Uint32 *beam_line = ppu->beam_line;

  SDL_memcpy(ppu, in, sizeof(SmxPpu));
  ppu->bus = bus;
  ppu->pixels = pixels;
  ppu->picture = picture;
  ppu->beam_line = beam_line;
}

static void smx_state_restore_apu(SmxApu *apu, const Uint8 *in) {
  SmxBus *bus = apu->bus;

  SDL_memcpy(apu, in, sizeof(SmxApu));
  apu->bus = bus;
  apu->dsp.ram = apu->ram;
}

static void smx_state_restore_clock(SmxClock *clock, const Uint8 *in) {
  SmxBus *bus = clock->bus;
  SmxCpu *cpu = clock->cpu;
  SmxDma *dma = clock->dma;
  SmxPpu *ppu = clock->ppu;

  SDL_memcpy(clock, in, sizeof(SmxClock));
  clock->bus = bus;
  clock->cpu = cpu;
  clock->dma = dma;
  clock->ppu = ppu;
}

bool smx_state_restore(SmxMachine *machine, Uint32 rom_hash, const Uint8 *in, Uint32 size) {
  const Uint8 *at = in + SMX_STATE_HEADER;

  if (size != smx_state_size() || SDL_memcmp(in, smx_state_magic, sizeof(smx_state_magic)) != 0 ||
      smx_state_get32(in + 8) != SMX_STATE_VERSION ||
      smx_state_get32(in + 12) != smx_state_layout() || smx_state_get32(in + 16) != rom_hash) {
    return false;
  }
  smx_state_restore_bus(machine->bus, at);
  at += sizeof(SmxBus);
  SDL_memcpy(machine->bus->wram, at, SMX_BUS_WRAM_SIZE);
  at += SMX_BUS_WRAM_SIZE;
  smx_state_restore_cpu(machine->cpu, at);
  at += sizeof(SmxCpu);
  smx_state_restore_dma(machine->dma, at);
  at += sizeof(SmxDma);
  smx_state_restore_ppu(machine->ppu, at);
  at += sizeof(SmxPpu);
  smx_state_restore_apu(machine->apu, at);
  at += sizeof(SmxApu);
  smx_state_restore_clock(machine->clock, at);
  return true;
}

bool smx_state_save(const SmxMachine *machine, Uint32 rom_hash, const char *path) {
  Uint32 size = smx_state_size();
  Uint8 *data = (Uint8 *)SDL_malloc(size);
  char temp[1024];
  bool ok = false;

  if (data == NULL) {
    return false;
  }
  smx_state_capture(machine, rom_hash, data);
  SDL_snprintf(temp, sizeof(temp), "%s.tmp", path);
  ok = SDL_SaveFile(temp, data, size) && SDL_RenamePath(temp, path);
  if (!ok) {
    SDL_Log("state: cannot save %s: %s", path, SDL_GetError());
  }
  SDL_free(data);
  return ok;
}

void smx_state_path(char *path, Uint32 size, const char *sram_path) {
  size_t length = 0;

  SDL_strlcpy(path, sram_path, size);
  length = SDL_strlen(path);
  if (length > 4 && SDL_strcmp(path + length - 4, ".srm") == 0) {
    path[length - 4] = '\0';
  }
  SDL_strlcat(path, ".state", size);
}

bool smx_state_load(SmxMachine *machine, Uint32 rom_hash, const char *path) {
  size_t size = 0;
  Uint8 *data = (Uint8 *)SDL_LoadFile(path, &size);
  bool ok = false;

  if (data == NULL) {
    return false;
  }
  ok = size <= 0xFFFFFFFFu && smx_state_restore(machine, rom_hash, data, (Uint32)size);
  SDL_free(data);
  return ok;
}
