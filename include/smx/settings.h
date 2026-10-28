#ifndef SMX_SETTINGS_H_
#define SMX_SETTINGS_H_

#include "smx/present.h"
#include "smx/sound.h"

#include <SDL3/SDL.h>

/** The settings file, in the save folder (or `SMX_DATA_DIR`). */
#define SMX_SETTINGS_FILE "settings.ini"

/**
 * The player's preferences: everything the settings menu and the
 * command-line options change, except cheats, which last until the game quits.
 *
 * They come from the defaults, then the settings file, then the command
 * line. The menu saves what it changes; an option given on the command line
 * applies to that run only, until the menu changes the same setting.
 */
typedef struct SmxSettings {
  SmxPresentOptions display;
  SmxSoundOptions sound;
} SmxSettings;

/** Each setting, in the order the menu shows them. */
typedef enum SmxSettingId {
  SMX_SETTING_WINDOW_MODE,
  SMX_SETTING_SCALE,
  SMX_SETTING_ASPECT,
  SMX_SETTING_SCANLINES,
  SMX_SETTING_AUDIO_INTERPOLATION,
  SMX_SETTING_AUDIO_RESAMPLER,
  SMX_SETTING_COUNT
} SmxSettingId;

/** The setting's value as an index into its choices. */
Uint32 smx_settings_get(const SmxSettings *settings, SmxSettingId id);
void smx_settings_set(SmxSettings *settings, SmxSettingId id, Uint32 value);

/** How many values the setting has. */
Uint32 smx_settings_choices(SmxSettingId id);

/** The setting's key in the file, and value `value`'s name there. */
const char *smx_settings_key(SmxSettingId id);
const char *smx_settings_value_name(SmxSettingId id, Uint32 value);

void smx_settings_defaults(SmxSettings *settings);

/**
 * Read `key = value` lines from `text` over `settings`. Lines that are
 * blank, comments (`#`), unknown, or out of range are skipped, so a damaged
 * file costs only the lines that are damaged.
 */
void smx_settings_parse(SmxSettings *settings, const char *text);

/** Write `settings` as text to `out`; returns false if `size` is too small. */
bool smx_settings_format(const SmxSettings *settings, char *out, Uint32 size);

/** `<save folder>/settings.ini`. */
bool smx_settings_path(char *path, Uint32 size);

/** Read the settings file over `settings`. Returns false when there is none. */
bool smx_settings_load(SmxSettings *settings, const char *path);

/** Write the settings file whole, through a temporary file, so a crash cannot leave half of one. */
bool smx_settings_save(const SmxSettings *settings, const char *path);

/** Delete the settings file, for `--reset-settings` and the menu's reset. */
bool smx_settings_remove(const char *path);

#endif /* SMX_SETTINGS_H_ */
