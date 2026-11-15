/* t2_settings_test: the settings panel (src/ui/t2_settings.h) driven by pad
 * presses and keys, as a player would. No window, disc or runtime: the
 * runtime's side (renderer, resolution, fullscreen, restart, quit) is a stub.
 *
 *   t2_settings_test [<scratch dir>]
 *
 * Ported from the GGX+ port's tests/settings/settings_test.c (MIT, Copyright (c) 2026 Lil Gohan):
 *   - the first launch shows a line saying how to open the panel, once;
 *   - Select held a second opens it, not sooner; F1 and Escape open it too;
 *   - the rows on the desktop and on a phone, and what each one changes;
 *   - a change that waits for a restart brings the Restart row;
 *   - a player's controls page rebinds one button (the next key) and goes
 *     back with Circle;
 *   - Start closes it and saves the config;
 * and draws the desktop panel, the phone panel and the controls page to
 * BMP files in the scratch directory (headless captures for the evidence).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "t2_settings.h"

static int failures, checks;

#define CHECK(c, ...)                                                    \
    do {                                                                 \
        checks++;                                                        \
        if (!(c)) {                                                      \
            fprintf(stderr, "FAIL line %d: ", __LINE__);                 \
            fprintf(stderr, __VA_ARGS__);                                \
            fprintf(stderr, "\n");                                       \
            failures++;                                                  \
        }                                                                \
    } while (0)

/* ---- the runtime, stubbed ---- */

static int renderer = 1, resolution = 4, fullscreen, restarts, quits;

static int st_renderer(void *c) { (void)c; return renderer; }
static void st_set_renderer(void *c, int r) { (void)c; renderer = r; }
static int st_resolution(void *c) { (void)c; return renderer ? resolution : 0; }
static void st_set_resolution(void *c, int n) { (void)c; resolution = n; }
static int st_fullscreen(void *c) { (void)c; return fullscreen; }
static void st_set_fullscreen(void *c, int on) { (void)c; fullscreen = on; }
static void st_restart(void *c) { (void)c; restarts++; }
static void st_quit(void *c) { (void)c; quits++; }

static uint64_t now = 1000;

static void tick(T2Settings *s, uint16_t b)
{
    T2Pad p[2];

    memset(p, 0, sizeof(p));
    p[0].buttons = b;
    if (t2_settings_is_open(s)) {
        t2_settings_update(s, p, now);
    } else {
        t2_settings_watch(s, p, now);
    }
    now += 16;
}

static void press(T2Settings *s, uint16_t b)
{
    tick(s, b);
    tick(s, 0);
}

/* Down until the row is the one named; 0 if it is not there. */
static int go(T2Settings *s, const char *label)
{
    int i;

    for (i = 0; i < 40; i++) {
        if (!strcmp(t2_settings_row_label(s), label)) {
            return 1;
        }
        press(s, T2_PAD_DOWN);
    }
    return 0;
}

static void save_bmp(T2Settings *s, T2Font *f, int w, int h, const char *dir, const char *name)
{
    T2Image img;
    SDL_Surface *surf;
    char path[1100];

    img.w = w;
    img.h = h;
    img.px = calloc((size_t)w * h, 4);
    CHECK(img.px && t2_settings_draw(s, f, &img, now), "draw %s", name);
    /* over a mid grey, as the game would show behind it */
    surf = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
    if (surf && img.px) {
        SDL_Surface *src = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ARGB8888, img.px, w * 4);

        SDL_FillSurfaceRect(surf, NULL, SDL_MapSurfaceRGB(surf, 0x50, 0x58, 0x60));
        SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_BLEND);
        SDL_BlitSurface(src, NULL, surf, NULL);
        snprintf(path, sizeof(path), "%s/%s", dir, name);
        CHECK(SDL_SaveBMP(surf, path), "save %s: %s", path, SDL_GetError());
        printf("drew %s (%dx%d)\n", path, w, h);
        SDL_DestroySurface(src);
    }
    SDL_DestroySurface(surf);
    free(img.px);
}

/* ---- Original and Enhanced: the default and the runtime switches ---- */

static const char *env_of(const T2Env *e, int n, const char *name)
{
    int i;

    for (i = 0; i < n; i++) {
        if (!strcmp(e[i].name, name)) {
            return e[i].value;
        }
    }
    return NULL;
}

static void test_mode_env(void)
{
    /* every hardware-timing model the runtime has a switch for */
    static const char *const MODELS[] = {
        "PSX_GPU_TIMING", "PSX_CD_TIMING", "PSX_ROM_TIMING", "PSX_SIO_TIMING",
        "PSX_IRQ_HW_LATENCY", "PSX_SPU_DMA_TIMING", "PSX_VIDEO_TIMING",
    };
    T2Config *c = t2_config_load("t2_settings_test-no-settings.cfg");
    T2Env e[16];
    const char *v;
    size_t i;
    int n;

    /* No saved settings: Enhanced, every model off, Beetle CPU costs, frame
       skip left to the platform (the release default). */
    CHECK(t2_setting_mode(c) == T2_MODE_ENHANCED, "no settings: Enhanced");
    n = t2_mode_env(c, t2_setting_mode(c), e, 16);
    for (i = 0; i < sizeof(MODELS) / sizeof(MODELS[0]); i++) {
        v = env_of(e, n, MODELS[i]);
        CHECK(v && !strcmp(v, "0"), "Enhanced turns %s off (%s)", MODELS[i], v ? v : "unset");
    }
    v = env_of(e, n, "PSX_CPU_COST");
    CHECK(v && !strcmp(v, "beetle"), "Enhanced: Beetle CPU costs (%s)", v ? v : "unset");
    CHECK(!env_of(e, n, "PSX_PRESENT_CATCHUP"), "Enhanced: frame skip left to the platform");

    /* Original chosen (the panel's Mode row or "mode = original" in the
       settings file): every model on by itself, DuckStation's CPU costs (the
       models' reference), every frame kept. */
    t2_config_set(c, "mode", "original");
    CHECK(t2_setting_mode(c) == T2_MODE_ORIGINAL, "settings file: Original");
    n = t2_mode_env(c, t2_setting_mode(c), e, 16);
    for (i = 0; i < sizeof(MODELS) / sizeof(MODELS[0]); i++) {
        v = env_of(e, n, MODELS[i]);
        CHECK(v && !strcmp(v, "1"), "Original turns %s on (%s)", MODELS[i], v ? v : "unset");
    }
    v = env_of(e, n, "PSX_CPU_COST");
    CHECK(v && !strcmp(v, "ds"), "Original: DuckStation CPU costs (%s)", v ? v : "unset");
    v = env_of(e, n, "PSX_PRESENT_CATCHUP");
    CHECK(v && !strcmp(v, "0"), "Original keeps every frame");
    /* Frame skip saved on in Enhanced does not leak into Original. */
    t2_config_set(c, "frameskip", "on");
    n = t2_mode_env(c, T2_MODE_ORIGINAL, e, 16);
    v = env_of(e, n, "PSX_PRESENT_CATCHUP");
    CHECK(v && !strcmp(v, "0"), "Original ignores a saved frame skip");
    n = t2_mode_env(c, T2_MODE_ENHANCED, e, 16);
    v = env_of(e, n, "PSX_PRESENT_CATCHUP");
    CHECK(v && !strcmp(v, "1"), "Enhanced keeps its saved frame skip");
    CHECK(t2_mode_env(c, T2_MODE_ORIGINAL, e, 3) == 3, "the count is capped");
    t2_config_free(c);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    char path[1024], text[160];
    T2Config *cfg;
    T2Input *in;
    T2Settings *s;
    T2SettingsHost host;
    T2Font *font;
    T2Image probe;
    int i;

    /* only the virtual controllers: a real pad plugged into the test
       machine must not take a player */
    {
        static const char *const OFF[] = {
            "SDL_JOYSTICK_HIDAPI", "SDL_JOYSTICK_RAWINPUT", "SDL_JOYSTICK_WGI", "SDL_XINPUT_ENABLED",
            "SDL_JOYSTICK_DIRECTINPUT", "SDL_JOYSTICK_GAMEINPUT", "SDL_JOYSTICK_MFI", "SDL_JOYSTICK_IOKIT",
        };
        size_t h;

        for (h = 0; h < sizeof(OFF) / sizeof(OFF[0]); h++) {
            SDL_SetHint(OFF[h], "0");
        }
        t2_input_only_virtual(1);
    }
    if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
        fprintf(stderr, "t2_settings_test: SDL: %s\n", SDL_GetError());
        return 1;
    }
    snprintf(path, sizeof(path), "%s/t2_settings_test.cfg", dir);
    remove(path);
    cfg = t2_config_load(path);
    in = t2_input_new(cfg);
    font = t2_font_new();
    CHECK(font != NULL, "the embedded font loads");
    memset(&host, 0, sizeof(host));
    host.renderer = st_renderer;
    host.set_renderer = st_set_renderer;
    host.resolution = st_resolution;
    host.set_resolution = st_set_resolution;
    host.fullscreen = st_fullscreen;
    host.set_fullscreen = st_set_fullscreen;
    host.restart = st_restart;
    host.quit = st_quit;
    s = t2_settings_new(in, cfg, &host);

    /* the first launch: a line says how to open it, and is remembered */
    probe.w = 320;
    probe.h = 240;
    probe.px = calloc(320 * 240, 4);
    CHECK(t2_settings_draw(s, font, &probe, now) == 1, "the first launch's line shows");
    CHECK(!strcmp(t2_config_get_or(cfg, "hint", ""), "shown"), "the line is remembered");
    free(probe.px);

    /* Select held: not before a second, then open */
    for (i = 0; i < 55; i++) {
        tick(s, T2_PAD_SELECT);
    }
    CHECK(!t2_settings_is_open(s), "Select held %d ms: still closed", 55 * 16);
    for (i = 0; i < 10; i++) {
        tick(s, T2_PAD_SELECT);
    }
    CHECK(t2_settings_is_open(s), "Select held a second opens the panel");
    tick(s, T2_PAD_SELECT);
    CHECK(t2_settings_is_open(s) && !strcmp(t2_settings_row_label(s), "Resume"), "the held Select does nothing inside");
    tick(s, 0);

    /* the rows on the desktop */
    {
        static const char *const ROWS[] = {
            "Resume", "Player 1 controls", "Player 2 controls", "Swap players' controllers", "Controller mode",
            "Renderer", "Internal resolution", "Fullscreen", "Audio latency", "FPS display", "Mode",
            "Frame skip when slow", "Quit",
        };

        for (i = 0; i < (int)(sizeof(ROWS) / sizeof(ROWS[0])); i++) {
            CHECK(!strcmp(t2_settings_row_label(s), ROWS[i]), "row %d: %s, want %s", i, t2_settings_row_label(s),
                  ROWS[i]);
            press(s, T2_PAD_DOWN);
        }
        CHECK(!strcmp(t2_settings_row_label(s), "Resume"), "Down wraps to the top: %s", t2_settings_row_label(s));
        press(s, T2_PAD_UP);
        CHECK(!strcmp(t2_settings_row_label(s), "Quit"), "Up wraps to the bottom: %s", t2_settings_row_label(s));
    }

    /* each row changes what it says */
    CHECK(go(s, "Controller mode"), "Controller mode row");
    press(s, T2_PAD_RIGHT);
    CHECK(!strcmp(t2_config_get_or(cfg, "controller.mode", ""), "direct-usb"), "direct USB: %s",
          t2_config_get_or(cfg, "controller.mode", ""));
    CHECK(go(s, "Restart now"), "a restart-only change brings the Restart row");
    CHECK(go(s, "Controller mode"), "Controller mode row again");
    press(s, T2_PAD_LEFT);
    CHECK(!strcmp(t2_config_get_or(cfg, "controller.mode", ""), "standard"), "back to standard");
    CHECK(!go(s, "Restart now"), "back as booted: no Restart row");
    CHECK(go(s, "Internal resolution"), "Internal resolution row");
    press(s, T2_PAD_RIGHT);
    CHECK(resolution == 5, "Right: 5x (%d)", resolution);
    press(s, T2_PAD_LEFT);
    press(s, T2_PAD_LEFT);
    CHECK(resolution == 3, "Left twice: 3x (%d)", resolution);
    CHECK(go(s, "Fullscreen"), "Fullscreen row");
    press(s, T2_PAD_X);
    CHECK(fullscreen == 1, "X: fullscreen on");
    press(s, T2_PAD_RIGHT);
    CHECK(fullscreen == 0, "Right: fullscreen off");
    CHECK(go(s, "Audio latency"), "Audio latency row");
    press(s, T2_PAD_LEFT);
    CHECK(!strcmp(t2_config_get_or(cfg, "audio.latency", ""), "low") && t2_setting_audio_ms(t2_setting_audio(cfg)) == 80,
          "audio: low, 80 ms");
    CHECK(go(s, "FPS display"), "FPS row");
    press(s, T2_PAD_X);
    CHECK(t2_setting_fps(cfg) == 1, "FPS on");
    CHECK(go(s, "Frame skip when slow"), "Frame skip row (Enhanced)");
    CHECK(t2_setting_frameskip(cfg, 0) == 0, "frame skip off by default on the desktop");
    press(s, T2_PAD_X);
    CHECK(t2_setting_frameskip(cfg, 0) == 1, "frame skip on");
    press(s, T2_PAD_X);
    CHECK(t2_setting_frameskip(cfg, 0) == 0, "frame skip off again");
    CHECK(!go(s, "Display refresh"), "no Display refresh row off Android");
    CHECK(go(s, "Mode"), "Mode row");
    press(s, T2_PAD_RIGHT);
    CHECK(t2_setting_mode(cfg) == T2_MODE_ORIGINAL, "Original mode");
    CHECK(!go(s, "Frame skip when slow"), "Original: no frame skip row");
    CHECK(go(s, "Renderer"), "Renderer row");
    press(s, T2_PAD_RIGHT);
    CHECK(renderer == 0, "Software renderer");
    CHECK(go(s, "Internal resolution"), "Internal resolution row (software)");
    CHECK(go(s, "Restart now"), "Restart row");
    press(s, T2_PAD_X);
    CHECK(restarts == 1, "Restart now restarts");
    CHECK(go(s, "Renderer"), "Renderer row");
    press(s, T2_PAD_LEFT);
    CHECK(renderer == 1, "OpenGL again");
    save_bmp(s, font, 1280, 960, dir, "t2-settings-desktop.bmp");

    /* a player's controls: X on a button takes the next key */
    CHECK(go(s, "Player 1 controls"), "Player 1 controls row");
    press(s, T2_PAD_X);
    CHECK(t2_settings_page(s) == 1 && !strcmp(t2_settings_row_label(s), "X"), "page 1, row X: %d %s",
          t2_settings_page(s), t2_settings_row_label(s));
    press(s, T2_PAD_DOWN);
    CHECK(!strcmp(t2_settings_row_label(s), "Circle"), "row Circle: %s", t2_settings_row_label(s));
    press(s, T2_PAD_X);
    CHECK(t2_input_remap_button(in) == 13, "capturing Circle: %d", t2_input_remap_button(in));
    save_bmp(s, font, 1280, 720, dir, "t2-settings-controls-capture.bmp");
    SDL_Delay(350);
    {
        SDL_Event e;

        memset(&e, 0, sizeof(e));
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.scancode = SDL_SCANCODE_D;
        e.key.down = true;
        CHECK(t2_input_event(in, &e) == 1, "the key goes to the capture");
    }
    tick(s, 0);
    CHECK(t2_input_remap_button(in) < 0, "capture done");
    t2_input_binding_text(in, 0, 13, text, sizeof(text));
    CHECK(strstr(text, "D key") && strstr(text, "east"), "Circle: %s", text);
    /* a capture nobody answers gives up after five seconds */
    press(s, T2_PAD_X);
    CHECK(t2_input_remap_button(in) == 13, "capturing again");
    now += 5100;
    tick(s, 0);
    CHECK(t2_input_remap_button(in) < 0, "gave up after five seconds");
    t2_input_binding_text(in, 0, 13, text, sizeof(text));
    CHECK(strstr(text, "D key") != NULL, "kept: %s", text);
    press(s, T2_PAD_CIRCLE);
    CHECK(t2_settings_page(s) == 0 && !strcmp(t2_settings_row_label(s), "Player 1 controls"),
          "Circle goes back to the row: %s", t2_settings_row_label(s));
    /* Escape goes back from a page, then closes */
    press(s, T2_PAD_X);
    CHECK(t2_settings_page(s) == 1, "page 1 again");
    CHECK(t2_settings_key(s, SDL_SCANCODE_ESCAPE, 1, 0) == 1 && t2_settings_page(s) == 0, "Escape: back");

    /* Start closes and saves */
    press(s, T2_PAD_START);
    CHECK(!t2_settings_is_open(s), "Start closes");
    {
        T2Config *c2 = t2_config_load(path);

        CHECK(t2_setting_fps(c2) == 1 && t2_setting_mode(c2) == T2_MODE_ORIGINAL, "saved on close");
        t2_config_free(c2);
    }
    /* a key opens and closes it; Select still held from closing does not reopen it */
    CHECK(t2_settings_key(s, SDL_SCANCODE_F1, 1, 0) == 1 && t2_settings_is_open(s), "F1 opens");
    CHECK(t2_settings_key(s, SDL_SCANCODE_F1, 1, 0) == 1 && !t2_settings_is_open(s), "F1 closes");
    CHECK(t2_settings_key(s, SDL_SCANCODE_ESCAPE, 1, 0) == 1 && t2_settings_is_open(s), "Escape opens");
    CHECK(t2_settings_key(s, SDL_SCANCODE_ESCAPE, 1, 0) == 1 && !t2_settings_is_open(s), "Escape closes");
    CHECK(t2_settings_key(s, SDL_SCANCODE_X, 1, 0) == 0, "other keys are the game's");
    t2_settings_open(s);
    CHECK(go(s, "Quit"), "Quit row");
    press(s, T2_PAD_X);
    CHECK(quits == 0 && t2_settings_is_open(s), "one X on Quit only asks");
    press(s, T2_PAD_UP);
    press(s, T2_PAD_DOWN);
    press(s, T2_PAD_X);
    CHECK(quits == 0, "moving away takes the question back");
    press(s, T2_PAD_X);
    CHECK(quits == 1, "X twice on Quit quits");
    t2_settings_free(s);

    /* a player release (-DT2_RELEASE=ON): no controller mode, renderer or frame skip rows,
       and the version after the title */
    host.minimal = 1;
    host.version = "0.1.0";
    s = t2_settings_new(in, cfg, &host);
    t2_settings_open(s);
    tick(s, 0);
    {
        static const char *const ROWS[] = {
            "Resume", "Player 1 controls", "Player 2 controls", "Swap players' controllers",
            "Internal resolution", "Fullscreen", "Audio latency", "FPS display", "Mode", "Quit",
        };

        for (i = 0; i < (int)(sizeof(ROWS) / sizeof(ROWS[0])); i++) {
            CHECK(!strcmp(t2_settings_row_label(s), ROWS[i]), "release row %d: %s, want %s", i,
                  t2_settings_row_label(s), ROWS[i]);
            press(s, T2_PAD_DOWN);
        }
        CHECK(!strcmp(t2_settings_row_label(s), "Resume"), "release rows end at Quit: %s",
              t2_settings_row_label(s));
    }
    save_bmp(s, font, 640, 480, dir, "t2_settings_release.bmp");
    t2_settings_free(s);
    host.minimal = 0;
    host.version = NULL;

    /* on a phone: the on-screen control rows; no renderer, resolution, fullscreen or restart */
    host.mobile = 1;
    host.set_renderer = NULL;
    host.set_resolution = NULL;
    host.set_fullscreen = NULL;
    host.restart = NULL;
    s = t2_settings_new(in, cfg, &host);
    t2_settings_open(s);
    tick(s, 0);   /* the first tick after opening only notes what is held */
    {
        static const char *const ROWS[] = {
            "Resume", "Player 1 controls", "Player 2 controls", "Swap players' controllers", "Controller mode",
            "On-screen controls", "On-screen control size", "On-screen control opacity", "Audio latency",
            "FPS display", "Mode", "Quit",
        };

        for (i = 0; i < (int)(sizeof(ROWS) / sizeof(ROWS[0])); i++) {
            CHECK(!strcmp(t2_settings_row_label(s), ROWS[i]), "phone row %d: %s, want %s", i,
                  t2_settings_row_label(s), ROWS[i]);
            press(s, T2_PAD_DOWN);
        }
    }
    CHECK(go(s, "On-screen controls"), "On-screen controls row");
    CHECK(t2_setting_touch(cfg) == T2_TOUCH_AUTO, "auto by default");
    press(s, T2_PAD_RIGHT);
    CHECK(t2_setting_touch(cfg) == T2_TOUCH_ALWAYS, "always");
    press(s, T2_PAD_RIGHT);
    CHECK(t2_setting_touch(cfg) == T2_TOUCH_NEVER, "never");
    press(s, T2_PAD_RIGHT);
    CHECK(t2_setting_touch(cfg) == T2_TOUCH_AUTO, "auto again");
    CHECK(go(s, "On-screen control size"), "size row");
    press(s, T2_PAD_RIGHT);
    CHECK(t2_setting_touch_size(cfg) == 110, "size 110%%: %d", t2_setting_touch_size(cfg));
    CHECK(go(s, "On-screen control opacity"), "opacity row");
    press(s, T2_PAD_LEFT);
    CHECK(t2_setting_touch_opacity(cfg) == 60, "opacity 60%%: %d", t2_setting_touch_opacity(cfg));
    CHECK(go(s, "Mode"), "Mode row");
    press(s, T2_PAD_RIGHT);
    CHECK(!go(s, "Restart now"), "no Restart row on a phone");
    CHECK(go(s, "On-screen controls"), "back to the touch rows for the picture");
    save_bmp(s, font, 960, 720, dir, "t2-settings-phone.bmp");
    t2_settings_close(s);
    t2_settings_free(s);

    /* Android: in Enhanced mode, frame skip (on by default there) and the display refresh */
    host.android = 1;
    host.frameskip_default = 1;
    t2_config_set(cfg, "mode", "enhanced");
    s = t2_settings_new(in, cfg, &host);
    t2_settings_open(s);
    tick(s, 0);
    CHECK(go(s, "Frame skip when slow"), "Android: frame skip row");
    {
        T2Config *fresh = t2_config_load("t2_settings_test-unwritten.cfg");

        CHECK(t2_setting_frameskip(fresh, 1) == 1, "nothing chosen: on by default on Android");
        CHECK(t2_setting_frameskip(fresh, 0) == 0, "nothing chosen: off on the desktop");
        t2_config_free(fresh);
    }
    t2_config_set(cfg, "frameskip", "on");
    press(s, T2_PAD_X);
    CHECK(t2_setting_frameskip(cfg, 1) == 0 && !strcmp(t2_config_get_or(cfg, "frameskip", ""), "off"), "off, saved");
    CHECK(go(s, "Display refresh"), "Android: display refresh row");
    CHECK(t2_setting_refresh(cfg) == T2_REFRESH_60, "60 Hz by default");
    press(s, T2_PAD_RIGHT);
    CHECK(t2_setting_refresh(cfg) == T2_REFRESH_NATIVE && !strcmp(t2_config_get_or(cfg, "display.refresh", ""), "native"),
          "native, saved");
    CHECK(go(s, "Mode"), "Mode row");
    press(s, T2_PAD_RIGHT);
    CHECK(!go(s, "Display refresh") && !go(s, "Frame skip when slow"), "Original hides both");
    t2_settings_close(s);
    t2_settings_free(s);

    test_mode_env();

    t2_font_free(font);
    t2_input_free(in);
    t2_config_free(cfg);
    SDL_Quit();
    printf("t2_settings_test: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
