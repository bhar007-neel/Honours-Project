/*
 * Timing statistics for one periodic real-time task.
 *
 * Timestamps are raw cycle counts: DWT->CYCCNT on the STM32, the low 32 bits
 * of ClockCycles() on QNX. Unsigned subtraction handles a single counter wrap,
 * which at 250 MHz allows periods and execution times up to about 17 seconds.
 *
 * Definitions used in the report:
 *   period      time between two consecutive job releases
 *   jitter      |measured period - nominal period|
 *   overrun     a period longer than nominal + tolerance, i.e. a late release
 *   exec time   release to completion of one job
 */
#ifndef RT_STATS_H
#define RT_STATS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t nominal_period_us;
    uint32_t tolerance_us;
    uint32_t cycles_per_us;

    uint32_t jobs;
    uint32_t min_period_us;
    uint32_t max_period_us;
    uint64_t sum_period_us;
    uint32_t max_jitter_us;
    uint32_t overruns;
    uint32_t max_exec_us;

    uint32_t last_release;
    bool has_last_release;
} rt_stats_t;

/* Prepares s for one periodic task.
 * nominal_period_us is the intended gap between releases.
 * tolerance_pct is how far past that gap a release may slip before it counts
 * as an overrun (10 means 10 percent).
 * cycles_per_us converts the caller's cycle counter into microseconds.
 * A value of 0 is treated as 1 so later divisions stay safe. */
void rt_stats_init(rt_stats_t *s, uint32_t nominal_period_us, uint32_t tolerance_pct,
                   uint32_t cycles_per_us);

/* Wipes the measurements and stores a new nominal period and tolerance.
 * cycles_per_us is left alone. Call this after a sample-rate change. */
void rt_stats_reset(rt_stats_t *s, uint32_t nominal_period_us, uint32_t tolerance_pct);

/* Call when a job is released, with the cycle counter at that instant.
 * The first call only remembers now_cycles. Each later call measures the gap
 * since the previous release and updates period, jitter, and overrun. */
void rt_stats_on_release(rt_stats_t *s, uint32_t now_cycles);

/* Call when that job finishes. Measures execution time from the latest
 * release to now_cycles and keeps the longest one seen. Does nothing if
 * on_release has never been called. */
void rt_stats_on_complete(rt_stats_t *s, uint32_t now_cycles);

/* Average of the measured periods, in microseconds.
 * Returns 0 until two releases have happened (jobs is still 0). */
uint32_t rt_stats_avg_period_us(const rt_stats_t *s);

#endif /* RT_STATS_H */
