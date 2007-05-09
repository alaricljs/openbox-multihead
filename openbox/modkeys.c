/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   modkeys.c for the Openbox window manager
   Copyright (c) 2007        Dana Jansens

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   See the COPYING file for a copy of the GNU General Public License.
*/

#include "modkeys.h"
#include "openbox.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>

/* These masks are constants and the modifier keys are bound to them as
   anyone sees fit:
        ShiftMask (1<<0), LockMask (1<<1), ControlMask (1<<2), Mod1Mask (1<<3),
        Mod2Mask (1<<4), Mod3Mask (1<<5), Mod4Mask (1<<6), Mod5Mask (1<<7)
*/
#define NUM_MASKS 8
#define ALL_MASKS 0xff /* an or'ing of all 8 keyboard masks */

/* Get the bitflag for the n'th modifier mask */
#define nth_mask(n) (1 << n)

static void set_modkey_mask(guchar mask, KeySym sym);

static XModifierKeymap *modmap;
/* This is a bitmask of the different masks for each modifier key */
static guchar modkeys_keys[OB_MODKEY_NUM_KEYS];

void modkeys_startup(gboolean reconfigure)
{
    KeySym *keymap;
    gint i, j, k;
    gint min_keycode, max_keycode, keysyms_per_keycode;

    /* reset the keys to not be bound to any masks */
    for (i = 0; i < OB_MODKEY_NUM_KEYS; ++i)
        modkeys_keys[i] = 0;

    modmap = XGetModifierMapping(ob_display);
    g_assert(modmap->max_keypermod > 0);

    XDisplayKeycodes(ob_display, &min_keycode, &max_keycode);
    keymap = XGetKeyboardMapping(ob_display, min_keycode,
                                 max_keycode - min_keycode + 1,
                                 &keysyms_per_keycode);

    /* go through each of the modifier masks (eg ShiftMask, CapsMask...) */
    for (i = 0; i < NUM_MASKS; ++i) {
        /* go through each keycode that is bound to the mask */
        for (j = 0; j < modmap->max_keypermod; ++j) {
            KeySym sym;
            /* get a keycode that is bound to the mask (i) */
            KeyCode keycode = modmap->modifiermap[i*modmap->max_keypermod + j];
            /* go through each keysym bound to the given keycode */
            for (k = 0; k < keysyms_per_keycode; ++k) {
                sym = keymap[(keycode-min_keycode) * keysyms_per_keycode + k];
                if (sym != NoSymbol) {
                    /* bind the key to the mask (e.g. Alt_L => Mod1Mask) */
                    set_modkey_mask(nth_mask(i), sym);
                }
            }
        }
    }
    XFree(keymap);
}

void modkeys_shutdown(gboolean reconfigure)
{
    XFreeModifiermap(modmap);
}

guint modkeys_keycode_to_mask(guint keycode)
{
    gint i, j;
    guint mask = 0;

    if (keycode == NoSymbol) return 0;

    /* go through each of the modifier masks (eg ShiftMask, CapsMask...) */
    for (i = 0; i < NUM_MASKS; ++i) {
        /* go through each keycode that is bound to the mask */
        for (j = 0; j < modmap->max_keypermod; ++j) {
            /* compare with a keycode that is bound to the mask (i) */
            if (modmap->modifiermap[i*modmap->max_keypermod + j] == keycode)
                mask |= nth_mask(i);
        }
    }
    return mask;
}

guint modkeys_only_modifier_masks(guint mask)
{
    mask &= ALL_MASKS;
    /* strip off these lock keys. they shouldn't affect key bindings */
    mask &= ~modkeys_key_to_mask(OB_MODKEY_KEY_CAPSLOCK);
    mask &= ~modkeys_key_to_mask(OB_MODKEY_KEY_NUMLOCK);
    mask &= ~modkeys_key_to_mask(OB_MODKEY_KEY_SCROLLLOCK);
    return mask;
}

guint modkeys_key_to_mask(ObModkeysKey key)
{
    return modkeys_keys[key];
}

static void set_modkey_mask(guchar mask, KeySym sym)
{
    /* find what key this is, and bind it to the mask */

    if (sym == XK_Num_Lock)
        modkeys_keys[OB_MODKEY_KEY_NUMLOCK] |= mask;
    else if (sym == XK_Scroll_Lock)
        modkeys_keys[OB_MODKEY_KEY_SCROLLLOCK] |= mask;
    else if (sym == XK_Caps_Lock)
        modkeys_keys[OB_MODKEY_KEY_CAPSLOCK] |= mask;
    else if (sym == XK_Shift_L || sym == XK_Shift_R)
        modkeys_keys[OB_MODKEY_KEY_SHIFT] |= mask;
    else if (sym == XK_Control_L || sym == XK_Control_R)
        modkeys_keys[OB_MODKEY_KEY_CONTROL] |= mask;
    else if (sym == XK_Super_L || sym == XK_Super_R)
        modkeys_keys[OB_MODKEY_KEY_SUPER] |= mask;
    else if (sym == XK_Hyper_L || sym == XK_Hyper_R)
        modkeys_keys[OB_MODKEY_KEY_HYPER] |= mask;
    else if (sym == XK_Alt_L || sym == XK_Alt_R)
        modkeys_keys[OB_MODKEY_KEY_ALT] |= mask;
    else if (sym == XK_Meta_L || sym == XK_Meta_R)
        modkeys_keys[OB_MODKEY_KEY_META] |= mask;
}