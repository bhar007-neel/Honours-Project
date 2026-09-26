#include "edge_protocol.h"

#include <string.h>

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)(v >> 24);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no xorout. */
uint16_t edge_crc16_update(uint16_t crc, uint8_t byte)
{
    uint32_t value = (uint32_t)crc ^ ((uint32_t)byte << 8);
    for (int bit = 0; bit < 8; ++bit) {
        value = (value & 0x8000u) ? (value << 1) ^ 0x1021u : value << 1;
    }
    return (uint16_t)(value & 0xFFFFu);
}

uint16_t edge_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc = edge_crc16_update(crc, data[i]);
    }
    return crc;
}

size_t edge_frame_encode(const edge_frame_t *frame, uint8_t *out, size_t out_cap)
{
    if (frame == NULL || out == NULL || frame->payload_len > EDGE_MAX_PAYLOAD) {
        return 0;
    }
    const size_t total = EDGE_OVERHEAD + frame->payload_len;
    if (out_cap < total) {
        return 0;
    }

    out[0] = EDGE_SOF0;
    out[1] = EDGE_SOF1;
    out[2] = EDGE_PROTOCOL_VERSION;
    out[3] = frame->type;
    put_u16(&out[4], frame->sequence);
    put_u32(&out[6], frame->timestamp_ms);
    out[10] = frame->payload_len;
    memcpy(&out[EDGE_HEADER_LEN], frame->payload, frame->payload_len);

    const size_t crc_offset = EDGE_HEADER_LEN + frame->payload_len;
    put_u16(&out[crc_offset], edge_crc16(&out[2], crc_offset - 2));
    return total;
}

void edge_decoder_init(edge_decoder_t *dec)
{
    memset(dec, 0, sizeof(*dec));
}

static void discard(edge_decoder_t *dec, size_t n)
{
    memmove(dec->buf, dec->buf + n, dec->len - n);
    dec->len -= n;
}

static void report(edge_decode_handler_t handler, void *ctx, edge_decode_event_t event,
                   const edge_frame_t *frame)
{
    if (handler != NULL) {
        handler(ctx, event, frame);
    }
}

static void reject(edge_decoder_t *dec, edge_decode_handler_t handler, void *ctx,
                   edge_decode_event_t event, uint32_t *counter)
{
    ++*counter;
    report(handler, ctx, event, NULL);
    ++dec->stats.bytes_discarded;
    discard(dec, 1);
}

static void process(edge_decoder_t *dec, edge_decode_handler_t handler, void *ctx)
{
    while (dec->len > 0) {
        if (dec->buf[0] != EDGE_SOF0 || (dec->len >= 2 && dec->buf[1] != EDGE_SOF1)) {
            ++dec->stats.bytes_discarded;
            discard(dec, 1);
            continue;
        }
        if (dec->len < EDGE_HEADER_LEN) {
            return;
        }
        if (dec->buf[2] != EDGE_PROTOCOL_VERSION) {
            reject(dec, handler, ctx, EDGE_DECODE_BAD_VERSION, &dec->stats.bad_version);
            continue;
        }
        const uint8_t payload_len = dec->buf[10];
        if (payload_len > EDGE_MAX_PAYLOAD) {
            reject(dec, handler, ctx, EDGE_DECODE_BAD_LENGTH, &dec->stats.bad_length);
            continue;
        }
        const size_t total = EDGE_OVERHEAD + payload_len;
        if (dec->len < total) {
            return;
        }
        const size_t crc_offset = EDGE_HEADER_LEN + payload_len;
        if (edge_crc16(&dec->buf[2], crc_offset - 2) != get_u16(&dec->buf[crc_offset])) {
            reject(dec, handler, ctx, EDGE_DECODE_BAD_CRC, &dec->stats.bad_crc);
            continue;
        }

        edge_frame_t frame;
        frame.type = dec->buf[3];
        frame.sequence = get_u16(&dec->buf[4]);
        frame.timestamp_ms = get_u32(&dec->buf[6]);
        frame.payload_len = payload_len;
        memcpy(frame.payload, &dec->buf[EDGE_HEADER_LEN], payload_len);

        ++dec->stats.frames_ok;
        discard(dec, total);
        report(handler, ctx, EDGE_DECODE_FRAME_OK, &frame);
    }
}

void edge_decoder_feed(edge_decoder_t *dec, const uint8_t *data, size_t len,
                       edge_decode_handler_t handler, void *ctx)
{
    for (size_t i = 0; i < len; ++i) {
        /* process() always leaves fewer than EDGE_MAX_FRAME bytes buffered. */
        dec->buf[dec->len++] = data[i];
        process(dec, handler, ctx);
    }
}

void edge_sensor_encode(const edge_sensor_payload_t *in, uint8_t out[EDGE_SENSOR_PAYLOAD_LEN])
{
    put_u16(&out[0], (uint16_t)in->temperature_centi_c);
    put_u16(&out[2], in->humidity_centi_pct);
    put_u32(&out[4], in->pressure_pa);
}

bool edge_sensor_decode(const uint8_t *payload, size_t len, edge_sensor_payload_t *out)
{
    if (len != EDGE_SENSOR_PAYLOAD_LEN) {
        return false;
    }
    out->temperature_centi_c = (int16_t)get_u16(&payload[0]);
    out->humidity_centi_pct = get_u16(&payload[2]);
    out->pressure_pa = get_u32(&payload[4]);
    return true;
}

void edge_status_encode(const edge_status_payload_t *in, uint8_t out[EDGE_STATUS_PAYLOAD_LEN])
{
    put_u32(&out[0], in->frames_sent);
    put_u32(&out[4], in->samples_dropped);
    put_u16(&out[8], in->sensor_period_ms);
    put_u16(&out[10], in->max_jitter_us);
    put_u16(&out[12], in->deadline_misses);
    put_u16(&out[14], in->min_stack_free_words);
    put_u32(&out[16], in->free_heap_bytes);
}

bool edge_status_decode(const uint8_t *payload, size_t len, edge_status_payload_t *out)
{
    if (len != EDGE_STATUS_PAYLOAD_LEN) {
        return false;
    }
    out->frames_sent = get_u32(&payload[0]);
    out->samples_dropped = get_u32(&payload[4]);
    out->sensor_period_ms = get_u16(&payload[8]);
    out->max_jitter_us = get_u16(&payload[10]);
    out->deadline_misses = get_u16(&payload[12]);
    out->min_stack_free_words = get_u16(&payload[14]);
    out->free_heap_bytes = get_u32(&payload[16]);
    return true;
}

const char *edge_msg_type_name(uint8_t type)
{
    switch (type) {
    case EDGE_MSG_SENSOR:  return "SENSOR";
    case EDGE_MSG_STATUS:  return "STATUS";
    case EDGE_MSG_COMMAND: return "COMMAND";
    default:               return "UNKNOWN";
    }
}

const char *edge_decode_event_name(edge_decode_event_t event)
{
    switch (event) {
    case EDGE_DECODE_FRAME_OK:    return "frame_ok";
    case EDGE_DECODE_BAD_VERSION: return "bad_version";
    case EDGE_DECODE_BAD_LENGTH:  return "bad_length";
    case EDGE_DECODE_BAD_CRC:     return "bad_crc";
    default:                      return "unknown";
    }
}
