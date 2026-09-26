/*
 * Host-side unit tests for the portable C modules shared by both nodes.
 * Build and run with tests/host/run_tests.ps1 (Windows) or `make -C tests/host`.
 */
#include <stdio.h>
#include <string.h>

#include "edge_protocol.h"
#include "rt_stats.h"
#include "sensor_sim.h"

static int g_failures;
static int g_checks;

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                      \
    } while (0)

#define RUN(test)                                                              \
    do {                                                                       \
        printf("%s\n", #test);                                                 \
        test();                                                                \
    } while (0)

typedef struct {
    int ok;
    int bad_crc;
    int bad_length;
    int bad_version;
    edge_frame_t last;
} capture_t;

static void capture_handler(void *ctx, edge_decode_event_t event, const edge_frame_t *frame)
{
    capture_t *c = ctx;
    switch (event) {
    case EDGE_DECODE_FRAME_OK:
        ++c->ok;
        c->last = *frame;
        break;
    case EDGE_DECODE_BAD_CRC:     ++c->bad_crc; break;
    case EDGE_DECODE_BAD_LENGTH:  ++c->bad_length; break;
    case EDGE_DECODE_BAD_VERSION: ++c->bad_version; break;
    }
}

static size_t make_sensor_frame(uint16_t seq, uint8_t *out)
{
    edge_sensor_payload_t reading = {-1234, 4510, 101325};
    edge_frame_t frame = {0};
    frame.type = EDGE_MSG_SENSOR;
    frame.sequence = seq;
    frame.timestamp_ms = 0x01020304u;
    frame.payload_len = EDGE_SENSOR_PAYLOAD_LEN;
    edge_sensor_encode(&reading, frame.payload);
    return edge_frame_encode(&frame, out, EDGE_MAX_FRAME);
}

static void test_crc_known_vector(void)
{
    const uint8_t check[] = "123456789";
    CHECK(edge_crc16(check, 9) == 0x29B1u);
}

static void test_encode_layout(void)
{
    uint8_t buf[EDGE_MAX_FRAME];
    const size_t n = make_sensor_frame(0xBEEF, buf);
    CHECK(n == EDGE_OVERHEAD + EDGE_SENSOR_PAYLOAD_LEN);
    CHECK(buf[0] == EDGE_SOF0 && buf[1] == EDGE_SOF1);
    CHECK(buf[2] == EDGE_PROTOCOL_VERSION);
    CHECK(buf[3] == EDGE_MSG_SENSOR);
    CHECK(buf[4] == 0xEF && buf[5] == 0xBE);
    CHECK(buf[6] == 0x04 && buf[9] == 0x01);
    CHECK(buf[10] == EDGE_SENSOR_PAYLOAD_LEN);
}

static void test_encode_rejects_oversize(void)
{
    uint8_t buf[EDGE_MAX_FRAME];
    edge_frame_t frame = {0};
    frame.payload_len = EDGE_MAX_PAYLOAD + 1;
    CHECK(edge_frame_encode(&frame, buf, sizeof buf) == 0);

    frame.payload_len = 4;
    CHECK(edge_frame_encode(&frame, buf, EDGE_OVERHEAD + 3) == 0);
}

static void test_roundtrip_byte_by_byte(void)
{
    uint8_t buf[EDGE_MAX_FRAME];
    const size_t n = make_sensor_frame(42, buf);
    edge_decoder_t dec;
    capture_t cap = {0};
    edge_decoder_init(&dec);
    for (size_t i = 0; i < n; ++i) {
        edge_decoder_feed(&dec, &buf[i], 1, capture_handler, &cap);
    }
    CHECK(cap.ok == 1);
    CHECK(cap.last.sequence == 42);
    CHECK(cap.last.timestamp_ms == 0x01020304u);

    edge_sensor_payload_t reading;
    CHECK(edge_sensor_decode(cap.last.payload, cap.last.payload_len, &reading));
    CHECK(reading.temperature_centi_c == -1234);
    CHECK(reading.humidity_centi_pct == 4510);
    CHECK(reading.pressure_pa == 101325u);
}

static void test_resync_after_garbage(void)
{
    uint8_t stream[3 * EDGE_MAX_FRAME];
    size_t n = 0;
    const uint8_t garbage[] = {0x00, 0xA5, 0x13, 0xFF, 0xA5};
    memcpy(stream, garbage, sizeof garbage);
    n += sizeof garbage;
    n += make_sensor_frame(1, &stream[n]);
    n += make_sensor_frame(2, &stream[n]);

    edge_decoder_t dec;
    capture_t cap = {0};
    edge_decoder_init(&dec);
    edge_decoder_feed(&dec, stream, n, capture_handler, &cap);
    CHECK(cap.ok == 2);
    CHECK(cap.last.sequence == 2);
    CHECK(dec.stats.bytes_discarded == sizeof garbage);
}

static void test_corrupted_frame_rejected_next_recovered(void)
{
    uint8_t stream[2 * EDGE_MAX_FRAME];
    const size_t first = make_sensor_frame(7, stream);
    const size_t second = make_sensor_frame(8, &stream[first]);
    stream[EDGE_HEADER_LEN] ^= 0x01; /* flip a payload bit in frame 7 */

    edge_decoder_t dec;
    capture_t cap = {0};
    edge_decoder_init(&dec);
    edge_decoder_feed(&dec, stream, first + second, capture_handler, &cap);
    CHECK(cap.bad_crc == 1);
    CHECK(cap.ok == 1);
    CHECK(cap.last.sequence == 8);
}

static void test_truncated_frame_recovered(void)
{
    /* A frame cut off mid-payload (e.g. a reset) followed by a good frame. */
    uint8_t stream[2 * EDGE_MAX_FRAME];
    const size_t full = make_sensor_frame(3, stream);
    const size_t cut = full - 4;
    const size_t second = make_sensor_frame(4, &stream[cut]);

    edge_decoder_t dec;
    capture_t cap = {0};
    edge_decoder_init(&dec);
    edge_decoder_feed(&dec, stream, cut + second, capture_handler, &cap);
    CHECK(cap.ok == 1);
    CHECK(cap.last.sequence == 4);
    CHECK(cap.bad_crc >= 1);
}

static void test_bad_length_and_version(void)
{
    uint8_t buf[EDGE_MAX_FRAME];
    make_sensor_frame(9, buf);

    uint8_t bad_len[EDGE_HEADER_LEN];
    memcpy(bad_len, buf, EDGE_HEADER_LEN);
    bad_len[10] = EDGE_MAX_PAYLOAD + 1;

    uint8_t bad_ver[EDGE_HEADER_LEN];
    memcpy(bad_ver, buf, EDGE_HEADER_LEN);
    bad_ver[2] = 99;

    edge_decoder_t dec;
    capture_t cap = {0};
    edge_decoder_init(&dec);
    edge_decoder_feed(&dec, bad_len, sizeof bad_len, capture_handler, &cap);
    edge_decoder_feed(&dec, bad_ver, sizeof bad_ver, capture_handler, &cap);
    CHECK(cap.bad_length == 1);
    CHECK(cap.bad_version == 1);
    CHECK(cap.ok == 0);
}

static void test_max_payload_roundtrip(void)
{
    uint8_t buf[EDGE_MAX_FRAME];
    edge_frame_t frame = {0};
    frame.type = EDGE_MSG_STATUS;
    frame.payload_len = EDGE_MAX_PAYLOAD;
    for (size_t i = 0; i < EDGE_MAX_PAYLOAD; ++i) {
        frame.payload[i] = (uint8_t)(i * 7u);
    }
    const size_t n = edge_frame_encode(&frame, buf, sizeof buf);
    CHECK(n == EDGE_MAX_FRAME);

    edge_decoder_t dec;
    capture_t cap = {0};
    edge_decoder_init(&dec);
    edge_decoder_feed(&dec, buf, n, capture_handler, &cap);
    CHECK(cap.ok == 1);
    CHECK(memcmp(cap.last.payload, frame.payload, EDGE_MAX_PAYLOAD) == 0);
}

static void test_status_roundtrip(void)
{
    const edge_status_payload_t in = {123456, 7, 100, 250, 3, 88, 20480};
    uint8_t raw[EDGE_STATUS_PAYLOAD_LEN];
    edge_status_payload_t out;
    edge_status_encode(&in, raw);
    CHECK(edge_status_decode(raw, sizeof raw, &out));
    CHECK(memcmp(&in, &out, sizeof in) == 0);
    CHECK(!edge_status_decode(raw, sizeof raw - 1, &out));
}

static void test_sensor_sim_is_deterministic_and_in_range(void)
{
    sensor_sim_t a, b;
    sensor_sim_init(&a, 1234);
    sensor_sim_init(&b, 1234);
    for (uint32_t t = 0; t < 600000u; t += 100u) {
        edge_sensor_payload_t ra, rb;
        sensor_sim_read(&a, t, &ra);
        sensor_sim_read(&b, t, &rb);
        CHECK(memcmp(&ra, &rb, sizeof ra) == 0);
        CHECK(ra.temperature_centi_c >= 1800 && ra.temperature_centi_c <= 2600);
        CHECK(ra.humidity_centi_pct >= 4300 && ra.humidity_centi_pct <= 4700);
        CHECK(ra.pressure_pa >= 101150u && ra.pressure_pa <= 101500u);
    }
}

static void test_rt_stats_jitter_and_overruns(void)
{
    rt_stats_t s;
    const uint32_t cpu = 250; /* cycles per microsecond at 250 MHz */
    rt_stats_init(&s, 100000, 10, cpu);

    uint32_t t = 0xFFFF0000u; /* start near wrap to exercise unsigned math */
    const uint32_t periods_us[] = {100000, 100050, 99900, 115000};
    rt_stats_on_release(&s, t);
    rt_stats_on_complete(&s, t + 20 * cpu);
    for (size_t i = 0; i < sizeof periods_us / sizeof periods_us[0]; ++i) {
        t += periods_us[i] * cpu;
        rt_stats_on_release(&s, t);
        rt_stats_on_complete(&s, t + 35 * cpu);
    }

    CHECK(s.jobs == 4);
    CHECK(s.min_period_us == 99900);
    CHECK(s.max_period_us == 115000);
    CHECK(s.max_jitter_us == 15000);
    CHECK(s.overruns == 1);
    CHECK(s.max_exec_us == 35);
    CHECK(rt_stats_avg_period_us(&s) == (100000 + 100050 + 99900 + 115000) / 4);

    rt_stats_reset(&s, 50000, 10);
    CHECK(s.jobs == 0 && s.max_jitter_us == 0 && !s.has_last_release);
}

int main(void)
{
    RUN(test_crc_known_vector);
    RUN(test_encode_layout);
    RUN(test_encode_rejects_oversize);
    RUN(test_roundtrip_byte_by_byte);
    RUN(test_resync_after_garbage);
    RUN(test_corrupted_frame_rejected_next_recovered);
    RUN(test_truncated_frame_recovered);
    RUN(test_bad_length_and_version);
    RUN(test_max_payload_roundtrip);
    RUN(test_status_roundtrip);
    RUN(test_sensor_sim_is_deterministic_and_in_range);
    RUN(test_rt_stats_jitter_and_overruns);

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
