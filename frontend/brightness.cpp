/* Screen brightness: see brightness.h. The levels spread in equal steps over
 * the backlight register's range, 0 (brightest) to 225 (darkest), the values
 * the OS writes (platform.h). */
#include "brightness.h"
#include "config.h"
#include "platform.h"

#define DARKEST 225

static int shown = -1;   /* the setting the backlight was last set for */

int brightness_value(int level)
{
    if (level < 1)
        level = 1;
    if (level > BRIGHTNESS_LEVELS)
        level = BRIGHTNESS_LEVELS;
    return (BRIGHTNESS_LEVELS - level) * DARKEST / (BRIGHTNESS_LEVELS - 1);
}

int brightness_level(int value)
{
    if (value < 0)
        value = 0;
    if (value > DARKEST)
        value = DARKEST;
    return BRIGHTNESS_LEVELS - (value * (BRIGHTNESS_LEVELS - 1) + DARKEST / 2) / DARKEST;
}

void brightness_update(void)
{
    int setting = (int) cfg.brightness;
    if (setting == shown)
        return;
    shown = setting;
    if (platform_os_backlight() < 0)
        return;
    platform_set_backlight(setting ? brightness_value(setting) : -1);
}

int brightness_step(int delta)
{
    if (platform_os_backlight() < 0)
        return 0;
    int level = cfg.brightness ? (int) cfg.brightness : brightness_level(platform_os_backlight());
    level += delta;
    if (level < 1)
        level = 1;
    if (level > BRIGHTNESS_LEVELS)
        level = BRIGHTNESS_LEVELS;
    cfg.brightness = (uint32_t) level;
    brightness_update();
    return level;
}
