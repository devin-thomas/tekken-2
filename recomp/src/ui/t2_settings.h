/* Tekken 2's settings panel, drawn over the game by the runtime
 * (psx_title_hooks.h layers) on every platform.
 *
 * The state machine comes from the GGX+ port's sdl/settings.c (MIT, Copyright (c) 2026 Lil Gohan): it
 * opens when Select is held for a second on any pad, the game holds and
 * gets no input while it is open, Up and Down pick a row, Left and Right (or
 * X) change it, and closing saves. The drawing is new: Noto Sans through
 * t2_text.h instead of GGX's HUD font.
 *
 * It also opens with F1 or Escape on a keyboard and with the on-screen
 * controller's settings button. Circle goes back (and closes the main page),
 * Start closes, Escape closes or goes back.
 *
 * Rows: Resume; each player's controls (a page that rebinds one PlayStation
 * button at a time, or resets them); swapping the players' controllers;
 * controller mode (standard or direct USB); on Android and iOS the
 * on-screen controls (auto, always, never), their size and opacity; on the
 * desktop the renderer, the internal resolution and fullscreen; audio
 * latency; the FPS readout; Original or Enhanced mode; in Enhanced mode,
 * frame skip when slow and (Android) the display refresh; Restart when a
 * change waits for one; Quit.
 *
 * Tekken 2's own choices go in t2-input.cfg (t2_config.h) beside the
 * runtime's settings.toml; the renderer, resolution and fullscreen go
 * through the host, which saves them in the runtime's own files.
 */
#ifndef T2_SETTINGS_H
#define T2_SETTINGS_H

#include <stdint.h>

#include "t2_config.h"
#include "t2_input.h"
#include "t2_text.h"

typedef struct T2Settings T2Settings;

/* What the panel needs from the runtime. Any function may be NULL. */
typedef struct {
    void *ctx;
    int mobile;          /* Android or iOS: touch rows, no renderer or window rows */
    int android;         /* the display refresh row */
    int frameskip_default; /* frame skip when slow, when not chosen: the platform's default */
    int minimal;         /* player release: no controller mode, renderer, frame skip or refresh rows */
    const char *version; /* shown after the panel's title; NULL: none */
    int (*renderer)(void *ctx);                  /* 0 software, 1 OpenGL */
    void (*set_renderer)(void *ctx, int r);      /* applies after a restart */
    int (*resolution)(void *ctx);                /* 1-8x; 0: this renderer has none */
    void (*set_resolution)(void *ctx, int scale);
    int (*fullscreen)(void *ctx);
    void (*set_fullscreen)(void *ctx, int on);
    void (*restart)(void *ctx);
    void (*quit)(void *ctx);
} T2SettingsHost;

/* Tekken 2's own settings, as the rest of the glue reads them. */
enum { T2_TOUCH_AUTO, T2_TOUCH_ALWAYS, T2_TOUCH_NEVER };
enum { T2_CTRL_STANDARD, T2_CTRL_DIRECT_USB };
enum { T2_AUDIO_LOW, T2_AUDIO_NORMAL, T2_AUDIO_HIGH };
enum { T2_MODE_ENHANCED, T2_MODE_ORIGINAL };

int t2_setting_touch(const T2Config *cfg);            /* T2_TOUCH_* */
int t2_setting_touch_size(const T2Config *cfg);       /* percent, 50-150 */
int t2_setting_touch_opacity(const T2Config *cfg);    /* percent, 20-100 */
int t2_setting_controller_mode(const T2Config *cfg);  /* T2_CTRL_* */
int t2_setting_audio(const T2Config *cfg);            /* T2_AUDIO_* */
int t2_setting_audio_ms(int audio);                   /* 0: the game's own */
int t2_setting_fps(const T2Config *cfg);
int t2_setting_mode(const T2Config *cfg);             /* T2_MODE_* */
/* The runtime switches a mode sets before the runtime starts (t2_host.c's
   early hook; a variable already in the environment still wins). Original
   turns on every hardware-timing model the runtime has: GPU draw time
   (PSX_GPU_TIMING), CD drive (PSX_CD_TIMING), BIOS ROM (PSX_ROM_TIMING),
   pad and card serial (PSX_SIO_TIMING), interrupt latency
   (PSX_IRQ_HW_LATENCY), SPU DMA (PSX_SPU_DMA_TIMING) and the hardware frame
   period (PSX_VIDEO_TIMING), picks the CPU cost model those were fitted with
   (PSX_CPU_COST=ds, DuckStation's) and keeps every frame
   (PSX_PRESENT_CATCHUP=0), so it is the same whatever the build's profile.
   Enhanced turns the models off, keeps Beetle's CPU costs and sets frame
   skip only when the config has it. Fills at most cap pairs and returns the
   count. */
typedef struct {
    const char *name, *value;
} T2Env;
int t2_mode_env(const T2Config *cfg, int mode, T2Env *out, int cap);
/* Enhanced only (Original keeps every frame and 60 Hz): presenting fewer
   frames when the host falls behind, and asking Android's display for 60 Hz
   or leaving it at its own rate. */
int t2_setting_frameskip(const T2Config *cfg, int platform_default);   /* 1 on */
enum { T2_REFRESH_60, T2_REFRESH_NATIVE };
int t2_setting_refresh(const T2Config *cfg);          /* T2_REFRESH_* */

T2Settings *t2_settings_new(T2Input *in, T2Config *cfg, const T2SettingsHost *host);
void t2_settings_free(T2Settings *s);

int t2_settings_is_open(const T2Settings *s);
void t2_settings_open(T2Settings *s);
/* Closes it and saves the config. */
void t2_settings_close(T2Settings *s);

/* Keyboard events: F1 and Escape open, close and go back. Returns 1 if the
   event was the panel's. */
int t2_settings_key(T2Settings *s, int scancode, int down, int repeat);

/* Each tick while closed, with the pads as the players hold them
   (t2_input_raw) and the time in ms: counts Select held, and opens the
   panel after a second. Returns 1 if it opened. */
int t2_settings_watch(T2Settings *s, const T2Pad pads[2], uint64_t now_ms);

/* Each tick while open: moves, changes, closes. */
void t2_settings_update(T2Settings *s, const T2Pad pads[2], uint64_t now_ms);

/* Draws the panel into img (cleared to transparent first). Returns 0 when
   there is nothing to show. */
int t2_settings_draw(T2Settings *s, T2Font *font, T2Image *img, uint64_t now_ms);
/* A number that changes whenever what t2_settings_draw shows does. */
uint32_t t2_settings_version(const T2Settings *s);

/* For the tests: the selected row's label, and the page (0 main, 1-2 a
   player's buttons). */
const char *t2_settings_row_label(const T2Settings *s);
int t2_settings_page(const T2Settings *s);

#endif
