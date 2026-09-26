/*
 * msg_ipc: processes and native QNX message passing.
 *
 * The server registers a name, spawns itself as a separate client process,
 * and answers the client's synchronous MsgSend() requests. Each request
 * carries a sensor reading; the reply carries a range-check verdict. The
 * client measures round-trip time, and the server records the priority it
 * ran at while handling a request, showing that QNX servers inherit the
 * priority of the client they are serving.
 *
 * Usage: msg_ipc [-n messages] [-p client_priority]
 */
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dispatch.h>
#include <sys/neutrino.h>
#include <sys/syspage.h>
#include <sys/wait.h>
#include <unistd.h>

#define SERVICE_NAME "edge_ipc_demo"
#define MSG_TYPE_READING (_IO_MAX + 1)

typedef struct {
    uint16_t type;
    uint16_t sequence;
    int32_t client_priority;
    int16_t temperature_centi_c;
    uint16_t humidity_centi_pct;
    uint32_t pressure_pa;
} reading_msg_t;

typedef struct {
    uint16_t sequence;
    uint8_t accepted;
    int32_t server_priority;
} reading_reply_t;

typedef union {
    uint16_t type;
    struct _pulse pulse;
    reading_msg_t reading;
} server_rx_t;

extern char **environ;

static int current_priority(void)
{
    int policy;
    struct sched_param param;
    pthread_getschedparam(pthread_self(), &policy, &param);
    return param.sched_curpriority;
}

static int run_client(int count, int priority)
{
    if (pthread_setschedprio(pthread_self(), priority) != 0) {
        fprintf(stderr, "client: could not set priority %d\n", priority);
    }

    const int coid = name_open(SERVICE_NAME, 0);
    if (coid == -1) {
        perror("client: name_open");
        return EXIT_FAILURE;
    }

    const uint64_t cps = SYSPAGE_ENTRY(qtime)->cycles_per_sec;
    uint64_t min_ns = UINT64_MAX, max_ns = 0, sum_ns = 0;
    int accepted = 0;

    for (int i = 0; i < count; ++i) {
        reading_msg_t msg = {
            .type = MSG_TYPE_READING,
            .sequence = (uint16_t)i,
            .client_priority = current_priority(),
            /* Every 10th reading is deliberately out of range. */
            .temperature_centi_c = (i % 10 == 9) ? 12000 : 2200,
            .humidity_centi_pct = 4500,
            .pressure_pa = 101325,
        };
        reading_reply_t reply;

        const uint64_t start = ClockCycles();
        if (MsgSend(coid, &msg, sizeof msg, &reply, sizeof reply) == -1) {
            perror("client: MsgSend");
            break;
        }
        const uint64_t rtt_ns = (ClockCycles() - start) * 1000000000ULL / cps;

        if (rtt_ns < min_ns) min_ns = rtt_ns;
        if (rtt_ns > max_ns) max_ns = rtt_ns;
        sum_ns += rtt_ns;
        accepted += reply.accepted;
    }

    name_close(coid);
    printf("client pid=%d priority=%d messages=%d accepted=%d rejected=%d "
           "rtt_min_us=%.2f rtt_avg_us=%.2f rtt_max_us=%.2f\n",
           (int)getpid(), current_priority(), count, accepted, count - accepted,
           (double)min_ns / 1000.0, (double)sum_ns / count / 1000.0, (double)max_ns / 1000.0);
    return EXIT_SUCCESS;
}

static int in_range(const reading_msg_t *m)
{
    return m->temperature_centi_c >= -4000 && m->temperature_centi_c <= 8500 &&
           m->humidity_centi_pct <= 10000 && m->pressure_pa >= 30000 && m->pressure_pa <= 110000;
}

static int run_server(const char *self, int count, int client_priority)
{
    name_attach_t *attach = name_attach(NULL, SERVICE_NAME, 0);
    if (attach == NULL) {
        perror("server: name_attach");
        return EXIT_FAILURE;
    }

    char count_arg[16], prio_arg[16];
    snprintf(count_arg, sizeof count_arg, "%d", count);
    snprintf(prio_arg, sizeof prio_arg, "%d", client_priority);
    char *child_argv[] = {(char *)self, "client", count_arg, prio_arg, NULL};

    pid_t child;
    const int rc = posix_spawnp(&child, self, NULL, NULL, child_argv, environ);
    if (rc != 0) {
        fprintf(stderr, "server: posix_spawnp: %s\n", strerror(rc));
        name_detach(attach, 0);
        return EXIT_FAILURE;
    }
    printf("server pid=%d priority=%d spawned client pid=%d\n", (int)getpid(),
           current_priority(), (int)child);

    int handled = 0, handling_priority = -1, done = 0;
    while (!done) {
        server_rx_t rx;
        const int rcvid = MsgReceive(attach->chid, &rx, sizeof rx, NULL);
        if (rcvid == -1) {
            if (errno == EINTR) continue;
            perror("server: MsgReceive");
            break;
        }

        if (rcvid == 0) {
            if (rx.pulse.code == _PULSE_CODE_DISCONNECT) {
                ConnectDetach(rx.pulse.scoid);
                done = 1;
            }
            continue;
        }

        if (rx.type == _IO_CONNECT) {
            MsgReply(rcvid, EOK, NULL, 0); /* name_open() handshake */
            continue;
        }
        if (rx.type != MSG_TYPE_READING) {
            MsgError(rcvid, ENOSYS);
            continue;
        }

        handling_priority = current_priority();
        const reading_reply_t reply = {
            .sequence = rx.reading.sequence,
            .accepted = (uint8_t)in_range(&rx.reading),
            .server_priority = handling_priority,
        };
        MsgReply(rcvid, EOK, &reply, sizeof reply);
        ++handled;
    }

    int status = 0;
    waitpid(child, &status, 0);
    name_detach(attach, 0);
    printf("server handled=%d handling_priority=%d (client asked for %d) client_exit=%d\n",
           handled, handling_priority, client_priority, WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    if (argc == 4 && strcmp(argv[1], "client") == 0) {
        return run_client(atoi(argv[2]), atoi(argv[3]));
    }

    int count = 1000;
    int client_priority = 15;
    int opt;
    while ((opt = getopt(argc, argv, "n:p:")) != -1) {
        switch (opt) {
        case 'n': count = atoi(optarg); break;
        case 'p': client_priority = atoi(optarg); break;
        default:
            fprintf(stderr, "usage: %s [-n messages] [-p client_priority]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }
    if (count <= 0) {
        fprintf(stderr, "messages must be positive\n");
        return EXIT_FAILURE;
    }
    return run_server(argv[0], count, client_priority);
}
