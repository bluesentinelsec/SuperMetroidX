#include "smx/apu.h"
#include "smx/bus.h"

#include <SDL3/SDL.h>

static bool g_failed = false;

static void expect_u8(const char *name, Uint8 got, Uint8 want) {
  if (got != want) {
    SDL_Log("apu: %s got %u want %u", name, (unsigned)got, (unsigned)want);
    g_failed = true;
  }
}

static void expect_u16(const char *name, Uint16 got, Uint16 want) {
  if (got != want) {
    SDL_Log("apu: %s got %u want %u", name, (unsigned)got, (unsigned)want);
    g_failed = true;
  }
}

static void expect_true(const char *name, bool value) {
  if (!value) {
    SDL_Log("apu: %s", name);
    g_failed = true;
  }
}

static void step_until_port(SmxApu *apu, Uint8 want) {
  Uint32 guard = 0;

  for (; guard < 100000u; guard += 1) {
    smx_apu_step(apu);
    if (smx_apu_read_port(apu, 0) == want) {
      return;
    }
  }
  SDL_Log("apu: port wait expired");
  g_failed = true;
}

/* The 8 kHz prescaler runs from reset, so timer 0 ticks at cycle 128, not 128 after enabling. */
static void test_timer_prescaler(void) {
  SmxBus bus;
  SmxApu apu;
  static const Uint8 program[] = {0x8F, 0x01, 0xFA, 0x8F, 0x01, 0xF1, 0x2F, 0xFE};
  Uint32 cycles = 0;
  Uint32 i = 0;

  smx_bus_init(&bus, NULL, 0, 0);
  smx_apu_init(&apu, &bus);
  SDL_memcpy(&apu.ram[0x0200], program, sizeof(program));
  for (; i < 45u; i += 1) {
    apu.ram[0x0300 + i] = 0x00;
  }
  apu.ram[0x0300 + 45u] = 0x5F;
  apu.ram[0x0300 + 46u] = 0x00;
  apu.ram[0x0300 + 47u] = 0x02;
  apu.pc = 0x0300;
  /* 45 NOPs, JMP, then two MOV dp,#imm enable timer 0 at cycle 103. */
  while (cycles < 127u) {
    smx_apu_step(&apu);
    cycles += apu.cycles;
  }
  expect_u8("timer quiet before 128", apu.timer_out[0], 0);
  while (cycles < 132u) {
    smx_apu_step(&apu);
    cycles += apu.cycles;
  }
  expect_u8("timer ticks at 128", apu.timer_out[0], 1);
  smx_bus_quit(&bus);
}

int main(void) {
  SmxBus bus;
  SmxApu apu;

  smx_bus_init(&bus, NULL, 0, 0);
  smx_apu_init(&apu, &bus);
  {
    /* MOV X,#; MOV SP,X; MOV A,#; MOV (X),A; DEC X; BNE taken. */
    static const Uint8 want[] = {2, 2, 2, 4, 2, 4};
    Uint8 index = 0;
    for (; index < sizeof(want); index += 1) {
      smx_apu_step(&apu);
      expect_u8("ipl cycles", apu.cycles, want[index]);
    }
    smx_apu_init(&apu, &bus);
  }
  expect_u8("ready 0", smx_bus_read(&bus, 0x002140), 0xAA);
  expect_u8("ready 1", smx_bus_read(&bus, 0x002141), 0xBB);
  smx_bus_write(&bus, 0x002140, 0xFF);
  expect_u8("ignore request", smx_bus_read(&bus, 0x002140), 0xAA);
  expect_true("still waiting", !apu.program_ready);

  smx_bus_write(&bus, 0x002142, 0x00);
  smx_bus_write(&bus, 0x002143, 0x02);
  smx_bus_write(&bus, 0x002141, 0x01);
  smx_bus_write(&bus, 0x002140, 0xCC);
  step_until_port(&apu, 0xCC);
  expect_u8("echo kick", smx_bus_read(&bus, 0x002140), 0xCC);

  smx_bus_write(&bus, 0x002140, 0x00);
  smx_bus_write(&bus, 0x002141, 0x12);
  step_until_port(&apu, 0x00);
  smx_apu_step(&apu);
  smx_bus_write(&bus, 0x002140, 0x01);
  smx_bus_write(&bus, 0x002141, 0x34);
  step_until_port(&apu, 0x01);
  smx_apu_step(&apu);
  expect_u8("byte 0", smx_apu_ram(&apu, 0x0200), 0x12);
  expect_u8("byte 1", smx_apu_ram(&apu, 0x0201), 0x34);
  expect_u8("echo index", smx_bus_read(&bus, 0x002140), 0x01);

  smx_bus_write(&bus, 0x002142, 0x00);
  smx_bus_write(&bus, 0x002143, 0x02);
  smx_bus_write(&bus, 0x002141, 0x00);
  smx_bus_write(&bus, 0x002140, 0x05);
  step_until_port(&apu, 0x05);
  {
    Uint32 guard = 0;
    for (; guard < 100000u && !apu.program_ready; guard += 1) {
      smx_apu_step(&apu);
    }
  }
  expect_u8("echo entry", smx_bus_read(&bus, 0x002140), 0x05);
  expect_u16("entry", apu.entry, 0x0200);
  expect_true("program waiting", apu.program_ready);
  expect_u16("spc pc", apu.pc, 0x0200);
  expect_true("spc running", smx_apu_core_running(&apu));

  apu.ram[0x0300] = 0xE8;
  apu.ram[0x0301] = 0xAA;
  apu.ram[0x0302] = 0xC4;
  apu.ram[0x0303] = 0xF4;
  apu.ram[0x0304] = 0xE8;
  apu.ram[0x0305] = 0xBB;
  apu.ram[0x0306] = 0xC4;
  apu.ram[0x0307] = 0xF5;
  apu.pc = 0x0300;
  smx_apu_step(&apu);
  smx_apu_step(&apu);
  smx_apu_step(&apu);
  smx_apu_step(&apu);
  expect_u8("spc port 0", smx_bus_read(&bus, 0x002140), 0xAA);
  expect_u8("spc port 1", smx_bus_read(&bus, 0x002141), 0xBB);

  apu.ram[0x0310] = 0xE8;
  apu.ram[0x0311] = 0x12;
  apu.ram[0x0312] = 0xC5;
  apu.ram[0x0313] = 0x40;
  apu.ram[0x0314] = 0x02;
  apu.pc = 0x0310;
  smx_apu_step(&apu);
  smx_apu_step(&apu);
  expect_u8("abs store", smx_apu_ram(&apu, 0x0240), 0x12);
  expect_u16("abs pc", apu.pc, 0x0315);

  apu.ram[0x0320] = 0x1F;
  apu.ram[0x0321] = 0x10;
  apu.ram[0x0322] = 0x02;
  apu.ram[0x0210] = 0x34;
  apu.ram[0x0211] = 0x12;
  apu.x = 0;
  apu.pc = 0x0320;
  apu.running = true;
  smx_apu_step(&apu);
  expect_u16("jmp ind", apu.pc, 0x1234);

  apu.x = 0x12;
  apu.ram[0x0330] = 0xC9;
  apu.ram[0x0331] = 0x40;
  apu.ram[0x0332] = 0x02;
  apu.pc = 0x0330;
  apu.running = true;
  smx_apu_step(&apu);
  expect_u8("mov abs,x", smx_apu_ram(&apu, 0x0240), 0x12);
  expect_u8("mov abs,x skip", smx_apu_ram(&apu, 0x0252), 0x00);
  expect_u16("mov abs,x pc", apu.pc, 0x0333);

  apu.ram[0x0010] = 0x01;
  apu.ram[0x0340] = 0x98;
  apu.ram[0x0341] = 0x02;
  apu.ram[0x0342] = 0x10;
  apu.psw = 0;
  apu.pc = 0x0340;
  apu.running = true;
  smx_apu_step(&apu);
  expect_u8("adc dp imm", smx_apu_ram(&apu, 0x0010), 0x03);

  smx_bus_quit(&bus);
  test_timer_prescaler();
  return g_failed ? 1 : 0;
}
