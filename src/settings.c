#include "smx/settings.h"

/* Each setting's key in the file and the names of its values, in SmxSettingId and enum order. */
typedef struct SmxSettingsKey {
  const char *key;
  const char *values[4];
} SmxSettingsKey;

static const SmxSettingsKey smx_settings_keys[] = {
    {"window_mode", {"windowed", "borderless", "exclusive", NULL}},
    {"scale", {"integer", "fit", NULL, NULL}},
    {"aspect", {"square", "tv", NULL, NULL}},
    {"scanlines", {"off", "on", NULL, NULL}},
    {"audio_interpolation", {"gaussian", "cubic", "sinc", NULL}},
    {"audio_resampler", {"sinc", "sdl", NULL, NULL}},
};

#define SMX_SETTINGS_KEYS ((Uint32)SMX_SETTING_COUNT)

Uint32 smx_settings_get(const SmxSettings *settings, SmxSettingId id) {
  switch (id) {
  case SMX_SETTING_WINDOW_MODE:
    return (Uint32)settings->display.window_mode;
  case SMX_SETTING_SCALE:
    return settings->display.fill ? 1u : 0u;
  case SMX_SETTING_ASPECT:
    return settings->display.tv_aspect ? 1u : 0u;
  case SMX_SETTING_SCANLINES:
    return settings->display.scanlines ? 1u : 0u;
  case SMX_SETTING_AUDIO_INTERPOLATION:
    return (Uint32)settings->sound.interpolation;
  default:
    return (Uint32)settings->sound.resampler;
  }
}

void smx_settings_set(SmxSettings *settings, SmxSettingId id, Uint32 value) {
  switch (id) {
  case SMX_SETTING_WINDOW_MODE:
    settings->display.window_mode = (SmxWindowMode)value;
    break;
  case SMX_SETTING_SCALE:
    settings->display.fill = value != 0;
    break;
  case SMX_SETTING_ASPECT:
    settings->display.tv_aspect = value != 0;
    break;
  case SMX_SETTING_SCANLINES:
    settings->display.scanlines = value != 0;
    break;
  case SMX_SETTING_AUDIO_INTERPOLATION:
    settings->sound.interpolation = (SmxDspInterpolation)value;
    break;
  default:
    settings->sound.resampler = (SmxSoundResampler)value;
    break;
  }
}

Uint32 smx_settings_choices(SmxSettingId id) {
  Uint32 count = 0;

  while (count < 4u && smx_settings_keys[id].values[count] != NULL) {
    count += 1;
  }
  return count;
}

const char *smx_settings_key(SmxSettingId id) { return smx_settings_keys[id].key; }

const char *smx_settings_value_name(SmxSettingId id, Uint32 value) {
  return smx_settings_keys[id].values[value];
}

void smx_settings_defaults(SmxSettings *settings) {
  SDL_memset(settings, 0, sizeof(*settings));
  smx_present_defaults(&settings->display);
  smx_sound_defaults(&settings->sound);
}

/* Copy `text` from `from` to `to` without surrounding spaces. */
static void smx_settings_trim(char *out, Uint32 size, const char *from, const char *to) {
  Uint32 length = 0;

  while (from < to && (*from == ' ' || *from == '\t')) {
    from += 1;
  }
  while (to > from && (to[-1] == ' ' || to[-1] == '\t' || to[-1] == '\r')) {
    to -= 1;
  }
  length = (Uint32)(to - from);
  if (length >= size) {
    length = size - 1u;
  }
  SDL_memcpy(out, from, length);
  out[length] = '\0';
}

void smx_settings_parse(SmxSettings *settings, const char *text) {
  const char *line = text;

  while (*line != '\0') {
    const char *end = line;
    const char *equals = NULL;
    char key[32];
    char value[32];
    Uint32 index = 0;

    while (*end != '\0' && *end != '\n') {
      if (*end == '=' && equals == NULL) {
        equals = end;
      }
      end += 1;
    }
    if (equals != NULL && *line != '#') {
      smx_settings_trim(key, sizeof(key), line, equals);
      smx_settings_trim(value, sizeof(value), equals + 1, end);
      for (; index < SMX_SETTINGS_KEYS; index += 1) {
        Uint32 choice = 0;
        if (SDL_strcmp(key, smx_settings_keys[index].key) != 0) {
          continue;
        }
        for (; choice < 4u && smx_settings_keys[index].values[choice] != NULL; choice += 1) {
          if (SDL_strcmp(value, smx_settings_keys[index].values[choice]) == 0) {
            smx_settings_set(settings, (SmxSettingId)index, choice);
          }
        }
      }
    }
    line = *end == '\n' ? end + 1 : end;
  }
}

bool smx_settings_format(const SmxSettings *settings, char *out, Uint32 size) {
  Uint32 at = 0;
  Uint32 index = 0;
  Sint32 wrote = SDL_snprintf(out, size, "# SuperMetroidX settings\n");

  if (wrote < 0 || (Uint32)wrote >= size) {
    return false;
  }
  at = (Uint32)wrote;
  for (; index < SMX_SETTINGS_KEYS; index += 1) {
    wrote = SDL_snprintf(out + at, size - at, "%s = %s\n", smx_settings_keys[index].key,
                         smx_settings_keys[index].values[smx_settings_get(settings, (SmxSettingId)index)]);
    if (wrote < 0 || (Uint32)wrote >= size - at) {
      return false;
    }
    at += (Uint32)wrote;
  }
  return true;
}

bool smx_settings_path(char *path, Uint32 size) {
  const char *override = SDL_getenv("SMX_DATA_DIR");
  char *dir = NULL;

  if (override != NULL && override[0] != '\0') {
    SDL_CreateDirectory(override);
    SDL_snprintf(path, size, "%s/%s", override, SMX_SETTINGS_FILE);
    return true;
  }
  dir = SDL_GetPrefPath("SuperMetroidX", "SuperMetroidX");
  if (dir == NULL) {
    return false;
  }
  SDL_snprintf(path, size, "%s%s", dir, SMX_SETTINGS_FILE);
  SDL_free(dir);
  return true;
}

bool smx_settings_load(SmxSettings *settings, const char *path) {
  size_t size = 0;
  char *text = (char *)SDL_LoadFile(path, &size);

  if (text == NULL) {
    return false;
  }
  smx_settings_parse(settings, text);
  SDL_free(text);
  return true;
}

bool smx_settings_save(const SmxSettings *settings, const char *path) {
  char text[1024];
  char temporary[1100];
  SDL_IOStream *io = NULL;
  size_t length = 0;
  bool ok = false;

  if (!smx_settings_format(settings, text, sizeof(text))) {
    return false;
  }
  SDL_snprintf(temporary, sizeof(temporary), "%s.new", path);
  io = SDL_IOFromFile(temporary, "wb");
  if (io == NULL) {
    SDL_Log("cannot save settings: %s", SDL_GetError());
    return false;
  }
  length = SDL_strlen(text);
  ok = SDL_WriteIO(io, text, length) == length;
  ok = SDL_CloseIO(io) && ok;
  if (ok) {
    ok = SDL_RenamePath(temporary, path);
  }
  if (!ok) {
    SDL_Log("cannot save settings to %s: %s", path, SDL_GetError());
    SDL_RemovePath(temporary);
  }
  return ok;
}

bool smx_settings_remove(const char *path) {
  if (!SDL_GetPathInfo(path, NULL)) {
    return true;
  }
  return SDL_RemovePath(path);
}
