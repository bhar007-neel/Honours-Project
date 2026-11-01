/*
 * Host-side unit tests for the portable C modules shared by both nodes.
 * Build and run with tests/host/run_tests.ps1 (Windows) or `make -C tests/host`.
 */
#include <stdio.h>
#include <string.h>

#include "edge_protocol.h"
#include "link_health.h"
#include "rt_stats.h"
#include "sensor_gate.h"
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

static void test_sensor_gate_matches_spark_contracts(void)
{
    const edge_sensor_payload_t room = {2345, 4510, 101325};
    const edge_sensor_payload_t hot = {12000, 4510, 101325};
    const edge_sensor_payload_t wet = {2345, 10001, 101325};
    const edge_sensor_payload_t low_pressure = {2345, 4510, 29999};
    const edge_sensor_payload_t boundary = {-4000, 10000, 110000};
    const edge_sensor_payload_t other_boundary = {8500, 0, 30000};
    /* Temperature is checked first, so a wet-and-hot reading names temperature. */
    const edge_sensor_payload_t hot_and_wet = {12000, 10001, 101325};

    CHECK(sensor_gate_check(&room) == SENSOR_GATE_ACCEPTED);
    CHECK(sensor_gate_check(&hot) == SENSOR_GATE_TEMPERATURE_OUT_OF_RANGE);
    CHECK(sensor_gate_check(&wet) == SENSOR_GATE_HUMIDITY_OUT_OF_RANGE);
    CHECK(sensor_gate_check(&low_pressure) == SENSOR_GATE_PRESSURE_OUT_OF_RANGE);
    CHECK(sensor_gate_check(&boundary) == SENSOR_GATE_ACCEPTED);
    CHECK(sensor_gate_check(&other_boundary) == SENSOR_GATE_ACCEPTED);
    CHECK(sensor_gate_check(&hot_and_wet) == SENSOR_GATE_TEMPERATURE_OUT_OF_RANGE);

    CHECK(sensor_gate_temperature_step_ok(2200, 2240, SENSOR_GATE_MAX_STEP_CENTI_C));
    CHECK(sensor_gate_temperature_step_ok(2200, 2300, SENSOR_GATE_MAX_STEP_CENTI_C));
    CHECK(!sensor_gate_temperature_step_ok(2200, 2301, SENSOR_GATE_MAX_STEP_CENTI_C));
    /* 32767 - (-32768) is 65535, which is wider than any int16 step limit. */
    CHECK(!sensor_gate_temperature_step_ok(INT16_MIN, INT16_MAX, SENSOR_GATE_MAX_STEP_CENTI_C));
    CHECK(!sensor_gate_temperature_step_ok(INT16_MIN, INT16_MAX, INT16_MAX));
    CHECK(sensor_gate_temperature_step_ok(0, INT16_MAX, INT16_MAX));
}

static void test_command_roundtrip(void)
{
    const edge_command_payload_t command = {EDGE_CMD_SET_PERIOD, 50};
    uint8_t payload[EDGE_COMMAND_PAYLOAD_LEN];
    edge_command_encode(&command, payload);

    edge_command_payload_t decoded;
    CHECK(edge_command_decode(payload, sizeof payload, &decoded));
    CHECK(decoded.id == EDGE_CMD_SET_PERIOD);
    CHECK(decoded.period_ms == 50);
    CHECK(!edge_command_decode(payload, 2, &decoded));
    CHECK(strcmp(edge_command_name(EDGE_CMD_SET_PERIOD), "set_period") == 0);
    CHECK(strcmp(edge_command_name(0x7Fu), "unknown") == 0);

    CHECK(edge_sensor_period_allowed(10));
    CHECK(edge_sensor_period_allowed(20));
    CHECK(edge_sensor_period_allowed(50));
    CHECK(edge_sensor_period_allowed(100));
    CHECK(edge_sensor_period_allowed(200));
    CHECK(!edge_sensor_period_allowed(0));
    CHECK(!edge_sensor_period_allowed(15));
    CHECK(!edge_sensor_period_allowed(1000));

    edge_frame_t frame = {0};
    frame.type = EDGE_MSG_COMMAND;
    frame.sequence = 7;
    frame.timestamp_ms = 42;
    frame.payload_len = EDGE_COMMAND_PAYLOAD_LEN;
    memcpy(frame.payload, payload, sizeof payload);

    uint8_t wire[EDGE_MAX_FRAME];
    const size_t len = edge_frame_encode(&frame, wire, sizeof wire);
    CHECK(len == EDGE_OVERHEAD + EDGE_COMMAND_PAYLOAD_LEN);

    edge_decoder_t decoder;
    edge_decoder_init(&decoder);
    capture_t captured;
    memset(&captured, 0, sizeof captured);
    edge_decoder_feed(&decoder, wire, len, capture_handler, &captured);
    CHECK(captured.ok == 1);
    CHECK(captured.last.type == EDGE_MSG_COMMAND);
    CHECK(edge_command_decode(captured.last.payload, captured.last.payload_len, &decoded));
    CHECK(decoded.period_ms == 50);
}

static void test_link_health_sequence_and_silence(void)
{
    edge_link_health_t health;
    edge_link_health_init(&health);

    CHECK(edge_link_health_observe(&health, 1) == EDGE_SEQ_FIRST);
    CHECK(edge_link_health_observe(&health, 2) == EDGE_SEQ_IN_ORDER);
    CHECK(edge_link_health_observe(&health, 3) == EDGE_SEQ_IN_ORDER);
    CHECK(edge_link_health_observe(&health, 3) == EDGE_SEQ_DUPLICATE);
    CHECK(health.last_sequence == 3);
    CHECK(edge_link_health_observe(&health, 2) == EDGE_SEQ_REORDER);
    CHECK(health.last_sequence == 3);
    CHECK(edge_link_health_observe(&health, 6) == EDGE_SEQ_GAP);
    CHECK(health.missing == 2u);
    CHECK(health.gaps == 1u);
    CHECK(health.duplicates == 1u);
    CHECK(health.reorders == 1u);
    CHECK(health.in_order == 2u);
    CHECK(strcmp(edge_seq_event_name(EDGE_SEQ_GAP), "gap") == 0);

    /* 65535 then 0 is the natural wrap, a distance of one. */
    edge_link_health_init(&health);
    CHECK(edge_link_health_observe(&health, 65535) == EDGE_SEQ_FIRST);
    CHECK(edge_link_health_observe(&health, 0) == EDGE_SEQ_IN_ORDER);
    CHECK(health.last_sequence == 0);

    /* No silence alarm before the first byte, then one alarm per quiet stretch. */
    edge_link_health_init(&health);
    CHECK(!edge_link_health_poll_silence(&health, 5000u, EDGE_LINK_SILENCE_MS));
    edge_link_health_hear(&health, 1000u);
    CHECK(!edge_link_health_poll_silence(&health, 3999u, EDGE_LINK_SILENCE_MS));
    CHECK(edge_link_health_poll_silence(&health, 4000u, EDGE_LINK_SILENCE_MS));
    CHECK(!edge_link_health_poll_silence(&health, 8000u, EDGE_LINK_SILENCE_MS));
    edge_link_health_hear(&health, 8000u);
    CHECK(edge_link_health_poll_silence(&health, 11000u, EDGE_LINK_SILENCE_MS));
    CHECK(health.silence_events == 2u);
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
    RUN(test_sensor_gate_matches_spark_contracts);
    RUN(test_command_roundtrip);
    RUN(test_link_health_sequence_and_silence);

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
