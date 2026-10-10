/* Calculator side of platform.h (TI-Nspire CX / CX II with Ndless). */
#include <os.h>
#include <libndls.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "rotate.h"

/* How frames reach the screen. The CX II's LCD reads 320 lines of 240
 * pixels, and landscape pixel (x, y) belongs at position 239 - y of line x
 * (seen on a CX II, OS 6.4). The OS's LCD buffer (0xA8000000) is memory that
 * turns every pixel as it is written, so a plain landscape copy into it shows
 * upright. Ndless's lcd_blit makes that copy with the CPU, which is slow
 * (about 4 ms a frame). The OS has its DMA controller make it instead
 * (Hackspire), and so does PocketSNES ("dma"): the game draws into one of two
 * buffers in normal memory, and while the DMA controller copies a finished
 * frame to the LCD buffer, the next one is drawn into the other buffer.
 * If the DMA controller fails its test, each frame is turned in software
 * (rotate.cpp) into one of three portrait buffers and the LCD is pointed at
 * it ("flip"). Screens without the CX II layout use lcd_blit. The speed test
 * compares all three. See FINDINGS.md. The game's buffers have spare lines
 * around the picture (see platform.h). */
#define GUARD_LINES 16
/* Interlaced Mode 5 draws up to twice the lines (the old 480-line buffer). */
#define BELOW_LINES 272
#define SCREEN_PIXELS (SCREEN_W * SCREEN_H)
#define GAME_SCREENS 2
static uint16_t screen_memory[GAME_SCREENS][(GUARD_LINES + SCREEN_H + BELOW_LINES) * SCREEN_W]
    __attribute__ ((aligned (32)));
static uint32_t screen_physical[GAME_SCREENS];   /* where the DMA controller reads them */
static int game_screen;                          /* the one being drawn into */
static uint16_t portrait_screens[PLATFORM_MAX_SCREENS][SCREEN_PIXELS] __attribute__ ((aligned (32)));
static int current_screen;   /* the portrait buffer "flip" writes next */
static int fast_available;   /* a CX II LCD with the layout described above */
static int dma_works;        /* the DMA controller copied a frame at startup */
static int dma_failed;       /* ... but a copy failed later */
static int forced_output = OUTPUT_BEST;   /* the speed test's choice */
static int last_frame_turned;   /* the last frame came in pieces (platform_frame_begin) */
static int game_frames_by_dma;  /* the player's choice (platform_set_game_frames_by_dma) */
static uint32_t os_framebuffer;

static uint32_t keys_now[4], keys_old[4];
static int touchpad;
static char exe_dir[256];

/* The first SP804 dual timer, run free at 32768 Hz the same way nSDL's
 * SDL_GetTicks does. Its old settings are put back on exit. */
#define TIMER_LOAD    ((volatile uint32_t *) 0x900C0000)
#define TIMER_VALUE   ((volatile uint32_t *) 0x900C0004)
#define TIMER_CONTROL ((volatile uint32_t *) 0x900C0008)
#define TIMER_CLOCK   ((volatile uint32_t *) 0x900C0080)
#define RTC_SECONDS   ((volatile uint32_t *) 0x90090000)
/* The buffer the LCD shows, where lcd_blit writes (Ndless's
 * REAL_SCREEN_BASE_ADDRESS), and the LCD controller's timing registers,
 * which are only read for the speed test report. */
#define LCD_BASE      (*(volatile uint32_t *) 0xC0000010)
#define LCD_TIMING0   (*(volatile uint32_t *) 0xC0000000)
#define LCD_TIMING1   (*(volatile uint32_t *) 0xC0000004)

static uint32_t saved_load, saved_control, saved_clock;

/* The timer's clock comes from the 32768 Hz crystal on the CX and the CX II
 * alike (checked against the real-time clock; the speed test build reports
 * it). It used to be measured against the real-time clock while the program
 * ran, but loading a ROM or a state held up the measurement and it came out
 * 5-20% wrong, which made games run that much too slow and the FPS counter
 * and the measured CPU clock read high. */
static const uint32_t tick_hz = 32768;

/* If the timer turns out not to run, time is only advanced by waiting, so
 * the game runs unthrottled (like PocketSNES 2.0) instead of hanging. */
static int timer_works;
static uint32_t fallback_ticks;

/* Ticks per second of the timer, timed against the real-time clock with
 * nothing else running (about 3 seconds), for the diagnostics file; 0 if the
 * real-time clock doesn't tick. */
static uint32_t measure_tick_hz(void)
{
    uint32_t first = *RTC_SECONDS, start = platform_ticks();
    while (*RTC_SECONDS == first)
        if (platform_ticks() - start > 2 * tick_hz)
            return 0;
    uint32_t edge = *RTC_SECONDS, edge_tick = platform_ticks();
    while (*RTC_SECONDS - edge < 2)
        if (platform_ticks() - edge_tick > 3 * tick_hz)
            return 0;
    return (platform_ticks() - edge_tick) / 2;
}

/* Writes the cached pixels out to memory, where the LCD's DMA reads them. */
static void clean_dcache(const void *start, size_t size)
{
    uintptr_t addr = (uintptr_t) start & ~31u;
    uintptr_t end = (uintptr_t) start + size;
    for (; addr < end; addr += 32)
        __asm__ volatile ("mcr p15, 0, %0, c7, c10, 1" : : "r" (addr));
    __asm__ volatile ("mcr p15, 0, %0, c7, c10, 4" : : "r" (0));
}

/* Writes back and drops a range's cached lines, so it is read from memory. */
static void flush_dcache(const void *start, size_t size)
{
    uintptr_t addr = (uintptr_t) start & ~31u;
    uintptr_t end = (uintptr_t) start + size;
    for (; addr < end; addr += 32)
        __asm__ volatile ("mcr p15, 0, %0, c7, c14, 1" : : "r" (addr));
    __asm__ volatile ("mcr p15, 0, %0, c7, c10, 4" : : "r" (0));
}

/* Physical address of a virtual one, walking the MMU's tables the way
 * Ndless's lcd_compat code reads them (the tables are mapped 1:1). The
 * descriptors are returned for their cache bits. A second-level table is
 * only read when it sits in a 1:1 mapped section. */
static uint32_t mmu_lookup(uint32_t va, uint32_t *first, uint32_t *second)
{
    uint32_t ttb;
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r" (ttb));
    const uint32_t *table = (const uint32_t *) (ttb & 0xFFFFC000);
    uint32_t d1 = table[va >> 20];

    *first = d1;
    *second = 0;
    switch (d1 & 3)
    {
    case 2:
        return (d1 & 0xFFF00000) | (va & 0xFFFFF);
    case 0:
        return 0xFFFFFFFF;
    }

    uint32_t pt = (d1 & 3) == 1 ? (d1 & 0xFFFFFC00) : (d1 & 0xFFFFF000);
    uint32_t pt_section = table[pt >> 20];
    if ((pt_section & 3) != 2 || (pt_section & 0xFFF00000) != (pt & 0xFFF00000))
        return 0xFFFFFFFE;
    uint32_t d2 = (d1 & 3) == 1 ? ((const uint32_t *) pt)[(va >> 12) & 0xFF]
                                : ((const uint32_t *) pt)[(va >> 10) & 0x3FF];
    *second = d2;
    switch (d2 & 3)
    {
    case 1: return (d2 & 0xFFFF0000) | (va & 0xFFFF);   /* 64 KB page */
    case 2: return (d2 & 0xFFFFF000) | (va & 0xFFF);    /* 4 KB page */
    case 3: return (d2 & 0xFFFFFC00) | (va & 0x3FF);    /* 1 KB page */
    default: return 0xFFFFFFFF;
    }
}

/* Is every 1 KB step of the buffer the same distance away physically? */
static int physically_contiguous(uint32_t va, uint32_t pa, uint32_t size)
{
    uint32_t first, second;
    for (uint32_t offset = 1024; offset < size; offset += 1024)
        if (mmu_lookup(va + offset, &first, &second) != pa + offset)
            return 0;
    return 1;
}

/* The DMA controller, a Faraday FTDMAC020 (Hackspire's "Memory-mapped I/O
 * ports on CX II"; Firebird's core/cx2.cpp shows what the OS writes to it).
 * Normal memory and the LCD buffer are on its second bus port (AHB1); using
 * the first gives a bus error. Linked lists and 64-bit transfers reportedly
 * lock up the calculator, so neither is used. The OS uses channel 0; this
 * uses channel 1 with its interrupts masked, and polls it. */
#define DMA_BASE        0xBC000000
#define DMA_REG(offset) (*(volatile uint32_t *) (DMA_BASE + (offset)))
#define DMA_TC_CLEAR    0x008
#define DMA_ERR_CLEAR   0x010
#define DMA_ERR         0x018   /* error bits per channel, abort bits from bit 16 */
#define DMA_CONFIG      0x024   /* bit 0 turns the controller on */
#define DMA_CHANNEL     1
#define DMA_CH(offset)  DMA_REG(0x100 + DMA_CHANNEL * 0x20 + (offset))
#define CH_CONTROL      0x00    /* bit 0 starts a copy and clears when it's done */
#define CH_CONFIG       0x04
#define CH_SRC          0x08
#define CH_DST          0x0C
#define CH_LINK         0x10
#define CH_SIZE         0x14    /* counted in reads, 32-bit ones here */
/* Both ports AHB1, both addresses counting up, 32-bit reads and writes. The
 * burst length goes in bits 16-18: 1, 4, 8, 16, 32, 64, 128 or 256 words. */
#define DMA_COPY_CONTROL ((1u << 1) | (1u << 2) | (2u << 8) | (2u << 11))
#define DMA_TIMEOUT      4000000   /* polls; a frame takes a small part of that */

static uint32_t dma_control;
static int dma_pending, dma_mapped, dma_turned_on;
static uint32_t saved_dma_config;

/* The controller's registers at startup, for the diagnostics file and the
 * burst length the OS uses on channel 0. Write-only ones are skipped. */
static const uint16_t dma_reg_offsets[] = { 0x00, 0x04, 0x0C, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x30, 0x34 };
#define DMA_REGS (int) (sizeof(dma_reg_offsets) / sizeof(dma_reg_offsets[0]))
#define DMA_CHANNELS 6
static uint32_t dma_start_regs[DMA_REGS], dma_start_channels[DMA_CHANNELS][6];

static void read_dma_regs(uint32_t *regs, uint32_t (*channels)[6])
{
    for (int i = 0; i < DMA_REGS; i++)
        regs[i] = DMA_REG(dma_reg_offsets[i]);
    for (int c = 0; c < DMA_CHANNELS; c++)
        for (int r = 0; r < 6; r++)
            channels[c][r] = DMA_REG(0x100 + c * 0x20 + r * 4);
}

static void dma_start(uint32_t src, uint32_t dst, uint32_t bytes)
{
    DMA_REG(DMA_TC_CLEAR) = 1u << DMA_CHANNEL;
    DMA_REG(DMA_ERR_CLEAR) = 0x10001u << DMA_CHANNEL;
    DMA_CH(CH_CONFIG) = 7;   /* its done, error and abort interrupts masked */
    DMA_CH(CH_SRC) = src;
    DMA_CH(CH_DST) = dst;
    DMA_CH(CH_LINK) = 0;
    DMA_CH(CH_SIZE) = bytes / 4;
    DMA_CH(CH_CONTROL) = dma_control | 1;
    dma_pending = 1;
}

/* Waits for the last copy to end. If it fails or never ends, the channel is
 * stopped and frames are turned in software from then on. */
static int dma_finish(void)
{
    if (!dma_pending)
        return dma_works;
    dma_pending = 0;
    for (int polls = 0; DMA_CH(CH_CONTROL) & 1; polls++)
        if (polls == DMA_TIMEOUT)
        {
            DMA_CH(CH_CONTROL) |= 1u << 15;   /* abort */
            dma_works = 0;
            dma_failed = 1;
            return 0;
        }
    if (DMA_REG(DMA_ERR) & (0x10001u << DMA_CHANNEL))
    {
        dma_works = 0;
        dma_failed = 1;
        return 0;
    }
    return 1;
}

static uint16_t *game_screen_start(int index)
{
    return screen_memory[index] + GUARD_LINES * SCREEN_W;
}

/* Gets the DMA controller ready and has it copy a frame (black) to the LCD.
 * Returns 0 if it can't be used. */
static int dma_init(void)
{
    uint32_t first, second;

    if (mmu_lookup(DMA_BASE, &first, &second) != DMA_BASE)
        return 0;
    dma_mapped = 1;
    read_dma_regs(dma_start_regs, dma_start_channels);

    for (int i = 0; i < GAME_SCREENS; i++)
    {
        uint32_t va = (uint32_t) game_screen_start(i);
        uint32_t pa = mmu_lookup(va, &first, &second);
        if (pa >= 0xFFFFFFFE || !physically_contiguous(va, pa, SCREEN_PIXELS * 2))
            return 0;
        screen_physical[i] = pa;
    }

    /* 8-word bursts: the fastest measured from normal memory (the OS uses
     * 256-word ones). */
    dma_control = DMA_COPY_CONTROL | 2u << 16;

    saved_dma_config = DMA_REG(DMA_CONFIG);
    if ((saved_dma_config & 6) || (DMA_CH(CH_CONTROL) & 1))
        return 0;   /* big-endian ports, or the channel is busy */
    if (!(saved_dma_config & 1))
    {
        DMA_REG(DMA_CONFIG) = saved_dma_config | 1;
        dma_turned_on = 1;
    }

    clean_dcache(game_screen_start(0), SCREEN_PIXELS * 2);
    dma_works = 1;
    dma_start(screen_physical[0], os_framebuffer, SCREEN_PIXELS * 2);
    return dma_finish();
}

/* The on-chip SRAM (Hackspire: 0x40000 bytes at 0xA4000000). It is several
 * times faster than normal memory (FINDINGS.md), so the main depth buffer and
 * the game screen go there. Its first 16 KB are left alone: they hold the
 * CPU's exception vectors (mapped at address 0), the OS's interrupt entry
 * code and its sleep code, and wiping them crashes the calculator at the next
 * system call. The rest holds leftovers from startup that nothing touches
 * while PocketSNES runs; it is saved at startup and put back at exit.
 * Layout after the first 16 KB: depth buffer (guard lines, then 248 lines),
 * then the screen (guard lines, 240 lines, spare lines), to the end.
 * There is room for one screen only: the DMA controller copies a frame out
 * of it while the next one is drawn, and the renderer waits for the copy to
 * get past the lines it is about to draw (platform_wait_lines). */
#define SRAM_BASE          0xA4000000
#define SRAM_SIZE          0x40000
/* Off: both SRAM builds froze the calculator at startup (with and without
 * the first 16 KB kept), and why isn't known yet. The speed test checks
 * whether the DMA controller can read the SRAM at all. */
#define SRAM_RENDERING     0
#define SRAM_KEEP          0x4000
#define SRAM_DEPTH_LINES   248   /* 239 lines and a tile's spill */
#define SRAM_SCREEN_GUARD  8     /* lines before the screen, for the renderer's strays */
#define SRAM_SCREEN_SPARE  8     /* lines after it, for the spill below a 239-line picture */
#define SRAM_SCREEN_REGION (SRAM_KEEP + (PLATFORM_DEPTH_GUARD + SRAM_DEPTH_LINES) * SCREEN_W)
#define SRAM_END           (SRAM_SCREEN_REGION + (SRAM_SCREEN_GUARD + SCREEN_H + SRAM_SCREEN_SPARE) * SCREEN_W * 2)
static_assert(SRAM_END <= SRAM_SIZE, "the screen and depth buffer don't fit in the SRAM");
#define SRAM_DEPTH  ((uint8_t *) SRAM_BASE + SRAM_KEEP + PLATFORM_DEPTH_GUARD * SCREEN_W)
#define SRAM_SCREEN ((uint16_t *) (SRAM_BASE + SRAM_SCREEN_REGION) + SRAM_SCREEN_GUARD * SCREEN_W)

static int sram_used;
static int sram_write_through;   /* writes go straight to it: draining the write buffer is enough */
static uint32_t sram_screen_physical;
static uint8_t *sram_backup;

/* Moves the screen and depth buffer into the SRAM if it is mapped in one
 * piece and writable. */
static void sram_init(void)
{
    uint32_t first, second;
    uint32_t pa = mmu_lookup(SRAM_BASE, &first, &second);
    if (pa >= 0xFFFFFFFE || !physically_contiguous(SRAM_BASE, pa, SRAM_SIZE))
        return;
    /* C and B are bits 3 and 2 of both kinds of descriptor; the access bits
     * are 11-10 in a section and 5-4 (first quarter) in a page. */
    uint32_t descriptor = (first & 3) == 2 ? first : second;
    uint32_t access = (first & 3) == 2 ? (first >> 10) & 3 : (second >> 4) & 3;
    if (access == 0 || !(sram_backup = (uint8_t *) malloc(SRAM_END - SRAM_KEEP)))
        return;
    memcpy(sram_backup, (const void *) (SRAM_BASE + SRAM_KEEP), SRAM_END - SRAM_KEEP);
    memset((void *) (SRAM_BASE + SRAM_KEEP), 0, SRAM_END - SRAM_KEEP);
    sram_write_through = (descriptor & 0xC) == 0x8;
    sram_screen_physical = pa + SRAM_SCREEN_REGION + SRAM_SCREEN_GUARD * SCREEN_W * 2;
    sram_used = 1;
}

static void sram_restore(void)
{
    if (!sram_used)
        return;
    memcpy((void *) (SRAM_BASE + SRAM_KEEP), sram_backup, SRAM_END - SRAM_KEEP);
    clean_dcache((const void *) (SRAM_BASE + SRAM_KEEP), SRAM_END - SRAM_KEEP);
    free(sram_backup);
    sram_backup = NULL;
    sram_used = 0;
}

/* Where the DMA controller reads the frame being shown. */
static uint32_t frame_physical(void)
{
    return sram_used ? sram_screen_physical : screen_physical[game_screen];
}

/* The LCD controller's registers (PL111 layout), skipping the write-only
 * interrupt clear register. */
static const uint16_t lcd_reg_offsets[] = { 0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x2C, 0x30 };
#define LCD_REGS (int) (sizeof(lcd_reg_offsets) / sizeof(lcd_reg_offsets[0]))

static void read_lcd_regs(uint32_t *regs)
{
    for (int i = 0; i < LCD_REGS; i++)
        regs[i] = *(volatile uint32_t *) (0xC0000000 + lcd_reg_offsets[i]);
}

static uint32_t read_cpsr(void)
{
    uint32_t value;
    __asm__ volatile ("mrs %0, cpsr" : "=r" (value));
    return value;
}

/* IRQs and FIQs off (they are always off for IRQs while PocketSNES runs);
 * returns the old state for interrupts_restore. */
static uint32_t interrupts_off(void)
{
    uint32_t old, off;
    __asm__ volatile ("mrs %0, cpsr\n\torr %1, %0, #0xC0\n\tmsr cpsr_c, %1"
                      : "=r" (old), "=r" (off) : : "memory");
    return old;
}

static void interrupts_restore(uint32_t old)
{
    __asm__ volatile ("msr cpsr_c, %0" : : "r" (old) : "memory");
}

/* For the diagnostics file: the LCD registers and CPU state at startup. */
static uint32_t start_regs[LCD_REGS], start_cpsr;

/* The CX II LCD layout the fast output is written for: 240 clocks per line
 * (PL111 timing 2), 320 lines (timing 1), 16-bit 5:6:5 pixels. */
static int lcd_is_cx2_portrait(void)
{
    uint32_t clocks_per_line = ((*(volatile uint32_t *) 0xC0000008 >> 16) & 0x3FF) + 1;
    uint32_t lines = (LCD_TIMING1 & 0x3FF) + 1;
    uint32_t bpp = (*(volatile uint32_t *) 0xC0000018 >> 1) & 7;
    return is_cx2 && clocks_per_line == SCREEN_H && lines == SCREEN_W && bpp == 6;
}

/* The CX II's LCD backlight (Hackspire, "Memory-mapped I/O ports on CX II",
 * 90130000): the OS writes the brightness to 0x90130014, from 0 (brightest)
 * to 225 (darkest). Something other than the OS's 0..255 in it means it
 * isn't what it seems, and the backlight is left alone. */
#define BACKLIGHT (*(volatile uint32_t *) 0x90130014)

static int os_backlight = -1;
static int backlight_changed;

static void backlight_init(void)
{
    os_backlight = -1;
    backlight_changed = 0;
    if (!is_cx2)
        return;
    uint32_t value = BACKLIGHT;
    if (value <= 255)
        os_backlight = (int) value;
}

int platform_os_backlight(void)
{
    return os_backlight;
}

void platform_set_backlight(int value)
{
    if (os_backlight < 0)
        return;
    if (value < 0)
    {
        if (!backlight_changed)
            return;
        value = os_backlight;
        backlight_changed = 0;
    }
    else
        backlight_changed = 1;
    BACKLIGHT = (uint32_t) (value > 255 ? 255 : value);
}

int platform_init(int *argc, char **argv)
{
    (void) argc;

    if (!has_colors)
    {
        show_msgbox("PocketSNES", "PocketSNES needs a TI-Nspire with a color screen.");
        return 0;
    }

    strncpy(exe_dir, argv[0], sizeof(exe_dir) - 1);
    char *slash = strrchr(exe_dir, '/');
    if (slash)
        *slash = 0;
    else
        strcpy(exe_dir, "/documents/ndless");

    touchpad = is_touchpad;

    /* ROMs named like game.sfc.tns then open in PocketSNES straight from the
     * calculator's documents (Ndless keeps file associations in
     * ndless.cfg.tns, and an existing one is left alone). Ndless finds the
     * program by this name, so it works while it is called pocketsnes.tns. */
    static const char *const rom_types[] = { "sfc", "smc", "fig", "swc" };
    for (size_t i = 0; i < sizeof(rom_types) / sizeof(rom_types[0]); i++)
        cfg_register_fileext(rom_types[i], "pocketsnes");

    backlight_init();

    saved_load = *TIMER_LOAD;
    saved_control = *TIMER_CONTROL;
    saved_clock = *TIMER_CLOCK;
    *TIMER_CONTROL = 0;
    *TIMER_CLOCK = 0xA;
    *TIMER_LOAD = 0xFFFFFFFF;
    *TIMER_CONTROL = 0x82;   /* enabled, free running, 32-bit, no interrupt */

    /* At 32 kHz the timer moves dozens of times during this loop. */
    uint32_t first = *TIMER_VALUE;
    for (int i = 0; i < 400000 && !timer_works; i++)
        timer_works = *TIMER_VALUE != first;

    start_cpsr = read_cpsr();
    read_lcd_regs(start_regs);
    if (!lcd_init(SCR_320x240_565))
    {
        platform_shutdown();
        return 0;
    }
    os_framebuffer = LCD_BASE;
    fast_available = lcd_is_cx2_portrait();
    memset(screen_memory, 0, sizeof(screen_memory));
    memset(portrait_screens, 0, sizeof(portrait_screens));
    if (fast_available)
    {
        dma_works = dma_init();
        if (SRAM_RENDERING)
            sram_init();
    }
    platform_present();
    return 1;
}

void platform_shutdown(void)
{
    platform_set_cpu_multiplier(0);
    platform_set_backlight(-1);
    if (dma_mapped)
    {
        dma_finish();
        if (dma_turned_on)
            DMA_REG(DMA_CONFIG) = saved_dma_config;
    }
    sram_restore();
    if (os_framebuffer)
        LCD_BASE = os_framebuffer;
    lcd_init(SCR_TYPE_INVALID);
    *TIMER_CONTROL = 0;
    *TIMER_CLOCK = saved_clock;
    *TIMER_LOAD = saved_load;
    *TIMER_CONTROL = saved_control;
}

uint16_t *platform_screen(void)
{
    if (!sram_used)
        return game_screen_start(game_screen);
    dma_finish();   /* the frame before may still be being copied out */
    return SRAM_SCREEN;
}

uint16_t *platform_screen_nowait(void)
{
    return sram_used ? SRAM_SCREEN : game_screen_start(game_screen);
}

void platform_wait_lines(int last_line)
{
    if (!sram_used || !dma_pending)
        return;
    if (last_line < SCREEN_H - 1)
    {
        /* The copy's source address counts up as it reads. */
        uint32_t needed = sram_screen_physical + (uint32_t) (last_line + 1) * SCREEN_W * 2;
        for (int polls = 0; polls < DMA_TIMEOUT && (DMA_CH(CH_CONTROL) & 1); polls++)
            if (DMA_CH(CH_SRC) >= needed)
                return;
    }
    dma_finish();
}

int platform_screen_index(void)
{
    return sram_used ? 0 : game_screen;
}

int platform_screen_spare_lines(void)
{
    return sram_used ? SRAM_SCREEN_SPARE : BELOW_LINES;
}

uint8_t *platform_fast_depth_buffer(int *lines)
{
    if (!sram_used)
        return NULL;
    *lines = SRAM_DEPTH_LINES;
    return SRAM_DEPTH;
}

enum Output { SHOWN_BY_DMA, SHOWN_BY_FLIP, SHOWN_BY_LCD_BLIT };

static enum Output output_now(void)
{
    if (!fast_available || forced_output == OUTPUT_LCD_BLIT)
        return SHOWN_BY_LCD_BLIT;
    if (forced_output == OUTPUT_FLIP || !dma_works)
        return SHOWN_BY_FLIP;
    return SHOWN_BY_DMA;
}

/* Back to the OS's buffer, which lcd_blit and the DMA controller write. */
static void show_os_framebuffer(void)
{
    if (os_framebuffer && LCD_BASE != os_framebuffer)
        LCD_BASE = os_framebuffer;
}

void platform_present(void)
{
    uint16_t *screen = platform_screen();

    last_frame_turned = 0;
    switch (output_now())
    {
    case SHOWN_BY_DMA:
        show_os_framebuffer();
        if (sram_used && sram_write_through)
            __asm__ volatile ("mcr p15, 0, %0, c7, c10, 4" : : "r" (0));   /* drain the write buffer */
        else
            clean_dcache(screen, SCREEN_PIXELS * 2);
        if (dma_finish())
        {
            dma_start(frame_physical(), os_framebuffer, SCREEN_PIXELS * 2);
            /* In normal memory the next frame goes in the other buffer. */
            if (!sram_used)
                game_screen = (game_screen + 1) % GAME_SCREENS;
            return;
        }
        /* The DMA controller stopped working: turn this frame instead. */
        [[fallthrough]];
    case SHOWN_BY_FLIP:
    {
        /* The LCD switches at its next refresh; with three buffers, the one
         * written next is never the one still being shown. */
        uint16_t *buffer = portrait_screens[current_screen];
        rotate_frame(screen, buffer, ROTATE_FLIP_COLUMNS);
        clean_dcache(buffer, SCREEN_PIXELS * 2);
        LCD_BASE = (uint32_t) buffer;
        current_screen = (current_screen + 1) % PLATFORM_MAX_SCREENS;
        return;
    }
    case SHOWN_BY_LCD_BLIT:
        show_os_framebuffer();
        lcd_blit(screen, SCR_320x240_565);
        clean_dcache((const void *) LCD_BASE, SCREEN_PIXELS * 2);
        return;
    }
}

/* Game frames (platform.h). On the CX II LCD the parts of a frame that
 * changed are turned into one of the three portrait buffers, which the LCD
 * is pointed at when the frame is done; the LCD only switches buffers at its
 * next refresh, so it always shows whole frames. Only the changed parts are
 * turned (the 256x224 picture and the text over it: about 1.8 ms less than
 * the whole 320x240 frame, see FINDINGS.md), and rotate_block writes them in
 * bursts. The CPU never reads these buffers, and its data cache only fills
 * on reads, so none of them is in the cache and only the write buffer needs
 * emptying before the LCD reads them. Code that does read them (the speed
 * test's memory checks) sets portrait_in_cache, and they are written out of
 * the cache once before the next frame. */
static uint16_t *frame_target;   /* the portrait buffer being filled, or NULL */
static int portrait_in_cache = 1;

/* How long platform_frame_begin waits at most for the LCD to stop reading
 * the buffer it's about to fill (it only reads one when frames come faster
 * than the LCD refreshes). */
#define LCD_WAIT_TICKS (32768 / 25)
#define LCD_WAIT_POLLS 1000000   /* in case the timer doesn't run */

void platform_set_game_frames_by_dma(int dma)
{
    game_frames_by_dma = dma;
}

int platform_frame_begin(void)
{
    if (!fast_available || forced_output != OUTPUT_BEST || (game_frames_by_dma && dma_works))
    {
        frame_target = NULL;
        return platform_screen_index();
    }
    dma_finish();
    if (portrait_in_cache)
    {
        flush_dcache(portrait_screens, sizeof(portrait_screens));
        portrait_in_cache = 0;
    }
    frame_target = portrait_screens[current_screen];

    uint32_t start = (uint32_t) frame_target, end = start + SCREEN_PIXELS * 2;
    uint32_t since = platform_ticks();
    for (int polls = 0; polls < LCD_WAIT_POLLS; polls++)
    {
        uint32_t reading = *(volatile uint32_t *) 0xC000002C;   /* the LCD's current address */
        if (reading < start || reading >= end || platform_ticks() - since > LCD_WAIT_TICKS)
            break;
    }
    return PLATFORM_MAX_SCREENS + current_screen;
}

void platform_frame_copy(int x, int y, int w, int h)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (!frame_target || w <= 0 || h <= 0)
        return;
    rotate_block(platform_screen() + y * SCREEN_W + x, SCREEN_W, x, y, w, h, frame_target);
}

void platform_frame_present(void)
{
    if (!frame_target)
    {
        platform_present();
        return;
    }
    __asm__ volatile ("mcr p15, 0, %0, c7, c10, 4" : : "r" (0) : "memory");   /* drain the write buffer */
    LCD_BASE = (uint32_t) frame_target;
    current_screen = (current_screen + 1) % PLATFORM_MAX_SCREENS;
    frame_target = NULL;
    last_frame_turned = 1;
}

const char *platform_screen_mode_name(void)
{
    if (last_frame_turned)
        return "turned";
    switch (output_now())
    {
    case SHOWN_BY_DMA:
        return "dma";
    case SHOWN_BY_FLIP:
        return dma_failed && forced_output == OUTPUT_BEST ? "flip (dma failed)" : "flip";
    default:
        return "lcd_blit";
    }
}

/* ---- CPU clock ----
 *
 * The CX II's power controller (0x90140000, Firebird's "Aladdin PMU") has a
 * clock register: the multiplier of the 12 MHz base clock in bits 24-29 (the
 * OS sets 33, 396 MHz, or 24, 288 MHz while USB is plugged in) and the bus
 * divider in bits 16-20 (2: the bus runs at half the CPU clock, so memory
 * speeds up with it). Writing the register alone changes nothing (seen with
 * tools/clocktest, on USB and on battery, also NoverII's way). The OS's own
 * switch, in the on-chip SRAM from 0xA4001640 (FINDINGS.md), puts normal
 * memory to sleep (self-refresh), then for each multiplier step writes the
 * register, writes 0x10000100 to register 0x20 and waits for the power
 * controller's interrupt, then wakes the memory. clock_switch_nspire.S does
 * the same, from the SRAM, because normal memory sleeps meanwhile; that is
 * also why the LCD reads the OS's buffer (on-chip memory) during a switch.
 * Every switch is checked by timing a loop against the 32 kHz timer. */
#define CLOCK_CONTROL  (*(volatile uint32_t *) 0x90140030)
#define PMU_COMMAND    (*(volatile uint32_t *) 0x90140020)   /* only read here */
#define PMU_FLAGS      (*(volatile uint32_t *) 0x90140024)   /* only read here */
#define MEMC_STATE     (*(volatile uint32_t *) 0x90120004)   /* bit 10: normal memory sleeps */
#define CLOCK_MULTIPLIER(value) ((int) (((value) >> 24) & 0x3F))
#define LCD_CURRENT    (*(volatile uint32_t *) 0xC000002C)   /* where the LCD reads now */

/* The second SP804 dual timer, at 32768 Hz: the switch gives up on a step
 * that doesn't finish when it runs out (interrupt 19). It belongs to the OS
 * (and Ndless's msleep), so it is put back afterwards. */
#define T2_LOAD    (*(volatile uint32_t *) 0x900D0000)
#define T2_CONTROL (*(volatile uint32_t *) 0x900D0008)
#define T2_CLEAR   (*(volatile uint32_t *) 0x900D000C)
#define T2_SPEED   (*(volatile uint32_t *) 0x900D0080)

/* Where the switch runs in the SRAM: past the OS's code (0xA4000000) and
 * the MMU's page table (0xA4004000-0xA4007FFF), in 1 KB that was zero in
 * every dump. What's there is put back after each switch. */
#define SWITCH_SRAM       ((uint8_t *) 0xA4035000)
#define SWITCH_STEP_TICKS 1092   /* a step may take 1/30 s */
extern "C" const uint8_t clock_switch_code[], clock_switch_code_end[];

/* The OS's LCD buffer is on-chip memory there. */
#define ON_CHIP_LCD_START 0xA8000000
#define ON_CHIP_LCD_END   0xA8040000

/* The CPU clock in MHz (rounded), from a loop of 4 * 'loops' cycles timed
 * with the 32 kHz timer: on the ARM926EJ-S, SUBS takes 1 cycle and a taken
 * branch 3, and the loop runs from the instruction cache, so memory speed
 * doesn't come into it. Interrupts are off meanwhile. 0 if there's no timer. */
static uint32_t measure_mhz(uint32_t loops)
{
    uint64_t cycles = (uint64_t) loops * 4;
    uint32_t cpsr = interrupts_off();
    uint32_t start = platform_ticks();
    __asm__ volatile ("1: subs %0, %0, #1\n\tbne 1b" : "+r" (loops) : : "cc");
    uint32_t ticks = platform_ticks() - start;
    interrupts_restore(cpsr);
    if (!timer_works || ticks == 0)
        return 0;
    return (uint32_t) ((cycles * tick_hz + ticks * 500000ull) / ticks / 1000000);
}

#define QUICK_LOOPS 4000000   /* 16 million cycles: about 40 ms, to within 0.1% */

/* Within 2% of 12 MHz times the multiplier. */
static int clock_matches(uint32_t mhz, int multiplier)
{
    uint32_t want = (uint32_t) multiplier * 12;
    return mhz * 50 >= want * 49 && mhz * 50 <= want * 51;
}

static uint32_t os_clock_control;   /* the OS's setting, put back after a raised one */
static int clock_raised;
static uint32_t clock_mhz;          /* measured after the last switch, 0 if none */

/* The last switch, for the speed test results. */
static struct
{
    uint32_t before, target, after, result, ticks, mhz;
    const char *refused;   /* why it wasn't tried, or NULL */
} last_switch;

static void sync_code(void)
{
    __asm__ volatile ("mcr p15, 0, %0, c7, c10, 4\n\t"   /* drain the write buffer */
                      "mcr p15, 0, %0, c7, c5, 0"        /* invalidate the instruction cache */
                      : : "r" (0) : "memory");
}

/* Points the LCD at the OS's buffer, with the picture on the screen copied
 * there first, and waits until it reads from there. Returns 0 if it can't
 * (then normal memory mustn't sleep). */
static int lcd_to_on_chip(void)
{
    if (os_framebuffer < ON_CHIP_LCD_START || os_framebuffer >= ON_CHIP_LCD_END)
        return 0;
    if (LCD_BASE != os_framebuffer)
    {
        /* The OS's buffer turns what is written into it (see the top). */
        memcpy((void *) os_framebuffer, platform_screen(), SCREEN_PIXELS * 2);
        clean_dcache((const void *) os_framebuffer, SCREEN_PIXELS * 2);
        LCD_BASE = os_framebuffer;
    }
    uint32_t since = platform_ticks();
    for (int polls = 0; polls < 4000000; polls++)
    {
        uint32_t reading = LCD_CURRENT;
        if (reading >= os_framebuffer && reading < os_framebuffer + SCREEN_PIXELS * 2)
            return 1;
        if (platform_ticks() - since > tick_hz / 10)
            break;
    }
    return 0;
}

/* Switches the clock register to 'value' (only its multiplier may differ
 * from the register's now) the OS's way. Returns the measured clock in MHz,
 * 0 if it can't be measured. */
static uint32_t switch_clock(uint32_t value)
{
    static uint8_t saved[1024];
    size_t size = (size_t) (clock_switch_code_end - clock_switch_code);
    uint32_t now = CLOCK_CONTROL, lcd = LCD_BASE;

    /* Already there (e.g. back to normal after a raise that didn't happen):
     * the last switch stays on record for the speed test. */
    if (now == value)
        return measure_mhz(QUICK_LOOPS);
    last_switch.before = now;
    last_switch.target = value;
    last_switch.result = last_switch.ticks = 0;
    last_switch.refused = NULL;
    dma_finish();   /* no frame copy runs across the switch */
    if (size > sizeof(saved))
        last_switch.refused = "switch code too big";
    else if ((now & ~(0x3Fu << 24)) != (value & ~(0x3Fu << 24)) || (now & 0xFF) != 0x03)
        last_switch.refused = "clock register in an unknown mode";
    else if ((PMU_COMMAND & ~0x100u) != 0x10000000)
        last_switch.refused = "power controller in an unknown state";
    else if (MEMC_STATE & 0x400)
        last_switch.refused = "memory asleep";
    else if (!lcd_to_on_chip())
        last_switch.refused = "LCD reads normal memory";

    if (!last_switch.refused)
    {
        uint32_t cpsr = interrupts_off();
        uint32_t control = T2_CONTROL, load = T2_LOAD, speed = T2_SPEED;
        T2_CONTROL = 0;
        T2_SPEED = 0xA;   /* 32768 Hz, like the first timer */
        memcpy(saved, SWITCH_SRAM, size);
        memcpy(SWITCH_SRAM, clock_switch_code, size);
        sync_code();
        uint32_t start = platform_ticks();
        last_switch.result = ((uint32_t (*)(uint32_t, uint32_t)) SWITCH_SRAM)(value, SWITCH_STEP_TICKS);
        last_switch.ticks = platform_ticks() - start;
        memcpy(SWITCH_SRAM, saved, size);
        sync_code();
        T2_CONTROL = 0;
        T2_CLEAR = 1;
        T2_SPEED = speed;
        T2_CONTROL = control & 0x7F;
        T2_LOAD = load;
        T2_CONTROL = control;
        interrupts_restore(cpsr);
        LCD_BASE = lcd;
    }

    uint32_t mhz = measure_mhz(QUICK_LOOPS);
    /* A step that didn't happen leaves the register ahead of the clock: it
     * is set to the clock the CPU runs at, so the OS's own switches (when
     * it idles) start from the truth. */
    int running = (int) ((mhz + 6) / 12);
    if (mhz && !clock_matches(mhz, CLOCK_MULTIPLIER(CLOCK_CONTROL)) && clock_matches(mhz, running))
        CLOCK_CONTROL = (CLOCK_CONTROL & ~(0x3Fu << 24)) | (uint32_t) running << 24;
    last_switch.after = CLOCK_CONTROL;
    last_switch.mhz = mhz;
    return mhz;
}

/* The clock now: measured after the last switch, or the register's. */
static uint32_t clock_now_mhz(void)
{
    return clock_mhz ? clock_mhz : (uint32_t) CLOCK_MULTIPLIER(CLOCK_CONTROL) * 12;
}

uint32_t platform_set_cpu_multiplier(int multiplier)
{
    if (!is_cx2 || multiplier < 0 || multiplier > 63)
        return 0;
    if (!multiplier)
    {
        if (clock_raised)
        {
            clock_raised = 0;
            clock_mhz = switch_clock(os_clock_control);
        }
        return clock_now_mhz();
    }
    if (!clock_raised)
        os_clock_control = CLOCK_CONTROL;
    uint32_t value = (os_clock_control & ~(0x3Fu << 24)) | (uint32_t) multiplier << 24;
    if (!clock_raised || value != CLOCK_CONTROL)
        clock_mhz = switch_clock(value);
    clock_raised = 1;
    return clock_now_mhz();
}

uint32_t platform_cpu_normal_mhz(void)
{
    if (!is_cx2)
        return 0;
    return (uint32_t) CLOCK_MULTIPLIER(clock_raised ? os_clock_control : CLOCK_CONTROL) * 12;
}

int platform_cpu_multiplier(void)
{
    return is_cx2 ? CLOCK_MULTIPLIER(CLOCK_CONTROL) : 0;
}

int platform_test_glitch(void)
{
    return 0;
}

void platform_clock_report(char *text, size_t size)
{
    if (!is_cx2)
    {
        snprintf(text, size, "clock register: not a CX II");
        return;
    }
    snprintf(text, size, "clock register %08lx (x%d)", (unsigned long) CLOCK_CONTROL,
             CLOCK_MULTIPLIER(CLOCK_CONTROL));
    if (!last_switch.target)
        return;
    size_t len = strlen(text);
    if (last_switch.refused)
        snprintf(text + len, size - len, "; last switch %08lx -> %08lx not tried: %s, %lu MHz",
                 (unsigned long) last_switch.before, (unsigned long) last_switch.target,
                 last_switch.refused, (unsigned long) last_switch.mhz);
    else
        snprintf(text + len, size - len,
                 "; last switch %08lx -> %08lx: %lu steps%s%s%s%s in %lu.%02lu ms, register %08lx, %lu MHz",
                 (unsigned long) last_switch.before, (unsigned long) last_switch.target,
                 (unsigned long) (last_switch.result & 0xFF),
                 last_switch.result & 0x100 ? ", memory didn't sleep" : "",
                 last_switch.result & 0x200 ? ", a step timed out" : "",
                 last_switch.result & 0x400 ? ", memory didn't wake" : "",
                 last_switch.result & 0x800 ? ", power interrupt stuck" : "",
                 (unsigned long) (last_switch.ticks * 1000 / tick_hz),
                 (unsigned long) (last_switch.ticks * 100000 / tick_hz % 100),
                 (unsigned long) last_switch.after, (unsigned long) last_switch.mhz);
}

uint32_t platform_cpu_mhz(void)
{
    return measure_mhz(20000000);   /* 80 million cycles */
}

static void write_mapping(FILE *f, const char *name, const void *start, uint32_t size)
{
    uint32_t va = (uint32_t) start, first, second;
    uint32_t pa = mmu_lookup(va, &first, &second);
    int contiguous = physically_contiguous(va, pa, size);

    fprintf(f, "  %-14s virtual %08lx physical %08lx (%s), descriptors %08lx %08lx\n", name,
            (unsigned long) va, (unsigned long) pa,
            pa == va ? "same" : contiguous ? "moved, contiguous" : "moved, scattered",
            (unsigned long) first, (unsigned long) second);
}

/* Ticks to copy 'size' bytes 'reps' times. */
static uint32_t time_copy(void *dst, const void *src, uint32_t size, int reps)
{
    uint32_t start = platform_ticks();
    for (int i = 0; i < reps; i++)
        memcpy(dst, src, size);
    return platform_ticks() - start;
}

/* ---- More for the diagnostics file: CPU, DMA controller, memory speeds ---- */

/* Each report gets the file to itself, so whatever was written before a
 * crash is saved. */
static void append_report(const char *path, void (*report)(FILE *))
{
    FILE *f = fopen(path, "a");
    if (!f)
        return;
    report(f);
    fclose(f);
}

/* Prints ticks / count as milliseconds. */
static void print_ms(FILE *f, uint32_t ticks, uint32_t count)
{
    uint32_t hundredths = (uint32_t) ((uint64_t) ticks * 100000 / count / tick_hz);
    fprintf(f, "%lu.%02lu", (unsigned long) (hundredths / 100), (unsigned long) (hundredths % 100));
}

static void write_cpu_report(FILE *f)
{
    uint32_t id, cache, tcm;
    __asm__ volatile ("mrc p15, 0, %0, c0, c0, 0" : "=r" (id));
    __asm__ volatile ("mrc p15, 0, %0, c0, c0, 1" : "=r" (cache));
    __asm__ volatile ("mrc p15, 0, %0, c0, c0, 2" : "=r" (tcm));
    /* Cache sizes are 512 bytes << the size code. */
    fprintf(f, "cpu id %08lx, cache type %08lx: data cache %lu KB, instruction cache %lu KB\n",
            (unsigned long) id, (unsigned long) cache, (unsigned long) (1u << ((cache >> 18) & 15)) / 2,
            (unsigned long) (1u << ((cache >> 6) & 15)) / 2);
    fprintf(f, "TCM status %08lx: data TCM %s, instruction TCM %s\n", (unsigned long) tcm,
            tcm & (1u << 16) ? "yes" : "no", tcm & 1 ? "yes" : "no");
    if (tcm & (1u << 16))
    {
        uint32_t region;
        __asm__ volatile ("mrc p15, 0, %0, c9, c1, 0" : "=r" (region));
        fprintf(f, "  data TCM region %08lx: base %08lx, size code %lu, %s\n", (unsigned long) region,
                (unsigned long) (region & 0xFFFFF000), (unsigned long) ((region >> 2) & 15),
                region & 1 ? "on" : "off");
    }
    if (tcm & 1)
    {
        uint32_t region;
        __asm__ volatile ("mrc p15, 0, %0, c9, c1, 1" : "=r" (region));
        fprintf(f, "  instruction TCM region %08lx: base %08lx, size code %lu, %s\n",
                (unsigned long) region, (unsigned long) (region & 0xFFFFF000),
                (unsigned long) ((region >> 2) & 15), region & 1 ? "on" : "off");
    }
}

/* The clock's registers, and a small raise (two steps of 12 MHz) and back,
 * the way PocketSNES switches (see "CPU clock"). */
static void write_clock_report(FILE *f)
{
    if (!is_cx2)
        return;
    uint32_t original = CLOCK_CONTROL;
    int multiplier = CLOCK_MULTIPLIER(original);
    fprintf(f, "Clock register %08lx (x%d); power controller 20 %08lx, 24 %08lx, 810 %08lx; "
            "memory controller 04 %08lx, 1C %08lx, 74 %08lx; second timer speed %08lx\n",
            (unsigned long) original, multiplier, (unsigned long) PMU_COMMAND, (unsigned long) PMU_FLAGS,
            (unsigned long) *(volatile uint32_t *) 0x90140810, (unsigned long) MEMC_STATE,
            (unsigned long) *(volatile uint32_t *) 0x9012001C, (unsigned long) *(volatile uint32_t *) 0x90120074,
            (unsigned long) T2_SPEED);
    if (multiplier < 20 || multiplier > 36 || !timer_works)
        return;

    char up[200], back[200];
    switch_clock((original & ~(0x3Fu << 24)) | (uint32_t) (multiplier + 2) << 24);
    platform_clock_report(up, sizeof(up));
    switch_clock(original);
    platform_clock_report(back, sizeof(back));
    fprintf(f, "Clock switch test, x%d to x%d and back:\n  %s\n  %s\n", multiplier, multiplier + 2, up, back);
}

static void write_dma_report(FILE *f)
{
    fprintf(f, "DMA controller: mapped %d, works %d, failed later %d, channel %d, control %08lx\n",
            dma_mapped, dma_works, dma_failed, DMA_CHANNEL, (unsigned long) dma_control);
    if (!dma_mapped)
        return;

    uint32_t regs[DMA_REGS], channels[DMA_CHANNELS][6];
    read_dma_regs(regs, channels);
    fprintf(f, "DMA registers (offset: at start / now):\n");
    for (int i = 0; i < DMA_REGS; i++)
        fprintf(f, "  %02x: %08lx %08lx\n", dma_reg_offsets[i], (unsigned long) dma_start_regs[i],
                (unsigned long) regs[i]);
    fprintf(f, "DMA channels, control config source destination link size (at start / now):\n");
    for (int c = 0; c < DMA_CHANNELS; c++)
    {
        fprintf(f, "  %d:", c);
        for (int r = 0; r < 6; r++)
            fprintf(f, " %08lx", (unsigned long) dma_start_channels[c][r]);
        fprintf(f, " /");
        for (int r = 0; r < 6; r++)
            fprintf(f, " %08lx", (unsigned long) channels[c][r]);
        fprintf(f, "\n");
    }
    if (!dma_works)
        return;

    uint16_t *picture = platform_screen();
    uint32_t start = platform_ticks();
    for (int i = 0; i < 10; i++)
        clean_dcache(picture, SCREEN_PIXELS * 2);
    fprintf(f, "writing a frame out of the cache: ");
    print_ms(f, platform_ticks() - start, 10);
    fprintf(f, " ms\n");

    /* Ten frame copies with each burst length, from the screen's memory and
     * (when that's the SRAM) from normal memory, stopping at a failure. */
    static const uint16_t burst_words[] = { 1, 4, 8, 16, 32, 64, 128, 256 };
    const int bursts = (int) (sizeof(burst_words) / sizeof(burst_words[0]));
    uint32_t normal_control = dma_control;
    int failed = 0, normal_failed = 0;
    for (int source = 0; source < (sram_used ? 2 : 1) && !failed; source++)
    {
        uint32_t from = source == 0 ? frame_physical() : screen_physical[0];
        fprintf(f, "DMA frame copy from %s to the LCD buffer, ms per frame by burst length:",
                from == sram_screen_physical && sram_used ? "the SRAM" : "normal memory");
        for (int b = 0; b < bursts && !failed; b++)
        {
            int ok = 1;
            dma_control = DMA_COPY_CONTROL | (uint32_t) b << 16;
            start = platform_ticks();
            for (int i = 0; i < 10 && ok; i++)
            {
                dma_start(from, os_framebuffer, SCREEN_PIXELS * 2);
                ok = dma_finish();
            }
            uint32_t ticks = platform_ticks() - start;
            fprintf(f, " %u: ", burst_words[b]);
            if (ok)
                print_ms(f, ticks, 10);
            else
            {
                fprintf(f, "failed");
                failed = 1;
                normal_failed = source == 0 && (uint32_t) b == ((normal_control >> 16) & 7);
            }
        }
        fprintf(f, "\n");
    }
    /* Keep using DMA unless the normal setting itself failed: it worked
     * for every frame until now. */
    dma_control = normal_control;
    dma_works = !normal_failed;
    dma_failed = normal_failed;
}

/* Store patterns, timed on a 256 x 96 area of a buffer laid out like the
 * game screen (320 pixels a line). Each pass draws it the way the renderer
 * draws tiles: the 8 lines of a tile, 8 pixels each, then the next tile. */
#define TEST_W      256
#define TEST_H      96
#define TEST_PASSES 20
#define TEST_BYTES  (TEST_H * SCREEN_W * 2)
static volatile uint32_t test_sink;

static void pass_halfwords(uint16_t *area)
{
    for (int ty = 0; ty < TEST_H; ty += 8)
        for (int tx = 0; tx < TEST_W; tx += 8)
            for (int y = ty; y < ty + 8; y++)
            {
                volatile uint16_t *p = area + y * SCREEN_W + tx;
                p[0] = y; p[1] = y; p[2] = y; p[3] = y; p[4] = y; p[5] = y; p[6] = y; p[7] = y;
            }
}

/* The same, but reading each tile line first: the CPU only fills its cache
 * on reads, so this fetches the line and the stores then land in the cache. */
static void pass_read_first(uint16_t *area)
{
    for (int ty = 0; ty < TEST_H; ty += 8)
        for (int tx = 0; tx < TEST_W; tx += 8)
            for (int y = ty; y < ty + 8; y++)
            {
                volatile uint16_t *p = area + y * SCREEN_W + tx;
                (void) *(volatile uint32_t *) ((uintptr_t) p & ~31u);
                p[0] = y; p[1] = y; p[2] = y; p[3] = y; p[4] = y; p[5] = y; p[6] = y; p[7] = y;
            }
}

static void pass_words(uint16_t *area)
{
    for (int ty = 0; ty < TEST_H; ty += 8)
        for (int tx = 0; tx < TEST_W; tx += 8)
            for (int y = ty; y < ty + 8; y++)
            {
                volatile uint32_t *p = (volatile uint32_t *) (area + y * SCREEN_W + tx);
                p[0] = y; p[1] = y; p[2] = y; p[3] = y;
            }
}

static void pass_reads(uint16_t *area)
{
    uint32_t sum = 0;
    for (int ty = 0; ty < TEST_H; ty += 8)
        for (int tx = 0; tx < TEST_W; tx += 8)
            for (int y = ty; y < ty + 8; y++)
            {
                const volatile uint32_t *p = (const volatile uint32_t *) (area + y * SCREEN_W + tx);
                sum += p[0] + p[1] + p[2] + p[3];
            }
    test_sink = sum;
}

static void pass_memset(uint16_t *area)
{
    for (int y = 0; y < TEST_H; y++)
        memset(area + y * SCREEN_W, 0, TEST_W * 2);
}

static void (*const store_passes[])(uint16_t *) =
    { pass_halfwords, pass_read_first, pass_words, pass_reads, pass_memset };
static const char *const store_pass_names[] =
    { "16-bit stores", "read line first, 16-bit stores", "32-bit stores", "32-bit reads", "memset (bursts)" };
#define STORE_PASSES (int) (sizeof(store_passes) / sizeof(store_passes[0]))

/* Ticks for TEST_PASSES passes, with none of the area cached at the start. */
static uint32_t time_passes(uint16_t *area, void (*pass)(uint16_t *))
{
    flush_dcache(area, TEST_BYTES);
    uint32_t start = platform_ticks();
    for (int i = 0; i < TEST_PASSES; i++)
        pass(area);
    return platform_ticks() - start;
}

static void time_all_passes(uint16_t *area, uint32_t *ticks)
{
    for (int k = 0; k < STORE_PASSES; k++)
        ticks[k] = time_passes(area, store_passes[k]);
}

static void print_passes(FILE *f, const uint32_t *ticks, uint32_t mhz)
{
    const uint32_t pixels = TEST_W * TEST_H * TEST_PASSES;
    for (int k = 0; k < STORE_PASSES; k++)
    {
        uint32_t screen_hundredths = (uint32_t) ((uint64_t) ticks[k] * 100000 * (256 * 224) / pixels / tick_hz);
        uint32_t cycle_tenths = (uint32_t) ((uint64_t) ticks[k] * mhz * 10000000 / tick_hz / pixels);
        fprintf(f, "  %-32s %3lu.%02lu ms per 256x224 screen, %3lu.%lu cycles per pixel\n",
                store_pass_names[k], (unsigned long) (screen_hundredths / 100),
                (unsigned long) (screen_hundredths % 100), (unsigned long) (cycle_tenths / 10),
                (unsigned long) (cycle_tenths % 10));
    }
}

static void write_store_report(FILE *f)
{
    uint32_t mhz = platform_cpu_mhz();
    uint32_t ticks[STORE_PASSES];
    uint32_t irq = interrupts_off();
    time_all_passes(portrait_screens[1], ticks);   /* unused while frames go out by DMA */
    portrait_in_cache = 1;
    interrupts_restore(irq);
    fprintf(f, "Memory speed in normal memory (cpu %lu MHz), drawing tiles like the renderer:\n",
            (unsigned long) mhz);
    print_passes(f, ticks, mhz);
}

/* The on-chip SRAM (Hackspire: 0x40000 bytes at 0xA4000000). Nothing says
 * whether the OS uses it, so it's watched for changes, saved to a file, and
 * written to only last (platform_write_memory_tests), with its contents
 * saved and put back while interrupts are off. */
#define SRAM_BLOCK  4096
#define SRAM_BLOCKS (SRAM_SIZE / SRAM_BLOCK)
#define SRAM_WINDOW_BLOCKS 16   /* 64 KB, enough for the store test */
static int sram_window = -1;    /* first block of a stretch nothing changed */

/* The SRAM's physical address and the descriptor that maps it, or 0 if it
 * isn't all mapped in one piece. */
static uint32_t sram_physical(uint32_t *descriptor)
{
    uint32_t first, second;
    uint32_t pa = mmu_lookup(SRAM_BASE, &first, &second);
    *descriptor = (first & 3) == 2 ? first : second;
    if (pa >= 0xFFFFFFFE || !physically_contiguous(SRAM_BASE, pa, SRAM_SIZE))
        return 0;
    return pa;
}

/* Section and small-page descriptors both keep C and B in bits 3 and 2; the
 * access bits are 11-10 for a section and 5-4 (first quarter) for a page. */
static int sram_writable(uint32_t descriptor)
{
    uint32_t first, second;
    mmu_lookup(SRAM_BASE, &first, &second);
    uint32_t ap = (first & 3) == 2 ? (first >> 10) & 3 : (descriptor >> 4) & 3;
    return ap != 0;
}

static void sram_sums(uint32_t *sums)
{
    flush_dcache((const void *) SRAM_BASE, SRAM_SIZE);
    const volatile uint32_t *p = (const volatile uint32_t *) SRAM_BASE;
    for (int b = 0; b < SRAM_BLOCKS; b++)
    {
        uint32_t sum = 0;
        for (int i = 0; i < SRAM_BLOCK / 4; i++)
            sum = (sum << 5 | sum >> 27) ^ *p++;
        sums[b] = sum;
    }
}

static void write_sram_report(FILE *f)
{
    uint32_t descriptor;
    uint32_t physical = sram_physical(&descriptor);
    if (sram_used)
    {
        fprintf(f, "On-chip SRAM at %08lx: physical %08lx, descriptor %08lx, holds the screen and depth buffer%s\n",
                (unsigned long) SRAM_BASE, (unsigned long) physical, (unsigned long) descriptor,
                sram_write_through ? " (write-through)" : "");
        return;
    }
    if (!physical)
    {
        fprintf(f, "On-chip SRAM at %08lx: not mapped in one piece (descriptor %08lx), not tested\n",
                (unsigned long) SRAM_BASE, (unsigned long) descriptor);
        return;
    }
    fprintf(f, "On-chip SRAM at %08lx: physical %08lx, descriptor %08lx (cached %lu, buffered %lu, writable %d)\n",
            (unsigned long) SRAM_BASE, (unsigned long) physical, (unsigned long) descriptor,
            (unsigned long) ((descriptor >> 3) & 1), (unsigned long) ((descriptor >> 2) & 1),
            sram_writable(descriptor));

    static uint32_t before[SRAM_BLOCKS], after_wait[SRAM_BLOCKS], after_file[SRAM_BLOCKS];
    sram_sums(before);
    uint32_t until = platform_ticks() + 2 * tick_hz;
    while (timer_works && (int32_t) (until - platform_ticks()) > 0)
        ;
    sram_sums(after_wait);

    uint32_t zero_words = 0;
    uint32_t *copy = (uint32_t *) malloc(SRAM_SIZE);
    if (copy)
    {
        memcpy(copy, (const void *) SRAM_BASE, SRAM_SIZE);
        for (int i = 0; i < SRAM_SIZE / 4; i++)
            zero_words += copy[i] == 0;
        char dump_path[300];
        snprintf(dump_path, sizeof(dump_path), "%s/pocketsnes_sram.bin.tns", exe_dir);
        FILE *dump = fopen(dump_path, "wb");
        if (dump)
        {
            fwrite(copy, 1, SRAM_SIZE, dump);
            fclose(dump);
        }
        free(copy);
    }
    sram_sums(after_file);

    char map[SRAM_BLOCKS + 1];
    for (int b = 0; b < SRAM_BLOCKS; b++)
    {
        int waited = after_wait[b] != before[b], filed = after_file[b] != after_wait[b];
        map[b] = waited ? (filed ? 'b' : 'w') : (filed ? 'f' : '.');
    }
    map[SRAM_BLOCKS] = 0;
    fprintf(f, "  4 KB blocks that changed by themselves in 2 s (w), while a file was written (f), both (b):\n  %s\n", map);
    fprintf(f, "  %lu of %lu words are 0; the contents are in pocketsnes_sram.bin.tns\n",
            (unsigned long) zero_words, (unsigned long) (SRAM_SIZE / 4));

    for (int b = SRAM_KEEP / SRAM_BLOCK; b + SRAM_WINDOW_BLOCKS <= SRAM_BLOCKS && sram_window < 0; b += SRAM_WINDOW_BLOCKS)
    {
        int quiet = 1;
        for (int i = b; i < b + SRAM_WINDOW_BLOCKS; i++)
            quiet &= map[i] == '.';
        if (quiet && sram_writable(descriptor))
            sram_window = b;
    }
}

void platform_write_diagnostics(const char *path)
{
    /* A new file each run: appending to the old one has left it garbled on
     * the calculator now and then (parts repeated, the end cut off). */
    FILE *f = fopen(path, "w");
    if (!f)
        return;

    uint16_t *picture = platform_screen();
    uint16_t *normal = game_screen_start(0), *spare = game_screen_start(1);   /* in normal memory */
    uint16_t *shown = (uint16_t *) os_framebuffer;
    uint32_t regs[LCD_REGS];
    int i;

    dma_finish();
    fprintf(f, "== diagnostics\n");
    fprintf(f, "cpu %lu MHz, cpsr at start %08lx, now %08lx\n", (unsigned long) platform_cpu_mhz(),
            (unsigned long) start_cpsr, (unsigned long) read_cpsr());
    fprintf(f, "timer: works %d, %lu ticks/s used, %lu measured against the real-time clock\n",
            timer_works, (unsigned long) tick_hz, (unsigned long) (timer_works ? measure_tick_hz() : 0));
    fprintf(f, "lcd_type %d, CX II portrait LCD %d, screen output %s\n", (int) lcd_type(), fast_available,
            platform_screen_mode_name());

    read_lcd_regs(regs);
    fprintf(f, "LCD registers (offset: at start / now):\n");
    for (i = 0; i < LCD_REGS; i++)
        fprintf(f, "  %02x: %08lx %08lx\n", lcd_reg_offsets[i], (unsigned long) start_regs[i],
                (unsigned long) regs[i]);

    /* Where the LCD's DMA is reading, sampled for about 50 ms. */
    uint32_t cur_min = 0xFFFFFFFF, cur_max = 0, base_changes = 0, base = LCD_BASE;
    uint32_t until = platform_ticks() + tick_hz / 20;
    while ((int32_t) (until - platform_ticks()) > 0)
    {
        uint32_t cur = *(volatile uint32_t *) 0xC000002C;
        if (cur < cur_min) cur_min = cur;
        if (cur > cur_max) cur_max = cur;
        if (LCD_BASE != base)
        {
            base_changes++;
            base = LCD_BASE;
        }
    }
    fprintf(f, "LCD reading %08lx..%08lx (base %08lx, %lu base changes)\n", (unsigned long) cur_min,
            (unsigned long) cur_max, (unsigned long) base, (unsigned long) base_changes);

    fprintf(f, "memory mapping:\n");
    write_mapping(f, "LCD buffer", shown, SCREEN_PIXELS * 2);
    write_mapping(f, "game screen", picture, SCREEN_PIXELS * 2);
    write_mapping(f, "spare screen", spare, SCREEN_PIXELS * 2);
    write_mapping(f, "fast buffer", portrait_screens[0], SCREEN_PIXELS * 2);
    void *heap = malloc(4096);
    if (heap)
    {
        write_mapping(f, "heap", heap, 4096);
        free(heap);
    }
    write_mapping(f, "code", (const void *) &platform_write_diagnostics, 4096);
    write_mapping(f, "vectors", (const void *) 0, 1024);
    write_mapping(f, "stack", &regs, 1024);

    /* Copy speeds, 5 frames' worth each, in ms per 320x240 frame. */
    uint32_t to_own = time_copy(spare, normal, SCREEN_PIXELS * 2, 5);
    uint32_t to_lcd = time_copy(shown, normal, SCREEN_PIXELS * 2, 5);
    uint32_t from_lcd = time_copy(spare, shown, SCREEN_PIXELS * 2, 5);
    uint32_t start = platform_ticks();
    for (i = 0; i < 5; i++)
        rotate_frame(normal, portrait_screens[current_screen], ROTATE_FLIP_COLUMNS);
    uint32_t turn = platform_ticks() - start;
    if (sram_used)
    {
        uint32_t sram_to_lcd = time_copy(shown, picture, SCREEN_PIXELS * 2, 5);
        fprintf(f, "copy a frame from the SRAM screen to the LCD buffer with the CPU: ");
        print_ms(f, sram_to_lcd, 5);
        fprintf(f, " ms\n");
    }
    fprintf(f, "copy a frame (ms): normal memory to normal memory %lu.%02lu, to LCD buffer %lu.%02lu, LCD buffer to spare %lu.%02lu, turned to fast buffer %lu.%02lu\n",
            (unsigned long) (to_own * 1000 / 5 / tick_hz), (unsigned long) (to_own * 100000 / 5 / tick_hz % 100),
            (unsigned long) (to_lcd * 1000 / 5 / tick_hz), (unsigned long) (to_lcd * 100000 / 5 / tick_hz % 100),
            (unsigned long) (from_lcd * 1000 / 5 / tick_hz), (unsigned long) (from_lcd * 100000 / 5 / tick_hz % 100),
            (unsigned long) (turn * 1000 / 5 / tick_hz), (unsigned long) (turn * 100000 / 5 / tick_hz % 100));
    clean_dcache(shown, SCREEN_PIXELS * 2);
    fclose(f);

    append_report(path, write_cpu_report);
    append_report(path, write_clock_report);
    append_report(path, write_dma_report);
    append_report(path, write_store_report);
    append_report(path, write_sram_report);
}

void platform_write_memory_tests(const char *path)
{
    /* Can the DMA controller read the SRAM? It copies 64 KB of startup
     * leftovers (never written here) into normal memory, compared with what
     * the CPU reads. Nothing in the SRAM is changed. */
    uint32_t descriptor;
    uint32_t physical = sram_physical(&descriptor);
    if (!physical || !dma_works || sram_used)
        return;
    const uint32_t bytes = 0x10000, offset = 0x20000;
    uint8_t *copy = (uint8_t *) portrait_screens[2];   /* unused while frames go out by DMA */
    portrait_in_cache = 1;
    uint32_t first, second;
    uint32_t copy_physical = mmu_lookup((uint32_t) copy, &first, &second);
    if (copy_physical >= 0xFFFFFFFE || !physically_contiguous((uint32_t) copy, copy_physical, bytes))
        return;

    memset(copy, 0x5A, bytes);
    flush_dcache(copy, bytes);
    dma_start(physical + offset, copy_physical, bytes);
    int ok = dma_finish();
    flush_dcache(copy, bytes);
    const uint8_t *sram = (const uint8_t *) (SRAM_BASE + offset);
    uint32_t same = 0, zero = 0, untouched = 0;
    for (uint32_t i = 0; i < bytes; i++)
    {
        same += copy[i] == sram[i];
        zero += copy[i] == 0;
        untouched += copy[i] == 0x5A;
    }
    dma_works = 1;
    dma_failed = 0;

    FILE *f = fopen(path, "a");
    if (!f)
        return;
    fprintf(f, "DMA read of 64 KB of SRAM into normal memory: %s, %lu bytes match what the CPU reads, %lu are 0, %lu untouched\n",
            ok ? "finished" : "FAILED", (unsigned long) same, (unsigned long) zero, (unsigned long) untouched);
    fclose(f);
}

int platform_set_screen_output(int output)
{
    if (output != OUTPUT_BEST && !fast_available)
        return 0;   /* only lcd_blit works on this screen */
    if (output == OUTPUT_DMA && !dma_works)
        return 0;
    /* A copy may still be writing the LCD buffer. */
    dma_finish();
    forced_output = output;
    if (output_now() != SHOWN_BY_FLIP)
        show_os_framebuffer();
    return 1;
}

void platform_poll_keys(void)
{
    volatile uint32_t *keypad = (volatile uint32_t *) 0x900E0010;

    memcpy(keys_old, keys_now, sizeof(keys_now));
    keys_now[0] = keypad[0] | (on_key_pressed() ? 0x200 : 0);
    keys_now[1] = keypad[1];
    keys_now[2] = keypad[2];
    keys_now[3] = keypad[3];

    /* On touchpad keypads the arrows come from the pad, as in lr-gpsp-nspire. */
    if (touchpad)
    {
        touchpad_report_t report;
        uint32_t arrows = 0;

        touchpad_scan(&report);
        switch ((tpad_arrow_t) report.arrow)
        {
        case TPAD_ARROW_UP:         arrows = 0x00010000; break;
        case TPAD_ARROW_UPRIGHT:    arrows = 0x00050000; break;
        case TPAD_ARROW_RIGHT:      arrows = 0x00040000; break;
        case TPAD_ARROW_RIGHTDOWN:  arrows = 0x00140000; break;
        case TPAD_ARROW_DOWN:       arrows = 0x00100000; break;
        case TPAD_ARROW_DOWNLEFT:   arrows = 0x00500000; break;
        case TPAD_ARROW_LEFT:       arrows = 0x00400000; break;
        case TPAD_ARROW_LEFTUP:     arrows = 0x00410000; break;
        case TPAD_ARROW_CLICK:      arrows = 0x00000002; break;
        default:                    break;
        }
        keys_now[3] = (keys_now[3] & 0xFF00FFFD) | arrows;
    }
}

int platform_key_down(int key)
{
    if (key <= 0 || key > NUM_KEYS)
        return 0;
    return (keys_now[(key - 1) >> 5] >> ((key - 1) & 31)) & 1;
}

int platform_key_pressed(int key)
{
    if (key <= 0 || key > NUM_KEYS)
        return 0;
    return ((keys_now[(key - 1) >> 5] & ~keys_old[(key - 1) >> 5]) >> ((key - 1) & 31)) & 1;
}

int platform_any_key_down(void)
{
    return (keys_now[0] | keys_now[1] | keys_now[2] | keys_now[3]) != 0;
}

int platform_first_key_down(void)
{
    for (int key = 1; key <= NUM_KEYS; key++)
        if (platform_key_down(key))
            return key;
    return 0;
}

uint32_t platform_ticks(void)
{
    if (!timer_works)
        return fallback_ticks;
    return ~*TIMER_VALUE;   /* the timer counts down */
}

uint32_t platform_tick_hz(void)
{
    return tick_hz;
}

void platform_wait_until(uint32_t tick)
{
    if (!timer_works)
    {
        if ((int32_t) (tick - fallback_ticks) > 0)
            fallback_ticks = tick;
        return;
    }
    /* Never wait more than a second, whatever the caller asked for. */
    if ((int32_t) (tick - platform_ticks()) > (int32_t) tick_hz)
        return;
    while ((int32_t) (tick - platform_ticks()) > 0)
        ;
}

const char *platform_exe_dir(void)
{
    return exe_dir;
}

int platform_quit_requested(void)
{
    return 0;
}

void platform_log(const char *text)
{
    (void) text;
}
