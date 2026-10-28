#include "smx/resample.h"

/* Kaiser window shape: about 80 dB of stopband. */
#define SMX_RESAMPLE_BETA 8.0

static double smx_resample_bessel_i0(double x) {
  double sum = 1.0;
  double term = 1.0;
  Uint32 k = 1;

  for (; k < 32u; k += 1) {
    term *= (x / (2.0 * (double)k)) * (x / (2.0 * (double)k));
    sum += term;
  }
  return sum;
}

void smx_resample_init(SmxResampler *resampler, Uint32 in_rate, Uint32 out_rate) {
  const double half = (double)SMX_RESAMPLE_TAPS / 2.0;
  /* The cutoff, in cycles per input sample: the lower Nyquist frequency. */
  double cutoff = 0.5 * (out_rate < in_rate ? (double)out_rate / (double)in_rate : 1.0);
  Uint32 phase = 0;

  SDL_memset(resampler, 0, sizeof(*resampler));
  resampler->in_rate = in_rate;
  resampler->out_rate = out_rate;
  resampler->step = (double)in_rate / (double)out_rate;
  for (; phase <= SMX_RESAMPLE_PHASES; phase += 1) {
    double frac = (double)phase / (double)SMX_RESAMPLE_PHASES;
    double sum = 0.0;
    Uint32 tap = 0;
    for (; tap < SMX_RESAMPLE_TAPS; tap += 1) {
      double x = (double)tap - (half - 1.0) - frac;
      double ratio = x / half;
      double window = 0.0;
      double sinc = 2.0 * cutoff;
      if (ratio > -1.0 && ratio < 1.0) {
        window = smx_resample_bessel_i0(SMX_RESAMPLE_BETA * SDL_sqrt(1.0 - ratio * ratio)) /
                 smx_resample_bessel_i0(SMX_RESAMPLE_BETA);
      }
      if (x != 0.0) {
        sinc = SDL_sin(2.0 * SDL_PI_D * cutoff * x) / (SDL_PI_D * x);
      }
      resampler->filter[phase][tap] = (float)(sinc * window);
      sum += sinc * window;
    }
    for (tap = 0; tap < SMX_RESAMPLE_TAPS; tap += 1) {
      resampler->filter[phase][tap] = (float)(resampler->filter[phase][tap] / sum);
    }
  }
  smx_resample_reset(resampler);
}

void smx_resample_reset(SmxResampler *resampler) {
  /* Silence before the first sample, so the first output has its history. */
  resampler->count = SMX_RESAMPLE_TAPS / 2u - 1u;
  SDL_memset(resampler->input, 0, sizeof(resampler->input[0]) * resampler->count);
  resampler->pos = (double)resampler->count;
}

static Sint16 smx_resample_clamp(float value) {
  if (value >= 32767.0f) {
    return 32767;
  }
  if (value <= -32768.0f) {
    return -32768;
  }
  return (Sint16)(value < 0.0f ? value - 0.5f : value + 0.5f);
}

Uint32 smx_resample_run(SmxResampler *resampler, const Sint16 *in, Uint32 frames, Sint16 *out,
                        Uint32 max) {
  const Uint32 before = SMX_RESAMPLE_TAPS / 2u - 1u;
  Uint32 written = 0;
  Uint32 index = 0;
  Uint32 used = 0;

  while (index < frames) {
    Uint32 room = SMX_RESAMPLE_INPUT - resampler->count;
    Uint32 take = frames - index < room ? frames - index : room;
    Uint32 i = 0;
    for (; i < take; i += 1) {
      resampler->input[resampler->count + i][0] = (float)in[(index + i) * 2u];
      resampler->input[resampler->count + i][1] = (float)in[(index + i) * 2u + 1u];
    }
    resampler->count += take;
    index += take;

    /* An output needs input up to TAPS/2 past its position. */
    while (written < max && (Uint32)resampler->pos + SMX_RESAMPLE_TAPS / 2u < resampler->count) {
      Uint32 base = (Uint32)resampler->pos;
      double phase = (resampler->pos - (double)base) * (double)SMX_RESAMPLE_PHASES;
      Uint32 p = (Uint32)phase;
      float blend = (float)(phase - (double)p);
      const float *a = resampler->filter[p];
      const float *b = resampler->filter[p + 1u];
      const float(*x)[2] = &resampler->input[base - before];
      float left_a = 0.0f;
      float left_b = 0.0f;
      float right_a = 0.0f;
      float right_b = 0.0f;
      Uint32 tap = 0;
      for (; tap < SMX_RESAMPLE_TAPS; tap += 1) {
        left_a += a[tap] * x[tap][0];
        left_b += b[tap] * x[tap][0];
        right_a += a[tap] * x[tap][1];
        right_b += b[tap] * x[tap][1];
      }
      out[written * 2u] = smx_resample_clamp(left_a + (left_b - left_a) * blend);
      out[written * 2u + 1u] = smx_resample_clamp(right_a + (right_b - right_a) * blend);
      written += 1u;
      resampler->pos += resampler->step;
    }

    /* Keep only the history the next output needs. */
    used = (Uint32)resampler->pos - before;
    if (used > resampler->count) {
      used = resampler->count;
    }
    SDL_memmove(resampler->input, resampler->input[used],
                sizeof(resampler->input[0]) * (resampler->count - used));
    resampler->count -= used;
    resampler->pos -= (double)used;
    if (written == max) {
      break;
    }
  }
  return written;
}
