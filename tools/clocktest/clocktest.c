/* PocketSNES clock test for the TI-Nspire CX II: does the clock switch
 * PocketSNES uses work on this calculator?
 *
 * The CPU clock is 12 MHz times the multiplier in bits 24-29 of the power
 * controller's register 0x90140030. Writing the register alone does nothing
 * (the first version of this test showed that, on USB and on battery). The
 * OS switches by putting normal memory to sleep, writing the register,
 * starting the switch through register 0x20 and waiting for the power
 * controller's interrupt (frontend/clock_switch_nspire.S). This test does the
 * same: two steps down (24 MHz) and back, then two steps up and back, and
 * measures the clock after each by timing a loop of known length with the
 * 32768 Hz timer. The results are shown and written to
 * pocketsnes_clocktest.txt.tns next to this program; the file is written
 * before each switch too, so after a freeze it says how far the test got. */
#include <os.h>
#include <libndls.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define REG(address) (*(volatile uint32_t *) (address))
#define CLOCK        REG(0x90140030)
#define MULTIPLIER(value) ((int) (((value) >> 24) & 0x3F))
#define PMU(offset)  REG(0x90140000 + (offset))
#define MEMC(offset) REG(0x90120000 + (offset))
/* The first timer measures (32768 Hz, free running); the switch uses the
 * second one to give up on a step that doesn't finish. */
#define T1(offset)   REG(0x900C0000 + (offset))
#define T2(offset)   REG(0x900D0000 + (offset))
#define VIC(offset)  REG(0xDC000000 + (offset))
#define LCD_BASE     REG(0xC0000010)
#define LCD_CURRENT  REG(0xC000002C)

/* The switch runs from here in the on-chip SRAM: past the OS's code
 * (0xA4000000) and the MMU's page table (0xA4004000-0xA4007FFF), in 1 KB
 * that was zero in every dump. What's there is put back afterwards. */
#define SWITCH_SRAM 0xA4035000u
#define STEP_TICKS  1092   /* 1/30 s */

extern const uint8_t clock_switch_code[], clock_switch_code_end[];

static char report[6144];
static size_t report_len;
static char log_path[300];

static void say(const char *format, ...) __attribute__ ((format (printf, 1, 2)));
static void say(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int n = vsnprintf(report + report_len, sizeof(report) - report_len, format, args);
    va_end(args);
    if (n > 0)
        report_len += (size_t) n < sizeof(report) - report_len ? (size_t) n : sizeof(report) - report_len - 1;
}

/* A new file each time: appending has garbled files on the calculator. */
static void save_report(void)
{
    FILE *f = fopen(log_path, "w");
    if (f)
    {
        fputs(report, f);
        fclose(f);
    }
}

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

static uint32_t ticks(void)
{
    return ~T1(0x04);   /* counts down */
}

/* 16 million cycles (SUBS 1 + taken branch 3, from the instruction cache),
 * timed with interrupts off. */
static uint32_t measure_mhz(void)
{
    uint32_t loops = 4000000;
    uint32_t cpsr = interrupts_off();
    uint32_t start = ticks();
    __asm__ volatile ("1: subs %0, %0, #1\n\tbne 1b" : "+r" (loops) : : "cc");
    uint32_t elapsed = ticks() - start;
    interrupts_restore(cpsr);
    if (!elapsed)
        return 0;
    return (uint32_t) ((16000000ull * 32768 + elapsed * 500000ull) / elapsed / 1000000);
}

static void sync_code(void)
{
    __asm__ volatile ("mcr p15, 0, %0, c7, c10, 4\n\t"   /* drain the write buffer */
                      "mcr p15, 0, %0, c7, c5, 0"        /* invalidate the instruction cache */
                      : : "r" (0) : "memory");
}

/* Switches to 'multiplier' from the SRAM, with interrupts off. Returns the
 * switch's result (clock_switch_nspire.S) and the ticks it took. */
static uint32_t run_switch(int multiplier, uint32_t *took)
{
    static uint8_t saved[1024];
    size_t size = (size_t) (clock_switch_code_end - clock_switch_code);
    uint8_t *sram = (uint8_t *) SWITCH_SRAM;
    uint32_t target = (CLOCK & ~(0x3Fu << 24)) | (uint32_t) multiplier << 24;

    uint32_t cpsr = interrupts_off();
    uint32_t control = T2(0x08), load = T2(0x00), speed = T2(0x80);
    T2(0x08) = 0;
    T2(0x80) = 0xA;   /* 32768 Hz, as the first timer */
    memcpy(saved, sram, size);
    memcpy(sram, clock_switch_code, size);
    sync_code();
    uint32_t start = ticks();
    uint32_t result = ((uint32_t (*)(uint32_t, uint32_t)) SWITCH_SRAM)(target, STEP_TICKS);
    *took = ticks() - start;
    memcpy(sram, saved, size);
    sync_code();
    T2(0x08) = 0;
    T2(0x0C) = 1;
    T2(0x80) = speed;
    T2(0x08) = control & 0x7F;
    T2(0x00) = load;
    T2(0x08) = control;
    interrupts_restore(cpsr);
    return result;
}

static void state(void)
{
    say("    clock %08lx, 0x20 %08lx, flags %08lx, memory 4:%08lx 1C:%08lx 74:%08lx, irq raw %08lx\n",
        (unsigned long) CLOCK, (unsigned long) PMU(0x20), (unsigned long) PMU(0x24),
        (unsigned long) MEMC(0x04), (unsigned long) MEMC(0x1C), (unsigned long) MEMC(0x74),
        (unsigned long) VIC(0x08));
}

static const char *result_text(uint32_t result)
{
    static char text[120];
    snprintf(text, sizeof(text), "%lu step(s)%s%s%s%s", (unsigned long) (result & 0xFF),
             result & 0x100 ? ", memory didn't sleep" : "",
             result & 0x200 ? ", a step timed out" : "",
             result & 0x400 ? ", memory didn't wake" : "",
             result & 0x800 ? ", power interrupt stuck on" : "");
    return text;
}

/* One switch and a measurement; returns the clock in MHz. */
static uint32_t switch_and_measure(const char *name, int multiplier)
{
    uint32_t took;
    uint32_t result = run_switch(multiplier, &took);
    uint32_t mhz = measure_mhz();
    say("%s to x%d (%d MHz): %s, %lu.%02lu ms; measured %lu MHz %s\n", name, multiplier, multiplier * 12,
        result_text(result), (unsigned long) (took * 1000 / 32768),
        (unsigned long) (took * 100000 / 32768 % 100), (unsigned long) mhz,
        mhz * 50 >= (uint32_t) multiplier * 12 * 49 && mhz * 50 <= (uint32_t) multiplier * 12 * 51
        ? "(switched)" : "(NOT switched)");
    state();
    return mhz;
}

/* Down or up by 'delta' steps and back. Returns 1 if both switches got there. */
static int round_trip(int from, int delta)
{
    char line[80];
    snprintf(line, sizeof(line), "Now switching x%d -> x%d -> x%d...\n", from, from + delta, from);
    size_t mark = report_len;
    say("%s", line);
    save_report();
    report_len = mark;
    report[report_len] = 0;

    uint32_t there = switch_and_measure(delta < 0 ? "Down" : "Up", from + delta);
    uint32_t back = switch_and_measure("  back", from);
    save_report();
    int want_there = (from + delta) * 12, want_back = from * 12;
    return there * 50 >= (uint32_t) want_there * 49 && there * 50 <= (uint32_t) want_there * 51 &&
           back * 50 >= (uint32_t) want_back * 49 && back * 50 <= (uint32_t) want_back * 51;
}

int main(int argc, char **argv)
{
    (void) argc;
    snprintf(log_path, sizeof(log_path), "%s", argv[0]);
    char *slash = strrchr(log_path, '/');
    snprintf(slash ? slash + 1 : log_path, sizeof(log_path) - (slash ? (size_t) (slash + 1 - log_path) : 0),
             "pocketsnes_clocktest.txt.tns");

    if (!is_cx2)
    {
        show_msgbox("Clock test", "This test is for the TI-Nspire CX II.");
        return 0;
    }

    uint32_t saved_load = T1(0x00), saved_control = T1(0x08), saved_speed = T1(0x80);
    T1(0x08) = 0;
    T1(0x80) = 0xA;            /* 32768 Hz */
    T1(0x00) = 0xFFFFFFFF;
    T1(0x08) = 0x82;           /* on, free running, 32-bit, no interrupt */
    uint32_t first = T1(0x04);
    for (int i = 0; i < 4000000 && T1(0x04) == first; i++)
        ;
    int timer_runs = T1(0x04) != first;

    uint32_t original = CLOCK, cpsr, ttb;
    int m = MULTIPLIER(original);
    __asm__ volatile ("mrs %0, cpsr" : "=r" (cpsr));
    __asm__ volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r" (ttb));
    say("PocketSNES clock test 2. Clock register %08lx: x%d = %d MHz%s. cpsr %08lx, page table %08lx\n",
        (unsigned long) original, m, m * 12, m == 24 ? " (USB plugged in)" : "", (unsigned long) cpsr,
        (unsigned long) ttb);
    say("Power controller: 00 %08lx, 04 %08lx, 08 %08lx, 20 %08lx, 24 %08lx, 50 %08lx, 60 %08lx, "
        "800 %08lx, 804 %08lx, 808 %08lx, 80C %08lx, 810 %08lx\n",
        (unsigned long) PMU(0x00), (unsigned long) PMU(0x04), (unsigned long) PMU(0x08),
        (unsigned long) PMU(0x20), (unsigned long) PMU(0x24), (unsigned long) PMU(0x50),
        (unsigned long) PMU(0x60), (unsigned long) PMU(0x800), (unsigned long) PMU(0x804),
        (unsigned long) PMU(0x808), (unsigned long) PMU(0x80C), (unsigned long) PMU(0x810));
    say("Memory controller: 04 %08lx, 1C %08lx, 74 %08lx. Interrupts: raw %08lx, enabled %08lx, "
        "FIQ %08lx. LCD: base %08lx, reading %08lx. Second timer: control %08lx, speed %08lx\n",
        (unsigned long) MEMC(0x04), (unsigned long) MEMC(0x1C), (unsigned long) MEMC(0x74),
        (unsigned long) VIC(0x08), (unsigned long) VIC(0x10), (unsigned long) VIC(0x0C),
        (unsigned long) LCD_BASE, (unsigned long) LCD_CURRENT, (unsigned long) T2(0x08),
        (unsigned long) T2(0x80));

    /* The OS's own switch code in the SRAM, as on OS 6.4.0.74, and its table
     * of clock steps (frequency, clock register) that it steps through. */
    static const uint32_t os_step_code[] = { 0xE92D40F8, 0xE59F4038, 0xE3A05337, 0xE5957010, 0xE5840030 };
    int os_code_known = memcmp((const void *) 0xA4001678, os_step_code, sizeof(os_step_code)) == 0;
    say("OS switch code at A4001678: %s\n", os_code_known ? "as on OS 6.4" : "different");
    uint32_t table = REG(0xA40017A4);
    if (os_code_known && table >= 0x10000000 && table < 0x14000000)
    {
        say("OS clock steps at %08lx:", (unsigned long) table);
        for (int i = 0; i < 16; i++)
            say(" %08lx", (unsigned long) REG(table + i * 4));
        say("\n");
    }

    size_t size = (size_t) (clock_switch_code_end - clock_switch_code);
    uint32_t lcd = LCD_BASE, pmu20 = PMU(0x20);
    const char *refused = NULL;
    if (!timer_runs)
        refused = "the timer doesn't run";
    else if (size > 1024)
        refused = "the switch code is too big";
    else if (m < 20 || m > 36)
        refused = "the multiplier is outside 20..36";
    else if ((original & 0xFF) != 0x03)
        refused = "the clock register isn't in its usual mode (low byte 03)";
    else if ((pmu20 & ~0x100u) != 0x10000000)
        refused = "power controller register 0x20 isn't 10000000";
    else if (MEMC(0x04) & 0x400)
        refused = "the memory is asleep?";
    else if (lcd < 0xA8000000 || lcd >= 0xA8040000)
        refused = "the LCD reads normal memory, which sleeps during a switch";

    int works = 0;
    if (refused)
        say("Nothing tried: %s.\n", refused);
    else
    {
        say("Start: %lu MHz\n", (unsigned long) measure_mhz());
        int down = round_trip(m, -2);
        int up = round_trip(m, 2);
        works = down && up;
        say(works ? "The clock switch works.\n" : "The clock switch did NOT work.\n");
    }

    T1(0x08) = 0;
    T1(0x80) = saved_speed;
    T1(0x00) = saved_load;
    T1(0x08) = saved_control;
    save_report();

    char summary[160];
    if (refused)
        snprintf(summary, sizeof(summary), "Nothing tried: %s.", refused);
    else
        snprintf(summary, sizeof(summary), "%s\n\nDetails are in pocketsnes_clocktest.txt.",
                 works ? "The clock switch works on this calculator."
                       : "The clock switch did NOT work on this calculator.");
    show_msgbox("PocketSNES clock test", summary);
    return 0;
}
