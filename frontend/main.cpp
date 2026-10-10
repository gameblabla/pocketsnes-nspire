/* PocketSNES for TI-Nspire: pick a ROM, play it, repeat. */
#include <stdio.h>
#include <string.h>

#include "browser.h"
#include "config.h"
#include "emu.h"
#include "gui.h"
#include "menu.h"
#include "overclock.h"
#include "paths.h"
#include "platform.h"
#include "states.h"

#ifdef AUTO_BENCH
/* Speed test build (make -f Makefile.nspire BENCH=1): runs the speed test on
 * this ROM from the program's folder, starting at its newest save state, then
 * closes. Nothing is saved except the results. */
#define BENCH_ROM "Super Mario World (USA).sfc.tns"

static void run_benchmark(void)
{
    char rom[600];

    snprintf(rom, sizeof(rom), "%s/%s", platform_exe_dir(), BENCH_ROM);
    if (!emu_load_game(rom))
    {
        gui_message("Couldn't load the speed test ROM:", BENCH_ROM);
        return;
    }
    if (states_newest())
        emu_load_state(states_newest());
    emu_benchmark();
}
#endif

int main(int argc, char **argv)
{
    char rom[512] = "";
    int have_rom = 0;

    if (!platform_init(&argc, argv))
        return 0;
    /* Files from older versions into PocketSNES_saves (paths.h). */
    paths_move_old_files(platform_exe_dir(), 1);
    config_load();

    if (!emu_init())
    {
        gui_message("Not enough free memory to start PocketSNES.", NULL);
        platform_shutdown();
        return 0;
    }

    /* A raised CPU speed that froze the calculator last time goes back to
     * normal (see emu.cpp). */
    char crashed_rom[512];
    int crashed_multiplier = emu_check_cpu_speed_crash(crashed_rom, sizeof(crashed_rom));
    if (crashed_multiplier)
    {
        char line[64];
        config_reset_cpu_speed(crashed_rom);
        snprintf(line, sizeof(line), "PocketSNES didn't close normally at %d MHz.", crashed_multiplier * 12);
        gui_message(line, "CPU speed is back to normal (Graphics & speed).");
    }
    /* The overclock test froze: it keeps the highest speed that passed. */
    overclock_check_freeze();

#ifdef AUTO_BENCH
    run_benchmark();
    emu_deinit();
    platform_shutdown();
    return 0;
#endif

    /* The first time, a word on where ROMs go and which keys do what. */
    if (config_first_start())
    {
        menu_show_welcome();
        config_save();
    }

    /* A ROM path on the command line (e.g. from a file association) skips
     * the browser the first time. */
    if (argc > 1)
    {
        snprintf(rom, sizeof(rom), "%s", argv[1]);
        have_rom = 1;
    }

    for (;;)
    {
        int start = BROWSER_START;
        if (!have_rom && (start = browser_run(rom, sizeof(rom))) == BROWSER_QUIT)
            break;
        have_rom = 0;
        config_save();

        if (!emu_load_game(rom))
        {
            const char *slash = strrchr(rom, '/');
            char line[600];
            snprintf(line, sizeof(line), "Couldn't load %s", slash ? slash + 1 : rom);
            gui_message(line, emu_load_error());
            continue;
        }

        enum EmuExit result = emu_run(start != BROWSER_START_WITHOUT_STATE);
        emu_close_game();
        if (result == EMU_QUIT || platform_quit_requested())
            break;
    }

    config_save();
    emu_deinit();
    platform_shutdown();
    return 0;
}
