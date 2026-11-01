/*
 * Reliability checks for one stream of edge frames.
 *
 * The sensing node puts a sequence number on every frame. This module turns
 * those numbers into four outcomes: the next number, a repeat, a number that
 * went backwards, or a jump that skipped one or more numbers. A late or
 * repeated number does not move the cursor backwards, so the following good
 * frame is still judged against the newest sequence that was in order.
 *
 * Silence is separate from a bad frame. After the first byte of traffic, a
 * quiet stretch of EDGE_LINK_SILENCE_MS (three 1-second heartbeats) is one
 * silence event. Traffic again clears that state so a later quiet stretch
 * counts again.
 *
 * now_ms is the caller's clock. Only the difference between two readings is
 * used, so a 32-bit wrap is fine.
 */
#ifndef LINK_HEALTH_H
#define LINK_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

/* Week 1 target: report a stopped node within three heartbeat periods. */
#define EDGE_LINK_SILENCE_MS 3000u

typedef enum {
    EDGE_SEQ_FIRST = 0, /* no earlier sequence to compare with */
    EDGE_SEQ_IN_ORDER,  /* exactly one past the cursor          */
    EDGE_SEQ_DUPLICATE, /* same number as the cursor            */
    EDGE_SEQ_REORDER,   /* behind the cursor                    */
    EDGE_SEQ_GAP        /* ahead by more than one               */
} edge_seq_event_t;

typedef struct {
    bool have_sequence;
    uint16_t last_sequence;
    uint32_t in_order;
    uint32_t duplicates;
    uint32_t reorders;
    uint32_t gaps;
    uint32_t missing; /* how many sequence numbers the gaps skipped */

    bool heard;
    bool silent;
    uint32_t last_heard_ms;
    uint32_t silence_events;
} edge_link_health_t;

/* Zeroes every counter. Call once before the first frame. */
void edge_link_health_init(edge_link_health_t *health);

/* Records one accepted frame's sequence number and returns which case it was.
 * Duplicate and reorder leave last_sequence where it was. */
edge_seq_event_t edge_link_health_observe(edge_link_health_t *health, uint16_t sequence);

/* Marks that bytes arrived at now_ms. A silent link becomes active again. */
void edge_link_health_hear(edge_link_health_t *health, uint32_t now_ms);

/* Returns true once when the link has just gone silent.
 * Does nothing until edge_link_health_hear has been called, and does not
 * return true again until traffic resumes and then stops. */
bool edge_link_health_poll_silence(edge_link_health_t *health, uint32_t now_ms,
                                   uint32_t timeout_ms);

/* Returns "first", "in_order", "duplicate", "reorder", "gap", or "unknown". */
const char *edge_seq_event_name(edge_seq_event_t event);

#endif /* LINK_HEALTH_H */
