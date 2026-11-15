/* Tekken 2's input layer (t2_input.h). Adapted from the GGX+ port's
 * sdl/input.c (MIT, Copyright (c) 2026 Lil Gohan). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "t2_input.h"

#define MAX_BIND 8
#define MAX_DEV 16
#define STICK_ON 16384      /* half way */
#define TRIGGER_ON 8192     /* a quarter */
#define CAPTURE_AXIS 20000  /* remapping takes an axis past 60% */
#define CAPTURE_GAP_NS 300000000ull

enum { B_NONE, B_KEY, B_PAD_BUTTON, B_PAD_AXIS, B_JOY_BUTTON, B_JOY_HAT, B_JOY_AXIS };
/* what a remap replaces: a button's bindings of the same class */
enum { C_KEY, C_PAD, C_JOY };

typedef struct {
    uint8_t kind;
    int8_t dir;        /* an axis' sign; a hat's SDL_HAT_* bit */
    int16_t code;      /* scancode, gamepad button or axis, joystick button, hat or axis */
} Bind;

typedef struct {
    SDL_JoystickID id;
    SDL_Gamepad *gp;     /* a gamepad SDL has a mapping for, or */
    SDL_Joystick *joy;   /* a joystick without one (an arcade stick, say) */
    int player;
    InputFamily family;
} Device;

struct T2Input {
    T2Config *cfg;
    Bind bind[2][T2_PAD_NBUTTONS][MAX_BIND];
    int analog[2];
    int socd;            /* 1: hitbox (left+right neutral, up+down up) */
    Device dev[MAX_DEV];
    int ndev;
    bool keys[SDL_SCANCODE_COUNT];
    int keys_from_sdl;
    uint16_t touch;
    int remap_player, remap_button, remap_result;
    uint64_t remap_at;
};

/* the gamepad buttons by position; SDL's own names ("a", "dpup") are read too */
static const struct {
    const char *name;
    SDL_GamepadButton b;
} PAD_NAMES[] = {
    { "south", SDL_GAMEPAD_BUTTON_SOUTH },
    { "east", SDL_GAMEPAD_BUTTON_EAST },
    { "west", SDL_GAMEPAD_BUTTON_WEST },
    { "north", SDL_GAMEPAD_BUTTON_NORTH },
    /* the back buttons, as the Steam Deck and Steam Controller label them */
    { "r4", SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1 },
    { "l4", SDL_GAMEPAD_BUTTON_LEFT_PADDLE1 },
    { "r5", SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2 },
    { "l5", SDL_GAMEPAD_BUTTON_LEFT_PADDLE2 },
};

/* The defaults, by T2_PAD_* bit index. The keys are psxrecomp's own
   (runtime/src/psx_keybinds.c), so a keyboard player's habits carry over. */
static const char *const DEFAULTS[T2_PAD_NBUTTONS] = {
    /* Select */ "key:Right Shift, pad:back, joy:b8",
    /* L3 */ "pad:leftstick, joy:b10",
    /* R3 */ "pad:rightstick, joy:b11",
    /* Start */ "key:Return, pad:start, joy:b9",
    /* Up */ "key:Up, pad:dpup, pad:lefty-, joy:h0.up, joy:a1-",
    /* Right */ "key:Right, pad:dpright, pad:leftx+, joy:h0.right, joy:a0+",
    /* Down */ "key:Down, pad:dpdown, pad:lefty+, joy:h0.down, joy:a1+",
    /* Left */ "key:Left, pad:dpleft, pad:leftx-, joy:h0.left, joy:a0-",
    /* L2 */ "key:E, pad:lefttrigger+, pad:l5, joy:b6",
    /* R2 */ "key:R, pad:righttrigger+, pad:r5, joy:b7",
    /* L1 */ "key:Q, pad:leftshoulder, pad:l4, joy:b4",
    /* R1 */ "key:W, pad:rightshoulder, pad:r4, joy:b5",
    /* Triangle */ "key:A, pad:north, joy:b3",
    /* Circle */ "key:S, pad:east, joy:b2",
    /* X */ "key:X, pad:south, joy:b1",
    /* Square */ "key:Z, pad:west, joy:b0",
};

static const struct {
    const char *name;
    int hat;
} HAT_NAMES[] = { { "up", SDL_HAT_UP }, { "right", SDL_HAT_RIGHT }, { "down", SDL_HAT_DOWN }, { "left", SDL_HAT_LEFT } };

/* ------------------------------------------------------------------ bindings as text */

static int bind_class(const Bind *b)
{
    return b->kind == B_KEY ? C_KEY : (b->kind == B_PAD_BUTTON || b->kind == B_PAD_AXIS) ? C_PAD : C_JOY;
}

static int parse_bind(const char *s, Bind *out)
{
    char *end;
    long n;
    size_t i;

    memset(out, 0, sizeof(*out));
    if (!strncmp(s, "key:", 4)) {
        SDL_Scancode k = SDL_GetScancodeFromName(s + 4);

        if (k == SDL_SCANCODE_UNKNOWN) {
            return -1;
        }
        out->kind = B_KEY;
        out->code = (int16_t)k;
        return 0;
    }
    if (!strncmp(s, "pad:", 4)) {
        const char *p = s + 4;
        size_t len = strlen(p);
        char axis[32];

        for (i = 0; i < sizeof(PAD_NAMES) / sizeof(PAD_NAMES[0]); i++) {
            if (!strcmp(p, PAD_NAMES[i].name)) {
                out->kind = B_PAD_BUTTON;
                out->code = (int16_t)PAD_NAMES[i].b;
                return 0;
            }
        }
        if (len > 1 && len < sizeof(axis) && (p[len - 1] == '+' || p[len - 1] == '-')) {
            SDL_GamepadAxis a;

            memcpy(axis, p, len - 1);
            axis[len - 1] = 0;
            a = SDL_GetGamepadAxisFromString(axis);
            if (a == SDL_GAMEPAD_AXIS_INVALID) {
                return -1;
            }
            out->kind = B_PAD_AXIS;
            out->code = (int16_t)a;
            out->dir = p[len - 1] == '+' ? 1 : -1;
            return 0;
        }
        {
            SDL_GamepadButton b = SDL_GetGamepadButtonFromString(p);

            if (b == SDL_GAMEPAD_BUTTON_INVALID) {
                return -1;
            }
            out->kind = B_PAD_BUTTON;
            out->code = (int16_t)b;
            return 0;
        }
    }
    if (!strncmp(s, "joy:", 4) && (s[4] == 'b' || s[4] == 'h' || s[4] == 'a')) {
        n = strtol(s + 5, &end, 10);
        if (end == s + 5 || n < 0 || n > 255) {
            return -1;
        }
        out->code = (int16_t)n;
        if (s[4] == 'b' && !*end) {
            out->kind = B_JOY_BUTTON;
            return 0;
        }
        if (s[4] == 'a' && (end[0] == '+' || end[0] == '-') && !end[1]) {
            out->kind = B_JOY_AXIS;
            out->dir = end[0] == '+' ? 1 : -1;
            return 0;
        }
        if (s[4] == 'h' && end[0] == '.') {
            for (i = 0; i < 4; i++) {
                if (!strcmp(end + 1, HAT_NAMES[i].name)) {
                    out->kind = B_JOY_HAT;
                    out->dir = (int8_t)HAT_NAMES[i].hat;
                    return 0;
                }
            }
        }
    }
    return -1;
}

static void format_bind(const Bind *b, char *out, size_t cap)
{
    size_t i;

    switch (b->kind) {
    case B_KEY:
        snprintf(out, cap, "key:%s", SDL_GetScancodeName((SDL_Scancode)b->code));
        return;
    case B_PAD_BUTTON:
        for (i = 0; i < sizeof(PAD_NAMES) / sizeof(PAD_NAMES[0]); i++) {
            if (PAD_NAMES[i].b == (SDL_GamepadButton)b->code) {
                snprintf(out, cap, "pad:%s", PAD_NAMES[i].name);
                return;
            }
        }
        snprintf(out, cap, "pad:%s", SDL_GetGamepadStringForButton((SDL_GamepadButton)b->code));
        return;
    case B_PAD_AXIS:
        snprintf(out, cap, "pad:%s%c", SDL_GetGamepadStringForAxis((SDL_GamepadAxis)b->code), b->dir > 0 ? '+' : '-');
        return;
    case B_JOY_BUTTON:
        snprintf(out, cap, "joy:b%d", b->code);
        return;
    case B_JOY_AXIS:
        snprintf(out, cap, "joy:a%d%c", b->code, b->dir > 0 ? '+' : '-');
        return;
    case B_JOY_HAT:
        for (i = 0; i < 4; i++) {
            if (HAT_NAMES[i].hat == b->dir) {
                snprintf(out, cap, "joy:h%d.%s", b->code, HAT_NAMES[i].name);
                return;
            }
        }
        break;
    }
    snprintf(out, cap, "none");
}

/* What a person reads for one binding. */
static void describe_bind(const Bind *b, char *out, size_t cap)
{
    static const char *const POS[] = { "south", "east", "west", "north" };

    switch (b->kind) {
    case B_KEY:
        snprintf(out, cap, "%s key", SDL_GetScancodeName((SDL_Scancode)b->code));
        return;
    case B_PAD_BUTTON:
        switch (b->code) {
        case SDL_GAMEPAD_BUTTON_SOUTH:
        case SDL_GAMEPAD_BUTTON_EAST:
        case SDL_GAMEPAD_BUTTON_WEST:
        case SDL_GAMEPAD_BUTTON_NORTH:
            snprintf(out, cap, "%s button", POS[b->code - SDL_GAMEPAD_BUTTON_SOUTH]);
            return;
        case SDL_GAMEPAD_BUTTON_BACK: snprintf(out, cap, "Create/Back"); return;
        case SDL_GAMEPAD_BUTTON_START: snprintf(out, cap, "Options/Start"); return;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: snprintf(out, cap, "pad L1"); return;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: snprintf(out, cap, "pad R1"); return;
        case SDL_GAMEPAD_BUTTON_LEFT_STICK: snprintf(out, cap, "pad L3"); return;
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK: snprintf(out, cap, "pad R3"); return;
        case SDL_GAMEPAD_BUTTON_DPAD_UP: snprintf(out, cap, "d-pad up"); return;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: snprintf(out, cap, "d-pad down"); return;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: snprintf(out, cap, "d-pad left"); return;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: snprintf(out, cap, "d-pad right"); return;
        default: break;
        }
        break;
    case B_PAD_AXIS:
        switch (b->code) {
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: snprintf(out, cap, "pad L2"); return;
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: snprintf(out, cap, "pad R2"); return;
        case SDL_GAMEPAD_AXIS_LEFTX: snprintf(out, cap, "left stick %s", b->dir > 0 ? "right" : "left"); return;
        case SDL_GAMEPAD_AXIS_LEFTY: snprintf(out, cap, "left stick %s", b->dir > 0 ? "down" : "up"); return;
        case SDL_GAMEPAD_AXIS_RIGHTX: snprintf(out, cap, "right stick %s", b->dir > 0 ? "right" : "left"); return;
        case SDL_GAMEPAD_AXIS_RIGHTY: snprintf(out, cap, "right stick %s", b->dir > 0 ? "down" : "up"); return;
        default: break;
        }
        break;
    case B_JOY_BUTTON:
        snprintf(out, cap, "stick button %d", b->code + 1);
        return;
    case B_JOY_HAT:
    case B_JOY_AXIS:
        snprintf(out, cap, "stick %s", b->dir > 0 && b->kind == B_JOY_AXIS ? "+" : "direction");
        return;
    }
    format_bind(b, out, cap);
}

static void config_key(int player, int button, char *out, size_t cap)
{
    snprintf(out, cap, "p%d.%s", player + 1, t2_pad_key(button));
}

/* "key:Z, pad:south" into a button's bindings; bad entries are reported */
static void load_binds(Bind *dst, const char *text, const char *key)
{
    char buf[512], *p = buf;
    int n = 0;

    memset(dst, 0, sizeof(Bind) * MAX_BIND);
    snprintf(buf, sizeof(buf), "%s", text);
    while (*p) {
        char *comma = strchr(p, ','), *e;

        if (comma) {
            *comma = 0;
        }
        while (*p == ' ') {
            p++;
        }
        e = p + strlen(p);
        while (e > p && e[-1] == ' ') {
            *--e = 0;
        }
        if (*p && strcmp(p, "none")) {
            if (n < MAX_BIND && parse_bind(p, &dst[n]) == 0) {
                n++;
            } else {
                fprintf(stderr, "t2: %s: \"%s\" is not a binding (key:<name>, pad:<button or axis+->, "
                                "joy:b<n> / joy:h<n>.up / joy:a<n>+): ignored\n", key, p);
            }
        }
        if (!comma) {
            break;
        }
        p = comma + 1;
    }
}

static void load_default(T2Input *in, int p, int b, const char *key)
{
    load_binds(in->bind[p][b], DEFAULTS[b], key);
    if (p == 1) {
        /* player 2 has no keys by default: the keyboard is player 1's */
        Bind *l = in->bind[p][b];
        int i, n = 0;

        for (i = 0; i < MAX_BIND; i++) {
            if (l[i].kind != B_KEY) {
                l[n++] = l[i];
            }
        }
        memset(l + n, 0, sizeof(Bind) * (size_t)(MAX_BIND - n));
    }
}

static void save_binds(T2Input *in, int player, int button)
{
    char key[32], val[512] = "", one[64];
    int i;

    if (!in->cfg) {
        return;
    }
    for (i = 0; i < MAX_BIND; i++) {
        if (in->bind[player][button][i].kind == B_NONE) {
            continue;
        }
        format_bind(&in->bind[player][button][i], one, sizeof(one));
        if (val[0]) {
            strncat(val, ", ", sizeof(val) - strlen(val) - 1);
        }
        strncat(val, one, sizeof(val) - strlen(val) - 1);
    }
    config_key(player, button, key, sizeof(key));
    t2_config_set(in->cfg, key, val[0] ? val : "none");
}

/* ------------------------------------------------------------------ devices */

int t2_input_on_steam_deck(void)
{
    const char *e = SDL_getenv("SteamDeck");

    if (e && !strcmp(e, "1")) {
        return 1;
    }
#if defined(__linux__) && !defined(__ANDROID__)
    {
        FILE *f = fopen("/sys/devices/virtual/dmi/id/product_name", "r");
        char name[64] = "";

        if (f) {
            if (!fgets(name, sizeof(name), f)) {
                name[0] = 0;
            }
            fclose(f);
        }
        return !strncmp(name, "Jupiter", 7) || !strncmp(name, "Galileo", 7);
    }
#else
    return 0;
#endif
}

InputFamily t2_input_gamepad_family(SDL_GamepadType type, Uint16 vendor, Uint16 product, int on_deck)
{
    const char *name = SDL_GetGamepadStringForType(type);

    /* newer SDLs have a Steam type; by name, so older ones build too */
    if (name && !strcmp(name, "steam")) {
        return INPUT_FAMILY_STEAM;
    }
    if (vendor == 0x28de) {
        /* Valve: the Steam Deck's controls, the Steam Controller (wired, its dongle, Bluetooth) */
        if (product == 0x1205 || product == 0x1102 || product == 0x1142 || product == 0x1106) {
            return INPUT_FAMILY_STEAM;
        }
        /* Steam Input's virtual gamepad on a Deck: the Deck's own controls, shown as an Xbox pad */
        if (product == 0x11ff && on_deck && type != SDL_GAMEPAD_TYPE_PS3 && type != SDL_GAMEPAD_TYPE_PS4 &&
            type != SDL_GAMEPAD_TYPE_PS5) {
            return INPUT_FAMILY_STEAM;
        }
    }
    switch (type) {
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5:
        return INPUT_FAMILY_PLAYSTATION;
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE:
        return INPUT_FAMILY_XBOX;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return INPUT_FAMILY_NINTENDO;
    default:
        return INPUT_FAMILY_OTHER;
    }
}

static Device *find_dev(T2Input *in, SDL_JoystickID id)
{
    int i;

    for (i = 0; i < in->ndev; i++) {
        if (in->dev[i].id == id) {
            return &in->dev[i];
        }
    }
    return NULL;
}

static int pick_player(T2Input *in, SDL_JoystickID id)
{
    char guid[64], key[32];
    int p, i, used[2] = { 0, 0 };

    SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), guid, sizeof(guid));
    for (p = 0; p < 2; p++) {
        snprintf(key, sizeof(key), "p%d.device", p + 1);
        if (!SDL_strcasecmp(t2_config_get_or(in->cfg, key, ""), guid)) {
            return p;
        }
    }
    for (i = 0; i < in->ndev; i++) {
        used[in->dev[i].player] = 1;
    }
    return !used[0] ? 0 : !used[1] ? 1 : 0;
}

static int s_only_virtual;

void t2_input_only_virtual(int on)
{
    s_only_virtual = on != 0;
}

static void add_device(T2Input *in, SDL_JoystickID id, int gamepad)
{
    Device *d;

    if (find_dev(in, id) || in->ndev >= MAX_DEV || (s_only_virtual && !SDL_IsJoystickVirtual(id))) {
        return;
    }
    d = &in->dev[in->ndev];
    memset(d, 0, sizeof(*d));
    if (gamepad) {
        d->gp = SDL_OpenGamepad(id);
    } else {
        d->joy = SDL_OpenJoystick(id);
    }
    if (!d->gp && !d->joy) {
        fprintf(stderr, "t2: cannot open a controller: %s\n", SDL_GetError());
        return;
    }
    d->id = id;
    d->player = pick_player(in, id);
    d->family = gamepad ? t2_input_gamepad_family(SDL_GetGamepadType(d->gp), SDL_GetGamepadVendor(d->gp),
                                                  SDL_GetGamepadProduct(d->gp), t2_input_on_steam_deck())
                        : INPUT_FAMILY_OTHER;
    in->ndev++;
    fprintf(stderr, "t2: player %d: %s (%s, %s, %04x:%04x)\n", d->player + 1,
            d->gp ? SDL_GetGamepadName(d->gp) : SDL_GetJoystickName(d->joy), d->gp ? "gamepad" : "joystick",
            input_family_name(d->family), SDL_GetJoystickVendorForID(id), SDL_GetJoystickProductForID(id));
}

static void remove_device(T2Input *in, SDL_JoystickID id)
{
    Device *d = find_dev(in, id);

    if (!d) {
        return;
    }
    fprintf(stderr, "t2: player %d: %s disconnected\n", d->player + 1,
            d->gp ? SDL_GetGamepadName(d->gp) : SDL_GetJoystickName(d->joy));
    if (d->gp) {
        SDL_CloseGamepad(d->gp);
    } else {
        SDL_CloseJoystick(d->joy);
    }
    *d = in->dev[--in->ndev];
}

/* ------------------------------------------------------------------ the layer */

T2Input *t2_input_new(T2Config *cfg)
{
    T2Input *in = calloc(1, sizeof(T2Input));
    SDL_JoystickID *ids;
    int p, b, n, i;

    if (!in) {
        return NULL;
    }
    in->cfg = cfg;
    in->remap_player = -1;
    in->remap_button = -1;
    for (p = 0; p < 2; p++) {
        char key[32];

        for (b = 0; b < T2_PAD_NBUTTONS; b++) {
            const char *v;

            config_key(p, b, key, sizeof(key));
            v = t2_config_get(cfg, key);
            if (v) {
                load_binds(in->bind[p][b], v, key);
            } else {
                load_default(in, p, b, key);
            }
        }
        snprintf(key, sizeof(key), "p%d.analog", p + 1);
        in->analog[p] = !strcmp(t2_config_get_or(cfg, key, "off"), "on");
    }
    in->socd = strcmp(t2_config_get_or(cfg, "socd", "hitbox"), "off") != 0;
    /* the devices already there: their "added" events may have gone by */
    ids = SDL_GetGamepads(&n);
    for (i = 0; ids && i < n; i++) {
        add_device(in, ids[i], 1);
    }
    SDL_free(ids);
    ids = SDL_GetJoysticks(&n);
    for (i = 0; ids && i < n; i++) {
        if (!SDL_IsGamepad(ids[i])) {
            add_device(in, ids[i], 0);
        }
    }
    SDL_free(ids);
    return in;
}

void t2_input_free(T2Input *in)
{
    if (!in) {
        return;
    }
    while (in->ndev) {
        remove_device(in, in->dev[0].id);
    }
    free(in);
}

void t2_input_keys_from_sdl(T2Input *in, int on)
{
    in->keys_from_sdl = on != 0;
}

void t2_input_set_touch(T2Input *in, uint16_t held)
{
    in->touch = held;
}

static void remap_done(T2Input *in, int result)
{
    in->remap_player = -1;
    in->remap_button = -1;
    in->remap_result = result;
}

/* A remap capture: b replaces the current button's bindings of its class. */
static void remap_take(T2Input *in, const Bind *b)
{
    Bind *list = in->bind[in->remap_player][in->remap_button];
    Bind keep[MAX_BIND];
    int i, n = 0;
    uint64_t now = SDL_GetTicksNS();

    if (now - in->remap_at < CAPTURE_GAP_NS) {
        return;   /* the press that started it, or a stick still coming back */
    }
    for (i = 0; i < MAX_BIND; i++) {
        if (list[i].kind != B_NONE && bind_class(&list[i]) != bind_class(b)) {
            keep[n++] = list[i];
        }
    }
    keep[n < MAX_BIND ? n++ : MAX_BIND - 1] = *b;
    memset(list, 0, sizeof(Bind) * MAX_BIND);
    memcpy(list, keep, sizeof(Bind) * (size_t)n);
    save_binds(in, in->remap_player, in->remap_button);
    {
        char one[64];

        format_bind(b, one, sizeof(one));
        fprintf(stderr, "t2: player %d's %s is now %s\n", in->remap_player + 1, t2_pad_label(in->remap_button), one);
    }
    remap_done(in, 1);
}

int t2_input_event(T2Input *in, const SDL_Event *e)
{
    Device *d;
    Bind b;

    memset(&b, 0, sizeof(b));
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (e->key.scancode < SDL_SCANCODE_COUNT) {
            in->keys[e->key.scancode] = e->type == SDL_EVENT_KEY_DOWN;
        }
        if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat && in->remap_player >= 0) {
            if (e->key.scancode == SDL_SCANCODE_ESCAPE) {
                remap_done(in, 2);
            } else {
                b.kind = B_KEY;
                b.code = (int16_t)e->key.scancode;
                remap_take(in, &b);
            }
            return 1;
        }
        return 0;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        memset(in->keys, 0, sizeof(in->keys));
        return 0;
    case SDL_EVENT_GAMEPAD_ADDED:
        add_device(in, e->gdevice.which, 1);
        return 0;
    case SDL_EVENT_JOYSTICK_ADDED:
        if (!SDL_IsGamepad(e->jdevice.which)) {
            add_device(in, e->jdevice.which, 0);
        }
        return 0;
    case SDL_EVENT_GAMEPAD_REMOVED:
    case SDL_EVENT_JOYSTICK_REMOVED:
        remove_device(in, e->type == SDL_EVENT_GAMEPAD_REMOVED ? e->gdevice.which : e->jdevice.which);
        return 0;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        if ((d = find_dev(in, e->gbutton.which)) != NULL && d->gp && in->remap_player >= 0) {
            b.kind = B_PAD_BUTTON;
            b.code = (int16_t)e->gbutton.button;
            remap_take(in, &b);
            return 1;
        }
        return 0;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if ((d = find_dev(in, e->gaxis.which)) != NULL && d->gp && in->remap_player >= 0 &&
            (e->gaxis.value > CAPTURE_AXIS || e->gaxis.value < -CAPTURE_AXIS)) {
            b.kind = B_PAD_AXIS;
            b.code = (int16_t)e->gaxis.axis;
            b.dir = e->gaxis.value > 0 ? 1 : -1;
            remap_take(in, &b);
            return 1;
        }
        return 0;
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_HAT_MOTION:
    case SDL_EVENT_JOYSTICK_AXIS_MOTION:
        /* the same events come for a gamepad's joystick: only plain joysticks count here */
        d = find_dev(in, e->type == SDL_EVENT_JOYSTICK_BUTTON_DOWN ? e->jbutton.which
                         : e->type == SDL_EVENT_JOYSTICK_HAT_MOTION ? e->jhat.which : e->jaxis.which);
        if (!d || !d->joy || in->remap_player < 0) {
            return 0;
        }
        if (e->type == SDL_EVENT_JOYSTICK_BUTTON_DOWN) {
            b.kind = B_JOY_BUTTON;
            b.code = (int16_t)e->jbutton.button;
        } else if (e->type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
            int h = e->jhat.value;

            if (h == SDL_HAT_CENTERED) {
                return 0;
            }
            b.kind = B_JOY_HAT;
            b.code = (int16_t)e->jhat.hat;
            b.dir = (int8_t)((h & SDL_HAT_UP) ? SDL_HAT_UP : (h & SDL_HAT_DOWN) ? SDL_HAT_DOWN
                             : (h & SDL_HAT_LEFT) ? SDL_HAT_LEFT : SDL_HAT_RIGHT);
        } else {
            if (e->jaxis.value <= CAPTURE_AXIS && e->jaxis.value >= -CAPTURE_AXIS) {
                return 0;
            }
            b.kind = B_JOY_AXIS;
            b.code = (int16_t)e->jaxis.axis;
            b.dir = e->jaxis.value > 0 ? 1 : -1;
        }
        remap_take(in, &b);
        return 1;
    default:
        return 0;
    }
}

static int key_down(const T2Input *in, int sc)
{
    if (in->keys_from_sdl) {
        int n = 0;
        const bool *k = SDL_GetKeyboardState(&n);

        return k && sc < n && k[sc];
    }
    return in->keys[sc];
}

static int bind_on(const T2Input *in, int player, const Bind *b)
{
    int i;

    if (b->kind == B_KEY) {
        return key_down(in, b->code);
    }
    for (i = 0; i < in->ndev; i++) {
        const Device *d = &in->dev[i];

        if (d->player != player) {
            continue;
        }
        switch (b->kind) {
        case B_PAD_BUTTON:
            if (d->gp && SDL_GetGamepadButton(d->gp, (SDL_GamepadButton)b->code)) {
                return 1;
            }
            break;
        case B_PAD_AXIS:
            if (d->gp) {
                int v = SDL_GetGamepadAxis(d->gp, (SDL_GamepadAxis)b->code) * b->dir;
                int on = b->code == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || b->code == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER
                             ? TRIGGER_ON : STICK_ON;

                if (v > on) {
                    return 1;
                }
            }
            break;
        case B_JOY_BUTTON:
            if (d->joy && SDL_GetJoystickButton(d->joy, b->code)) {
                return 1;
            }
            break;
        case B_JOY_HAT:
            if (d->joy && (SDL_GetJoystickHat(d->joy, b->code) & b->dir)) {
                return 1;
            }
            break;
        case B_JOY_AXIS:
            if (d->joy && SDL_GetJoystickAxis(d->joy, b->code) * b->dir > STICK_ON) {
                return 1;
            }
            break;
        }
    }
    return 0;
}

static uint8_t axis_byte(Sint16 v)
{
    return (uint8_t)(((int)v + 32768) >> 8);
}

static void read_pads(T2Input *in, T2Pad pads[2], int clean)
{
    int p, b, i;

    for (p = 0; p < 2; p++) {
        T2Pad *o = &pads[p];
        const Device *stick = NULL;

        memset(o, 0, sizeof(*o));
        o->lx = o->ly = o->rx = o->ry = T2_PAD_CENTRE;
        if (clean && in->remap_player >= 0) {
            continue;
        }
        for (b = 0; b < T2_PAD_NBUTTONS; b++) {
            for (i = 0; i < MAX_BIND && in->bind[p][b][i].kind != B_NONE; i++) {
                if (bind_on(in, p, &in->bind[p][b][i])) {
                    o->buttons |= (uint16_t)(1u << b);
                    break;
                }
            }
        }
        if (p == 0) {
            o->buttons |= in->touch;
        }
        if (clean && in->socd) {
            if ((o->buttons & (T2_PAD_LEFT | T2_PAD_RIGHT)) == (T2_PAD_LEFT | T2_PAD_RIGHT)) {
                o->buttons &= (uint16_t)~(T2_PAD_LEFT | T2_PAD_RIGHT);
            }
            if ((o->buttons & (T2_PAD_UP | T2_PAD_DOWN)) == (T2_PAD_UP | T2_PAD_DOWN)) {
                o->buttons &= (uint16_t)~T2_PAD_DOWN;
            }
        }
        if (!in->analog[p]) {
            continue;
        }
        for (i = 0; i < in->ndev && !stick; i++) {
            if (in->dev[i].player == p) {
                stick = &in->dev[i];
            }
        }
        if (stick && stick->gp) {
            o->lx = axis_byte(SDL_GetGamepadAxis(stick->gp, SDL_GAMEPAD_AXIS_LEFTX));
            o->ly = axis_byte(SDL_GetGamepadAxis(stick->gp, SDL_GAMEPAD_AXIS_LEFTY));
            o->rx = axis_byte(SDL_GetGamepadAxis(stick->gp, SDL_GAMEPAD_AXIS_RIGHTX));
            o->ry = axis_byte(SDL_GetGamepadAxis(stick->gp, SDL_GAMEPAD_AXIS_RIGHTY));
        } else if (stick && SDL_GetNumJoystickAxes(stick->joy) >= 2) {
            o->lx = axis_byte(SDL_GetJoystickAxis(stick->joy, 0));
            o->ly = axis_byte(SDL_GetJoystickAxis(stick->joy, 1));
        }
        /* keys, d-pads and hats steer the left stick while it is centred */
        if (o->lx > 0x60 && o->lx < 0xA0 && o->ly > 0x60 && o->ly < 0xA0) {
            if (o->buttons & T2_PAD_LEFT) {
                o->lx = 0x00;
            } else if (o->buttons & T2_PAD_RIGHT) {
                o->lx = 0xFF;
            }
            if (o->buttons & T2_PAD_UP) {
                o->ly = 0x00;
            } else if (o->buttons & T2_PAD_DOWN) {
                o->ly = 0xFF;
            }
        }
    }
}

void t2_input_pads(T2Input *in, T2Pad pads[2])
{
    read_pads(in, pads, 1);
}

void t2_input_raw(T2Input *in, T2Pad pads[2])
{
    read_pads(in, pads, 0);
}

int t2_input_analog(const T2Input *in, int player)
{
    return in->analog[player & 1];
}

void t2_input_set_analog(T2Input *in, int player, int on)
{
    char key[32];

    in->analog[player & 1] = on != 0;
    snprintf(key, sizeof(key), "p%d.analog", (player & 1) + 1);
    if (in->cfg) {
        t2_config_set(in->cfg, key, on ? "on" : "off");
    }
}

void t2_input_swap_players(T2Input *in)
{
    int i;

    for (i = 0; i < in->ndev; i++) {
        in->dev[i].player ^= 1;
    }
    t2_input_describe(in, stderr);
}

void t2_input_remap_begin(T2Input *in, int player, int button)
{
    if (button < 0 || button >= T2_PAD_NBUTTONS) {
        return;
    }
    in->remap_player = player & 1;
    in->remap_button = button;
    in->remap_result = 0;
    in->remap_at = SDL_GetTicksNS();
}

void t2_input_remap_cancel(T2Input *in)
{
    if (in->remap_player >= 0) {
        remap_done(in, 2);
    }
}

int t2_input_remap_button(const T2Input *in)
{
    return in->remap_player >= 0 ? in->remap_button : -1;
}

int t2_input_remap_take_result(T2Input *in)
{
    int r = in->remap_result;

    in->remap_result = 0;
    return r;
}

void t2_input_reset_player(T2Input *in, int player)
{
    int b;
    char key[32];

    player &= 1;
    for (b = 0; b < T2_PAD_NBUTTONS; b++) {
        config_key(player, b, key, sizeof(key));
        load_default(in, player, b, key);
        save_binds(in, player, b);
    }
}

void t2_input_binding_text(const T2Input *in, int player, int button, char *out, size_t cap)
{
    const Bind *list = in->bind[player & 1][button];
    char one[48];
    size_t len = 0;
    int i, shown = 0;

    out[0] = 0;
    for (i = 0; i < MAX_BIND && list[i].kind != B_NONE; i++) {
        /* the stick halves that copy the d-pad are not news to anyone */
        if (list[i].kind == B_JOY_HAT || list[i].kind == B_JOY_AXIS ||
            (list[i].kind == B_PAD_AXIS && list[i].code != SDL_GAMEPAD_AXIS_LEFT_TRIGGER &&
             list[i].code != SDL_GAMEPAD_AXIS_RIGHT_TRIGGER && shown)) {
            continue;
        }
        if (list[i].kind == B_PAD_BUTTON && list[i].code >= SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1 &&
            list[i].code <= SDL_GAMEPAD_BUTTON_LEFT_PADDLE2) {
            continue;
        }
        describe_bind(&list[i], one, sizeof(one));
        len += (size_t)snprintf(out + len, len < cap ? cap - len : 0, "%s%s", shown ? ", " : "", one);
        shown++;
        if (len >= cap) {
            break;
        }
    }
    if (!shown) {
        snprintf(out, cap, "not set");
    }
}

void t2_input_player_devices(const T2Input *in, int player, char *out, size_t cap)
{
    size_t len = 0;
    int i, n = 0;

    out[0] = 0;
    player &= 1;
    for (i = 0; i < in->ndev; i++) {
        const Device *d = &in->dev[i];

        if (d->player != player) {
            continue;
        }
        len += (size_t)snprintf(out + len, len < cap ? cap - len : 0, "%s%s", n ? ", " : "",
                                d->gp ? SDL_GetGamepadName(d->gp) : SDL_GetJoystickName(d->joy));
        n++;
        if (len >= cap) {
            return;
        }
    }
    if (player == 0) {
        snprintf(out + len, len < cap ? cap - len : 0, "%skeyboard", n ? ", " : "");
    } else if (!n) {
        snprintf(out, cap, "none");
    }
}

int t2_input_device_count(const T2Input *in)
{
    return in->ndev;
}

void t2_input_describe(const T2Input *in, FILE *out)
{
    int i;

    if (!in->ndev) {
        fprintf(out, "t2: no controllers: player 1 is the keyboard\n");
    }
    for (i = 0; i < in->ndev; i++) {
        const Device *d = &in->dev[i];
        char guid[64];

        SDL_GUIDToString(SDL_GetJoystickGUIDForID(d->id), guid, sizeof(guid));
        fprintf(out, "t2: player %d: %s (%s, %s, %04x:%04x, GUID %s)\n", d->player + 1,
                d->gp ? SDL_GetGamepadName(d->gp) : SDL_GetJoystickName(d->joy), d->gp ? "gamepad" : "joystick",
                input_family_name(d->family), SDL_GetJoystickVendorForID(d->id), SDL_GetJoystickProductForID(d->id),
                guid);
    }
}
