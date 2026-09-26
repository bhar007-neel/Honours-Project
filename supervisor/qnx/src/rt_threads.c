/*
 * rt_threads: periodic real-time threads on QNX.
 *
 * Each worker thread owns a channel and a POSIX timer that delivers a pulse
 * every period, the idiomatic QNX way to build a periodic task. On each pulse
 * the thread busy-works for a fixed time and records period jitter, overruns,
 * and execution time. With -l, one SCHED_FIFO CPU hog per core runs below the
 * workers' priorities to show that priority-based preemption keeps the
 * high-priority threads on time even when every core is saturated.
 *
 * Usage: rt_threads [-d seconds] [-l]
 */
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/neutrino.h>
#include <sys/syspage.h>
#include <time.h>
#include <unistd.h>

#include "rt_stats.h"

#define PULSE_CODE_TICK      (_PULSE_CODE_MINAVAIL + 1)
#define HOG_PRIORITY         8
#define OVERRUN_TOLERANCE_PCT 10u

typedef struct {
    const char *name;
    int priority;
    uint32_t period_us;
    uint32_t work_us;
    pthread_t thread;
    rt_stats_t stats;
} worker_t;

/* Rate-monotonic: shorter period, higher priority. Unprivileged limit is 63. */
static worker_t g_workers[] = {
    {.name = "fast",   .priority = 30, .period_us = 2000,  .work_us = 300},
    {.name = "medium", .priority = 20, .period_us = 10000, .work_us = 2000},
    {.name = "slow",   .priority = 10, .period_us = 50000, .work_us = 10000},
};
#define WORKER_COUNT (sizeof g_workers / sizeof g_workers[0])

static atomic_bool g_stop;
static uint32_t g_cycles_per_us;

static void spin_for_us(uint32_t us)
{
    const uint64_t end = ClockCycles() + (uint64_t)us * g_cycles_per_us;
    while (ClockCycles() < end) {
    }
}

static void *worker_main(void *arg)
{
    worker_t *w = arg;

    const int chid = ChannelCreate(0);
    const int coid = ConnectAttach(0, 0, chid, _NTO_SIDE_CHANNEL, 0);
    if (chid == -1 || coid == -1) {
        perror("ChannelCreate/ConnectAttach");
        return NULL;
    }

    struct sigevent ev;
    SIGEV_PULSE_INIT(&ev, coid, w->priority, PULSE_CODE_TICK, 0);

    timer_t timer;
    if (timer_create(CLOCK_MONOTONIC, &ev, &timer) == -1) {
        perror("timer_create");
        return NULL;
    }
    const struct itimerspec its = {
        .it_value = {0, (long)w->period_us * 1000L},
        .it_interval = {0, (long)w->period_us * 1000L},
    };
    timer_settime(timer, 0, &its, NULL);

    rt_stats_init(&w->stats, w->period_us, OVERRUN_TOLERANCE_PCT, g_cycles_per_us);

    while (!atomic_load(&g_stop)) {
        struct _pulse pulse;
        if (MsgReceivePulse(chid, &pulse, sizeof pulse, NULL) == -1) {
            if (errno == EINTR) continue;
            perror("MsgReceivePulse");
            break;
        }
        if (pulse.code != PULSE_CODE_TICK) continue;

        rt_stats_on_release(&w->stats, (uint32_t)ClockCycles());
        spin_for_us(w->work_us);
        rt_stats_on_complete(&w->stats, (uint32_t)ClockCycles());
    }

    timer_delete(timer);
    ConnectDetach(coid);
    ChannelDestroy(chid);
    return NULL;
}

static void *hog_main(void *arg)
{
    (void)arg;
    while (!atomic_load(&g_stop)) {
    }
    return NULL;
}

static int start_fifo_thread(pthread_t *tid, int priority, void *(*fn)(void *), void *arg)
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    struct sched_param param = {.sched_priority = priority};
    pthread_attr_setschedparam(&attr, &param);
    const int rc = pthread_create(tid, &attr, fn, arg);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        fprintf(stderr, "pthread_create(priority %d): %s\n", priority, strerror(rc));
    }
    return rc;
}

static void on_signal(int sig)
{
    (void)sig;
    atomic_store(&g_stop, true);
}

int main(int argc, char **argv)
{
    int duration_s = 10;
    bool load = false;
    int opt;
    while ((opt = getopt(argc, argv, "d:l")) != -1) {
        switch (opt) {
        case 'd': duration_s = atoi(optarg); break;
        case 'l': load = true; break;
        default:
            fprintf(stderr, "usage: %s [-d seconds] [-l]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    g_cycles_per_us = (uint32_t)(SYSPAGE_ENTRY(qtime)->cycles_per_sec / 1000000ULL);
    signal(SIGINT, on_signal);

    const unsigned cpus = _syspage_ptr->num_cpu;
    printf("rt_threads: %d s, %u CPUs, load=%s, cycles_per_us=%" PRIu32 "\n", duration_s, cpus,
           load ? "on" : "off", g_cycles_per_us);

    for (size_t i = 0; i < WORKER_COUNT; ++i) {
        if (start_fifo_thread(&g_workers[i].thread, g_workers[i].priority, worker_main,
                              &g_workers[i]) != 0) {
            return EXIT_FAILURE;
        }
    }

    pthread_t hogs[64];
    unsigned hog_count = 0;
    if (load) {
        for (; hog_count < cpus && hog_count < 64; ++hog_count) {
            if (start_fifo_thread(&hogs[hog_count], HOG_PRIORITY, hog_main, NULL) != 0) {
                break;
            }
        }
    }

    for (int s = 0; s < duration_s && !atomic_load(&g_stop); ++s) {
        sleep(1);
    }
    atomic_store(&g_stop, true);

    for (size_t i = 0; i < WORKER_COUNT; ++i) {
        pthread_join(g_workers[i].thread, NULL);
    }
    for (unsigned i = 0; i < hog_count; ++i) {
        pthread_join(hogs[i], NULL);
    }

    printf("\n%-7s %4s %9s %6s %9s %9s %9s %10s %8s %9s\n", "thread", "prio", "period_us", "jobs",
           "min_us", "avg_us", "max_us", "jitter_us", "overruns", "exec_us");
    for (size_t i = 0; i < WORKER_COUNT; ++i) {
        const worker_t *w = &g_workers[i];
        const rt_stats_t *s = &w->stats;
        printf("%-7s %4d %9" PRIu32 " %6" PRIu32 " %9" PRIu32 " %9" PRIu32 " %9" PRIu32
               " %10" PRIu32 " %8" PRIu32 " %9" PRIu32 "\n",
               w->name, w->priority, w->period_us, s->jobs, s->jobs ? s->min_period_us : 0,
               rt_stats_avg_period_us(s), s->max_period_us, s->max_jitter_us, s->overruns,
               s->max_exec_us);
    }
    return EXIT_SUCCESS;
}
