#include "smx/bus.h"
#include "smx/cart.h"
#include "smx/cpu.h"

#include <SDL3/SDL.h>

static bool g_failed = false;

static void expect_u16(const char *name, Uint16 got, Uint16 want) {
  if (got != want) {
    SDL_Log("cpu: %s got %u want %u", name, (unsigned)got, (unsigned)want);
    g_failed = true;
  }
}

static void expect_u8(const char *name, Uint8 got, Uint8 want) {
  if (got != want) {
    SDL_Log("cpu: %s got %u want %u", name, (unsigned)got, (unsigned)want);
    g_failed = true;
  }
}

static void expect_true(const char *name, bool value) {
  if (!value) {
    SDL_Log("cpu: %s", name);
    g_failed = true;
  }
}

static void native(SmxCpu *cpu, const Uint8 *code, Uint32 size) {
  Uint32 index = 0;

  for (; index < size; index += 1) {
    smx_bus_write(cpu->bus, 0x1000 + index, code[index]);
  }
  cpu->e = false;
  cpu->p = 0x30;
  cpu->pb = 0;
  cpu->db = 0;
  cpu->d = 0;
  cpu->pc = 0x1000;
  cpu->s = 0x01FF;
  cpu->a = 0;
  cpu->x = 0;
  cpu->y = 0;
  cpu->stopped = false;
  cpu->waiting = false;
}

static void step_n(SmxCpu *cpu, Uint32 count) {
  Uint32 index = 0;
  for (; index < count; index += 1) {
    if (!smx_cpu_step(cpu)) {
      expect_true("step", false);
      return;
    }
  }
}

static void test_alu_and_flow(void) {
  SmxBus bus;
  SmxCpu cpu;
  static const Uint8 load[] = {0xA9, 0x42, 0x8D, 0x00, 0x12};
  static const Uint8 wide[] = {0xC2, 0x20, 0xA9, 0x34, 0x12};
  static const Uint8 overflow[] = {0x18, 0xA9, 0x7F, 0x69, 0x01};
  static const Uint8 decimal[] = {0xF8, 0x18, 0xA9, 0x09, 0x69, 0x01};
  static const Uint8 subtract[] = {0x38, 0xA9, 0x10, 0xE9, 0x01};
  static const Uint8 branch[] = {0xA9, 0x00, 0xF0, 0x02, 0xA9, 0xFF};
  static const Uint8 bra[] = {0x80, 0x02, 0xA9, 0xFF, 0xA9, 0x11};
  static const Uint8 call[] = {0x20, 0x06, 0x10, 0xA9, 0x11, 0x60, 0xA9, 0x22, 0x60};
  static const Uint8 block[] = {0xC2, 0x30, 0xA9, 0x01, 0x00, 0xA2, 0x00,
                                0x02, 0xA0, 0x00, 0x03, 0x54, 0x00, 0x00};
  static const Uint8 stack[] = {0xA9, 0x05, 0x83, 0x01, 0xA9, 0x00, 0xA3, 0x01};

  smx_bus_init(&bus, NULL, 0, 0);
  smx_cpu_init(&cpu, &bus);

  native(&cpu, load, sizeof(load));
  step_n(&cpu, 2);
  expect_u16("lda", cpu.a, 0x0042);
  expect_u8("sta", smx_bus_wram(&bus, 0x1200), 0x42);

  native(&cpu, wide, sizeof(wide));
  step_n(&cpu, 2);
  expect_u16("lda16", cpu.a, 0x1234);
  expect_true("m cleared", (cpu.p & 0x20) == 0);

  native(&cpu, overflow, sizeof(overflow));
  step_n(&cpu, 3);
  expect_u16("adc", cpu.a, 0x0080);
  expect_true("overflow", (cpu.p & 0x40) != 0);

  native(&cpu, decimal, sizeof(decimal));
  step_n(&cpu, 4);
  expect_u16("bcd", cpu.a, 0x0010);
  expect_true("bcd carry clear", (cpu.p & 0x01) == 0);

  native(&cpu, subtract, sizeof(subtract));
  step_n(&cpu, 3);
  expect_u16("sbc", cpu.a, 0x000F);

  native(&cpu, branch, sizeof(branch));
  step_n(&cpu, 2);
  expect_u16("beq", cpu.a, 0x0000);

  native(&cpu, bra, sizeof(bra));
  step_n(&cpu, 2);
  expect_u16("bra", cpu.a, 0x0011);

  {
    /* 8-bit CMP must clear Z when the bytes differ, or a following BNE is skipped. */
    static const Uint8 cmp[] = {0xE2, 0x20, 0xA9, 0x00, 0xC9, 0xFF,
                                0xD0, 0x02, 0xA9, 0x22, 0xA9, 0x33};
    native(&cpu, cmp, sizeof(cmp));
    step_n(&cpu, 4);
    expect_u16("cmp bne pc", cpu.pc, 0x100A);
    step_n(&cpu, 1);
    expect_u16("cmp bne", cpu.a, 0x0033);
  }

  native(&cpu, call, sizeof(call));
  step_n(&cpu, 4);
  expect_u16("jsr", cpu.a, 0x0011);

  native(&cpu, block, sizeof(block));
  smx_bus_write(&bus, 0x0200, 0xAB);
  smx_bus_write(&bus, 0x0201, 0xCD);
  step_n(&cpu, 6);
  expect_u8("mvn0", smx_bus_wram(&bus, 0x0300), 0xAB);
  expect_u8("mvn1", smx_bus_wram(&bus, 0x0301), 0xCD);
  expect_u16("mvn a", cpu.a, 0xFFFF);

  native(&cpu, stack, sizeof(stack));
  step_n(&cpu, 4);
  expect_u16("stack rel", cpu.a, 0x0005);

  cpu.e = true;
  cpu.p = 0x34;
  cpu.pc = 0x1000;
  cpu.pb = 0;
  smx_bus_write(&bus, 0x1000, 0x18);
  smx_bus_write(&bus, 0x1001, 0xFB);
  step_n(&cpu, 2);
  expect_true("xce native", !cpu.e);
  expect_true("xce carry", (cpu.p & 0x01) != 0);

  smx_bus_quit(&bus);
}

static void test_boot(void) {
  const char *path = SDL_getenv("SMX_ROM");
  SmxCart cart;
  SmxBus bus;
  SmxCpu cpu;
  Uint32 index = 0;

  if (path == NULL || path[0] == '\0') {
    path = "SuperMetroid.sfc";
  }
  smx_cart_init(&cart);
  if (!smx_cart_load_path(&cart, path)) {
    if (SDL_getenv("SMX_REQUIRE_ROM") != NULL) {
      expect_true("rom", false);
    }
    return;
  }

  smx_bus_init(&bus, cart.bytes, cart.size, smx_cart_sram_bytes(cart.sram_size_code));
  smx_cpu_init(&cpu, &bus);
  smx_cpu_reset(&cpu);
  expect_u16("reset pc", cpu.pc, cart.reset_vector);
  for (; index < 17; index += 1) {
    if (!smx_cpu_step(&cpu)) {
      expect_true("boot step", false);
      break;
    }
  }
  expect_true("native", !cpu.e);
  expect_u8("bank", cpu.pb, 0x80);
  expect_u8("data bank", cpu.db, 0x80);
  expect_u16("stack", cpu.s, 0x1FFF);
  expect_u16("direct", cpu.d, 0);
  expect_u16("x", cpu.x, 0x0004);
  expect_u16("a", cpu.a, 0);
  expect_true("irq masked", (cpu.p & 0x04) != 0);
  expect_true("carry", (cpu.p & 0x01) != 0);
  expect_u8("memsel", smx_bus_mmio(&bus, 0x420D), 0x01);
  expect_u8("dp 86", smx_bus_wram(&bus, 0x0086), 0x01);
  if (g_failed) {
    SDL_Log("cpu boot pc=%u:%u a=%u x=%u y=%u s=%u d=%u db=%u p=%u e=%u", (unsigned)cpu.pb,
            (unsigned)cpu.pc, (unsigned)cpu.a, (unsigned)cpu.x, (unsigned)cpu.y, (unsigned)cpu.s,
            (unsigned)cpu.d, (unsigned)cpu.db, (unsigned)cpu.p, cpu.e ? 1u : 0u);
  }
  smx_bus_quit(&bus);
  smx_cart_free(&cart);
}

static void test_stz_abs_x(void) {
  SmxBus bus;
  SmxCpu cpu;
  static const Uint8 code[] = {0xC2, 0x30, 0xA2, 0x00, 0x12, 0x9E, 0x00,
                               0x00, 0xA0, 0x00, 0x00, 0xBE, 0x00, 0x13};

  smx_bus_init(&bus, NULL, 0, 0);
  smx_cpu_init(&cpu, &bus);
  smx_bus_write(&bus, 0x001200, 0x34);
  smx_bus_write(&bus, 0x001201, 0x12);
  smx_bus_write(&bus, 0x001202, 0x56);
  smx_bus_write(&bus, 0x001300, 0xAB);
  smx_bus_write(&bus, 0x001301, 0x00);
  native(&cpu, code, sizeof(code));
  step_n(&cpu, 5);
  expect_u8("stz lo", smx_bus_wram(&bus, 0x1200), 0);
  expect_u8("stz hi", smx_bus_wram(&bus, 0x1201), 0);
  expect_u8("neighbor", smx_bus_wram(&bus, 0x1202), 0x56);
  expect_u16("ldx abs,y", cpu.x, 0x00AB);
  smx_bus_quit(&bus);
}

static void test_cycles(void) {
  SmxBus bus;
  SmxCpu cpu;
  static const Uint8 code[] = {0xEA, 0xAD, 0x34, 0x12, 0x64, 0x10, 0x80, 0x00, 0xC2,
                               0x20, 0xAD, 0x34, 0x12, 0xB5, 0x12, 0xD0, 0x00};
  static const Uint32 want[] = {14, 32, 24, 22, 22, 40, 38, 16};
  Uint32 index = 0;

  smx_bus_init(&bus, NULL, 0, 0);
  smx_cpu_init(&cpu, &bus);
  native(&cpu, code, sizeof(code));
  /* WRAM is 8 master clocks an access and an internal cycle is 6. */
  for (; index < sizeof(want) / sizeof(want[0]); index += 1) {
    cpu.clocks = 0;
    cpu.p = (Uint8)(cpu.p | 0x02);
    step_n(&cpu, 1);
    expect_u16("clocks", (Uint16)cpu.clocks, (Uint16)want[index]);
  }
  smx_bus_quit(&bus);
}

int main(void) {
  test_alu_and_flow();
  test_cycles();
  test_stz_abs_x();
  test_boot();
  return g_failed ? 1 : 0;
}
