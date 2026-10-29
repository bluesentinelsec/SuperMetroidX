#ifndef SMX_SOUND_H_
#define SMX_SOUND_H_

#include "smx/dsp.h"
#include "smx/resample.h"

#include <SDL3/SDL.h>

/** The S-DSP's output rate. */
#define SMX_SOUND_DSP_RATE 32040u

/** Who converts the S-DSP's 32040 Hz to the device's rate. */
typedef enum SmxSoundResampler {
  SMX_SOUND_RESAMPLE_SINC,
  SMX_SOUND_RESAMPLE_SDL
} SmxSoundResampler;

/** What the listener hears. Neither changes what the game runs. */
typedef struct SmxSoundOptions {
  SmxDspInterpolation interpolation;
  SmxSoundResampler resampler;
} SmxSoundOptions;

/** Sinc interpolation and the sinc resampler. */
void smx_sound_defaults(SmxSoundOptions *options);

/**
 * Parse one option at `args`, as `smx_present_option` does: returns the
 * arguments used (2), 0 if the option is not a sound option, or -1 if its
 * value is missing or unknown.
 */
Sint32 smx_sound_option(SmxSoundOptions *options, Sint32 count, char **args);

void smx_sound_usage(void);

/** "sinc", "cubic", or "gaussian". */
const char *smx_sound_interpolation_name(SmxDspInterpolation interpolation);

/**
 * The game's audio to the default device. With the sinc resampler the stream
 * is fed at the device's own rate, so SDL does not resample it again.
 */
typedef struct SmxSound {
  SDL_AudioStream *stream;
  SmxSoundOptions options;
  /** The rate the device plays, and the rate the stream is fed. */
  Uint32 device_rate;
  Uint32 rate;
  SmxResampler *resampler;
} SmxSound;

bool smx_sound_open(SmxSound *sound, const SmxSoundOptions *options);
void smx_sound_close(SmxSound *sound);

/** Change the resampler while playing. The interpolation is the S-DSP's to apply. */
void smx_sound_set(SmxSound *sound, const SmxSoundOptions *options);

/** Stop or restart playback, as while the settings menu is open. */
void smx_sound_pause(SmxSound *sound, bool paused);

/** Queue `count` stereo frames at 32040 Hz. */
void smx_sound_put(SmxSound *sound, const Sint16 *frames, Uint32 count);

/** Audio queued and not yet played, in 32040 Hz frames. */
Uint32 smx_sound_queued(const SmxSound *sound);

#endif /* SMX_SOUND_H_ */
