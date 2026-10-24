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

int main(void) {
  SmxBus bus;
  SmxApu apu;

  smx_bus_init(&bus, NULL, 0, 0);
  smx_apu_init(&apu, &bus);
  expect_u8("ready 0", smx_bus_read(&bus, 0x002140), 0xAA);
  expect_u8("ready 1", smx_bus_read(&bus, 0x002141), 0xBB);
  smx_bus_write(&bus, 0x002140, 0xFF);
  expect_u8("ignore request", smx_bus_read(&bus, 0x002140), 0xAA);
  expect_true("still waiting", !apu.program_ready);

  smx_bus_write(&bus, 0x002142, 0x00);
  smx_bus_write(&bus, 0x002143, 0x02);
  smx_bus_write(&bus, 0x002141, 0x01);
  smx_bus_write(&bus, 0x002140, 0xCC);
  expect_u8("echo kick", smx_bus_read(&bus, 0x002140), 0xCC);

  smx_bus_write(&bus, 0x002140, 0x00);
  smx_bus_write(&bus, 0x002141, 0x12);
  smx_bus_write(&bus, 0x002140, 0x01);
  smx_bus_write(&bus, 0x002141, 0x34);
  expect_u8("byte 0", smx_apu_ram(&apu, 0x0200), 0x12);
  expect_u8("byte 1", smx_apu_ram(&apu, 0x0201), 0x34);
  expect_u8("echo index", smx_bus_read(&bus, 0x002140), 0x01);

  smx_bus_write(&bus, 0x002142, 0x00);
  smx_bus_write(&bus, 0x002143, 0x02);
  smx_bus_write(&bus, 0x002141, 0x00);
  smx_bus_write(&bus, 0x002140, 0x05);
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

  smx_bus_quit(&bus);
  return g_failed ? 1 : 0;
}
