/* The on-screen controller's link to the native side (t2_host.c), for
 * the iOS overlay (ios/T2TouchPad.m). Android's TouchPadView reaches the
 * same functions through JNI (nativeOverlayState, nativeOpenSettings) and
 * sends its held buttons through patch 0005's nativeSetHeld. */
#ifndef T2_TOUCH_H
#define T2_TOUCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The held buttons, T2_PAD_* bits (src/input/t2_pad.h), active high. */
void t2_touch_set_held(uint32_t held);
/* The gear button: opens the settings panel at the next tick. */
void t2_touch_open_settings(void);
/* Bit 0: show the controls; bits 8-15: size in percent; bits 16-23:
   opacity in percent; bit 24: the settings panel is open; bit 25: hidden
   only because Auto saw a controller (the platform may show them anyway
   when it sees no gamepad itself). */
int t2_touch_overlay_state(void);

#ifdef __cplusplus
}
#endif
#endif
