#include "smx/pace.h"

#include <SDL3/SDL.h>

#define FRAME_NS 16639266u

static bool g_failed = false;

static void expect(const char *name, bool value) {
  if (!value) {
    SDL_Log("pace: %s", name);
    g_failed = true;
  }
}

/* A repeatable jitter in [-spread, spread] nanoseconds, as present returns wander. */
static Sint64 jitter(Uint32 *seed, Sint64 spread) {
  *seed = *seed * 1664525u + 1013904223u;
  if (spread == 0) {
    return 0;
  }
  return (Sint64)(*seed >> 8) % (2 * spread + 1) - spread;
}

/*
 * Present `count` times on a display with refresh period `period` and the
 * given jitter, after `settle` presents to measure it. Counts the game frames
 * run and the most run for any one present.
 */
static Uint32 run(SmxPace *pace, Uint64 period, Sint64 spread, Uint32 settle, Uint32 count,
                  Uint32 *most) {
  Uint32 seed = 1;
  Uint64 at = 1000000000u;
  Uint32 frames = 0;
  Uint32 index = 0;

  *most = 0;
  for (; index < settle + count; index += 1) {
    Uint32 got = 0;
    at += period;
    got = smx_pace_presented(pace, (Uint64)((Sint64)at + jitter(&seed, spread)));
    if (index >= settle) {
      frames += got;
      *most = got > *most ? got : *most;
    }
  }
  return frames;
}

static void test_60hz(void) {
  SmxPace pace;
  Uint32 most = 0;
  Uint32 frames = 0;

  smx_pace_init(&pace, FRAME_NS, 60.0f);
  frames = run(&pace, 16666667u, 1000000, 300, 600, &most);
  expect("60 Hz locks 1:1", pace.lock == 1);
  expect("60 Hz runs one frame per refresh", frames == 600 && most == 1);
  expect("60 Hz never interpolates", smx_pace_alpha(&pace) == 1.0f);
  expect("60 Hz slows audio 0.17%", smx_pace_audio_ratio(&pace, 100, 100) < 0.999f &&
                                        smx_pace_audio_ratio(&pace, 100, 100) > 0.997f);
}

static void test_5994hz(void) {
  SmxPace pace;
  Uint32 most = 0;
  Uint32 frames = 0;

  smx_pace_init(&pace, FRAME_NS, 0.0f);
  frames = run(&pace, 16683350u, 500000, 300, 600, &most);
  expect("59.94 Hz locks 1:1", pace.lock == 1 && frames == 600 && most == 1);
}

static void test_120hz(void) {
  SmxPace pace;
  Uint32 most = 0;
  Uint32 frames = 0;
  Uint32 seed = 7;
  Uint64 at = 1000000000u;
  Uint32 index = 0;
  Uint32 last = 0;
  bool alternates = true;

  /* The display mode says 60 Hz, but presents come at 120 Hz with 1.7 ms of jitter. */
  smx_pace_init(&pace, FRAME_NS, 60.0f);
  frames = run(&pace, 8333333u, 1700000, 300, 1200, &most);
  expect("120 Hz locks 2:1", pace.lock == 2);
  expect("120 Hz runs a frame every other refresh", frames == 600 && most == 1);
  at = pace.presents[(pace.next + SMX_PACE_PRESENTS - 1u) % SMX_PACE_PRESENTS];
  for (index = 0; index < 200; index += 1) {
    Uint32 got = 0;
    at += 8333333u;
    got = smx_pace_presented(&pace, (Uint64)((Sint64)at + jitter(&seed, 1700000)));
    if (index > 0 && got == last) {
      alternates = false;
    }
    last = got;
  }
  expect("120 Hz shows each frame for two refreshes", alternates);
  expect("120 Hz interpolates halfway, then shows the frame",
         (smx_pace_alpha(&pace) == 0.5f || smx_pace_alpha(&pace) == 1.0f));
}

static void test_mode_rate(void) {
  SmxPace pace;
  Uint32 most = 0;

  /* Presents measure 120.5 Hz on a 120 Hz mode: the mode's period stands, so audio is steady. */
  smx_pace_init(&pace, FRAME_NS, 120.0f);
  run(&pace, 8298755u, 1700000, 300, 300, &most);
  expect("the display mode's rate stands", pace.period_ns == 8333333u && pace.lock == 2);
}

static void test_144hz(void) {
  SmxPace pace;
  Uint32 most = 0;
  Uint32 frames = 0;

  smx_pace_init(&pace, FRAME_NS, 144.0f);
  frames = run(&pace, 6944444u, 500000, 300, 1440, &most);
  /* Ten seconds at the console's rate is 601 frames. */
  expect("144 Hz does not lock", pace.lock == 0);
  expect("144 Hz interpolates", smx_pace_alpha(&pace) < 1.0f);
  expect("144 Hz keeps the console's rate", frames >= 600 && frames <= 602 && most == 1);
  expect("144 Hz plays audio at its rate", smx_pace_audio_ratio(&pace, 100, 100) == 1.0f);
}

static void test_missed_refresh(void) {
  SmxPace pace;
  Uint32 most = 0;

  smx_pace_init(&pace, FRAME_NS, 60.0f);
  run(&pace, 16666667u, 0, 300, 10, &most);
  expect("a present two refreshes late runs two frames",
         smx_pace_presented(
             &pace, pace.presents[(pace.next + SMX_PACE_PRESENTS - 1u) % SMX_PACE_PRESENTS] +
                        33333333u) == 2);
}

static void test_no_vsync(void) {
  SmxPace pace;
  Uint32 most = 0;
  Uint64 now = 0;

  smx_pace_init(&pace, FRAME_NS, 60.0f);
  run(&pace, 500000u, 0, 100, 0, &most);
  expect("presents that do not wait turn vsync off", !pace.vsync);
  expect("without vsync nothing is interpolated", smx_pace_alpha(&pace) == 1.0f);
  now = pace.presents[(pace.next + SMX_PACE_PRESENTS - 1u) % SMX_PACE_PRESENTS];
  expect("without vsync the shell waits for the frame", smx_pace_wait_ns(&pace, now) > 0);
  smx_pace_set_vsync(&pace, true, now);
  expect("vsync comes back", pace.vsync && smx_pace_wait_ns(&pace, now) == 0);
}

static void test_audio_nudge(void) {
  SmxPace pace;
  Uint32 index = 0;
  float ratio = 0.0f;

  smx_pace_init(&pace, FRAME_NS, 144.0f);
  expect("audio starts at the game's rate", smx_pace_audio_ratio(&pace, 100, 100) == 1.0f);
  ratio = smx_pace_audio_ratio(&pace, 300, 100);
  expect("one long reading moves the ratio a little", ratio > 1.0f && ratio < 1.0002f);
  for (; index < 500; index += 1) {
    ratio = smx_pace_audio_ratio(&pace, 300, 100);
  }
  expect("a long audio queue plays at most 0.25% faster", ratio > 1.0024f && ratio < 1.0026f);
  for (index = 0; index < 500; index += 1) {
    ratio = smx_pace_audio_ratio(&pace, 50, 100);
  }
  expect("a short audio queue plays slower", ratio < 1.0f);
}

int main(void) {
  test_60hz();
  test_5994hz();
  test_120hz();
  test_mode_rate();
  test_144hz();
  test_missed_refresh();
  test_no_vsync();
  test_audio_nudge();
  return g_failed ? 1 : 0;
}
