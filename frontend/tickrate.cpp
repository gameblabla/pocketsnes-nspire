/* The timer's speed against the real-time clock: see tickrate.h. */
#include <stdio.h>

#include "tickrate.h"

#define MIN_SECONDS 2    /* each stretch timed */
#define MAX_SECONDS 60   /* longer: the clock was set, start over */
#define OFF_PERCENT 5    /* off by more than this: corrected */

/* A change of the real-time clock's second, between two timer readings. */
struct Edge
{
    uint32_t before, after, seconds;
};

static uint32_t meant_hz, measured_hz;
static int checked, corrected;
static int have_last, have_start;
static uint32_t last_raw, last_seconds;
static struct Edge start;    /* where the stretch being timed starts */
static uint32_t stretch_hz;  /* the last stretch's speed, for the next to agree with */

/* Once corrected: ticks = base_ticks + (raw - base_raw) * scale / 65536. */
static uint32_t base_raw, base_ticks, scale;

void tick_rate_start(uint32_t hz)
{
    meant_hz = hz;
    measured_hz = 0;
    checked = corrected = 0;
    have_last = have_start = 0;
    stretch_hz = 0;
}

static void decide(uint32_t raw, uint32_t hz)
{
    checked = 1;
    measured_hz = hz;
    uint32_t off = hz > meant_hz ? hz - meant_hz : meant_hz - hz;
    if ((uint64_t) off * 100 > (uint64_t) meant_hz * OFF_PERCENT)
    {
        corrected = 1;
        scale = (uint32_t) (((uint64_t) meant_hz << 16) / hz);
        base_raw = raw;
        base_ticks = raw;   /* the ticks until now were the readings themselves */
    }
}

static void edge(uint32_t raw, const struct Edge *e)
{
    if (!have_start)
    {
        start = *e;
        have_start = 1;
        return;
    }

    uint32_t seconds = e->seconds - start.seconds;
    if (seconds > MAX_SECONDS)
    {
        start = *e;
        stretch_hz = 0;
        return;
    }

    /* The second changed somewhere between 'before' and 'after' at both
     * ends, so the stretch is timed to within both of those gaps. */
    uint32_t shortest = e->before - start.after;
    uint32_t blur = (e->after - e->before) + (start.after - start.before);
    if (seconds >= MIN_SECONDS && shortest > 0 && blur <= shortest / 100)
    {
        uint32_t hz = (uint32_t) (((uint64_t) (e->before - start.before) + (e->after - start.after)) /
                                  (2 * seconds));
        uint32_t diff = hz > stretch_hz ? hz - stretch_hz : stretch_hz - hz;
        start = *e;
        if (hz < meant_hz / 64 || hz / 64 > meant_hz)
            stretch_hz = 0;
        else if (stretch_hz && (uint64_t) diff * 100 <= stretch_hz)
            decide(raw, (uint32_t) (((uint64_t) hz + stretch_hz) / 2));
        else
            stretch_hz = hz;
        return;
    }

    /* A sharper start for the stretch. */
    if (e->after - e->before < start.after - start.before)
        start = *e;
}

uint32_t tick_rate_ticks(uint32_t raw, uint32_t seconds)
{
    if (!checked)
    {
        if (have_last && seconds != last_seconds)
        {
            struct Edge e = { last_raw, raw, seconds };
            edge(raw, &e);
        }
        have_last = 1;
        last_raw = raw;
        last_seconds = seconds;
    }
    if (!corrected)
        return raw;

    uint32_t since = raw - base_raw;
    uint32_t ticks = base_ticks + (uint32_t) (((uint64_t) since * scale) >> 16);
    if (since >= 0x40000000u)
    {
        base_raw = raw;
        base_ticks = ticks;
    }
    return ticks;
}

int tick_rate_checked(void)
{
    return checked;
}

void tick_rate_report(char *text, size_t size)
{
    if (!checked)
        snprintf(text, size, "timer: %lu ticks/s, not checked against the real-time clock yet",
                 (unsigned long) meant_hz);
    else if (corrected)
        snprintf(text, size, "timer: %lu ticks/s by the real-time clock, not %lu; corrected",
                 (unsigned long) measured_hz, (unsigned long) meant_hz);
    else
        snprintf(text, size, "timer: %lu ticks/s (%lu by the real-time clock)", (unsigned long) meant_hz,
                 (unsigned long) measured_hz);
}
