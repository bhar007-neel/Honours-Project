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

void rt_stats_init(rt_stats_t *s, uint32_t nominal_period_us, uint32_t tolerance_pct,
                   uint32_t cycles_per_us);

/* Clears measurements and applies a new nominal period (e.g. after a rate change). */
void rt_stats_reset(rt_stats_t *s, uint32_t nominal_period_us, uint32_t tolerance_pct);

void rt_stats_on_release(rt_stats_t *s, uint32_t now_cycles);
void rt_stats_on_complete(rt_stats_t *s, uint32_t now_cycles);

/* Average measured period in microseconds, or 0 before two releases. */
uint32_t rt_stats_avg_period_us(const rt_stats_t *s);

#endif /* RT_STATS_H */
