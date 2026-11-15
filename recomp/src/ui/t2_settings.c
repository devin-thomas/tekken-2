/* Tekken 2's settings panel (t2_settings.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "t2_settings.h"

#define HOLD_MS 1000          /* Select held a second opens it */
#define CAPTURE_MS 5000       /* a remap capture gives up after five seconds */
#define REPEAT_DELAY_MS 400
#define REPEAT_MS 110
#define HINT_MS 8000          /* the first launch's line */

enum {
    R_RESUME, R_P1, R_P2, R_SWAP, R_CTRL, R_TOUCH, R_TSIZE, R_TOPAC,
    R_RENDERER, R_RES, R_FULL, R_AUDIO, R_FPS, R_MODE, R_FRAMESKIP, R_REFRESH, R_RESTART, R_QUIT,
    R_COUNT
};

/* the remap page: the buttons in the order a player thinks of them, then two actions */
static const int REMAP_ORDER[] = { 14, 13, 15, 12, 10, 11, 8, 9, 4, 6, 7, 5, 3, 0 };
#define REMAP_BUTTONS ((int)(sizeof(REMAP_ORDER) / sizeof(REMAP_ORDER[0])))
#define REMAP_RESET REMAP_BUTTONS
#define REMAP_BACK (REMAP_BUTTONS + 1)
#define REMAP_ROWS (REMAP_BUTTONS + 2)

static const char *const LABEL[R_COUNT] = {
    "Resume", "Player 1 controls", "Player 2 controls", "Swap players' controllers", "Controller mode",
    "On-screen controls", "On-screen control size", "On-screen control opacity", "Renderer",
    "Internal resolution", "Fullscreen", "Audio latency", "FPS display", "Mode", "Frame skip when slow",
    "Display refresh", "Restart now", "Quit",
};

static const char *const HELP[R_COUNT] = {
    "Close this panel and go back to the game.",
    "Choose the key or button for each PlayStation button.",
    "Choose the key or button for each PlayStation button.",
    "Player 1's controllers become player 2's, and player 2's become player 1's.",
    "Standard uses the system's controller support. Direct USB reads the controller itself; "
    "Android then asks for USB permission. Applies after a restart.",
    "Auto hides the on-screen controls while a controller is connected.",
    "How big the on-screen controls are.",
    "How solid the on-screen controls look.",
    "OpenGL can draw above the PlayStation's resolution. Applies after a restart.",
    "How sharp the 3D picture is. 1x is the PlayStation's own resolution.",
    "F11, Alt+Enter and a double-click on the picture also switch it.",
    "Lower answers faster; raise it if the sound crackles. Applies after a restart.",
    "Shows the frames per second in the top corner.",
    "Original keeps the PlayStation's loading times and slowdowns. Enhanced loads faster "
    "and never drops frames. Applies after a restart.",
    "When the device falls behind, skip showing up to two frames so the game and its sound "
    "keep full speed. Enhanced mode only. Applies after a restart.",
    "60 Hz suits the game's 60 frames a second and keeps the phone cooler; Native leaves the "
    "screen at its own rate. Enhanced mode only. Applies after a restart.",
    "Restart the game to apply the changes marked \"after restart\".",
    "Close Tekken 2 Recompiled. X twice: the first press only asks.",
};

struct T2Settings {
    T2Input *in;
    T2Config *cfg;
    T2SettingsHost host;
    int open, page, row;          /* page 0: main; 1, 2: that player's buttons */
    int rows[R_COUNT], nrows;
    uint64_t hold_since;          /* Select held since (0: not held) */
    int need_release;             /* Select must come up before a hold counts again */
    uint16_t prev;                /* both pads' buttons last tick, for presses */
    int fresh;                    /* just opened: what is held now is not a press */
    int rep_dir;                  /* the held direction repeating, or 0 */
    uint64_t rep_at;
    int capturing;                /* remap button index, or -1 */
    uint64_t capture_since;
    char note[96];                /* the last remap's result */
    int quit_armed;               /* Quit pressed once: the next X on it quits */
    int boot_ctrl, boot_audio, boot_mode, boot_renderer, renderer, boot_frameskip, boot_refresh;
    uint64_t hint_until;
    uint32_t version;
};

/* ------------------------------------------------------------------ values in the config */

static int pick(const T2Config *cfg, const char *key, const char *const *names, int n, int fallback)
{
    const char *v = t2_config_get(cfg, key);
    int i;

    for (i = 0; v && i < n; i++) {
        if (!strcmp(v, names[i])) {
            return i;
        }
    }
    return fallback;
}

static const char *const TOUCH_NAMES[] = { "auto", "always", "never" };
static const char *const CTRL_NAMES[] = { "standard", "direct-usb" };
static const char *const AUDIO_NAMES[] = { "low", "normal", "high" };
static const char *const MODE_NAMES[] = { "enhanced", "original" };

int t2_setting_touch(const T2Config *cfg) { return pick(cfg, "touch.controls", TOUCH_NAMES, 3, T2_TOUCH_AUTO); }
int t2_setting_controller_mode(const T2Config *cfg) { return pick(cfg, "controller.mode", CTRL_NAMES, 2, T2_CTRL_STANDARD); }
int t2_setting_audio(const T2Config *cfg) { return pick(cfg, "audio.latency", AUDIO_NAMES, 3, T2_AUDIO_NORMAL); }
int t2_setting_mode(const T2Config *cfg) { return pick(cfg, "mode", MODE_NAMES, 2, T2_MODE_ENHANCED); }

int t2_mode_env(const T2Config *cfg, int mode, T2Env *out, int cap)
{
    static const char *const MODELS[] = {
        "PSX_GPU_TIMING", "PSX_CD_TIMING", "PSX_ROM_TIMING", "PSX_SIO_TIMING",
        "PSX_IRQ_HW_LATENCY", "PSX_SPU_DMA_TIMING", "PSX_VIDEO_TIMING",
    };
    const int original = mode == T2_MODE_ORIGINAL;
    int n = 0;
    size_t i;

#define T2_ENV(k, v) do { if (n < cap) { out[n].name = (k); out[n].value = (v); } n++; } while (0)
    for (i = 0; i < sizeof(MODELS) / sizeof(MODELS[0]); i++) {
        T2_ENV(MODELS[i], original ? "1" : "0");
    }
    if (original) {
        T2_ENV("PSX_CPU_COST", "ds");
        T2_ENV("PSX_PRESENT_CATCHUP", "0");
    } else {
        T2_ENV("PSX_CPU_COST", "beetle");
        if (t2_config_get(cfg, "frameskip")) {
            T2_ENV("PSX_PRESENT_CATCHUP", t2_setting_frameskip(cfg, 0) ? "1" : "0");
        }
    }
#undef T2_ENV
    return n < cap ? n : cap;
}
int t2_setting_fps(const T2Config *cfg) { return !strcmp(t2_config_get_or(cfg, "fps", "off"), "on"); }

static const char *const REFRESH_NAMES[] = { "60", "native" };

int t2_setting_frameskip(const T2Config *cfg, int platform_default)
{
    const char *v = t2_config_get(cfg, "frameskip");

    return v ? !strcmp(v, "on") : platform_default != 0;
}

int t2_setting_refresh(const T2Config *cfg) { return pick(cfg, "display.refresh", REFRESH_NAMES, 2, T2_REFRESH_60); }

static int clamp_step(int v, int lo, int hi, int step)
{
    v = (v + step / 2) / step * step;
    return v < lo ? lo : v > hi ? hi : v;
}

int t2_setting_touch_size(const T2Config *cfg) { return clamp_step(t2_config_get_int(cfg, "touch.size", 100), 50, 150, 10); }
int t2_setting_touch_opacity(const T2Config *cfg) { return clamp_step(t2_config_get_int(cfg, "touch.opacity", 70), 20, 100, 10); }

int t2_setting_audio_ms(int audio)
{
    return audio == T2_AUDIO_LOW ? 80 : audio == T2_AUDIO_HIGH ? 250 : 0;
}

/* ------------------------------------------------------------------ rows */

static int restart_pending(const T2Settings *s)
{
    return t2_setting_controller_mode(s->cfg) != s->boot_ctrl || t2_setting_audio(s->cfg) != s->boot_audio ||
           t2_setting_mode(s->cfg) != s->boot_mode || s->renderer != s->boot_renderer ||
           t2_setting_frameskip(s->cfg, s->host.frameskip_default) != s->boot_frameskip ||
           (s->host.android && t2_setting_refresh(s->cfg) != s->boot_refresh);
}

static void build_rows(T2Settings *s)
{
    int n = 0, r, keep = s->nrows ? s->rows[s->row] : R_RESUME;

    for (r = 0; r < R_COUNT; r++) {
        if (s->host.minimal && (r == R_CTRL || r == R_RENDERER || r == R_FRAMESKIP || r == R_REFRESH)) {
            continue;   /* a player release keeps only what a player sets on first run */
        }
        if (s->host.mobile && (r == R_RENDERER || r == R_RES || r == R_FULL)) {
            continue;
        }
        if (!s->host.mobile && (r == R_TOUCH || r == R_TSIZE || r == R_TOPAC)) {
            continue;
        }
        if (r == R_RENDERER && !s->host.set_renderer) {
            continue;
        }
        if (r == R_RES && !s->host.set_resolution) {
            continue;
        }
        if (r == R_FULL && !s->host.set_fullscreen) {
            continue;
        }
        if ((r == R_FRAMESKIP || r == R_REFRESH) && t2_setting_mode(s->cfg) != T2_MODE_ENHANCED) {
            continue;   /* Enhanced only */
        }
        if (r == R_REFRESH && !s->host.android) {
            continue;
        }
        if (r == R_RESTART && (!s->host.restart || !restart_pending(s))) {
            continue;
        }
        if (r == R_QUIT && !s->host.quit) {
            continue;
        }
        s->rows[n++] = r;
    }
    s->nrows = n;
    s->row = 0;
    for (r = 0; r < n; r++) {
        if (s->rows[r] == keep) {
            s->row = r;
        }
    }
}

T2Settings *t2_settings_new(T2Input *in, T2Config *cfg, const T2SettingsHost *host)
{
    T2Settings *s = calloc(1, sizeof(*s));

    if (!s) {
        return NULL;
    }
    s->in = in;
    s->cfg = cfg;
    if (host) {
        s->host = *host;
    }
    s->capturing = -1;
    s->boot_ctrl = t2_setting_controller_mode(cfg);
    s->boot_audio = t2_setting_audio(cfg);
    s->boot_mode = t2_setting_mode(cfg);
    s->renderer = s->boot_renderer = s->host.renderer ? s->host.renderer(s->host.ctx) : 1;
    s->boot_frameskip = t2_setting_frameskip(cfg, s->host.frameskip_default);
    s->boot_refresh = t2_setting_refresh(cfg);
    build_rows(s);
    if (!t2_config_get(cfg, "hint")) {
        /* the first launch: say once how to open the panel */
        s->hint_until = SDL_GetTicks() + HINT_MS;
        t2_config_set(cfg, "hint", "shown");
        t2_config_save(cfg);
    }
    return s;
}

void t2_settings_free(T2Settings *s)
{
    free(s);
}

int t2_settings_is_open(const T2Settings *s)
{
    return s->open;
}

int t2_settings_page(const T2Settings *s)
{
    return s->open ? s->page : -1;
}

const char *t2_settings_row_label(const T2Settings *s)
{
    if (!s->open) {
        return "";
    }
    if (s->page) {
        return s->row < REMAP_BUTTONS ? t2_pad_label(REMAP_ORDER[s->row]) : s->row == REMAP_RESET ? "Reset to defaults"
                                                                                                  : "Back";
    }
    return LABEL[s->rows[s->row]];
}

void t2_settings_open(T2Settings *s)
{
    if (s->open) {
        return;
    }
    s->open = 1;
    s->page = 0;
    s->nrows = 0;
    build_rows(s);
    s->row = 0;
    s->hint_until = 0;
    s->note[0] = 0;
    s->quit_armed = 0;
    /* what is held now doesn't count as a press in the panel */
    s->fresh = 1;
    s->rep_dir = 0;
    s->version++;
    fprintf(stderr, "t2: settings open\n");
}

void t2_settings_close(T2Settings *s)
{
    if (!s->open) {
        return;
    }
    if (s->capturing >= 0) {
        t2_input_remap_cancel(s->in);
        s->capturing = -1;
    }
    s->open = 0;
    s->hold_since = 0;
    s->need_release = 1;
    s->version++;
    if (t2_config_save(s->cfg) != 0) {
        fprintf(stderr, "t2: cannot save %s\n", t2_config_path(s->cfg));
    }
    fprintf(stderr, "t2: settings closed\n");
}

static void back(T2Settings *s)
{
    if (s->page) {
        int target = s->page == 1 ? R_P1 : R_P2, r;

        s->page = 0;
        s->nrows = 0;
        build_rows(s);
        for (r = 0; r < s->nrows; r++) {
            if (s->rows[r] == target) {
                s->row = r;
            }
        }
        s->note[0] = 0;
        s->version++;
    } else {
        t2_settings_close(s);
    }
}

int t2_settings_key(T2Settings *s, int scancode, int down, int repeat)
{
    if (!down || repeat) {
        return s->open;
    }
    if (!s->open) {
        if (scancode == SDL_SCANCODE_F1 || scancode == SDL_SCANCODE_ESCAPE) {
            t2_settings_open(s);
            return 1;
        }
        return 0;
    }
    if (s->capturing >= 0) {
        return 1;
    }
    if (scancode == SDL_SCANCODE_F1) {
        t2_settings_close(s);
    } else if (scancode == SDL_SCANCODE_ESCAPE) {
        back(s);
    }
    return 1;
}

static uint16_t both(const T2Pad pads[2])
{
    return (uint16_t)(pads[0].buttons | pads[1].buttons);
}

int t2_settings_watch(T2Settings *s, const T2Pad pads[2], uint64_t now_ms)
{
    int held = (both(pads) & T2_PAD_SELECT) != 0;

    if (s->hint_until && now_ms > s->hint_until) {
        s->hint_until = 0;
        s->version++;
    }
    if (!held) {
        s->hold_since = 0;
        s->need_release = 0;
        return 0;
    }
    if (s->need_release) {
        return 0;
    }
    if (!s->hold_since) {
        s->hold_since = now_ms ? now_ms : 1;
    } else if (now_ms - s->hold_since >= HOLD_MS) {
        t2_settings_open(s);
        return 1;
    }
    return 0;
}

static int cycle(int v, int n, int dir)
{
    return ((v + dir) % n + n) % n;
}

static void set_choice(T2Settings *s, const char *key, const char *const *names, int n, int v, int dir)
{
    t2_config_set(s->cfg, key, names[cycle(v, n, dir)]);
}

static void change(T2Settings *s, int dir, int activate)
{
    int r = s->rows[s->row], v;

    switch (r) {
    case R_RESUME:
        if (activate) {
            t2_settings_close(s);
        }
        return;
    case R_P1:
    case R_P2:
        if (activate) {
            s->page = r == R_P1 ? 1 : 2;
            s->row = 0;
            s->note[0] = 0;
        }
        break;
    case R_SWAP:
        if (activate) {
            t2_input_swap_players(s->in);
            snprintf(s->note, sizeof(s->note), "Controllers swapped.");
        }
        break;
    case R_CTRL:
        set_choice(s, "controller.mode", CTRL_NAMES, 2, t2_setting_controller_mode(s->cfg), dir);
        break;
    case R_TOUCH:
        set_choice(s, "touch.controls", TOUCH_NAMES, 3, t2_setting_touch(s->cfg), dir);
        break;
    case R_TSIZE:
        v = t2_setting_touch_size(s->cfg) + dir * 10;
        t2_config_set_int(s->cfg, "touch.size", v < 50 ? 50 : v > 150 ? 150 : v);
        break;
    case R_TOPAC:
        v = t2_setting_touch_opacity(s->cfg) + dir * 10;
        t2_config_set_int(s->cfg, "touch.opacity", v < 20 ? 20 : v > 100 ? 100 : v);
        break;
    case R_RENDERER:
        s->renderer = s->renderer ? 0 : 1;
        s->host.set_renderer(s->host.ctx, s->renderer);
        break;
    case R_RES:
        v = s->host.resolution ? s->host.resolution(s->host.ctx) : 0;
        if (v > 0) {
            v += dir;
            v = v < 1 ? 8 : v > 8 ? 1 : v;
            s->host.set_resolution(s->host.ctx, v);
        }
        break;
    case R_FULL:
        s->host.set_fullscreen(s->host.ctx, !(s->host.fullscreen && s->host.fullscreen(s->host.ctx)));
        break;
    case R_AUDIO:
        v = t2_setting_audio(s->cfg) + dir;
        t2_config_set(s->cfg, "audio.latency", AUDIO_NAMES[v < 0 ? 0 : v > 2 ? 2 : v]);
        break;
    case R_FPS:
        t2_config_set(s->cfg, "fps", t2_setting_fps(s->cfg) ? "off" : "on");
        break;
    case R_MODE:
        set_choice(s, "mode", MODE_NAMES, 2, t2_setting_mode(s->cfg), dir);
        break;
    case R_FRAMESKIP:
        t2_config_set(s->cfg, "frameskip", t2_setting_frameskip(s->cfg, s->host.frameskip_default) ? "off" : "on");
        break;
    case R_REFRESH:
        set_choice(s, "display.refresh", REFRESH_NAMES, 2, t2_setting_refresh(s->cfg), dir);
        break;
    case R_RESTART:
        if (activate) {
            t2_config_save(s->cfg);
            s->host.restart(s->host.ctx);
        }
        return;
    case R_QUIT:
        /* One press only asks: on 2026-11-14 the Galaxy run ended at the
         * panel with one controller press (Up from Resume wraps to Quit). */
        if (activate && !s->quit_armed) {
            s->quit_armed = 1;
            snprintf(s->note, sizeof(s->note), "Press X again to quit. Any other button keeps playing.");
            return;
        }
        if (activate) {
            s->quit_armed = 0;
            t2_config_save(s->cfg);
            s->host.quit(s->host.ctx);
        }
        return;
    }
    if (!s->page) {
        build_rows(s);   /* Restart comes and goes */
    }
}

static void remap_activate(T2Settings *s)
{
    int player = s->page - 1;

    if (s->row < REMAP_BUTTONS) {
        s->capturing = REMAP_ORDER[s->row];
        s->capture_since = 0;
        t2_input_remap_begin(s->in, player, s->capturing);
        s->note[0] = 0;
    } else if (s->row == REMAP_RESET) {
        t2_input_reset_player(s->in, player);
        snprintf(s->note, sizeof(s->note), "Player %d's buttons are back to the defaults.", player + 1);
    } else {
        back(s);
    }
}

void t2_settings_update(T2Settings *s, const T2Pad pads[2], uint64_t now_ms)
{
    uint16_t now = both(pads), press;
    int nrows, dir = 0;

    if (!s->open) {
        return;
    }
    if (s->capturing >= 0) {
        int result = t2_input_remap_take_result(s->in);

        if (!s->capture_since) {
            s->capture_since = now_ms ? now_ms : 1;
        }
        if (!result && now_ms - s->capture_since >= CAPTURE_MS) {
            t2_input_remap_cancel(s->in);
            result = t2_input_remap_take_result(s->in);
        }
        if (result || t2_input_remap_button(s->in) < 0) {
            snprintf(s->note, sizeof(s->note), result == 1 ? "%s changed." : "%s kept as it was.",
                     t2_pad_label(s->capturing));
            s->capturing = -1;
            s->version++;
        }
        s->prev = now;   /* the press that was taken is not a move */
        return;
    }
    if (s->fresh) {
        s->fresh = 0;
        s->prev = now;
        return;
    }
    press = (uint16_t)(now & ~s->prev);
    s->prev = now;
    nrows = s->page ? REMAP_ROWS : s->nrows;

    /* Up and Down repeat while held */
    if (press & T2_PAD_UP) {
        dir = -1;
    } else if (press & T2_PAD_DOWN) {
        dir = 1;
    }
    if (dir) {
        s->rep_dir = dir;
        s->rep_at = now_ms + REPEAT_DELAY_MS;
    } else if (s->rep_dir && (now & (s->rep_dir < 0 ? T2_PAD_UP : T2_PAD_DOWN))) {
        if (now_ms >= s->rep_at) {
            dir = s->rep_dir;
            s->rep_at = now_ms + REPEAT_MS;
        }
    } else {
        s->rep_dir = 0;
    }
    if (dir) {
        s->row = cycle(s->row, nrows, dir);
        s->version++;
    }
    if (s->quit_armed && press && !(press & T2_PAD_X)) {
        s->quit_armed = 0;   /* anything but X on Quit takes the question back */
        s->note[0] = 0;
        s->version++;
    }
    if (press & T2_PAD_START) {
        t2_settings_close(s);
        return;
    }
    if (press & T2_PAD_CIRCLE) {
        back(s);
        return;
    }
    if (s->page) {
        if (press & T2_PAD_X) {
            remap_activate(s);
            s->version++;
        }
        return;
    }
    if (press & T2_PAD_X) {
        change(s, 1, 1);
        s->version++;
    } else if (press & (T2_PAD_LEFT | T2_PAD_RIGHT)) {
        int r = s->rows[s->row];

        if (r != R_RESUME && r != R_P1 && r != R_P2 && r != R_SWAP && r != R_RESTART && r != R_QUIT) {
            change(s, press & T2_PAD_LEFT ? -1 : 1, 0);
            s->version++;
        }
    }
}

uint32_t t2_settings_version(const T2Settings *s)
{
    return s->version * 4u + (s->open ? 1u : 0u) + (s->hint_until ? 2u : 0u);
}

/* ------------------------------------------------------------------ drawing */

static const char *pending(const T2Settings *s, int r)
{
    int p = (r == R_CTRL && t2_setting_controller_mode(s->cfg) != s->boot_ctrl) ||
            (r == R_AUDIO && t2_setting_audio(s->cfg) != s->boot_audio) ||
            (r == R_MODE && t2_setting_mode(s->cfg) != s->boot_mode) ||
            (r == R_RENDERER && s->renderer != s->boot_renderer) ||
            (r == R_FRAMESKIP && t2_setting_frameskip(s->cfg, s->host.frameskip_default) != s->boot_frameskip) ||
            (r == R_REFRESH && t2_setting_refresh(s->cfg) != s->boot_refresh);

    return p ? " (after restart)" : "";
}

static void value_text(T2Settings *s, int r, char *out, size_t cap)
{
    static const char *const CTRL[] = { "Standard", "Direct USB" };
    static const char *const TOUCH[] = { "Auto", "Always", "Never" };
    static const char *const AUDIO[] = { "Low (80 ms)", "Normal", "High (250 ms)" };
    static const char *const MODE[] = { "Enhanced", "Original" };
    char dev[128];
    int v;

    out[0] = 0;
    switch (r) {
    case R_P1:
    case R_P2:
        t2_input_player_devices(s->in, r == R_P1 ? 0 : 1, dev, sizeof(dev));
        snprintf(out, cap, "%s", dev);
        break;
    case R_CTRL: snprintf(out, cap, "%s%s", CTRL[t2_setting_controller_mode(s->cfg)], pending(s, r)); break;
    case R_TOUCH: snprintf(out, cap, "%s", TOUCH[t2_setting_touch(s->cfg)]); break;
    case R_TSIZE: snprintf(out, cap, "%d%%", t2_setting_touch_size(s->cfg)); break;
    case R_TOPAC: snprintf(out, cap, "%d%%", t2_setting_touch_opacity(s->cfg)); break;
    case R_RENDERER: snprintf(out, cap, "%s%s", s->renderer ? "OpenGL" : "Software", pending(s, r)); break;
    case R_RES:
        v = s->host.resolution ? s->host.resolution(s->host.ctx) : 0;
        if (v > 0) {
            snprintf(out, cap, "%dx", v);
        } else {
            snprintf(out, cap, "1x (OpenGL only)");
        }
        break;
    case R_FULL: snprintf(out, cap, "%s", s->host.fullscreen && s->host.fullscreen(s->host.ctx) ? "On" : "Off"); break;
    case R_AUDIO: snprintf(out, cap, "%s%s", AUDIO[t2_setting_audio(s->cfg)], pending(s, r)); break;
    case R_FPS: snprintf(out, cap, "%s", t2_setting_fps(s->cfg) ? "On" : "Off"); break;
    case R_MODE: snprintf(out, cap, "%s%s", MODE[t2_setting_mode(s->cfg)], pending(s, r)); break;
    case R_FRAMESKIP:
        snprintf(out, cap, "%s%s", t2_setting_frameskip(s->cfg, s->host.frameskip_default) ? "On" : "Off", pending(s, r));
        break;
    case R_REFRESH:
        snprintf(out, cap, "%s%s", t2_setting_refresh(s->cfg) == T2_REFRESH_60 ? "60 Hz" : "Native", pending(s, r));
        break;
    default: break;
    }
}

/* Draws text cut to fit width w, with "..." when cut. */
static void text_fit(T2Font *f, T2Image *img, float x, float y, float px, const char *t, float w, uint32_t c, int right)
{
    char buf[256];
    size_t n = strlen(t);

    snprintf(buf, sizeof(buf), "%s", t);
    if (n >= sizeof(buf)) {
        n = sizeof(buf) - 1;
    }
    while (n > 3 && t2_text_width(f, px, buf) > w) {
        n--;
        while (n > 0 && ((unsigned char)buf[n - 1] & 0xC0) == 0x80) {
            n--;   /* not inside a UTF-8 sequence */
        }
        memcpy(buf + n, "...", 4);
    }
    t2_text_draw(f, img, right ? x - t2_text_width(f, px, buf) : x, y, px, buf, c);
}

/* Wraps text into at most two lines inside width w. */
static void text_wrap(T2Font *f, T2Image *img, float x, float y, float px, float line, const char *t, float w, uint32_t c)
{
    char a[256];
    const char *rest = t;
    int lines = 0;

    while (*rest && lines < 3) {
        size_t n = 0, cut = 0;

        while (rest[n]) {
            size_t m = n;

            while (rest[m] && rest[m] != ' ') {
                m++;
            }
            if (m >= sizeof(a)) {
                break;
            }
            memcpy(a, rest, m);
            a[m] = 0;
            if (t2_text_width(f, px, a) > w && cut) {
                break;
            }
            cut = m;
            n = rest[m] ? m + 1 : m;
        }
        if (!cut) {
            cut = strlen(rest) < sizeof(a) ? strlen(rest) : sizeof(a) - 1;
            n = cut;
        }
        memcpy(a, rest, cut);
        a[cut] = 0;
        t2_text_draw(f, img, x, y + line * lines, px, a, c);
        rest += n;
        lines++;
    }
}

#define C_TEXT 0xFFF2F2F2u
#define C_DIM 0xFFA8ADB5u
#define C_SEL 0xFFFFFFFFu
#define C_ACCENT 0xFF6FA8FFu

int t2_settings_draw(T2Settings *s, T2Font *f, T2Image *img, uint64_t now_ms)
{
    float H = (float)img->h, W = (float)img->w;
    int nrows, i, bx, by, bw, bh, pad;
    float rowh, px, small, y;

    (void)now_ms;
    memset(img->px, 0, (size_t)img->w * img->h * 4);
    if (!f) {
        return 0;
    }
    if (!s->open) {
        if (!s->hint_until) {
            return 0;
        }
        {
            const char *t = s->host.mobile ? "Hold Select or tap the gear for settings"
                                           : "Hold Select, or press F1 or Esc, for settings";
            float tpx = H * 0.034f, tw = t2_text_width(f, tpx, t);
            int hw = (int)(tw + tpx * 2), hh = (int)(tpx * 2.0f);

            t2_fill_round(img, (int)((W - hw) / 2), (int)(H * 0.88f), hw, hh, hh / 2, 0xB0000000u);
            t2_text_draw(f, img, (W - tw) / 2, H * 0.88f + hh * 0.68f, tpx, t, C_TEXT);
        }
        return 1;
    }
    nrows = s->page ? REMAP_ROWS : s->nrows;
    /* the panel: a dark box, most of the height, no wider than 4:3 needs */
    bh = (int)(H * 0.94f);
    bw = (int)(W * 0.94f);
    if (bw > (int)(bh * 1.5f)) {
        bw = (int)(bh * 1.5f);
    }
    bx = (int)((W - bw) / 2);
    by = (int)((H - bh) / 2);
    pad = (int)(bh * 0.035f);
    t2_fill(img, 0, 0, img->w, img->h, 0x66000000u);
    t2_fill_round(img, bx, by, bw, bh, pad, 0xE8121418u);
    /* title, rows, then three lines of help */
    rowh = (bh - pad * 2) / (nrows + 1.6f + 3.4f);
    if (rowh > bh * 0.07f) {
        rowh = bh * 0.07f;
    }
    px = rowh * 0.56f;
    small = rowh * 0.46f;
    y = by + pad + rowh * 0.9f;
    {
        char title[64];

        if (s->page) {
            snprintf(title, sizeof(title), "Player %d controls", s->page);
        } else if (s->host.version && s->host.version[0]) {
            snprintf(title, sizeof(title), "Settings (version %s)", s->host.version);
        } else {
            snprintf(title, sizeof(title), "Settings");
        }
        t2_text_draw(f, img, bx + pad, y, rowh * 0.78f, title, C_SEL);
    }
    y += rowh * 0.7f;
    for (i = 0; i < nrows; i++) {
        float ry = y + rowh * i, base = ry + rowh * 0.68f;
        int sel = i == s->row;
        char value[160], label[64];
        const char *lab;

        if (sel) {
            t2_fill_round(img, bx + pad / 2, (int)ry + 1, bw - pad, (int)rowh - 2, (int)(rowh * 0.25f), 0xFF2C3440u);
        }
        if (s->page) {
            if (i < REMAP_BUTTONS) {
                lab = t2_pad_label(REMAP_ORDER[i]);
                if (s->capturing == REMAP_ORDER[i]) {
                    snprintf(value, sizeof(value), "Press a key or button (Esc cancels)");
                } else {
                    t2_input_binding_text(s->in, s->page - 1, REMAP_ORDER[i], value, sizeof(value));
                }
            } else {
                lab = i == REMAP_RESET ? "Reset to defaults" : "Back";
                value[0] = 0;
            }
        } else {
            lab = LABEL[s->rows[i]];
            value_text(s, s->rows[i], value, sizeof(value));
        }
        snprintf(label, sizeof(label), "%s", lab);
        t2_text_draw(f, img, bx + pad * 1.5f, base, px, label, sel ? C_SEL : C_TEXT);
        if (value[0]) {
            int changeable = !s->page && s->rows[i] != R_P1 && s->rows[i] != R_P2;
            float right = bx + bw - pad * 1.5f;

            if (sel && changeable) {
                t2_text_draw(f, img, right - t2_text_width(f, px, ">"), base, px, ">", C_ACCENT);
                right -= t2_text_width(f, px, "> ");
            }
            text_fit(f, img, right, base, px, value, bw * 0.5f, sel ? (s->capturing >= 0 ? C_ACCENT : C_SEL) : C_DIM,
                     1);
            if (sel && changeable) {
                float vw = t2_text_width(f, px, value);

                if (vw > bw * 0.5f) {
                    vw = bw * 0.5f;
                }
                t2_text_draw(f, img, right - vw - t2_text_width(f, px, "< "), base, px, "<", C_ACCENT);
            }
        }
    }
    /* help: what the selected row does (or the last remap's result), then the controls */
    y += rowh * nrows + rowh * 0.9f;
    {
        const char *help;
        char buf[160];

        if (s->note[0]) {
            help = s->note;
        } else if (s->page) {
            if (s->capturing >= 0) {
                snprintf(buf, sizeof(buf), "Press the key or button for %s. Esc cancels; so does waiting five seconds.",
                         t2_pad_label(s->capturing));
                help = buf;
            } else {
                help = s->row < REMAP_BUTTONS ? "X changes the selected button. The new key or button replaces the "
                                                "old one of the same kind."
                       : s->row == REMAP_RESET ? "Every button back to the one it had at first."
                                               : "Back to the settings.";
            }
        } else {
            help = HELP[s->rows[s->row]];
        }
        text_wrap(f, img, bx + pad * 1.5f, y, small, small * 1.35f, help, bw - pad * 3.0f, C_DIM);
    }
    {
        const char *keys = s->page ? "Up and Down choose, X selects, Circle or Esc goes back, Start closes"
                                   : "Up and Down choose, Left and Right change, X selects, Circle or Start closes";

        text_fit(f, img, bx + pad * 1.5f, by + bh - pad * 0.9f, small, keys, bw - pad * 3.0f, C_DIM, 0);
    }
    return 1;
}
