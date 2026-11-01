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
    EDGE_MSG_SENSOR  = 0x01, /* STM32 -> Pi: one sensor sample  */
    EDGE_MSG_STATUS  = 0x02, /* STM32 -> Pi: heartbeat + health */
    EDGE_MSG_COMMAND = 0x10  /* Pi -> STM32: one command        */
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

/* Command payload: one id byte, then a little-endian argument.
 * SET_PERIOD's argument is the new sample period in milliseconds.
 * The node accepts only the periods the button already uses. */
#define EDGE_CMD_SET_PERIOD       0x01u
#define EDGE_COMMAND_PAYLOAD_LEN  3u

typedef struct {
    uint8_t  id;
    uint16_t period_ms;
} edge_command_payload_t;

/* Folds one byte into a running CRC-16/CCITT-FALSE value.
 * crc is the result so far. A new buffer starts at 0xFFFF.
 * Returns the updated CRC. */
uint16_t edge_crc16_update(uint16_t crc, uint8_t byte);

/* CRC-16/CCITT-FALSE over data[0 .. len). Starts from 0xFFFF.
 * Callers pass the bytes from the version field through the end of the payload.
 * The two start bytes are not included. */
uint16_t edge_crc16(const uint8_t *data, size_t len);

/* Writes frame into out as wire bytes, little-endian, and appends the CRC.
 * Returns the number of bytes written, or 0 if frame or out is NULL, the
 * payload is longer than EDGE_MAX_PAYLOAD, or out_cap is too small.
 * A successful frame occupies at most EDGE_MAX_FRAME bytes. */
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

/* Called once per finished frame. frame is non-NULL only when event is
 * EDGE_DECODE_FRAME_OK. ctx is whatever the caller passed to edge_decoder_feed. */
typedef void (*edge_decode_handler_t)(void *ctx, edge_decode_event_t event,
                                      const edge_frame_t *frame);

/* Clears the decoder buffer and every counter. Call once before the first feed. */
void edge_decoder_init(edge_decoder_t *dec);

/* Appends len bytes from data and extracts every complete frame in the buffer.
 * handler runs once for each good frame and once for each rejected candidate.
 * A partial frame stays buffered until a later call delivers the rest.
 * After a rejection, scanning continues at the next byte, so a good frame
 * that follows garbage is still found. */
void edge_decoder_feed(edge_decoder_t *dec, const uint8_t *data, size_t len,
                       edge_decode_handler_t handler, void *ctx);

/* Packs a sensor sample into 8 little-endian bytes: temperature, then
 * humidity, then pressure. */
void edge_sensor_encode(const edge_sensor_payload_t *in, uint8_t out[EDGE_SENSOR_PAYLOAD_LEN]);

/* Unpacks an 8-byte sensor payload into out.
 * Returns false, and does not write out, when len is not 8. */
bool edge_sensor_decode(const uint8_t *payload, size_t len, edge_sensor_payload_t *out);

/* Packs a status report into 20 little-endian bytes, in the field order
 * of edge_status_payload_t. */
void edge_status_encode(const edge_status_payload_t *in, uint8_t out[EDGE_STATUS_PAYLOAD_LEN]);

/* Unpacks a 20-byte status payload into out.
 * Returns false, and does not write out, when len is not 20. */
bool edge_status_decode(const uint8_t *payload, size_t len, edge_status_payload_t *out);

/* Packs a command into 3 little-endian bytes: id, then period_ms. */
void edge_command_encode(const edge_command_payload_t *in, uint8_t out[EDGE_COMMAND_PAYLOAD_LEN]);

/* Unpacks a 3-byte command payload into out.
 * Returns false, and does not write out, when len is not 3. */
bool edge_command_decode(const uint8_t *payload, size_t len, edge_command_payload_t *out);

/* True for 10, 20, 50, 100, and 200 ms. Those are the periods the sensing
 * node already cycles with the user button. */
bool edge_sensor_period_allowed(uint16_t period_ms);

/* Returns "set_period" or "unknown". For logs. */
const char *edge_command_name(uint8_t id);

/* Returns "SENSOR", "STATUS", "COMMAND", or "UNKNOWN". For logs. */
const char *edge_msg_type_name(uint8_t type);

/* Returns "frame_ok", "bad_version", "bad_length", "bad_crc", or "unknown".
 * For logs. */
const char *edge_decode_event_name(edge_decode_event_t event);

#ifdef __cplusplus
}
#endif

#endif /* EDGE_PROTOCOL_H */
