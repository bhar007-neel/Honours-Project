/*
 * qnx_sysinfo: first QNX application for the Raspberry Pi 4.
 *
 * Reports what the kernel exposes about the platform (OS identity, CPUs,
 * clock tick, cycle counter, scheduling limits) and measures how accurately
 * nanosleep() honours short delays, which bounds the timing precision
 * available to the supervisor.
 *
 * Usage: qnx_sysinfo [-n sleep_samples] [-u sleep_us]
 */
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/neutrino.h>
#include <sys/syspage.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

static void print_identity(void)
{
    struct utsname u;
    if (uname(&u) == 0) {
        printf("os.system=%s os.release=%s os.version=%s os.machine=%s os.node=%s\n",
               u.sysname, u.release, u.version, u.machine, u.nodename);
    }
    printf("cpu.count=%u\n", (unsigned)_syspage_ptr->num_cpu);
    printf("process.pid=%d process.ppid=%d\n", (int)getpid(), (int)getppid());
}

static void print_clocks(uint64_t cycles_per_sec)
{
    struct _clockperiod period;
    if (ClockPeriod(CLOCK_REALTIME, NULL, &period, 0) == 0) {
        printf("clock.tick_ns=%" PRIu32 "\n", (uint32_t)period.nsec);
    }

    struct timespec res;
    if (clock_getres(CLOCK_MONOTONIC, &res) == 0) {
        printf("clock.monotonic_res_ns=%ld\n", (long)(res.tv_sec * 1000000000L + res.tv_nsec));
    }
    printf("clock.cycles_per_sec=%" PRIu64 "\n", cycles_per_sec);

    struct timespec up;
    if (clock_gettime(CLOCK_MONOTONIC, &up) == 0) {
        printf("clock.uptime_s=%ld\n", (long)up.tv_sec);
    }
}

static void print_scheduling(void)
{
    int policy;
    struct sched_param param;
    if (pthread_getschedparam(pthread_self(), &policy, &param) == 0) {
        const char *name = policy == SCHED_FIFO ? "FIFO" : policy == SCHED_RR ? "RR" : "OTHER";
        printf("sched.policy=%s sched.priority=%d\n", name, param.sched_priority);
    }
    printf("sched.fifo_priority_min=%d sched.fifo_priority_max=%d\n",
           sched_get_priority_min(SCHED_FIFO), sched_get_priority_max(SCHED_FIFO));
}

/* Requests sleep_us repeatedly and records how long each sleep really took. */
static void measure_sleep(uint64_t cycles_per_sec, int samples, long sleep_us)
{
    const struct timespec req = {sleep_us / 1000000L, (sleep_us % 1000000L) * 1000L};
    uint64_t min_ns = UINT64_MAX, max_ns = 0, sum_ns = 0;

    for (int i = 0; i < samples; ++i) {
        const uint64_t start = ClockCycles();
        nanosleep(&req, NULL);
        const uint64_t elapsed_ns = (ClockCycles() - start) * 1000000000ULL / cycles_per_sec;
        if (elapsed_ns < min_ns) min_ns = elapsed_ns;
        if (elapsed_ns > max_ns) max_ns = elapsed_ns;
        sum_ns += elapsed_ns;
    }

    printf("sleep.requested_us=%ld sleep.samples=%d sleep.min_us=%" PRIu64
           " sleep.avg_us=%" PRIu64 " sleep.max_us=%" PRIu64 "\n",
           sleep_us, samples, min_ns / 1000, sum_ns / (uint64_t)samples / 1000, max_ns / 1000);
}

int main(int argc, char **argv)
{
    int samples = 200;
    long sleep_us = 1000;
    int opt;
    while ((opt = getopt(argc, argv, "n:u:")) != -1) {
        switch (opt) {
        case 'n': samples = atoi(optarg); break;
        case 'u': sleep_us = atol(optarg); break;
        default:
            fprintf(stderr, "usage: %s [-n sleep_samples] [-u sleep_us]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }
    if (samples <= 0 || sleep_us <= 0) {
        fprintf(stderr, "sleep_samples and sleep_us must be positive\n");
        return EXIT_FAILURE;
    }

    const uint64_t cycles_per_sec = SYSPAGE_ENTRY(qtime)->cycles_per_sec;

    print_identity();
    print_clocks(cycles_per_sec);
    print_scheduling();
    measure_sleep(cycles_per_sec, samples, sleep_us);
    return EXIT_SUCCESS;
}
