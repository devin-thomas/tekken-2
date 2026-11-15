/* One PlayStation pad's state, as the Tekken 2 input layer hands it to the
 * runtime (t2_input.h). Nothing here depends on SDL.
 *
 * The bits are the PS1 pad word's, active high here (1 = held); the runtime
 * wants the word active low, so the glue inverts it. The same bits are the
 * on-screen controller's (android/.../TouchPadView.java, ios/T2TouchPad.m).
 */
#ifndef T2_PAD_H
#define T2_PAD_H

#include <stdint.h>

enum {
    T2_PAD_SELECT = 0x0001,
    T2_PAD_L3 = 0x0002,
    T2_PAD_R3 = 0x0004,
    T2_PAD_START = 0x0008,
    T2_PAD_UP = 0x0010,
    T2_PAD_RIGHT = 0x0020,
    T2_PAD_DOWN = 0x0040,
    T2_PAD_LEFT = 0x0080,
    T2_PAD_L2 = 0x0100,
    T2_PAD_R2 = 0x0200,
    T2_PAD_L1 = 0x0400,
    T2_PAD_R1 = 0x0800,
    T2_PAD_TRIANGLE = 0x1000,
    T2_PAD_CIRCLE = 0x2000,
    T2_PAD_X = 0x4000,
    T2_PAD_SQUARE = 0x8000,
};

#define T2_PAD_NBUTTONS 16
#define T2_PAD_DIRS (T2_PAD_UP | T2_PAD_RIGHT | T2_PAD_DOWN | T2_PAD_LEFT)
#define T2_PAD_CENTRE 0x80

typedef struct {
    uint16_t buttons;    /* active high */
    uint8_t lx, ly, rx, ry;
} T2Pad;

/* What a person reads for a button bit's index (0-15): "X", never "Cross". */
static inline const char *t2_pad_label(int index)
{
    static const char *const N[T2_PAD_NBUTTONS] = {
        "Select", "L3", "R3", "Start", "Up", "Right", "Down", "Left",
        "L2", "R2", "L1", "R1", "Triangle", "Circle", "X", "Square",
    };

    return index >= 0 && index < T2_PAD_NBUTTONS ? N[index] : "?";
}

/* The name used in t2-input.cfg keys ("p1.x"). */
static inline const char *t2_pad_key(int index)
{
    static const char *const N[T2_PAD_NBUTTONS] = {
        "select", "l3", "r3", "start", "up", "right", "down", "left",
        "l2", "r2", "l1", "r1", "triangle", "circle", "x", "square",
    };

    return index >= 0 && index < T2_PAD_NBUTTONS ? N[index] : "?";
}

#endif
