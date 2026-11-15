/* t2_input_test: Tekken 2's input layer (src/input/t2_input.h) with SDL's
 * virtual controllers. No window, GPU, disc or real controller.
 *
 *   t2_input_test [<scratch dir>]
 *
 * Ported from the GGX+ port's tests/input/input_test.c (MIT, Copyright (c) 2026 Lil Gohan), without
 * GGX's macros, confirm swap and prompt letters, and with mapping checks for
 * a DualSense (054c:0ce6), a DualShock 4 (054c:09cc) and an Xbox Series pad
 * (045e:0b12): every button by position, with no setup. Also: the keyboard
 * alongside the pads, players assigned as devices connect, hot-plug (a pad
 * leaving and coming back), the on-screen controller, SOCD, swapping the
 * players, analog mode, a joystick SDL has no mapping for, the remap
 * capture and its saved binding, and pinning a pad to a player. The config
 * file is written in the scratch directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "t2_config.h"
#include "t2_input.h"

static int failures, checks;

#define CHECK(cond, ...)                                     \
    do {                                                     \
        checks++;                                            \
        if (!(cond)) {                                       \
            failures++;                                      \
            fprintf(stderr, "FAIL line %d: ", __LINE__);     \
            fprintf(stderr, __VA_ARGS__);                    \
            fputc('\n', stderr);                             \
        }                                                    \
    } while (0)

static SDL_JoystickID attach(const char *name, Uint16 vid, Uint16 pid, int gamepad)
{
    SDL_VirtualJoystickDesc d;
    SDL_JoystickID id;

    SDL_INIT_INTERFACE(&d);
    d.vendor_id = vid;
    d.product_id = pid;
    d.name = name;
    if (gamepad) {
        d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        d.naxes = SDL_GAMEPAD_AXIS_COUNT;
        d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        d.button_mask = 0x1fffff;   /* the standard buttons and the four paddles */
        d.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    } else {
        /* a DirectInput arcade stick: ten buttons, a hat, two axes, no gamepad mapping */
        d.type = SDL_JOYSTICK_TYPE_ARCADE_STICK;
        d.naxes = 2;
        d.nbuttons = 10;
        d.nhats = 1;
    }
    id = SDL_AttachVirtualJoystick(&d);
    CHECK(id != 0, "attach %s: %s", name, SDL_GetError());
    return id;
}

/* Only the test's own (virtual) controllers: a real pad plugged into the
   machine must not change the results. Removals pass. */
static int physical(const SDL_Event *e)
{
    SDL_JoystickID id;

    switch (e->type) {
    case SDL_EVENT_GAMEPAD_ADDED: id = e->gdevice.which; break;
    case SDL_EVENT_JOYSTICK_ADDED: id = e->jdevice.which; break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: id = e->gbutton.which; break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: id = e->gaxis.which; break;
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_BUTTON_UP: id = e->jbutton.which; break;
    case SDL_EVENT_JOYSTICK_HAT_MOTION: id = e->jhat.which; break;
    case SDL_EVENT_JOYSTICK_AXIS_MOTION: id = e->jaxis.which; break;
    default: return 0;
    }
    return !SDL_IsJoystickVirtual(id);
}

static void pump(T2Input *in)
{
    SDL_Event e;

    SDL_UpdateJoysticks();
    while (SDL_PollEvent(&e)) {
        if (!physical(&e)) {
            t2_input_event(in, &e);
        }
    }
}

static void key(T2Input *in, SDL_Scancode k, int down)
{
    SDL_Event e;

    memset(&e, 0, sizeof(e));
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = k;
    e.key.down = down != 0;
    t2_input_event(in, &e);
}

static void pads(T2Input *in, T2Pad p[2])
{
    pump(in);
    t2_input_pads(in, p);
}

static SDL_Joystick *joy_of(SDL_JoystickID id)
{
    SDL_Joystick *j = SDL_GetJoystickFromID(id);

    CHECK(j != NULL, "no joystick %u", (unsigned)id);
    return j;
}

/* One button of a virtual gamepad, held alone: the PS1 bit it gives. */
static uint16_t press_one(T2Input *in, SDL_Joystick *j, int button, int player)
{
    T2Pad p[2];
    uint16_t got;

    SDL_SetJoystickVirtualButton(j, button, true);
    pads(in, p);
    got = p[player].buttons;
    SDL_SetJoystickVirtualButton(j, button, false);
    pads(in, p);
    return got;
}

/* The same, on whichever player the pad is now. */
static uint16_t press_any(T2Input *in, SDL_Joystick *j, int button)
{
    return (uint16_t)(press_one(in, j, button, 0) | press_one(in, j, button, 1));
}

/* Every PlayStation button on a pad by position (south X, east Circle...). */
static void check_by_position(T2Input *in, const char *what, SDL_Joystick *j, int player)
{
    static const struct {
        int button;
        uint16_t want;
        const char *name;
    } MAP[] = {
        { SDL_GAMEPAD_BUTTON_SOUTH, T2_PAD_X, "south: X" },
        { SDL_GAMEPAD_BUTTON_EAST, T2_PAD_CIRCLE, "east: Circle" },
        { SDL_GAMEPAD_BUTTON_WEST, T2_PAD_SQUARE, "west: Square" },
        { SDL_GAMEPAD_BUTTON_NORTH, T2_PAD_TRIANGLE, "north: Triangle" },
        { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, T2_PAD_L1, "L1" },
        { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, T2_PAD_R1, "R1" },
        { SDL_GAMEPAD_BUTTON_BACK, T2_PAD_SELECT, "Create/Share/View: Select" },
        { SDL_GAMEPAD_BUTTON_START, T2_PAD_START, "Options/Menu: Start" },
        { SDL_GAMEPAD_BUTTON_DPAD_UP, T2_PAD_UP, "d-pad up" },
        { SDL_GAMEPAD_BUTTON_DPAD_DOWN, T2_PAD_DOWN, "d-pad down" },
        { SDL_GAMEPAD_BUTTON_DPAD_LEFT, T2_PAD_LEFT, "d-pad left" },
        { SDL_GAMEPAD_BUTTON_DPAD_RIGHT, T2_PAD_RIGHT, "d-pad right" },
        { SDL_GAMEPAD_BUTTON_LEFT_STICK, T2_PAD_L3, "L3" },
        { SDL_GAMEPAD_BUTTON_RIGHT_STICK, T2_PAD_R3, "R3" },
    };
    T2Pad p[2];
    size_t i;

    for (i = 0; i < sizeof(MAP) / sizeof(MAP[0]); i++) {
        uint16_t got = press_one(in, j, MAP[i].button, player);

        CHECK(got == MAP[i].want, "%s, %s: got %04x, want %04x", what, MAP[i].name, got, MAP[i].want);
        printf("  %-28s %-26s -> %04x %s\n", what, MAP[i].name, got, got == MAP[i].want ? "ok" : "WRONG");
    }
    /* the triggers are L2 and R2 past a quarter */
    SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 20000);
    SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 20000);
    pads(in, p);
    CHECK(p[player].buttons == (T2_PAD_L2 | T2_PAD_R2), "%s, triggers: %04x", what, p[player].buttons);
    printf("  %-28s %-26s -> %04x %s\n", what, "triggers: L2 R2", p[player].buttons,
           p[player].buttons == (T2_PAD_L2 | T2_PAD_R2) ? "ok" : "WRONG");
    SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
    SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
    /* the left stick steers past half way, not inside it */
    SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_LEFTX, -30000);
    SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_LEFTY, 30000);
    pads(in, p);
    CHECK(p[player].buttons == (T2_PAD_LEFT | T2_PAD_DOWN), "%s, left stick down-left: %04x", what, p[player].buttons);
    printf("  %-28s %-26s -> %04x %s\n", what, "left stick down-left", p[player].buttons,
           p[player].buttons == (T2_PAD_LEFT | T2_PAD_DOWN) ? "ok" : "WRONG");
    SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_LEFTX, -10000);
    SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_LEFTY, 0);
    pads(in, p);
    CHECK(p[player].buttons == 0, "%s, stick inside half way: %04x", what, p[player].buttons);
    SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_LEFTX, 0);
    pads(in, p);
}

int main(int argc, char **argv)
{
    char path[1024], text[256];
    const char *dir = argc > 1 ? argv[1] : ".";
    T2Config *cfg;
    T2Input *in;
    T2Pad p[2];
    SDL_JoystickID ds5, ds4, xbox, stick;
    SDL_Joystick *j5, *j4, *jx, *js;

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
    }   /* only the virtual controllers */
    if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
        fprintf(stderr, "t2_input_test: SDL: %s\n", SDL_GetError());
        return 1;
    }
    printf("t2_input_test: SDL %d.%d.%d\n", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
    snprintf(path, sizeof(path), "%s/t2_input_test.cfg", dir);
    remove(path);
    cfg = t2_config_load(path);
    in = t2_input_new(cfg);
    CHECK(in != NULL, "t2_input_new");

    /* families from SDL's types and IDs */
    CHECK(t2_input_gamepad_family(SDL_GAMEPAD_TYPE_PS5, 0x054c, 0x0ce6, 0) == INPUT_FAMILY_PLAYSTATION, "DualSense");
    CHECK(t2_input_gamepad_family(SDL_GAMEPAD_TYPE_PS4, 0x054c, 0x09cc, 0) == INPUT_FAMILY_PLAYSTATION, "DualShock 4");
    CHECK(t2_input_gamepad_family(SDL_GAMEPAD_TYPE_XBOXONE, 0x045e, 0x0b12, 0) == INPUT_FAMILY_XBOX, "Xbox Series");
    CHECK(t2_input_gamepad_family(SDL_GAMEPAD_TYPE_STANDARD, 0x28de, 0x1205, 0) == INPUT_FAMILY_STEAM, "Steam Deck");
    CHECK(t2_input_gamepad_family(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO, 0x057e, 0x2009, 0) == INPUT_FAMILY_NINTENDO,
          "Switch Pro");

    /* the keyboard: player 1's psxrecomp keys, with no pad connected */
    key(in, SDL_SCANCODE_X, 1);
    key(in, SDL_SCANCODE_LEFT, 1);
    pads(in, p);
    CHECK(p[0].buttons == (T2_PAD_X | T2_PAD_LEFT), "keys X + Left: %04x", p[0].buttons);
    CHECK(p[1].buttons == 0, "player 2 has no keys: %04x", p[1].buttons);
    key(in, SDL_SCANCODE_RIGHT, 1);
    key(in, SDL_SCANCODE_UP, 1);
    key(in, SDL_SCANCODE_DOWN, 1);
    pads(in, p);
    CHECK(p[0].buttons == (T2_PAD_X | T2_PAD_UP), "SOCD: left+right neutral, up+down up: %04x", p[0].buttons);
    key(in, SDL_SCANCODE_X, 0);
    key(in, SDL_SCANCODE_LEFT, 0);
    key(in, SDL_SCANCODE_RIGHT, 0);
    key(in, SDL_SCANCODE_UP, 0);
    key(in, SDL_SCANCODE_DOWN, 0);
    {
        static const struct {
            SDL_Scancode k;
            uint16_t want;
        } KEYS[] = {
            { SDL_SCANCODE_S, T2_PAD_CIRCLE }, { SDL_SCANCODE_Z, T2_PAD_SQUARE }, { SDL_SCANCODE_A, T2_PAD_TRIANGLE },
            { SDL_SCANCODE_Q, T2_PAD_L1 },     { SDL_SCANCODE_W, T2_PAD_R1 },     { SDL_SCANCODE_E, T2_PAD_L2 },
            { SDL_SCANCODE_R, T2_PAD_R2 },     { SDL_SCANCODE_RETURN, T2_PAD_START },
            { SDL_SCANCODE_RSHIFT, T2_PAD_SELECT },
        };
        size_t i;

        for (i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); i++) {
            key(in, KEYS[i].k, 1);
            pads(in, p);
            CHECK(p[0].buttons == KEYS[i].want, "key %s: %04x, want %04x", SDL_GetScancodeName(KEYS[i].k),
                  p[0].buttons, KEYS[i].want);
            key(in, KEYS[i].k, 0);
        }
    }

    /* three pads and an arcade stick connect: players 1, 2, then 1 again */
    ds5 = attach("DualSense Wireless Controller (virtual)", 0x054c, 0x0ce6, 1);
    pump(in);
    ds4 = attach("DualShock 4 (virtual)", 0x054c, 0x09cc, 1);
    pump(in);
    xbox = attach("Xbox Series X Controller (virtual)", 0x045e, 0x0b12, 1);
    pump(in);
    stick = attach("Arcade stick (virtual)", 0x0f0d, 0x0011, 0);
    pump(in);
    t2_input_describe(in, stdout);
    CHECK(t2_input_device_count(in) == 4, "four devices: %d", t2_input_device_count(in));
    CHECK(SDL_GetGamepadTypeForID(ds5) == SDL_GAMEPAD_TYPE_PS5, "the DualSense is a PS5 pad to SDL: %d",
          SDL_GetGamepadTypeForID(ds5));
    CHECK(SDL_GetGamepadTypeForID(ds4) == SDL_GAMEPAD_TYPE_PS4, "the DualShock 4 is a PS4 pad to SDL: %d",
          SDL_GetGamepadTypeForID(ds4));
    CHECK(SDL_GetGamepadTypeForID(xbox) == SDL_GAMEPAD_TYPE_XBOXONE, "the Xbox pad is an Xbox One pad to SDL: %d",
          SDL_GetGamepadTypeForID(xbox));
    CHECK(!SDL_IsGamepad(stick), "the arcade stick has no gamepad mapping");
    j5 = joy_of(ds5), j4 = joy_of(ds4), jx = joy_of(xbox), js = joy_of(stick);
    t2_input_player_devices(in, 0, text, sizeof(text));
    printf("player 1: %s\n", text);
    CHECK(strstr(text, "DualSense") && strstr(text, "Xbox") && strstr(text, "keyboard"),
          "player 1 has the DualSense, the Xbox pad and the keyboard: %s", text);
    t2_input_player_devices(in, 1, text, sizeof(text));
    printf("player 2: %s\n", text);
    CHECK(strstr(text, "DualShock 4") != NULL, "player 2 has the DualShock 4: %s", text);

    /* every button by position, on each pad, with no setup */
    printf("mapping by position:\n");
    check_by_position(in, "DualSense (054c:0ce6)", j5, 0);
    check_by_position(in, "DualShock 4 (054c:09cc)", j4, 1);
    check_by_position(in, "Xbox Series (045e:0b12)", jx, 0);

    /* the keyboard and two pads of player 1's at once */
    SDL_SetJoystickVirtualButton(j5, SDL_GAMEPAD_BUTTON_SOUTH, true);
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_NORTH, true);
    key(in, SDL_SCANCODE_Z, 1);
    pads(in, p);
    CHECK(p[0].buttons == (T2_PAD_X | T2_PAD_TRIANGLE | T2_PAD_SQUARE), "keyboard and two pads together: %04x",
          p[0].buttons);
    CHECK(p[1].buttons == 0, "player 2 untouched: %04x", p[1].buttons);
    SDL_SetJoystickVirtualButton(j5, SDL_GAMEPAD_BUTTON_SOUTH, false);
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_NORTH, false);
    key(in, SDL_SCANCODE_Z, 0);
    pads(in, p);

    /* the arcade stick: DirectInput order, the hat */
    SDL_SetJoystickVirtualButton(js, 0, true);
    SDL_SetJoystickVirtualButton(js, 9, true);
    SDL_SetJoystickVirtualHat(js, 0, SDL_HAT_RIGHTUP);
    pads(in, p);
    CHECK(p[0].buttons == (T2_PAD_SQUARE | T2_PAD_START | T2_PAD_RIGHT | T2_PAD_UP), "arcade stick: %04x",
          p[0].buttons);
    SDL_SetJoystickVirtualButton(js, 0, false);
    SDL_SetJoystickVirtualButton(js, 9, false);
    SDL_SetJoystickVirtualHat(js, 0, SDL_HAT_CENTERED);
    pads(in, p);
    CHECK(p[0].buttons == 0 && p[1].buttons == 0, "all released: %04x %04x", p[0].buttons, p[1].buttons);

    /* the on-screen controller is player 1's too */
    t2_input_set_touch(in, T2_PAD_CIRCLE | T2_PAD_LEFT);
    SDL_SetJoystickVirtualButton(j5, SDL_GAMEPAD_BUTTON_WEST, true);
    pads(in, p);
    CHECK(p[0].buttons == (T2_PAD_CIRCLE | T2_PAD_LEFT | T2_PAD_SQUARE), "touch and the DualSense: %04x", p[0].buttons);
    SDL_SetJoystickVirtualButton(j5, SDL_GAMEPAD_BUTTON_WEST, false);
    t2_input_set_touch(in, 0);
    pads(in, p);

    /* hot-plug: player 2's DualShock 4 leaves; the next pad to connect takes its place */
    SDL_DetachVirtualJoystick(ds4);
    pump(in);
    CHECK(t2_input_device_count(in) == 3, "three devices after the DualShock 4 left: %d", t2_input_device_count(in));
    t2_input_player_devices(in, 1, text, sizeof(text));
    CHECK(!strcmp(text, "none"), "player 2 has none: %s", text);
    ds4 = attach("DualShock 4 (virtual, again)", 0x054c, 0x09cc, 1);
    pump(in);
    j4 = joy_of(ds4);
    CHECK(press_one(in, j4, SDL_GAMEPAD_BUTTON_EAST, 1) == T2_PAD_CIRCLE, "the DualShock 4 is player 2 again");
    printf("hot-plug: DualShock 4 out and back in, player 2 again: %s\n", failures ? "see failures" : "ok");

    /* swapping the players' devices */
    t2_input_swap_players(in);
    CHECK(press_one(in, j4, SDL_GAMEPAD_BUTTON_NORTH, 0) == T2_PAD_TRIANGLE, "swapped: the DualShock 4 is player 1");
    CHECK(press_one(in, j5, SDL_GAMEPAD_BUTTON_NORTH, 1) == T2_PAD_TRIANGLE, "swapped: the DualSense is player 2");
    t2_input_swap_players(in);

    /* analog mode: the sticks' bytes; a key steers the centred left stick */
    t2_input_set_analog(in, 0, 1);
    SDL_SetJoystickVirtualAxis(j5, SDL_GAMEPAD_AXIS_LEFTX, 32767);
    pads(in, p);
    CHECK(p[0].lx == 0xFF && p[0].ly == 0x80, "analog: left stick right: %02x %02x", p[0].lx, p[0].ly);
    SDL_SetJoystickVirtualAxis(j5, SDL_GAMEPAD_AXIS_LEFTX, 0);
    t2_input_set_analog(in, 0, 0);
    pads(in, p);

    /* remapping: player 1's X onto the east button; the keyboard binding stays */
    SDL_SetJoystickVirtualButton(j5, SDL_GAMEPAD_BUTTON_NORTH, true);   /* held from before */
    pump(in);
    t2_input_remap_begin(in, 0, 14);
    CHECK(t2_input_remap_button(in) == 14, "capturing X");
    t2_input_pads(in, p);
    CHECK(p[0].buttons == 0 && p[1].buttons == 0, "nothing reaches the game while capturing: %04x", p[0].buttons);
    SDL_SetJoystickVirtualButton(j5, SDL_GAMEPAD_BUTTON_NORTH, false);
    pump(in);
    CHECK(t2_input_remap_button(in) == 14, "a release is not a capture");
    SDL_Delay(350);   /* past the gap that keeps the starting press out */
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_EAST, true);
    pump(in);
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_EAST, false);
    pads(in, p);
    CHECK(t2_input_remap_button(in) < 0 && t2_input_remap_take_result(in) == 1, "X captured");
    CHECK(press_one(in, jx, SDL_GAMEPAD_BUTTON_EAST, 0) == (T2_PAD_X | T2_PAD_CIRCLE), "east is X and Circle now");
    CHECK(press_one(in, jx, SDL_GAMEPAD_BUTTON_SOUTH, 0) == 0, "south is nothing now");
    t2_input_binding_text(in, 0, 14, text, sizeof(text));
    printf("X after the remap: %s\n", text);
    CHECK(strstr(text, "X key") && strstr(text, "east"), "X's bindings: %s", text);
    /* the other kinds' bindings stay, the new one goes last */
    CHECK(!strcmp(t2_config_get_or(cfg, "p1.x", ""), "key:X, joy:b1, pad:east"), "saved as text: %s",
          t2_config_get_or(cfg, "p1.x", "(none)"));
    /* Escape cancels and keeps it */
    t2_input_remap_begin(in, 0, 13);
    key(in, SDL_SCANCODE_ESCAPE, 1);
    key(in, SDL_SCANCODE_ESCAPE, 0);
    CHECK(t2_input_remap_button(in) < 0 && t2_input_remap_take_result(in) == 2, "Escape cancels");
    CHECK(press_one(in, j5, SDL_GAMEPAD_BUTTON_EAST, 0) == (T2_PAD_X | T2_PAD_CIRCLE), "Circle kept east");
    /* a new run reads the saved binding; reset brings the defaults back */
    CHECK(t2_config_save(cfg) == 0, "save %s", path);
    {
        T2Config *c2 = t2_config_load(path);
        T2Input *in2;

        t2_input_free(in);
        in2 = t2_input_new(c2);
        in = in2;
        /* the old layer closed its handles: these are the new layer's */
        j5 = joy_of(ds5), j4 = joy_of(ds4), jx = joy_of(xbox), js = joy_of(stick);
        CHECK(t2_input_device_count(in) == 4, "the devices already connected are opened: %d",
              t2_input_device_count(in));
        /* the DualSense came first, so it is player 1's: the remap is player 1's */
        CHECK(press_one(in, j5, SDL_GAMEPAD_BUTTON_EAST, 0) == (T2_PAD_X | T2_PAD_CIRCLE), "the remap survives a restart");
        CHECK(press_any(in, jx, SDL_GAMEPAD_BUTTON_EAST) != 0, "the Xbox pad is someone's");
        t2_input_reset_player(in, 0);
        CHECK(press_one(in, j5, SDL_GAMEPAD_BUTTON_EAST, 0) == T2_PAD_CIRCLE, "reset: east is Circle again");
        CHECK(press_one(in, j5, SDL_GAMEPAD_BUTTON_SOUTH, 0) == T2_PAD_X, "reset: south is X again");
        t2_config_free(cfg);
        cfg = c2;
    }

    /* pinning: "p2.device = <GUID>" puts that pad on player 2 whatever connects first */
    {
        char guid[64];
        SDL_JoystickID x2;

        t2_input_free(in);
        SDL_DetachVirtualJoystick(ds5);
        SDL_DetachVirtualJoystick(ds4);
        SDL_DetachVirtualJoystick(xbox);
        SDL_DetachVirtualJoystick(stick);
        SDL_UpdateJoysticks();
        SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
        x2 = attach("Xbox Series X Controller (virtual)", 0x045e, 0x0b12, 1);
        SDL_GUIDToString(SDL_GetJoystickGUIDForID(x2), guid, sizeof(guid));
        t2_config_set(cfg, "p2.device", guid);
        in = t2_input_new(cfg);
        CHECK(press_one(in, joy_of(x2), SDL_GAMEPAD_BUTTON_SOUTH, 1) == T2_PAD_X, "the pinned pad is player 2's");
        SDL_DetachVirtualJoystick(x2);
        pump(in);
    }

    t2_input_free(in);
    t2_config_free(cfg);
    SDL_Quit();
    printf("t2_input_test: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
