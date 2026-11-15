/* A controller's family (PlayStation, Xbox, Steam, Nintendo, other), from
 * the GGX+ port's input/family.h (MIT, Copyright (c) 2026 Lil Gohan). The
 * input layer (t2_input.h) sets it per device and logs
 * it; the settings panel names it. No SDL here.
 */
#ifndef T2_FAMILY_H
#define T2_FAMILY_H

#include <string.h>

typedef enum {
    INPUT_FAMILY_PLAYSTATION,   /* DualShock, DualShock 4, DualSense */
    INPUT_FAMILY_XBOX,          /* Xbox 360 / One / Series and other XInput pads */
    INPUT_FAMILY_STEAM,         /* Steam Deck, Steam Controller */
    INPUT_FAMILY_OTHER,         /* other gamepads, joysticks and arcade sticks */
    INPUT_FAMILY_KEYBOARD,
    INPUT_FAMILY_NINTENDO,      /* Switch Pro, Joy-Con */
    INPUT_FAMILY_COUNT
} InputFamily;

static inline const char *input_family_name(InputFamily f)
{
    static const char *const N[] = { "playstation", "xbox", "steam", "other", "keyboard", "nintendo" };

    return (unsigned)f < INPUT_FAMILY_COUNT ? N[f] : "other";
}

/* -1 if name isn't one */
static inline int input_family_parse(const char *name)
{
    int f;

    for (f = 0; f < INPUT_FAMILY_COUNT; f++) {
        if (!strcmp(name, input_family_name((InputFamily)f))) {
            return f;
        }
    }
    return -1;
}

#endif
