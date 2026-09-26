/*
 * edge_rx: initial data-reception application for the QNX supervisor.
 *
 * Reads a byte stream from a serial port, a UDP socket, or a file, feeds it
 * through the shared edge-protocol decoder, and prints one key=value line per
 * received frame or rejected frame. Prints a summary with decoder counters and
 * sequence gaps on exit (Ctrl+C, end of file, or -c frames).
 *
 * Usage:
 *   edge_rx -s /dev/ser2 [-b 115200]   serial link to the STM32
 *   edge_rx -u 5000                    UDP datagrams on port 5000
 *   edge_rx -f capture.bin | -f -      recorded bytes or stdin
 *   options: -c max_frames   -q (summary only)
 */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <signal.h>
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

typedef struct {
    bool quiet;
    uint64_t start_ms;
    uint64_t bytes;
    uint32_t sequence_gaps;
    uint32_t frames_by_type[3]; /* sensor, status, other */
    bool have_sequence;
    uint16_t last_sequence;
    long max_frames;
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

static void print_centi(const char *key, int32_t value)
{
    const uint32_t mag = (uint32_t)(value < 0 ? -value : value);
    printf(" %s=%s%" PRIu32 ".%02" PRIu32, key, value < 0 ? "-" : "", mag / 100u, mag % 100u);
}

static void track_sequence(rx_state_t *st, uint16_t seq)
{
    if (st->have_sequence && seq != (uint16_t)(st->last_sequence + 1u)) {
        ++st->sequence_gaps;
    }
    st->last_sequence = seq;
    st->have_sequence = true;
}

static void on_decode(void *ctx, edge_decode_event_t event, const edge_frame_t *frame)
{
    rx_state_t *st = ctx;
    const uint64_t rx_ms = monotonic_ms() - st->start_ms;

    if (event != EDGE_DECODE_FRAME_OK) {
        if (!st->quiet) {
            printf("rx_ms=%" PRIu64 " event=%s\n", rx_ms, edge_decode_event_name(event));
        }
        return;
    }

    track_sequence(st, frame->sequence);
    if (!st->quiet) {
        printf("rx_ms=%" PRIu64 " event=frame type=%s seq=%u src_ms=%" PRIu32, rx_ms,
               edge_msg_type_name(frame->type), (unsigned)frame->sequence, frame->timestamp_ms);
    }

    edge_sensor_payload_t sensor;
    edge_status_payload_t status;
    if (frame->type == EDGE_MSG_SENSOR &&
        edge_sensor_decode(frame->payload, frame->payload_len, &sensor)) {
        ++st->frames_by_type[0];
        if (!st->quiet) {
            print_centi("temp_c", sensor.temperature_centi_c);
            print_centi("hum_pct", sensor.humidity_centi_pct);
            printf(" press_pa=%" PRIu32, sensor.pressure_pa);
        }
    } else if (frame->type == EDGE_MSG_STATUS &&
               edge_status_decode(frame->payload, frame->payload_len, &status)) {
        ++st->frames_by_type[1];
        if (!st->quiet) {
            printf(" sent=%" PRIu32 " dropped=%" PRIu32 " period_ms=%u max_jitter_us=%u"
                   " overruns=%u stack_min_words=%u heap_free=%" PRIu32,
                   status.frames_sent, status.samples_dropped, (unsigned)status.sensor_period_ms,
                   (unsigned)status.max_jitter_us, (unsigned)status.deadline_misses,
                   (unsigned)status.min_stack_free_words, status.free_heap_bytes);
        }
    } else {
        ++st->frames_by_type[2];
        if (!st->quiet) {
            printf(" payload_len=%u", (unsigned)frame->payload_len);
        }
    }
    if (!st->quiet) {
        printf("\n");
    }

    if (st->max_frames > 0 && (long)(st->frames_by_type[0] + st->frames_by_type[1] +
                                     st->frames_by_type[2]) >= st->max_frames) {
        g_stop = 1;
    }
}

static void print_summary(const rx_state_t *st, const edge_decoder_t *dec)
{
    const edge_decoder_stats_t *s = &dec->stats;
    printf("summary elapsed_ms=%" PRIu64 " bytes=%" PRIu64 " frames_ok=%" PRIu32
           " sensor=%" PRIu32 " status=%" PRIu32 " other=%" PRIu32 " bad_crc=%" PRIu32
           " bad_length=%" PRIu32 " bad_version=%" PRIu32 " bytes_discarded=%" PRIu32
           " seq_gaps=%" PRIu32 "\n",
           monotonic_ms() - st->start_ms, st->bytes, s->frames_ok, st->frames_by_type[0],
           st->frames_by_type[1], st->frames_by_type[2], s->bad_crc, s->bad_length,
           s->bad_version, s->bytes_discarded, st->sequence_gaps);
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s (-s device [-b baud] | -u udp_port | -f file|-) [-c max_frames] [-q]\n",
            prog);
}

int main(int argc, char **argv)
{
    const char *serial_path = NULL, *file_path = NULL;
    long baud = 115200;
    int udp_port = 0;
    rx_state_t st;
    memset(&st, 0, sizeof st);

    int opt;
    while ((opt = getopt(argc, argv, "s:b:u:f:c:q")) != -1) {
        switch (opt) {
        case 's': serial_path = optarg; break;
        case 'b': baud = atol(optarg); break;
        case 'u': udp_port = atoi(optarg); break;
        case 'f': file_path = optarg; break;
        case 'c': st.max_frames = atol(optarg); break;
        case 'q': st.quiet = true; break;
        default: usage(argv[0]); return EXIT_FAILURE;
        }
    }
    if ((serial_path != NULL) + (udp_port > 0) + (file_path != NULL) != 1) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    const bool is_udp = udp_port > 0;
    const int fd = serial_path ? open_serial(serial_path, baud)
                   : is_udp    ? open_udp(udp_port)
                               : open_file(file_path);
    if (fd == -1) {
        return EXIT_FAILURE;
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
    st.start_ms = monotonic_ms();

    uint8_t buf[1024];
    while (!g_stop) {
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
        edge_decoder_feed(&dec, buf, (size_t)n, on_decode, &st);
    }

    print_summary(&st, &dec);
    if (fd != STDIN_FILENO) {
        close(fd);
    }
    return EXIT_SUCCESS;
}
