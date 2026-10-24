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

static void expect_s16(const char *name, Sint16 got, Sint16 want) {
  if (got != want) {
    SDL_Log("apu: %s got %d want %d", name, (int)got, (int)want);
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

  {
    Uint8 block[9];
    Sint16 out[16];
    Sint16 prev1 = 0;
    Sint16 prev2 = 0;
    Uint8 i = 0;
    SDL_memset(block, 0, sizeof(block));
    block[0] = 0xC0;
    block[1] = 0x10;
    smx_apu_brr_decode(block, out, &prev1, &prev2);
    expect_s16("brr 0", out[0], 2048);
    expect_s16("brr 1", out[1], 0);
    prev1 = 16;
    prev2 = 0;
    block[0] = 0x04;
    block[1] = 0x00;
    for (; i < 16; i += 1) {
      out[i] = 0;
    }
    smx_apu_brr_decode(block, out, &prev1, &prev2);
    expect_s16("brr filter", out[0], 15);
  }

  {
    static const Uint8 program[] = {0xE8, 0x5D, 0xC4, 0xF2, 0xE8, 0x01, 0xC4, 0xF3,
                                    0xE8, 0x04, 0xC4, 0xF2, 0xE8, 0x00, 0xC4, 0xF3,
                                    0xE8, 0x00, 0xC4, 0xF2, 0xE8, 0x7F, 0xC4, 0xF3,
                                    0xE8, 0x4C, 0xC4, 0xF2, 0xE8, 0x01, 0xC4, 0xF3};
    Sint16 left[1];
    Uint8 step = 0;
    apu.ram[0x0100] = 0x00;
    apu.ram[0x0101] = 0x02;
    apu.ram[0x0102] = 0x00;
    apu.ram[0x0103] = 0x02;
    apu.ram[0x0200] = 0xC3;
    apu.ram[0x0201] = 0x10;
    SDL_memcpy(apu.ram + 0x0400, program, sizeof(program));
    apu.pc = 0x0400;
    apu.psw = 0;
    apu.running = true;
    for (; step < sizeof(program) / 2u; step += 1) {
      smx_apu_step(&apu);
    }
    smx_apu_render(&apu, left, 1);
    expect_s16("voice", left[0], 2032);
  }

  smx_bus_quit(&bus);
  return g_failed ? 1 : 0;
}
