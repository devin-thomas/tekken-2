/* Tekken 2's input layer: SDL3 keyboards, gamepads and joysticks (arcade
 * sticks) and the on-screen controller, to the PlayStation's two pads.
 *
 * Adapted from the GGX+ port's sdl/input.c and input.h (MIT, Copyright (c) 2026 Lil Gohan). Kept:
 * bindings as text, every device open at once, players assigned as devices
 * connect, pinning a device to a player, SOCD, the analog switch and the
 * device log. Dropped: GGX's macros, confirm swap and prompt letters, which
 * read Guilty Gear's own state. Added: a capture of one button at a time
 * for the settings panel, and the on-screen controller as a device.
 *
 * - Players. Each player has bindings for the sixteen pad buttons: keys,
 *   which belong to that player, and gamepad or joystick inputs, which apply
 *   to the devices assigned to that player. The keyboard and the on-screen
 *   controller are player 1's. A device goes to player 1 when it connects,
 *   then player 2; a third and later go to player 1 too. "pN.device = <SDL
 *   GUID>" pins one; t2_input_swap_players swaps the two players' devices.
 * - Gamepads map by position, so a DualSense, a DualShock 4, an Xbox pad, a
 *   Switch Pro pad and the Steam Deck all map the same way with no setup:
 *   south X, east Circle, west Square, north Triangle, the shoulders L1 R1,
 *   the triggers L2 R2, the d-pad and the left stick (past half way) the
 *   directions, back (Create, Share, View) Select, start (Options, Menu)
 *   Start, the stick clicks L3 R3; the Deck's back buttons L4 R4 L5 R5 are
 *   L1 R1 L2 R2. A joystick SDL has no gamepad mapping for (many arcade
 *   sticks) uses the DirectInput order PlayStation adapters use: buttons 0-9
 *   Square, X, Circle, Triangle, L1, R1, L2, R2, Select, Start; the first hat
 *   and the first two axes are the directions.
 * - The keyboard keeps psxrecomp's keys: arrows, X key X, S Circle, Z
 *   Square, A Triangle, Q W E R L1 R1 L2 R2, Return Start, Right Shift
 *   Select.
 * - Analog: each pad is digital unless "pN.analog = on".
 * - SOCD: "socd = hitbox" (the default) makes left+right neutral and up+down
 *   up; "socd = off" passes them on.
 * - Remapping: t2_input_remap_begin takes the next key, gamepad button or
 *   axis, or joystick button, hat or axis for one pad button, replacing that
 *   button's binding of the same kind (keyboard, gamepad or joystick).
 *   Escape cancels. The bindings are kept in the config as
 *   "p1.x = key:X, pad:south, joy:b1" (the caller saves it).
 */
#ifndef T2_INPUT_H
#define T2_INPUT_H

#include <stdio.h>

#include <SDL3/SDL.h>

#include "family.h"
#include "t2_config.h"
#include "t2_pad.h"

typedef struct T2Input T2Input;

/* The bindings and settings come from cfg (kept; may be NULL for the
   defaults). SDL must be initialised with SDL_INIT_GAMEPAD. Devices
   already connected are opened. */
T2Input *t2_input_new(T2Config *cfg);
void t2_input_free(T2Input *in);

/* Every SDL event goes through here. Returns 1 if the input layer used it
   (a remap capture: the event should go no further). */
int t2_input_event(T2Input *in, const SDL_Event *e);

/* Key state from SDL's keyboard state (the game) or from the events given
   to t2_input_event only (the tests, which push their own). */
void t2_input_keys_from_sdl(T2Input *in, int on);

/* The on-screen controller's held buttons (T2_PAD_* bits), player 1's. */
void t2_input_set_touch(T2Input *in, uint16_t held);

/* Both pads as they are now. While a remap capture runs, nothing is
   pressed. */
void t2_input_pads(T2Input *in, T2Pad pads[2]);
/* The pads with no SOCD cleaning and capture blanking: what the players
   hold, for the settings panel's navigation and the Select hold. */
void t2_input_raw(T2Input *in, T2Pad pads[2]);

int t2_input_analog(const T2Input *in, int player);
void t2_input_set_analog(T2Input *in, int player, int on);

/* Swaps the players' devices. */
void t2_input_swap_players(T2Input *in);

/* Remap capture: the next input becomes `button`'s (a T2_PAD_* bit index)
   binding of its kind for `player`. */
void t2_input_remap_begin(T2Input *in, int player, int button);
void t2_input_remap_cancel(T2Input *in);
/* -1 while nothing is being captured, else the button index. */
int t2_input_remap_button(const T2Input *in);
/* Set once by a capture that finished (taken or cancelled); cleared by the
   read. 1 taken, 2 cancelled. */
int t2_input_remap_take_result(T2Input *in);
/* Back to the built-in bindings for a player. */
void t2_input_reset_player(T2Input *in, int player);

/* A button's bindings as a person reads them ("X key, south button"). */
void t2_input_binding_text(const T2Input *in, int player, int button, char *out, size_t cap);
/* The devices a player has, as names ("DualSense Wireless Controller,
   keyboard"). */
void t2_input_player_devices(const T2Input *in, int player, char *out, size_t cap);
/* Connected gamepads and joysticks. */
int t2_input_device_count(const T2Input *in);

/* For the tests: only SDL's virtual controllers become devices, so a real
   pad on the test machine never takes a player. Set before t2_input_new. */
void t2_input_only_virtual(int on);

/* A gamepad's family from what SDL knows of it (exported for the tests). */
InputFamily t2_input_gamepad_family(SDL_GamepadType type, Uint16 vendor, Uint16 product, int on_deck);
int t2_input_on_steam_deck(void);

/* A line per connected device, for the log. */
void t2_input_describe(const T2Input *in, FILE *out);

#endif
