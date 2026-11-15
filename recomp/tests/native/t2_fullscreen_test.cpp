/* t2_fullscreen_test: the three fullscreen switches of patch 0014 (F11,
 * Alt+Enter and a double-click on the picture) and following the window's
 * own fullscreen control, with synthetic SDL events on SDL's dummy video
 * driver: no OS input, no visible window.
 *
 *   t2_fullscreen_test [<scratch dir>]
 *
 * It uses the runtime's own pieces: host_keymap.c's default bindings
 * (HOST_KEYMAP_FULLSCREEN), psx_fullscreen_input.h's double-click test and
 * window_fullscreen.h's switch. dispatch() below makes the same decisions,
 * in the same order, as drain_host_events in runtime/src/main.cpp (patch
 * 0011): key events through the keymap, a left double-click from a mouse in
 * the middle half of the 4:3 picture, ENTER/LEAVE_FULLSCREEN checked
 * against the window's flags.
 */
#include <cstdio>
#include <cstring>

/* the runtime's SDL3 shim first: it turns SDL's old names on before SDL.h */
#include "psx_sdl.h"
#include <SDL3/SDL_main.h>

extern "C" {
#include "host_keymap.h"
}
#include "psx_fullscreen_input.h"
#include "window_fullscreen.h"

static int failures, checks;

#define CHECK(c, ...)                                                    \
    do {                                                                 \
        checks++;                                                        \
        if (!(c)) {                                                      \
            std::fprintf(stderr, "FAIL line %d: ", __LINE__);            \
            std::fprintf(stderr, __VA_ARGS__);                           \
            std::fprintf(stderr, "\n");                                  \
            failures++;                                                  \
        }                                                                \
    } while (0)

static SDL_Window* win;
static PsxWindowFullscreen state;
static int toggles;

static void toggle(const char* why) {
    const int target = state.mode ? 0 : 1;
    const int rc = psx_window_fullscreen_set(win, &state, target);
    CHECK(rc == 0, "switch to %d (%s): %s", target, why, SDL_GetError());
    toggles++;
    std::printf("  %-34s -> %s\n", why, state.mode ? "fullscreen" : "windowed");
}

/* main.cpp's drain_host_events, fullscreen part (patch 0014). */
static void dispatch(const SDL_Event& ev) {
    if (ev.type == SDL_EVENT_KEY_DOWN) {
        if (!ev.key.repeat &&
            host_keymap_match_event(HOST_KEYMAP_FULLSCREEN, (int)ev.key.key, (int)ev.key.scancode,
                                    (int)ev.key.mod))
            toggle(ev.key.scancode == SDL_SCANCODE_F11 ? "F11" : "Alt+Enter / Ctrl+F");
    } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        int ww = 0, wh = 0;
        if (psx_fs_is_double_click(1, ev.button.button, ev.button.clicks,
                                   ev.button.which == SDL_TOUCH_MOUSEID) &&
            SDL_GetWindowSize(win, &ww, &wh) &&
            psx_fs_in_image_middle(ww, wh, 4, 3, ev.button.x, ev.button.y))
            toggle("double-click");
    } else if (ev.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN ||
               ev.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) {
        const bool full = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) != 0;
        if (ev.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN && full && !state.mode) {
            state.mode = 1;
            state.width = state.height = 0;
            std::printf("  %-34s -> fullscreen\n", "window control (enter)");
        } else if (ev.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN && !full && state.mode) {
            state.mode = 0;
            std::printf("  %-34s -> windowed\n", "window control (leave)");
        }
    }
}

static void pump(void) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) dispatch(ev);
}

static void key(SDL_Scancode sc, SDL_Keymod mod, bool repeat) {
    SDL_Event e;
    std::memset(&e, 0, sizeof e);
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.windowID = SDL_GetWindowID(win);
    e.key.scancode = sc;
    e.key.key = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
    e.key.mod = mod;
    e.key.down = true;
    e.key.repeat = repeat;
    SDL_PushEvent(&e);
    pump();
}

static void click(float x, float y, Uint8 button, Uint8 clicks, SDL_MouseID which) {
    SDL_Event e;
    std::memset(&e, 0, sizeof e);
    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    e.button.windowID = SDL_GetWindowID(win);
    e.button.which = which;
    e.button.button = button;
    e.button.clicks = clicks;
    e.button.down = true;
    e.button.x = x;
    e.button.y = y;
    SDL_PushEvent(&e);
    pump();
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    if (!SDL_getenv("SDL_VIDEO_DRIVER")) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {   /* psx_sdl.h: SDL2's convention */
        std::fprintf(stderr, "t2_fullscreen_test: SDL: %s\n", SDL_GetError());
        return 1;
    }
    std::printf("t2_fullscreen_test: video driver %s\n", SDL_GetCurrentVideoDriver());

    /* the hit test: a 1600x900 window has a 1200x900 picture in the middle */
    CHECK(psx_fs_in_image_middle(1600, 900, 4, 3, 800, 450), "centre");
    CHECK(psx_fs_in_image_middle(1600, 900, 4, 3, 800 - 299, 450 + 224), "just inside the middle half");
    CHECK(!psx_fs_in_image_middle(1600, 900, 4, 3, 800 - 301, 450), "just outside it, on the picture");
    CHECK(!psx_fs_in_image_middle(1600, 900, 4, 3, 100, 450), "on the left bar");
    CHECK(!psx_fs_in_image_middle(1600, 900, 4, 3, 800, 10), "at the top edge");
    CHECK(psx_fs_in_image_middle(800, 1200, 4, 3, 400, 600), "a tall window: centre");
    CHECK(!psx_fs_in_image_middle(800, 1200, 4, 3, 400, 400), "a tall window: on the bar above");
    CHECK(!psx_fs_in_image_middle(0, 0, 4, 3, 0, 0), "no window");
    CHECK(psx_fs_is_double_click(1, 1, 2, 0), "left double-click");
    CHECK(!psx_fs_is_double_click(1, 1, 1, 0), "a single click");
    CHECK(!psx_fs_is_double_click(1, 3, 2, 0), "a right double-click");
    CHECK(!psx_fs_is_double_click(1, 1, 3, 0), "a triple click's third");
    CHECK(!psx_fs_is_double_click(1, 1, 2, 1), "a double tap on a touch screen");

    host_keymap_load(nullptr);   /* the built-in bindings */
    CHECK(host_keymap_match_event(HOST_KEYMAP_FULLSCREEN, SDLK_F11, SDL_SCANCODE_F11, 0), "F11 is fullscreen");
    CHECK(host_keymap_match_event(HOST_KEYMAP_FULLSCREEN, SDLK_RETURN, SDL_SCANCODE_RETURN, SDL_KMOD_LALT),
          "Alt+Enter is fullscreen");
    CHECK(!host_keymap_match_event(HOST_KEYMAP_FULLSCREEN, SDLK_RETURN, SDL_SCANCODE_RETURN, 0),
          "Enter alone is the game's Start");
    CHECK(!host_keymap_match_event(HOST_KEYMAP_FULLSCREEN, SDLK_F9, SDL_SCANCODE_F9, 0), "F9 is not");
    CHECK(!host_keymap_match_event(HOST_KEYMAP_TURBO_TOGGLE, SDLK_F11, SDL_SCANCODE_F11, 0), "F11 is nothing else");
    CHECK(!host_keymap_match_event(HOST_KEYMAP_TURBO_TOGGLE, SDLK_F1, SDL_SCANCODE_F1, 0) &&
              !host_keymap_match_event(HOST_KEYMAP_FULLSCREEN, SDLK_F1, SDL_SCANCODE_F1, 0),
          "F1 (the settings panel) is free");

    /* psx_sdl.h's SDL2-shaped SDL_CreateWindow, as main.cpp calls it */
    win = SDL_CreateWindow("t2_fullscreen_test", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 960,
                           SDL_WINDOW_RESIZABLE);
    CHECK(win != nullptr, "window: %s", SDL_GetError());
    if (!win) return 1;
    pump();
    std::printf("synthetic events:\n");

    key(SDL_SCANCODE_F11, SDL_KMOD_NONE, false);
    CHECK(state.mode == 1 && toggles == 1, "F11: fullscreen (%d)", state.mode);
    key(SDL_SCANCODE_F11, SDL_KMOD_NONE, true);
    CHECK(toggles == 1, "a held F11's repeat does nothing");
    key(SDL_SCANCODE_F11, SDL_KMOD_NONE, false);
    CHECK(state.mode == 0 && toggles == 2, "F11 again: windowed");

    key(SDL_SCANCODE_RETURN, SDL_KMOD_LALT, false);
    CHECK(state.mode == 1 && toggles == 3, "Alt+Enter: fullscreen");
    key(SDL_SCANCODE_RETURN, SDL_KMOD_NONE, false);
    CHECK(state.mode == 1 && toggles == 3, "Enter alone does nothing here");
    key(SDL_SCANCODE_RETURN, SDL_KMOD_RALT, false);
    CHECK(state.mode == 0 && toggles == 4, "right Alt+Enter: windowed");

    {
        int ww = 0, wh = 0;
        SDL_GetWindowSize(win, &ww, &wh);
        std::printf("  window %dx%d\n", ww, wh);
        click(ww / 2.0f, wh / 2.0f, SDL_BUTTON_LEFT, 1, 0);
        CHECK(toggles == 4, "a single click does nothing");
        click(ww / 2.0f, wh / 2.0f, SDL_BUTTON_LEFT, 2, 0);
        CHECK(state.mode == 1 && toggles == 5, "double-click on the picture: fullscreen");
        SDL_GetWindowSize(win, &ww, &wh);
        click(ww / 2.0f, wh / 2.0f, SDL_BUTTON_RIGHT, 2, 0);
        CHECK(toggles == 5, "a right double-click does nothing");
        click(2.0f, wh / 2.0f, SDL_BUTTON_LEFT, 2, 0);
        CHECK(toggles == 5, "a double-click at the edge does nothing");
        click(ww / 2.0f, wh / 2.0f, SDL_BUTTON_LEFT, 2, SDL_TOUCH_MOUSEID);
        CHECK(toggles == 5, "a double tap does nothing");
        click(ww / 2.0f, wh / 2.0f, SDL_BUTTON_LEFT, 2, 0);
        CHECK(state.mode == 0 && toggles == 6, "double-click again: windowed");
    }

    /* the window's own control (macOS's green button): followed, and the next key goes back */
    SDL_SetWindowFullscreen(win, true);
    SDL_SyncWindow(win);
    pump();
    {
        SDL_Event e;
        std::memset(&e, 0, sizeof e);
        e.type = SDL_EVENT_WINDOW_ENTER_FULLSCREEN;
        e.window.windowID = SDL_GetWindowID(win);
        SDL_PushEvent(&e);   /* in case the driver sent none */
        pump();
    }
    CHECK(state.mode == 1, "the window went fullscreen by itself: followed");
    key(SDL_SCANCODE_F11, SDL_KMOD_NONE, false);
    SDL_SyncWindow(win);
    CHECK(state.mode == 0 && !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN),
          "F11 after it: windowed (flags %llx)", (unsigned long long)SDL_GetWindowFlags(win));
    {
        /* a late ENTER from an earlier switch, after the window is windowed again, is ignored */
        SDL_Event e;
        std::memset(&e, 0, sizeof e);
        e.type = SDL_EVENT_WINDOW_ENTER_FULLSCREEN;
        e.window.windowID = SDL_GetWindowID(win);
        SDL_PushEvent(&e);
        pump();
        CHECK(state.mode == 0, "a stale enter event is ignored");
    }

    SDL_DestroyWindow(win);
    SDL_Quit();
    std::printf("t2_fullscreen_test: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
