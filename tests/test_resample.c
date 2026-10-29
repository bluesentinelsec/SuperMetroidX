#include "smx/resample.h"

#include <SDL3/SDL.h>

static bool g_failed = false;

static void expect(const char *name, bool value) {
  if (!value) {
    SDL_Log("resample: %s", name);
    g_failed = true;
  }
}

#define IN_RATE 32040u
#define OUT_RATE 48000u
#define IN_FRAMES 16020u

/* `frames` of a full-scale-ish stereo tone at `hz`, resampled in 534-frame blocks. */
static Uint32 tone(SmxResampler *resampler, double hz, Sint16 *out, Uint32 max) {
  static Sint16 in[IN_FRAMES * 2u];
  Uint32 index = 0;
  Uint32 written = 0;

  for (; index < IN_FRAMES; index += 1) {
    Sint16 value = (Sint16)(16000.0 * SDL_sin(2.0 * SDL_PI_D * hz * index / IN_RATE));
    in[index * 2u] = value;
    in[index * 2u + 1u] = value;
  }
  for (index = 0; index < IN_FRAMES; index += 534u) {
    Uint32 frames = IN_FRAMES - index < 534u ? IN_FRAMES - index : 534u;
    written += smx_resample_run(resampler, in + index * 2u, frames, out + written * 2u,
                                max - written);
  }
  return written;
}

/* The level of `hz` in the left channel of `frames` output frames, by Goertzel, in dB below 16000. */
static double level(const Sint16 *out, Uint32 from, Uint32 frames, double hz) {
  double coeff = 2.0 * SDL_cos(2.0 * SDL_PI_D * hz / OUT_RATE);
  double s1 = 0.0;
  double s2 = 0.0;
  double power = 0.0;
  Uint32 index = 0;

  for (; index < frames; index += 1) {
    /* A Hann window, so a strong tone does not leak into the bins around it. */
    double w = 0.5 - 0.5 * SDL_cos(2.0 * SDL_PI_D * index / (frames - 1u));
    double s = w * out[(from + index) * 2u] + coeff * s1 - s2;
    s2 = s1;
    s1 = s;
  }
  power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
  /* A Hann window keeps half the amplitude: a 16000 tone reads 4000 * frames. */
  return 20.0 * SDL_log10(SDL_sqrt(power) / (4000.0 * frames) + 1e-12);
}

int main(void) {
  static SmxResampler resampler;
  static Sint16 out[IN_FRAMES * 4u];
  Uint32 written = 0;
  double error = 0.0;
  double signal = 0.0;
  Uint32 index = 0;

  smx_resample_init(&resampler, IN_RATE, OUT_RATE);
  written = tone(&resampler, 1000.0, out, IN_FRAMES * 2u);
  expect("half a second in is half a second out", written > 23900u && written <= 24000u);
  for (index = 1000; index < 20000u; index += 1) {
    double ideal = 16000.0 * SDL_sin(2.0 * SDL_PI_D * 1000.0 * index / OUT_RATE);
    error += (out[index * 2u] - ideal) * (out[index * 2u] - ideal);
    signal += ideal * ideal;
  }
  SDL_Log("resample: 1 kHz SNR %.1f dB", 10.0 * SDL_log10(signal / error));
  expect("a 1 kHz tone comes through clean", 10.0 * SDL_log10(signal / error) > 70.0);
  expect("both channels match", out[5000] == out[5001]);

  smx_resample_init(&resampler, IN_RATE, OUT_RATE);
  tone(&resampler, 14000.0, out, IN_FRAMES * 2u);
  SDL_Log("resample: 14 kHz at %.2f dB", level(out, 2000, 16384, 14000.0));
  expect("14 kHz passes flat", level(out, 2000, 16384, 14000.0) > -0.5);

  smx_resample_init(&resampler, IN_RATE, OUT_RATE);
  tone(&resampler, 15000.0, out, IN_FRAMES * 2u);
  SDL_Log("resample: 15 kHz image at 17040 Hz %.1f dB", level(out, 2000, 16384, 17040.0));
  /* 17040 Hz is in the filter's transition from pass to stop. */
  expect("15 kHz leaves a faint image at 17040 Hz", level(out, 2000, 16384, 17040.0) < -40.0);

  smx_resample_init(&resampler, IN_RATE, OUT_RATE);
  tone(&resampler, 5000.0, out, IN_FRAMES * 2u);
  /* Its image at 27040 Hz would fold back to 20960 Hz. */
  SDL_Log("resample: 5 kHz image at 20960 Hz %.1f dB", level(out, 2000, 16384, 20960.0));
  expect("5 kHz leaves no image", level(out, 2000, 16384, 20960.0) < -75.0);
  return g_failed ? 1 : 0;
}
