/* t2_host.c: Tekken 2's controls and settings panel, joined to psxrecomp.
 *
 * Registered before main() (PSX_MOD_CONSTRUCTOR):
 *   - the title hooks (patches 0012 and 0013, psx_title_hooks.h): the input layer
 *     (src/input/t2_input.h) supplies both pads and sees every SDL event;
 *     a tick runs the settings panel (src/ui/t2_settings.h) and the FPS
 *     readout; two layers draw them over the picture in every renderer;
 *   - an in-game menu provider (psx_menu_host.h): the runtime's own policy
 *     for pausing the game while the panel is open, keeping the game's
 *     input out, the live video settings it saves in settings.toml, and
 *     quitting or restarting.
 *
 * Before SDL starts (the early hook) it applies the choices that only take
 * effect at start: the controller mode (SDL's HIDAPI hint), Original or
 * Enhanced mode (every runtime timing switch, t2_mode_env) and the audio
 * latency (PSX_AUDIO_BUFFER_MS). A
 * variable already set in the environment wins, so diff and replay runs
 * keep theirs.
 *
 * The on-screen controller (Android: android/.../TouchPadView.java and
 * patch 0005's android_touch_pad.c; iOS: ios/T2TouchPad.m) is player 1's
 * device in the input layer; it asks here whether to show itself
 * (t2_touch_overlay_state) and opens the panel from its gear button.
 */
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "mod_plugins.h"
#include "psx_menu_host.h"
#include "psx_title_hooks.h"
#include "savestate.h"

#include "t2_config.h"
#include "t2_input.h"
#include "t2_settings.h"
#include "t2_text.h"
#include "t2_touch.h"

#define PKG_RESOLUTION "psx.enhancement.internal-resolution"
#define PANEL_MAX_H 1080    /* the panel's image, at most this tall (scaled up past it) */

extern uint64_t s_frame_count;   /* the runtime's guest frame counter */

#if defined(__ANDROID__)
/* patch 0005: the on-screen controller's held buttons, active low */
extern int psx_android_touch_pad(uint16_t *buttons, uint8_t st[4], uint8_t *lt, uint8_t *rt);
#endif

/* ------------------------------------------------------------------ state */

typedef struct {
    uint32_t *px;
    int w, h, x, y, dw, dh;
    uint32_t version;
} Layer;

static struct {
    char home[1024];
    T2Config *cfg;
    T2Input *in;
    T2Settings *panel;
    T2Font *font;
    const PsxMenuHost *host;
    int gl;                     /* the provider attached with an OpenGL context */
    int was_open;
    /* the FPS readout */
    uint64_t fps_at, fps_frames;
    int fps;
    /* layers: the main thread draws into `back`, publishes into `front` under
       the lock; the presenting thread copies a changed front into `shown` */
    SDL_Mutex *lock;
    Layer front[PSX_TITLE_LAYERS], shown[PSX_TITLE_LAYERS];
    uint32_t drawn_panel, drawn_fps_value;
    int drawn_fps_on;
    _Atomic int canvas_w, canvas_h, canvas_ppu1000, canvas_opaque;
    int drawn_cw, drawn_ch, drawn_ppu, drawn_opaque;
} T;

static _Atomic uint32_t s_touch_held;       /* iOS: the on-screen controller (T2_PAD_* bits) */
static _Atomic int s_open_request;          /* the gear button */
#if defined(__ANDROID__)
static _Atomic int s_suspend_request;       /* T2Activity.onPause (patch 0017) */
#endif
static _Atomic int s_overlay_state;         /* t2_touch_overlay_state */

/* ------------------------------------------------------------------ the environment */

static void set_env_default(const char *name, const char *value)
{
    if (getenv(name)) {
        fprintf(stderr, "t2: %s=%s from the environment\n", name, getenv(name));
        return;
    }
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 0);
#endif
    fprintf(stderr, "t2: %s=%s\n", name, value);
}

static void early(const char *home_dir)
{
    char path[1100];
    int mode, audio, n, i;
    T2Env env[16];

    snprintf(T.home, sizeof(T.home), "%s", home_dir ? home_dir : ".");
    snprintf(path, sizeof(path), "%s/t2-input.cfg", T.home);
    T.cfg = t2_config_load(path);
    if (!T.cfg) {
        return;
    }
    if (t2_setting_controller_mode(T.cfg) == T2_CTRL_DIRECT_USB) {
        /* patch 0010's Android switch, and the hint itself over any default */
        set_env_default("PSX_ANDROID_HIDAPI", "1");
        SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI, "1", SDL_HINT_OVERRIDE);
        fprintf(stderr, "t2: controller mode: direct USB (SDL HIDAPI on)\n");
    } else {
        /* the platform default: on Android, patch 0010 turns HIDAPI off, so a
           Sony pad uses Android's own input devices with no USB prompt */
        fprintf(stderr, "t2: controller mode: standard\n");
    }
    /* Every timing switch follows the mode (t2_mode_env), frame skip
       included (patch 0010's present catch-up: Enhanced only). Enhanced is
       the default; Original is chosen in the panel or the settings file. */
    mode = t2_setting_mode(T.cfg);
    n = t2_mode_env(T.cfg, mode, env, (int)(sizeof(env) / sizeof(env[0])));
    for (i = 0; i < n; i++) {
        set_env_default(env[i].name, env[i].value);
    }
    fprintf(stderr, "t2: mode: %s\n", mode == T2_MODE_ORIGINAL ? "Original" : "Enhanced");
    audio = t2_setting_audio_ms(t2_setting_audio(T.cfg));
    if (audio) {
        char ms[16];

        snprintf(ms, sizeof(ms), "%d", audio);
        set_env_default("PSX_AUDIO_BUFFER_MS", ms);
    }
}

/* ------------------------------------------------------------------ what the panel asks the runtime */

static int host_renderer(void *ctx)
{
    PsxHostVideoSettings v;

    (void)ctx;
    if (!T.host || !T.host->video_get) {
        return T.gl;
    }
    T.host->video_get(&v);
    return v.renderer == 1 ? 1 : 0;
}

static void host_set_renderer(void *ctx, int r)
{
    PsxHostVideoSettings v;

    (void)ctx;
    if (!T.host || !T.host->video_get || !T.host->video_apply) {
        return;
    }
    T.host->video_get(&v);
    v.renderer = r ? 1 : 0;
    T.host->video_apply(&v);
}

static int host_resolution(void *ctx)
{
    char v[16] = "";
    int n;

    (void)ctx;
    if (!T.gl) {
        return 0;
    }
    if (!psx_mod_option_value(PKG_RESOLUTION, "internal-resolution", "resolution", v, sizeof(v)) ||
        sscanf(v, "%dx", &n) != 1 || n < 1 || n > 8) {
        return 4;
    }
    return n;
}

static void host_set_resolution(void *ctx, int n)
{
    char v[16];

    (void)ctx;
    if (!T.host || !T.host->mod_set) {
        return;
    }
    snprintf(v, sizeof(v), "%dx", n);
    T.host->mod_set(PKG_RESOLUTION, "internal-resolution", "resolution", v);
}

static int host_fullscreen(void *ctx)
{
    PsxHostVideoSettings v;

    (void)ctx;
    if (!T.host || !T.host->video_get) {
        return 0;
    }
    T.host->video_get(&v);
    return v.fullscreen != 0;
}

static void host_set_fullscreen(void *ctx, int on)
{
    PsxHostVideoSettings v;

    (void)ctx;
    if (!T.host || !T.host->video_get || !T.host->video_apply) {
        return;
    }
    T.host->video_get(&v);
    v.fullscreen = on ? 1 : 0;
    T.host->video_apply(&v);
}

static void host_restart(void *ctx)
{
    (void)ctx;
    if (T.host && T.host->request_exit) {
        T.host->request_exit(PSX_MENU_EXIT_RELAUNCH);
    }
}

static void host_quit(void *ctx)
{
    (void)ctx;
#if defined(__ANDROID__)
    {   /* Quit means the next start is a fresh one: no suspend state (patch 0017) */
        char path[600];

        if (savestate_slot_path(SAVESTATE_SUSPEND_SLOT, path, sizeof(path))) {
            remove(path);
        }
    }
#endif
    if (T.host && T.host->request_exit) {
        T.host->request_exit(PSX_MENU_EXIT_QUIT);
    }
}

/* ------------------------------------------------------------------ setup */

static int is_mobile(void)
{
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
    return 1;
#else
    return 0;
#endif
}

/* Once SDL's gamepad subsystem is up: the input layer, the panel, the font. */
static int ready(void)
{
    T2SettingsHost h;

    if (T.in) {
        return 1;
    }
    if (!SDL_WasInit(SDL_INIT_GAMEPAD)) {
        return 0;
    }
    if (!T.cfg) {
        early(".");
        if (!T.cfg) {
            return 0;
        }
    }
    T.in = t2_input_new(T.cfg);
    if (!T.in) {
        return 0;
    }
    t2_input_keys_from_sdl(T.in, 1);
    memset(&h, 0, sizeof(h));
    h.mobile = is_mobile();
#if defined(__ANDROID__)
    h.android = 1;
    h.frameskip_default = 1;   /* patch 0010: on by default on Android */
#endif
#if defined(T2_RELEASE) && T2_RELEASE
    h.minimal = 1;   /* CMake -DT2_RELEASE=ON: the player release's panel */
#endif
#ifdef T2_VERSION
    h.version = T2_VERSION;
#endif
    h.renderer = host_renderer;
    h.set_renderer = is_mobile() ? NULL : host_set_renderer;
    h.resolution = host_resolution;
    h.set_resolution = is_mobile() ? NULL : host_set_resolution;
    h.fullscreen = host_fullscreen;
    h.set_fullscreen = is_mobile() ? NULL : host_set_fullscreen;
    /* a phone app cannot start itself again: changes wait for the next start */
    h.restart = is_mobile() ? NULL : host_restart;
    h.quit = host_quit;
    T.panel = t2_settings_new(T.in, T.cfg, &h);
    T.font = t2_font_new();
    if (!T.font) {
        fprintf(stderr, "t2: the panel's font is missing: the panel works, unseen\n");
    }
    T.lock = SDL_CreateMutex();
    t2_input_describe(T.in, stderr);
    fprintf(stderr, "t2: settings: hold Select, press F1 or Esc%s (%s)\n",
            is_mobile() ? ", or tap the gear" : "", t2_config_path(T.cfg));
    return 1;
}

/* ------------------------------------------------------------------ hooks */

static int on_event(const void *ev_ptr)
{
    const SDL_Event *e = (const SDL_Event *)ev_ptr;

    if (!ready()) {
        return 0;
    }
    if (t2_input_event(T.in, e)) {
        return 1;
    }
    if (e->type == SDL_EVENT_KEY_DOWN || e->type == SDL_EVENT_KEY_UP) {
        int open_before = t2_settings_is_open(T.panel);
        int used = t2_settings_key(T.panel, e->key.scancode, e->type == SDL_EVENT_KEY_DOWN, e->key.repeat);

        /* while the panel is open, no key reaches the game's hotkeys */
        return used || open_before;
    }
    return 0;
}

static void touch_state(void)
{
    uint16_t held = 0;
    int mode, visible, open;

#if defined(__ANDROID__)
    {
        uint16_t word;

        if (psx_android_touch_pad(&word, NULL, NULL, NULL)) {
            held = (uint16_t)~word;
        }
    }
#else
    held = (uint16_t)atomic_load(&s_touch_held);
#endif
    t2_input_set_touch(T.in, held);
    mode = t2_setting_touch(T.cfg);
    open = t2_settings_is_open(T.panel);
    visible = open || mode == T2_TOUCH_ALWAYS || (mode == T2_TOUCH_AUTO && t2_input_device_count(T.in) == 0);
    /* bit 25: hidden only because Auto saw a controller; the platform side
       shows them anyway when it sees no gamepad itself (an emulator's or a
       keyboard's d-pad keys can reach SDL as a "gamepad") */
    atomic_store(&s_overlay_state, (visible ? 1 : 0) | (t2_setting_touch_size(T.cfg) << 8) |
                                       (t2_setting_touch_opacity(T.cfg) << 16) | (open ? 1 << 24 : 0) |
                                       (!visible && mode == T2_TOUCH_AUTO ? 1 << 25 : 0));
}

static void publish(int i, uint32_t *px, int w, int h, int x, int y, int dw, int dh)
{
    Layer *f = &T.front[i];

    SDL_LockMutex(T.lock);
    if (f->px != px) {
        free(f->px);
    }
    f->px = px;
    f->w = w;
    f->h = h;
    f->x = x;
    f->y = y;
    f->dw = dw;
    f->dh = dh;
    f->version++;
    SDL_UnlockMutex(T.lock);
}

/* For a renderer that copies without blending (Vulkan): the drawn part of
 * img, cut to its bounding box and laid over a dark backdrop, so it is
 * opaque. NULL when nothing is drawn. */
static uint32_t *opaque_crop(const T2Image *img, int *x, int *y, int *w, int *h)
{
    int x0 = img->w, y0 = img->h, x1 = -1, y1 = -1, i, j;
    uint32_t *out;

    for (j = 0; j < img->h; j++) {
        for (i = 0; i < img->w; i++) {
            if (img->px[(size_t)j * img->w + i] >> 24) {
                if (i < x0) x0 = i;
                if (i > x1) x1 = i;
                if (j < y0) y0 = j;
                if (j > y1) y1 = j;
            }
        }
    }
    if (x1 < 0) {
        return NULL;
    }
    *x = x0;
    *y = y0;
    *w = x1 - x0 + 1;
    *h = y1 - y0 + 1;
    out = malloc((size_t)*w * *h * 4);
    if (!out) {
        return NULL;
    }
    for (j = 0; j < *h; j++) {
        for (i = 0; i < *w; i++) {
            uint32_t v = img->px[(size_t)(y0 + j) * img->w + x0 + i], a = v >> 24, o = 0xFF000000u;
            int sh;

            for (sh = 0; sh <= 16; sh += 8) {
                uint32_t c = (((v >> sh) & 0xFF) * a + 0x14 * (255 - a)) / 255;   /* over #141414 */

                o |= c << sh;
            }
            out[(size_t)j * *w + i] = o;
        }
    }
    return out;
}

static void draw_layers(uint64_t now)
{
    int cw = atomic_load(&T.canvas_w), ch = atomic_load(&T.canvas_h), ppu = atomic_load(&T.canvas_ppu1000);
    int opaque = atomic_load(&T.canvas_opaque);
    int resized = cw != T.drawn_cw || ch != T.drawn_ch || ppu != T.drawn_ppu || opaque != T.drawn_opaque;
    uint32_t ver = t2_settings_version(T.panel);
    int fps_on = t2_setting_fps(T.cfg);

    if (cw <= 0 || ch <= 0 || !T.font) {
        return;
    }
    /* layer 0: the panel, or the first launch's line */
    if (resized || ver != T.drawn_panel) {
        float scale = ppu / 1000.0f;
        int w = (int)(cw * scale), h = (int)(ch * scale);
        T2Image img;

        if (opaque) {
            w = cw;   /* copied pixel for pixel */
            h = ch;
        } else if (h > PANEL_MAX_H) {
            w = w * PANEL_MAX_H / h;
            h = PANEL_MAX_H;
        }
        img.w = w;
        img.h = h;
        img.px = malloc((size_t)w * h * 4);
        if (img.px && t2_settings_draw(T.panel, T.font, &img, now)) {
            if (opaque) {
                int x, y, ow, oh;
                uint32_t *o = opaque_crop(&img, &x, &y, &ow, &oh);

                free(img.px);
                if (o) {
                    publish(0, o, ow, oh, x, y, ow, oh);
                } else {
                    publish(0, NULL, 0, 0, 0, 0, 0, 0);
                }
            } else {
                publish(0, img.px, w, h, 0, 0, cw, ch);
            }
        } else {
            free(img.px);
            publish(0, NULL, 0, 0, 0, 0, 0, 0);
        }
        T.drawn_panel = ver;
    }
    /* layer 1: the FPS readout, top right */
    if (resized || fps_on != T.drawn_fps_on || (fps_on && (uint32_t)T.fps != T.drawn_fps_value)) {
        if (fps_on) {
            float scale = ppu / 1000.0f, px = ch * scale * 0.035f;
            char text[24];
            int w, h;
            T2Image img;

            if (px < 12) {
                px = 12;
            }
            snprintf(text, sizeof(text), "%d FPS", T.fps);
            w = (int)(t2_text_width(T.font, px, "000 FPS") + px);
            h = (int)(px * 1.5f);
            img.w = w;
            img.h = h;
            img.px = calloc((size_t)w * h, 4);
            if (img.px) {
                float tw = t2_text_width(T.font, px, text);

                t2_fill_round(&img, 0, 0, w, h, h / 4, opaque ? 0xFF000000u : 0x99000000u);
                t2_text_draw(T.font, &img, w - tw - px * 0.5f, h * 0.72f, px, text, 0xFFFFFFFFu);
                if (opaque) {
                    int x, y, ow, oh;
                    uint32_t *o = opaque_crop(&img, &x, &y, &ow, &oh);

                    free(img.px);
                    if (o) {
                        publish(1, o, ow, oh, cw - ow - (int)(ch * 0.015f), (int)(ch * 0.015f), ow, oh);
                    }
                } else {
                    publish(1, img.px, w, h, cw - (int)(w / scale) - (int)(ch * 0.015f),
                            (int)(ch * 0.015f), (int)(w / scale), (int)(h / scale));
                }
            }
        } else {
            publish(1, NULL, 0, 0, 0, 0, 0, 0);
        }
        T.drawn_fps_on = fps_on;
        T.drawn_fps_value = (uint32_t)T.fps;
    }
    T.drawn_cw = cw;
    T.drawn_ch = ch;
    T.drawn_ppu = ppu;
    T.drawn_opaque = opaque;
}

#if defined(SDL_PLATFORM_IOS)
int t2_ios_touch_attach(void);   /* ios/T2TouchPad.m */
#endif

static void on_tick(void)
{
    uint64_t now;
    T2Pad pads[2];
    int open;

    if (!ready()) {
        return;
    }
#if defined(SDL_PLATFORM_IOS)
    {
        static int attached;

        if (!attached) {
            attached = t2_ios_touch_attach();
        }
    }
#endif
    now = SDL_GetTicks();
    touch_state();
    t2_input_raw(T.in, pads);
#if defined(__ANDROID__)
    if (atomic_exchange(&s_suspend_request, 0) && savestate_dir()[0]) {
        savestate_request_save(SAVESTATE_SUSPEND_SLOT);   /* see nativeSuspendSave */
    }
#endif
    if (atomic_exchange(&s_open_request, 0) && !t2_settings_is_open(T.panel)) {
        t2_settings_open(T.panel);
    }
    {
        /* screen checks and evidence captures: T2_OPEN_SETTINGS_AFTER_MS opens
           the panel once, that long after SDL started, with no input at all */
        static int64_t open_at = -2;

        if (open_at == -2) {
            const char *e = getenv("T2_OPEN_SETTINGS_AFTER_MS");

            open_at = e && *e ? atoll(e) : -1;
        }
        if (open_at >= 0 && (int64_t)now >= open_at) {
            open_at = -1;
            fprintf(stderr, "t2: opening the settings panel (T2_OPEN_SETTINGS_AFTER_MS)\n");
            t2_settings_open(T.panel);
        }
    }
    if (t2_settings_is_open(T.panel)) {
        t2_settings_update(T.panel, pads, now);
    } else {
        t2_settings_watch(T.panel, pads, now);
    }
    open = t2_settings_is_open(T.panel);
    if (open != T.was_open) {
        /* the runtime holds the game and keeps its input out while it is open */
        if (T.host && T.host->set_paused) {
            T.host->set_paused(open);
        }
        T.was_open = open;
    }
    /* FPS: guest frames each half second */
    if (!T.fps_at || now - T.fps_at >= 500) {
        uint64_t frames = s_frame_count;

        if (T.fps_at && !open) {
            T.fps = (int)((frames - T.fps_frames) * 1000 / (now - T.fps_at));
        }
        T.fps_at = now;
        T.fps_frames = frames;
    }
    draw_layers(now);
}

static int on_pad(int port, PsxTitlePad *out)
{
    T2Pad pads[2];

    memset(out, 0, sizeof(*out));
    out->buttons = 0xFFFF;
    memset(out->sticks, 0x80, sizeof(out->sticks));
    if (!ready()) {
        return 1;   /* always connected, nothing held yet */
    }
    t2_input_pads(T.in, pads);
    out->buttons = (uint16_t)~pads[port & 1].buttons;
    out->analog = t2_input_analog(T.in, port);
    out->sticks[0] = pads[port & 1].lx;
    out->sticks[1] = pads[port & 1].ly;
    out->sticks[2] = pads[port & 1].rx;
    out->sticks[3] = pads[port & 1].ry;
    return 1;
}

/* The presenting thread. */
static int on_layer(int index, int canvas_w, int canvas_h, float ppu, PsxTitleLayer *out)
{
    Layer *f, *s;

    if (!T.lock) {
        return 0;
    }
    atomic_store(&T.canvas_w, canvas_w);
    atomic_store(&T.canvas_h, canvas_h);
    atomic_store(&T.canvas_ppu1000, (int)(ppu * 1000.0f));
    atomic_store(&T.canvas_opaque, out->opaque ? 1 : 0);
    f = &T.front[index];
    s = &T.shown[index];
    SDL_LockMutex(T.lock);
    if (f->version != s->version) {
        size_t n = (size_t)f->w * f->h * 4;

        if (!f->px) {
            s->w = s->h = 0;
        } else {
            if ((size_t)s->w * s->h * 4 < n || !s->px) {
                free(s->px);
                s->px = malloc(n);
            }
            if (s->px) {
                memcpy(s->px, f->px, n);
                s->w = f->w;
                s->h = f->h;
            }
        }
        s->x = f->x;
        s->y = f->y;
        s->dw = f->dw;
        s->dh = f->dh;
        s->version = f->version;
    }
    SDL_UnlockMutex(T.lock);
    if (!s->px || s->w <= 0) {
        return 0;
    }
    out->pixels = s->px;
    out->w = s->w;
    out->h = s->h;
    out->x = s->x;
    out->y = s->y;
    out->dw = s->dw;
    out->dh = s->dh;
    out->version = s->version;
    return 1;
}

/* ------------------------------------------------------------------ the menu provider */

static int prov_attach(void *ctx, void *window, void *gl_context, const PsxMenuHost *host)
{
    (void)ctx;
    (void)window;
    T.host = host;
    T.gl = gl_context != NULL;
    fprintf(stderr, "t2: settings panel attached (%s)\n", T.gl ? "OpenGL" : "software renderer");
    return 1;
}

static int prov_is_open(void *ctx)
{
    (void)ctx;
    return T.panel && t2_settings_is_open(T.panel);
}

static void prov_set_open(void *ctx, int open)
{
    (void)ctx;
    if (!T.panel) {
        return;
    }
    if (open) {
        t2_settings_open(T.panel);
    } else {
        t2_settings_close(T.panel);
    }
}

static void prov_detach(void *ctx)
{
    (void)ctx;
    T.host = NULL;
}

static const PsxMenuProvider kProvider = {
    PSX_MENU_ABI_VERSION, "Tekken 2 settings", NULL, prov_attach, NULL, prov_is_open, prov_set_open, NULL, prov_detach,
};

static const PsxTitleHooks kHooks = {
    PSX_TITLE_HOOKS_ABI, early, on_event, on_tick, on_pad, on_layer,
};

PSX_MOD_CONSTRUCTOR(t2_host_register)
{
    psx_title_set_hooks(&kHooks);
    psx_menu_set_provider(&kProvider);
}

/* ------------------------------------------------------------------ the on-screen controller (t2_touch.h) */

void t2_touch_set_held(uint32_t held)
{
    atomic_store(&s_touch_held, held & 0xFFFFu);
}

void t2_touch_open_settings(void)
{
    atomic_store(&s_open_request, 1);
}

int t2_touch_overlay_state(void)
{
    int s = atomic_load(&s_overlay_state);

    /* before the first tick: shown at the defaults */
    return s ? s : 1 | (100 << 8) | (70 << 16);
}

#if defined(__ANDROID__)
#include <jni.h>

JNIEXPORT jint JNICALL Java_com_uppercutlabs_tekken2recomp_TouchPadView_nativeOverlayState(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return t2_touch_overlay_state();
}

JNIEXPORT void JNICALL Java_com_uppercutlabs_tekken2recomp_TouchPadView_nativeOpenSettings(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    t2_touch_open_settings();
}

/* T2Activity.onPause, on the UI thread, before SDL pauses the game thread:
 * write the suspend state (patch 0017's slot) so that if Android kills the
 * process in the background, the recreated activity resumes there
 * (PSX_LOAD_SLOT=suspend). The game thread stages the save on its next tick
 * and writes it at the next safe block boundary, normally within a frame or
 * two; this waits for that, at most timeout_ms. While the settings panel has
 * the game paused no block runs, so the wait times out and the save happens
 * when play resumes. Returns 1 when the state was written. */
JNIEXPORT jboolean JNICALL Java_com_uppercutlabs_tekken2recomp_T2Activity_nativeSuspendSave(JNIEnv *env, jclass cls,
                                                                                            jint timeout_ms)
{
    PSXSavestateStatus before, now;
    int waited;

    (void)env;
    (void)cls;
    if (!T.in || !savestate_dir()[0]) {
        return JNI_FALSE;   /* the game has not started yet */
    }
    savestate_get_status(&before);
    atomic_store(&s_suspend_request, 1);
    for (waited = 0; waited < timeout_ms; waited += 4) {
        SDL_Delay(4);
        savestate_get_status(&now);
        if (now.generation != before.generation && !now.pending) {
            return (now.last_ok && now.last_slot == SAVESTATE_SUSPEND_SLOT) ? JNI_TRUE : JNI_FALSE;
        }
    }
    atomic_store(&s_suspend_request, 0);
    return JNI_FALSE;
}
#endif
