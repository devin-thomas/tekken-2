/* Tekken 2's own settings file, t2-input.cfg, beside the runtime's
 * settings.toml in the per-user home (the folder psxrecomp keeps every file
 * in: %LOCALAPPDATA%\Tekken 2 Recompiled\ on Windows, ~/Library/Application
 * Support/Tekken 2 Recompiled/ on macOS, the app's private files on Android
 * and iOS).
 *
 *   # comments
 *   p1.x = key:X, pad:south, joy:b1
 *   touch.controls = auto
 *
 * One "key = value" per line. Keys keep the order they were read in; set
 * adds new ones at the end and save writes them all back, so a hand-edited
 * file keeps its order. Adapted from the GGX+ port's sdl/config.c (MIT, Copyright (c) 2026 Lil Gohan), on
 * SDL's file calls so Windows paths in UTF-8 work.
 */
#ifndef T2_CONFIG_H
#define T2_CONFIG_H

typedef struct T2Config T2Config;

/* An empty config bound to path; the file is read if it exists. NULL only
   when out of memory. A line that isn't "key = value" is reported and
   dropped. */
T2Config *t2_config_load(const char *path);
void t2_config_free(T2Config *c);
/* NULL if the key is not set. */
const char *t2_config_get(const T2Config *c, const char *key);
const char *t2_config_get_or(const T2Config *c, const char *key, const char *fallback);
int t2_config_get_int(const T2Config *c, const char *key, int fallback);
void t2_config_set(T2Config *c, const char *key, const char *value);
void t2_config_set_int(T2Config *c, const char *key, int value);
/* Writes the file through a temporary one. 0 on success. */
int t2_config_save(const T2Config *c);
const char *t2_config_path(const T2Config *c);

#endif
