#include "smx/settings.h"

#include <SDL3/SDL.h>

static bool g_failed = false;

static void expect(const char *name, bool value) {
  if (!value) {
    SDL_Log("settings: %s", name);
    g_failed = true;
  }
}

static void test_round_trip(void) {
  SmxSettings settings;
  SmxSettings read;
  char text[1024];

  smx_settings_defaults(&settings);
  settings.display.window_mode = SMX_WINDOW_EXCLUSIVE;
  settings.display.fill = false;
  settings.display.scanlines = false;
  settings.sound.interpolation = SMX_DSP_CUBIC;
  settings.sound.resampler = SMX_SOUND_RESAMPLE_SDL;
  expect("settings format", smx_settings_format(&settings, text, sizeof(text)));
  expect("the file names its values",
         SDL_strstr(text, "window_mode = exclusive\n") != NULL &&
             SDL_strstr(text, "audio_interpolation = cubic\n") != NULL);
  smx_settings_defaults(&read);
  smx_settings_parse(&read, text);
  expect("settings read back as written", SDL_memcmp(&read, &settings, sizeof(read)) == 0);
  expect("a small buffer is refused", !smx_settings_format(&settings, text, 40));
}

static void test_damage(void) {
  SmxSettings settings;
  SmxSettings defaults;

  smx_settings_defaults(&defaults);
  smx_settings_defaults(&settings);
  smx_settings_parse(&settings, "# a comment\r\n\n  scale=integer  \r\nscanlines = maybe\n"
                                "volume = 11\nwindow_mode\naspect = square");
  expect("spaces and CRLF are fine", !settings.display.fill);
  expect("the last line needs no newline", !settings.display.tv_aspect);
  expect("bad values and unknown keys are skipped",
         settings.display.scanlines == defaults.display.scanlines &&
             settings.display.window_mode == defaults.display.window_mode);
}

static void test_ids(void) {
  SmxSettings settings;
  Uint32 id = 0;

  smx_settings_defaults(&settings);
  for (; id < SMX_SETTING_COUNT; id += 1) {
    Uint32 last = smx_settings_choices((SmxSettingId)id) - 1u;
    smx_settings_set(&settings, (SmxSettingId)id, last);
    expect("every value of every setting reads back",
           smx_settings_get(&settings, (SmxSettingId)id) == last &&
               smx_settings_value_name((SmxSettingId)id, last) != NULL);
  }
  expect("keys", SDL_strcmp(smx_settings_key(SMX_SETTING_AUDIO_RESAMPLER), "audio_resampler") == 0);
}

static void test_file(void) {
  char *base = SDL_GetPrefPath("SuperMetroidX", "tests");
  char path[1024];
  SmxSettings settings;
  SmxSettings read;

  if (base == NULL) {
    expect("a folder for the test file", false);
    return;
  }
  SDL_snprintf(path, sizeof(path), "%ssettings-test.ini", base);
  SDL_free(base);
  smx_settings_remove(path);
  smx_settings_defaults(&read);
  expect("no file loads nothing", !smx_settings_load(&read, path));
  smx_settings_defaults(&settings);
  settings.display.tv_aspect = false;
  expect("settings save", smx_settings_save(&settings, path));
  expect("settings save again over the file", smx_settings_save(&settings, path));
  expect("settings load", smx_settings_load(&read, path) && !read.display.tv_aspect);
  expect("removing the file", smx_settings_remove(path) && !SDL_GetPathInfo(path, NULL));
  expect("removing it again is fine", smx_settings_remove(path));
}

int main(void) {
  test_round_trip();
  test_damage();
  test_ids();
  test_file();
  return g_failed ? 1 : 0;
}
