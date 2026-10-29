#ifndef SMX_RESAMPLE_H_
#define SMX_RESAMPLE_H_

#include <SDL3/SDL.h>

/** Taps of the filter: 32 input samples either side of each output sample. */
#define SMX_RESAMPLE_TAPS 64u
/** Filter phases between two input samples; others interpolate between these. */
#define SMX_RESAMPLE_PHASES 512u
/** Stereo input frames waiting to be resampled. */
#define SMX_RESAMPLE_INPUT 8192u

/**
 * Stereo S16 resampling with a long Kaiser-windowed sinc, for the S-DSP's
 * 32040 Hz output to the audio device's rate. The cutoff is the lower of
 * the two Nyquist frequencies. From 32040 Hz, treble passes flat to about
 * 14.5 kHz, and images above 17.5 kHz are gone; SDL's own 12-tap filter
 * rolls off earlier and lets more through.
 */
typedef struct SmxResampler {
  Uint32 in_rate;
  Uint32 out_rate;
  /** Input frames per output frame, and how far into the input the next output is. */
  double step;
  double pos;
  float filter[SMX_RESAMPLE_PHASES + 1u][SMX_RESAMPLE_TAPS];
  /** Input waiting, as floats, with the history the filter still needs. */
  float input[SMX_RESAMPLE_INPUT][2];
  Uint32 count;
} SmxResampler;

/** Resample from `in_rate` to `out_rate`. */
void smx_resample_init(SmxResampler *resampler, Uint32 in_rate, Uint32 out_rate);

/** Forget the input, as when the stream restarts. */
void smx_resample_reset(SmxResampler *resampler);

/**
 * Take `frames` stereo input frames and write up to `max` output frames to
 * `out`. Returns the frames written. Input that cannot be used yet waits;
 * `max` of `frames * out_rate / in_rate + 2` takes everything it can.
 */
Uint32 smx_resample_run(SmxResampler *resampler, const Sint16 *in, Uint32 frames, Sint16 *out,
                        Uint32 max);

#endif /* SMX_RESAMPLE_H_ */
