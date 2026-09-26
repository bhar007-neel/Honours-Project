/*
 * Edge frame protocol, version 1 (preliminary; refined in Weeks 5-6).
 *
 * Shared by the STM32/FreeRTOS sensing node and the QNX supervisory node.
 * Plain C11 with no OS, heap, or HAL dependencies so it builds on the MCU,
 * on QNX, and on a development PC for unit testing.
 *
 * Frame layout (all multi-byte fields little-endian):
 *
 *   offset  size  field
 *   0       1     SOF0          0xA5
 *   1       1     SOF1          0x5A
 *   2       1     version       EDGE_PROTOCOL_VERSION
 *   3       1     type          edge_msg_type_t
 *   4       2     sequence      incremented per frame by the sender
 *   6       4     timestamp_ms  sender uptime in milliseconds
 *   10      1     payload_len   0..EDGE_MAX_PAYLOAD
 *   11      N     payload
 *   11+N    2     crc16         CRC-16/CCITT-FALSE over bytes [2, 11+N)
 *
 * Fields are serialized byte by byte rather than by casting packed structs,
 * so the wire format does not depend on compiler padding or CPU endianness.
 */
#ifndef EDGE_PROTOCOL_H
#define EDGE_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EDGE_SOF0             0xA5u
#define EDGE_SOF1             0x5Au
#define EDGE_PROTOCOL_VERSION 1u

#define EDGE_HEADER_LEN   11u
#define EDGE_CRC_LEN      2u
#define EDGE_MAX_PAYLOAD  64u
#define EDGE_OVERHEAD     (EDGE_HEADER_LEN + EDGE_CRC_LEN)
#define EDGE_MAX_FRAME    (EDGE_OVERHEAD + EDGE_MAX_PAYLOAD)

typedef enum {
    EDGE_MSG_SENSOR  = 0x01, /* STM32 -> Pi: one sensor sample   */
    EDGE_MSG_STATUS  = 0x02, /* STM32 -> Pi: heartbeat + health  */
    EDGE_MSG_COMMAND = 0x10  /* Pi -> STM32: reserved for Week 5 */
} edge_msg_type_t;

typedef struct {
    uint8_t  type;
    uint16_t sequence;
    uint32_t timestamp_ms;
    uint8_t  payload_len;
    uint8_t  payload[EDGE_MAX_PAYLOAD];
} edge_frame_t;

/* Fixed-point sensor sample; integer units keep the SPARK gate simple. */
typedef struct {
    int16_t  temperature_centi_c;  /* 2345  = 23.45 degC */
    uint16_t humidity_centi_pct;   /* 4510  = 45.10 %RH  */
    uint32_t pressure_pa;          /* 101325 = 1013.25 hPa */
} edge_sensor_payload_t;

#define EDGE_SENSOR_PAYLOAD_LEN 8u

typedef struct {
    uint32_t frames_sent;
    uint32_t samples_dropped;     /* sensor queue full */
    uint16_t sensor_period_ms;
    uint16_t max_jitter_us;       /* worst |actual - nominal| period */
    uint16_t deadline_misses;
    uint16_t min_stack_free_words;
    uint32_t free_heap_bytes;
} edge_status_payload_t;

#define EDGE_STATUS_PAYLOAD_LEN 20u

uint16_t edge_crc16_update(uint16_t crc, uint8_t byte);
uint16_t edge_crc16(const uint8_t *data, size_t len);

/*
 * Serializes a frame into out. Returns the number of bytes written, or 0 if
 * the payload is too long or out_cap is too small. Always needs at most
 * EDGE_MAX_FRAME bytes.
 */
size_t edge_frame_encode(const edge_frame_t *frame, uint8_t *out, size_t out_cap);

typedef enum {
    EDGE_DECODE_FRAME_OK,
    EDGE_DECODE_BAD_VERSION,
    EDGE_DECODE_BAD_LENGTH,
    EDGE_DECODE_BAD_CRC
} edge_decode_event_t;

typedef struct {
    uint32_t frames_ok;
    uint32_t bad_version;
    uint32_t bad_length;
    uint32_t bad_crc;
    uint32_t bytes_discarded;
} edge_decoder_stats_t;

typedef struct {
    uint8_t buf[EDGE_MAX_FRAME];
    size_t len;
    edge_decoder_stats_t stats;
} edge_decoder_t;

/* frame is only non-NULL when event == EDGE_DECODE_FRAME_OK. */
typedef void (*edge_decode_handler_t)(void *ctx, edge_decode_event_t event,
                                      const edge_frame_t *frame);

void edge_decoder_init(edge_decoder_t *dec);

/*
 * Streaming decoder: feed any number of bytes as they arrive. The handler is
 * called once per complete frame and once per rejected candidate frame. After
 * a rejection the decoder rescans from the byte after the bad SOF, so a valid
 * frame hidden inside corrupted data is still recovered.
 */
void edge_decoder_feed(edge_decoder_t *dec, const uint8_t *data, size_t len,
                       edge_decode_handler_t handler, void *ctx);

void edge_sensor_encode(const edge_sensor_payload_t *in, uint8_t out[EDGE_SENSOR_PAYLOAD_LEN]);
bool edge_sensor_decode(const uint8_t *payload, size_t len, edge_sensor_payload_t *out);

void edge_status_encode(const edge_status_payload_t *in, uint8_t out[EDGE_STATUS_PAYLOAD_LEN]);
bool edge_status_decode(const uint8_t *payload, size_t len, edge_status_payload_t *out);

const char *edge_msg_type_name(uint8_t type);
const char *edge_decode_event_name(edge_decode_event_t event);

#ifdef __cplusplus
}
#endif

#endif /* EDGE_PROTOCOL_H */
