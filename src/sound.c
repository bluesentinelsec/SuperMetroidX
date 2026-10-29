#include "smx/sound.h"

void smx_sound_defaults(SmxSoundOptions *options) {
  options->interpolation = SMX_DSP_SINC;
  options->resampler = SMX_SOUND_RESAMPLE_SINC;
}

const char *smx_sound_interpolation_name(SmxDspInterpolation interpolation) {
  if (interpolation == SMX_DSP_GAUSSIAN) {
    return "gaussian";
  }
  return interpolation == SMX_DSP_CUBIC ? "cubic" : "sinc";
}

Sint32 smx_sound_option(SmxSoundOptions *options, Sint32 count, char **args) {
  const char *name = args[0];
  const char *value = count > 1 ? args[1] : NULL;

  if (SDL_strcmp(name, "--audio-interpolation") == 0) {
    if (value == NULL) {
    } else if (SDL_strcmp(value, "sinc") == 0) {
      options->interpolation = SMX_DSP_SINC;
      return 2;
    } else if (SDL_strcmp(value, "cubic") == 0) {
      options->interpolation = SMX_DSP_CUBIC;
      return 2;
    } else if (SDL_strcmp(value, "gaussian") == 0) {
      options->interpolation = SMX_DSP_GAUSSIAN;
      return 2;
    }
    return -1;
  }
  if (SDL_strcmp(name, "--audio-resampler") == 0) {
    if (value == NULL) {
    } else if (SDL_strcmp(value, "sinc") == 0) {
      options->resampler = SMX_SOUND_RESAMPLE_SINC;
      return 2;
    } else if (SDL_strcmp(value, "sdl") == 0) {
      options->resampler = SMX_SOUND_RESAMPLE_SDL;
      return 2;
    }
    return -1;
  }
  return 0;
}

void smx_sound_usage(void) {
  SDL_Log("  --audio-interpolation sinc|cubic|gaussian");
  SDL_Log("                      how instrument samples are smoothed: sinc and cubic keep the");
  SDL_Log("                      treble the console's gaussian filter softens (default sinc)");
  SDL_Log("  --audio-resampler sinc|sdl");
  SDL_Log("                      convert 32040 Hz to the device's rate with a long sinc filter,");
  SDL_Log("                      or leave it to SDL (default sinc)");
}

/* The rate the stream takes from the shell: the device's with the sinc resampler. */
static Uint32 smx_sound_feed_rate(const SmxSound *sound) {
  if (sound->options.resampler == SMX_SOUND_RESAMPLE_SINC && sound->resampler != NULL &&
      sound->device_rate != SMX_SOUND_DSP_RATE) {
    return sound->device_rate;
  }
  return SMX_SOUND_DSP_RATE;
}

static void smx_sound_feed(SmxSound *sound) {
  SDL_AudioSpec spec;

  sound->rate = smx_sound_feed_rate(sound);
  spec.format = SDL_AUDIO_S16;
  spec.channels = 2;
  spec.freq = (int)sound->rate;
  SDL_SetAudioStreamFormat(sound->stream, &spec, NULL);
  if (sound->resampler != NULL) {
    smx_resample_reset(sound->resampler);
  }
}

bool smx_sound_open(SmxSound *sound, const SmxSoundOptions *options) {
  SDL_AudioSpec spec;
  Sint32 frames = 0;

  SDL_memset(sound, 0, sizeof(*sound));
  sound->options = *options;
  sound->device_rate = SMX_SOUND_DSP_RATE;
  if (SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &frames) &&
      spec.freq > 0) {
    sound->device_rate = (Uint32)spec.freq;
  }
  spec.format = SDL_AUDIO_S16;
  spec.channels = 2;
  spec.freq = SMX_SOUND_DSP_RATE;
  sound->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
  if (sound->stream == NULL) {
    SDL_Log("audio unavailable: %s", SDL_GetError());
    return false;
  }
  sound->resampler = (SmxResampler *)SDL_malloc(sizeof(SmxResampler));
  if (sound->resampler != NULL) {
    smx_resample_init(sound->resampler, SMX_SOUND_DSP_RATE, sound->device_rate);
  }
  smx_sound_feed(sound);
  if (!SDL_ResumeAudioStreamDevice(sound->stream)) {
    SDL_Log("audio resume failed: %s", SDL_GetError());
    smx_sound_close(sound);
    return false;
  }
  SDL_Log("audio: %u Hz, %s interpolation, %s resampler", (unsigned)sound->device_rate,
          smx_sound_interpolation_name(options->interpolation),
          sound->rate == SMX_SOUND_DSP_RATE ? "SDL" : "sinc");
  return true;
}

void smx_sound_close(SmxSound *sound) {
  if (sound->stream != NULL) {
    SDL_DestroyAudioStream(sound->stream);
  }
  SDL_free(sound->resampler);
  SDL_memset(sound, 0, sizeof(*sound));
}

void smx_sound_set(SmxSound *sound, const SmxSoundOptions *options) {
  bool feed = options->resampler != sound->options.resampler;

  sound->options = *options;
  if (feed && sound->stream != NULL) {
    smx_sound_feed(sound);
  }
}

void smx_sound_put(SmxSound *sound, const Sint16 *frames, Uint32 count) {
  Sint16 out[2048 * 2];
  Uint32 at = 0;

  if (sound->stream == NULL) {
    return;
  }
  if (sound->rate == SMX_SOUND_DSP_RATE) {
    SDL_PutAudioStreamData(sound->stream, frames, (int)(count * 4u));
    return;
  }
  /* 256 frames in make at most 256 * 192000 / 32040 + 2 out, within `out` at any device rate. */
  while (at < count) {
    Uint32 take = count - at < 256u ? count - at : 256u;
    Uint32 made = smx_resample_run(sound->resampler, frames + at * 2u, take, out, 2048u);
    SDL_PutAudioStreamData(sound->stream, out, (int)(made * 4u));
    at += take;
  }
}

Uint32 smx_sound_queued(const SmxSound *sound) {
  Sint32 bytes = 0;

  if (sound->stream == NULL) {
    return 0;
  }
  bytes = SDL_GetAudioStreamQueued(sound->stream);
  if (bytes <= 0) {
    return 0;
  }
  return (Uint32)((Uint64)(Uint32)bytes / 4u * SMX_SOUND_DSP_RATE / sound->rate);
}
