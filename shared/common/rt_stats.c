#include "rt_stats.h"

#include <stddef.h>

void rt_stats_init(rt_stats_t *s, uint32_t nominal_period_us, uint32_t tolerance_pct,
                   uint32_t cycles_per_us)
{
    s->cycles_per_us = (cycles_per_us != 0u) ? cycles_per_us : 1u;
    rt_stats_reset(s, nominal_period_us, tolerance_pct);
}

void rt_stats_reset(rt_stats_t *s, uint32_t nominal_period_us, uint32_t tolerance_pct)
{
    s->nominal_period_us = nominal_period_us;
    s->tolerance_us = (uint32_t)(((uint64_t)nominal_period_us * tolerance_pct) / 100u);
    s->jobs = 0;
    s->min_period_us = UINT32_MAX;
    s->max_period_us = 0;
    s->sum_period_us = 0;
    s->max_jitter_us = 0;
    s->overruns = 0;
    s->max_exec_us = 0;
    s->last_release = 0;
    s->has_last_release = false;
}

void rt_stats_on_release(rt_stats_t *s, uint32_t now_cycles)
{
    if (s->has_last_release) {
        const uint32_t period_us = (now_cycles - s->last_release) / s->cycles_per_us;
        const uint32_t jitter_us = (period_us > s->nominal_period_us)
                                       ? period_us - s->nominal_period_us
                                       : s->nominal_period_us - period_us;

        if (period_us < s->min_period_us) {
            s->min_period_us = period_us;
        }
        if (period_us > s->max_period_us) {
            s->max_period_us = period_us;
        }
        if (jitter_us > s->max_jitter_us) {
            s->max_jitter_us = jitter_us;
        }
        if (period_us > s->nominal_period_us + s->tolerance_us) {
            ++s->overruns;
        }
        s->sum_period_us += period_us;
        ++s->jobs;
    }
    s->last_release = now_cycles;
    s->has_last_release = true;
}

void rt_stats_on_complete(rt_stats_t *s, uint32_t now_cycles)
{
    if (!s->has_last_release) {
        return;
    }
    const uint32_t exec_us = (now_cycles - s->last_release) / s->cycles_per_us;
    if (exec_us > s->max_exec_us) {
        s->max_exec_us = exec_us;
    }
}

uint32_t rt_stats_avg_period_us(const rt_stats_t *s)
{
    return (s->jobs == 0u) ? 0u : (uint32_t)(s->sum_period_us / s->jobs);
}
