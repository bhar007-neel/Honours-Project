/*
 * edge_tx: sensing-node simulator.
 *
 * Generates the same frames the STM32 sends (simulated sensor samples plus a
 * status frame every second) so the receiver can be developed and tested on
 * the Pi or a PC before the boards are wired together. Can deliberately
 * corrupt frames to exercise the receiver's error handling.
 *
 * Usage:
 *   edge_tx -u 127.0.0.1:5000        UDP datagrams, one frame per datagram
 *   edge_tx -s /dev/ser2 [-b 115200] serial port
 *   edge_tx -o capture.bin | -o -    file or stdout
 *   options: -p period_ms (100)  -n frames (0 = forever)  -e corrupt_every_n
 */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
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
#include "sensor_sim.h"

static volatile sig_atomic_t g_stop;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static uint32_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

static void add_ms(struct timespec *t, long ms)
{
    t->tv_nsec += (ms % 1000) * 1000000L;
    t->tv_sec += ms / 1000 + t->tv_nsec / 1000000000L;
    t->tv_nsec %= 1000000000L;
}

static int open_serial(const char *path, long baud)
{
    const int fd = open(path, O_RDWR | O_NOCTTY);
    if (fd == -1) {
        perror(path);
        return -1;
    }
    struct termios tio;
    tcgetattr(fd, &tio);
    cfmakeraw(&tio);
    tio.c_cflag |= CLOCAL | CREAD;
#ifdef __QNX__
    cfsetospeed(&tio, (speed_t)baud);
#else
    (void)baud;
    cfsetospeed(&tio, B115200);
#endif
    if (tcsetattr(fd, TCSANOW, &tio) == -1) {
        perror("tcsetattr");
        close(fd);
        return -1;
    }
    return fd;
}

static int open_udp(const char *host_port, struct sockaddr_in *dest)
{
    char host[64];
    const char *colon = strrchr(host_port, ':');
    if (colon == NULL || (size_t)(colon - host_port) >= sizeof host) {
        fprintf(stderr, "expected host:port, got %s\n", host_port);
        return -1;
    }
    memcpy(host, host_port, (size_t)(colon - host_port));
    host[colon - host_port] = '\0';

    memset(dest, 0, sizeof *dest);
    dest->sin_family = AF_INET;
    dest->sin_port = htons((uint16_t)atoi(colon + 1));
    if (inet_pton(AF_INET, host, &dest->sin_addr) != 1) {
        fprintf(stderr, "invalid IPv4 address: %s\n", host);
        return -1;
    }
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) {
        perror("socket");
    }
    return fd;
}

int main(int argc, char **argv)
{
    const char *udp_target = NULL, *serial_path = NULL, *out_path = NULL;
    long baud = 115200, period_ms = 100, max_frames = 0, corrupt_every = 0;

    int opt;
    while ((opt = getopt(argc, argv, "u:s:b:o:p:n:e:")) != -1) {
        switch (opt) {
        case 'u': udp_target = optarg; break;
        case 's': serial_path = optarg; break;
        case 'b': baud = atol(optarg); break;
        case 'o': out_path = optarg; break;
        case 'p': period_ms = atol(optarg); break;
        case 'n': max_frames = atol(optarg); break;
        case 'e': corrupt_every = atol(optarg); break;
        default:
            fprintf(stderr, "usage: %s (-u host:port | -s device [-b baud] | -o file|-) "
                            "[-p period_ms] [-n frames] [-e corrupt_every_n]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }
    if ((udp_target != NULL) + (serial_path != NULL) + (out_path != NULL) != 1 || period_ms <= 0) {
        fprintf(stderr, "choose exactly one of -u, -s, -o; period must be positive\n");
        return EXIT_FAILURE;
    }

    struct sockaddr_in dest;
    int fd;
    if (udp_target) {
        fd = open_udp(udp_target, &dest);
    } else if (serial_path) {
        fd = open_serial(serial_path, baud);
    } else if (strcmp(out_path, "-") == 0) {
        fd = STDOUT_FILENO;
    } else {
        fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) perror(out_path);
    }
    if (fd == -1) {
        return EXIT_FAILURE;
    }

    signal(SIGINT, on_signal);
    signal(SIGPIPE, SIG_IGN);

    sensor_sim_t sim;
    sensor_sim_init(&sim, 0x1234ABCDu);
    uint16_t sequence = 0;
    uint32_t frames_sent = 0, last_status_ms = monotonic_ms();
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);

    while (!g_stop && (max_frames == 0 || (long)frames_sent < max_frames)) {
        add_ms(&next, period_ms);
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);

        edge_frame_t frame;
        memset(&frame, 0, sizeof frame);
        frame.timestamp_ms = monotonic_ms();
        frame.sequence = sequence++;

        if (frame.timestamp_ms - last_status_ms >= 1000u) {
            const edge_status_payload_t status = {
                .frames_sent = frames_sent,
                .sensor_period_ms = (uint16_t)period_ms,
            };
            frame.type = EDGE_MSG_STATUS;
            frame.payload_len = EDGE_STATUS_PAYLOAD_LEN;
            edge_status_encode(&status, frame.payload);
            last_status_ms = frame.timestamp_ms;
        } else {
            edge_sensor_payload_t reading;
            sensor_sim_read(&sim, frame.timestamp_ms, &reading);
            frame.type = EDGE_MSG_SENSOR;
            frame.payload_len = EDGE_SENSOR_PAYLOAD_LEN;
            edge_sensor_encode(&reading, frame.payload);
        }

        uint8_t wire[EDGE_MAX_FRAME];
        const size_t len = edge_frame_encode(&frame, wire, sizeof wire);
        if (corrupt_every > 0 && (frames_sent + 1u) % (uint32_t)corrupt_every == 0u) {
            wire[len - 3] ^= 0x5A; /* last payload byte: the CRC check must reject it */
        }

        const ssize_t written = udp_target
            ? sendto(fd, wire, len, 0, (struct sockaddr *)&dest, sizeof dest)
            : write(fd, wire, len);
        if (written != (ssize_t)len) {
            if (errno == EINTR) continue;
            perror("send");
            break;
        }
        ++frames_sent;
    }

    fprintf(stderr, "edge_tx sent=%u\n", (unsigned)frames_sent);
    if (fd != STDOUT_FILENO) {
        close(fd);
    }
    return EXIT_SUCCESS;
}
