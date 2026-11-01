/*
 * edge_rx: data-reception application for the QNX supervisor.
 *
 * Reads a byte stream from a serial port, a UDP socket, or a file, feeds it
 * through the shared edge-protocol decoder, and prints one key=value line per
 * received frame or rejected frame. A sensor frame also carries the acceptance
 * verdict from sensor_gate, the same rule proved in SPARK. Sequence numbers
 * are classified as in-order, duplicate, reorder, or gap. On a live serial or
 * UDP link, three seconds without bytes is one silence event.
 *
 * Prints a summary on exit (Ctrl+C, end of file, or -c frames). -l copies
 * every line, including the summary, into a log file.
 *
 * Usage:
 *   edge_rx -s /dev/ser2 [-b 115200]   serial link to the STM32
 *   edge_rx -u 5000                    UDP datagrams on port 5000
 *   edge_rx -f capture.bin | -f -      recorded bytes or stdin
 *   options: -c max_frames   -q (summary only)   -l logfile
 */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "edge_protocol.h"
#include "link_health.h"
#include "sensor_gate.h"

typedef struct {
    bool quiet;
    FILE *log;
    uint64_t start_ms;
    uint64_t bytes;
    uint32_t range_rejected;
    uint32_t step_rejected;
    uint32_t command_frames;
    uint32_t frames_by_type[3]; /* sensor, status, other */
    bool have_temperature;
    int16_t last_temperature_centi_c;
    long max_frames;
    edge_link_health_t health;
} rx_state_t;

static volatile sig_atomic_t g_stop;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static uint64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static uint32_t elapsed_ms(const rx_state_t *st)
{
    return (uint32_t)(monotonic_ms() - st->start_ms);
}

/* Live lines. Quiet mode keeps them out of the terminal; a log file still gets them. */
static void emitf(rx_state_t *st, const char *fmt, ...)
{
    va_list ap;
    if (!st->quiet) {
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
    }
    if (st->log != NULL) {
        va_start(ap, fmt);
        vfprintf(st->log, fmt, ap);
        va_end(ap);
    }
}

/* The summary is always printed, and copied to the log when one is open. */
static void summaryf(const rx_state_t *st, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    if (st->log != NULL) {
        va_start(ap, fmt);
        vfprintf(st->log, fmt, ap);
        va_end(ap);
    }
}

static speed_t to_speed(long baud)
{
#ifdef __QNX__
    return (speed_t)baud; /* QNX speed_t values are the numeric baud rate */
#else
    switch (baud) {
    case 9600:   return B9600;
    case 19200:  return B19200;
    case 38400:  return B38400;
    case 57600:  return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    default:     return 0;
    }
#endif
}

static int open_serial(const char *path, long baud)
{
    const int fd = open(path, O_RDWR | O_NOCTTY);
    if (fd == -1) {
        perror(path);
        return -1;
    }

    struct termios tio;
    if (tcgetattr(fd, &tio) == -1) {
        perror("tcgetattr");
        close(fd);
        return -1;
    }
    cfmakeraw(&tio); /* binary data: no echo, no line editing, no CR/LF mapping */
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~(tcflag_t)(CSTOPB | PARENB);
    tio.c_cc[VMIN] = 1;
    tio.c_cc[VTIME] = 0;

    const speed_t speed = to_speed(baud);
    if (speed == 0 || cfsetispeed(&tio, speed) == -1 || cfsetospeed(&tio, speed) == -1 ||
        tcsetattr(fd, TCSANOW, &tio) == -1) {
        fprintf(stderr, "cannot configure %s at %ld baud\n", path, baud);
        close(fd);
        return -1;
    }
    tcflush(fd, TCIFLUSH);
    return fd;
}

static int open_udp(int port)
{
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) {
        perror("socket");
        return -1;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) == -1) {
        perror("bind");
        close(fd);
        return -1;
    }
    return fd;
}

static int open_file(const char *path)
{
    if (strcmp(path, "-") == 0) {
        return STDIN_FILENO;
    }
    const int fd = open(path, O_RDONLY);
    if (fd == -1) {
        perror(path);
    }
    return fd;
}

/* 1 when the descriptor is readable, 0 on timeout, -1 on error. */
static int wait_input(int fd, int timeout_ms)
{
    struct pollfd pfd;
    memset(&pfd, 0, sizeof pfd);
    pfd.fd = fd;
    pfd.events = POLLIN;
    const int ready = poll(&pfd, 1, timeout_ms);
    if (ready < 0 && errno == EINTR) {
        return 0;
    }
    return ready;
}

static void print_centi(rx_state_t *st, const char *key, int32_t value)
{
    const uint32_t mag = (uint32_t)(value < 0 ? -value : value);
    emitf(st, " %s=%s%" PRIu32 ".%02" PRIu32, key, value < 0 ? "-" : "", mag / 100u, mag % 100u);
}

static void on_decode(void *ctx, edge_decode_event_t event, const edge_frame_t *frame)
{
    rx_state_t *st = ctx;
    const uint32_t rx_ms = elapsed_ms(st);

    if (event != EDGE_DECODE_FRAME_OK) {
        emitf(st, "rx_ms=%" PRIu32 " event=%s\n", rx_ms, edge_decode_event_name(event));
        return;
    }

    const edge_seq_event_t seq_event = edge_link_health_observe(&st->health, frame->sequence);
    emitf(st, "rx_ms=%" PRIu32 " event=frame type=%s seq=%u seq_event=%s src_ms=%" PRIu32, rx_ms,
          edge_msg_type_name(frame->type), (unsigned)frame->sequence, edge_seq_event_name(seq_event),
          frame->timestamp_ms);

    edge_sensor_payload_t sensor;
    edge_status_payload_t status;
    edge_command_payload_t command;
    if (frame->type == EDGE_MSG_SENSOR &&
        edge_sensor_decode(frame->payload, frame->payload_len, &sensor)) {
        ++st->frames_by_type[0];
        const sensor_gate_verdict_t verdict = sensor_gate_check(&sensor);
        if (verdict != SENSOR_GATE_ACCEPTED) {
            ++st->range_rejected;
        }
        bool have_step = false;
        bool step_ok = true;
        if (st->have_temperature) {
            have_step = true;
            step_ok = sensor_gate_temperature_step_ok(st->last_temperature_centi_c,
                                                     sensor.temperature_centi_c,
                                                     SENSOR_GATE_MAX_STEP_CENTI_C);
            if (!step_ok) {
                ++st->step_rejected;
            }
        }
        st->last_temperature_centi_c = sensor.temperature_centi_c;
        st->have_temperature = true;
        print_centi(st, "temp_c", sensor.temperature_centi_c);
        print_centi(st, "hum_pct", sensor.humidity_centi_pct);
        emitf(st, " press_pa=%" PRIu32 " verdict=%s", sensor.pressure_pa,
              sensor_gate_verdict_name(verdict));
        if (have_step) {
            emitf(st, " step_ok=%s", step_ok ? "yes" : "no");
        }
    } else if (frame->type == EDGE_MSG_STATUS &&
               edge_status_decode(frame->payload, frame->payload_len, &status)) {
        ++st->frames_by_type[1];
        emitf(st, " sent=%" PRIu32 " dropped=%" PRIu32 " period_ms=%u max_jitter_us=%u"
                  " overruns=%u stack_min_words=%u heap_free=%" PRIu32,
              status.frames_sent, status.samples_dropped, (unsigned)status.sensor_period_ms,
              (unsigned)status.max_jitter_us, (unsigned)status.deadline_misses,
              (unsigned)status.min_stack_free_words, status.free_heap_bytes);
    } else if (frame->type == EDGE_MSG_COMMAND &&
               edge_command_decode(frame->payload, frame->payload_len, &command)) {
        ++st->command_frames;
        emitf(st, " command=%s period_ms=%u", edge_command_name(command.id),
              (unsigned)command.period_ms);
    } else {
        ++st->frames_by_type[2];
        emitf(st, " payload_len=%u", (unsigned)frame->payload_len);
    }
    emitf(st, "\n");

    if (st->max_frames > 0 && (long)(st->frames_by_type[0] + st->frames_by_type[1] +
                                     st->frames_by_type[2] + st->command_frames) >= st->max_frames) {
        g_stop = 1;
    }
}

static void print_summary(const rx_state_t *st, const edge_decoder_t *dec)
{
    const edge_decoder_stats_t *s = &dec->stats;
    const edge_link_health_t *health = &st->health;
    summaryf(st,
             "summary elapsed_ms=%" PRIu64 " bytes=%" PRIu64 " frames_ok=%" PRIu32
             " sensor=%" PRIu32 " status=%" PRIu32 " command=%" PRIu32 " other=%" PRIu32
             " bad_crc=%" PRIu32 " bad_length=%" PRIu32 " bad_version=%" PRIu32
             " bytes_discarded=%" PRIu32 " seq_in_order=%" PRIu32 " seq_gaps=%" PRIu32
             " seq_missing=%" PRIu32 " seq_duplicates=%" PRIu32 " seq_reorders=%" PRIu32
             " silence=%" PRIu32 " range_rejected=%" PRIu32 " step_rejected=%" PRIu32 "\n",
             monotonic_ms() - st->start_ms, st->bytes, s->frames_ok, st->frames_by_type[0],
             st->frames_by_type[1], st->command_frames, st->frames_by_type[2], s->bad_crc,
             s->bad_length, s->bad_version, s->bytes_discarded, health->in_order, health->gaps,
             health->missing, health->duplicates, health->reorders, health->silence_events,
             st->range_rejected, st->step_rejected);
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s (-s device [-b baud] | -u udp_port | -f file|-) "
            "[-c max_frames] [-q] [-l logfile]\n",
            prog);
}

int main(int argc, char **argv)
{
    const char *serial_path = NULL, *file_path = NULL, *log_path = NULL;
    long baud = 115200;
    int udp_port = 0;
    rx_state_t st;
    memset(&st, 0, sizeof st);

    int opt;
    while ((opt = getopt(argc, argv, "s:b:u:f:c:ql:")) != -1) {
        switch (opt) {
        case 's': serial_path = optarg; break;
        case 'b': baud = atol(optarg); break;
        case 'u': udp_port = atoi(optarg); break;
        case 'f': file_path = optarg; break;
        case 'c': st.max_frames = atol(optarg); break;
        case 'q': st.quiet = true; break;
        case 'l': log_path = optarg; break;
        default: usage(argv[0]); return EXIT_FAILURE;
        }
    }
    if ((serial_path != NULL) + (udp_port > 0) + (file_path != NULL) != 1) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    const bool is_udp = udp_port > 0;
    const bool is_file = file_path != NULL;
    const int fd = serial_path ? open_serial(serial_path, baud)
                   : is_udp    ? open_udp(udp_port)
                               : open_file(file_path);
    if (fd == -1) {
        return EXIT_FAILURE;
    }

    if (log_path != NULL) {
        st.log = fopen(log_path, "w");
        if (st.log == NULL) {
            perror(log_path);
            if (fd != STDIN_FILENO) {
                close(fd);
            }
            return EXIT_FAILURE;
        }
        setvbuf(st.log, NULL, _IOLBF, 0);
    }

    /* No SA_RESTART, so a blocked read() returns EINTR on Ctrl+C. */
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    setvbuf(stdout, NULL, _IOLBF, 0);
    edge_decoder_t dec;
    edge_decoder_init(&dec);
    edge_link_health_init(&st.health);
    st.start_ms = monotonic_ms();

    uint8_t buf[1024];
    while (!g_stop) {
        /* A finished capture has no live clock to watch. Serial and UDP do. */
        if (!is_file) {
            const int ready = wait_input(fd, 200);
            if (ready < 0) {
                perror("poll");
                break;
            }
            if (ready == 0) {
                const uint32_t now = elapsed_ms(&st);
                if (edge_link_health_poll_silence(&st.health, now, EDGE_LINK_SILENCE_MS)) {
                    emitf(&st, "rx_ms=%" PRIu32 " event=silence\n", now);
                }
                continue;
            }
        }

        const ssize_t n = is_udp ? recv(fd, buf, sizeof buf, 0) : read(fd, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("read");
            break;
        }
        if (n == 0) {
            break; /* end of file */
        }
        st.bytes += (uint64_t)n;
        edge_link_health_hear(&st.health, elapsed_ms(&st));
        edge_decoder_feed(&dec, buf, (size_t)n, on_decode, &st);
    }

    print_summary(&st, &dec);
    if (st.log != NULL) {
        fclose(st.log);
    }
    if (fd != STDIN_FILENO) {
        close(fd);
    }
    return EXIT_SUCCESS;
}
