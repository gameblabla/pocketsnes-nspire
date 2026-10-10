/* Screen brightness on the CX II: Graphics & speed > Brightness, and the
 * Brightness up/down hotkeys, as lr-gpsp-nspire has them. cfg.brightness is
 * 0 for the calculator's own brightness (PocketSNES leaves the backlight
 * alone), or a level from 1 (dimmest) to BRIGHTNESS_LEVELS (brightest). The
 * calculator's own comes back when PocketSNES closes (platform_shutdown). */
#ifndef BRIGHTNESS_H
#define BRIGHTNESS_H

#define BRIGHTNESS_LEVELS 10

/* Sets the backlight for cfg.brightness if that changed since the last call.
 * Called for every frame shown, in the menus (gui_present) and in games. */
void brightness_update(void);

/* The hotkeys: one level brighter (+1) or dimmer (-1) than what the screen
 * shows now, into cfg.brightness. Returns the new level, or 0 where the
 * brightness can't be changed (not a CX II). */
int brightness_step(int delta);

/* The backlight register value for a level, and the level nearest a value. */
int brightness_value(int level);
int brightness_level(int value);

#endif
