#include "link_health.h"

/* Clears the cursor and every counter. See link_health.h. */
void edge_link_health_init(edge_link_health_t *health)
{
    health->have_sequence = false;
    health->last_sequence = 0;
    health->in_order = 0;
    health->duplicates = 0;
    health->reorders = 0;
    health->gaps = 0;
    health->missing = 0;
    health->heard = false;
    health->silent = false;
    health->last_heard_ms = 0;
    health->silence_events = 0;
}

/* Classifies sequence against the cursor. A forward distance of 1 is the
 * next frame. A forward distance below 32768 is a gap. Anything else is
 * behind the cursor (a duplicate when the distance is 0). See link_health.h. */
edge_seq_event_t edge_link_health_observe(edge_link_health_t *health, uint16_t sequence)
{
    if (!health->have_sequence) {
        health->have_sequence = true;
        health->last_sequence = sequence;
        return EDGE_SEQ_FIRST;
    }

    const uint16_t forward = (uint16_t)(sequence - health->last_sequence);
    if (forward == 0u) {
        health->duplicates = health->duplicates + 1u;
        return EDGE_SEQ_DUPLICATE;
    }
    if (forward >= 0x8000u) {
        health->reorders = health->reorders + 1u;
        return EDGE_SEQ_REORDER;
    }

    health->last_sequence = sequence;
    if (forward == 1u) {
        health->in_order = health->in_order + 1u;
        return EDGE_SEQ_IN_ORDER;
    }

    health->gaps = health->gaps + 1u;
    health->missing = health->missing + (uint32_t)forward - 1u;
    return EDGE_SEQ_GAP;
}

/* Remembers the latest traffic time and clears a silence alarm. See link_health.h. */
void edge_link_health_hear(edge_link_health_t *health, uint32_t now_ms)
{
    health->heard = true;
    health->silent = false;
    health->last_heard_ms = now_ms;
}

/* Fires once per quiet stretch. Unsigned subtraction absorbs a clock wrap.
 * See link_health.h. */
bool edge_link_health_poll_silence(edge_link_health_t *health, uint32_t now_ms,
                                   uint32_t timeout_ms)
{
    if (!health->heard || health->silent) {
        return false;
    }
    if ((uint32_t)(now_ms - health->last_heard_ms) < timeout_ms) {
        return false;
    }
    health->silent = true;
    health->silence_events = health->silence_events + 1u;
    return true;
}

/* Short name for a log line. See link_health.h. */
const char *edge_seq_event_name(edge_seq_event_t event)
{
    switch (event) {
    case EDGE_SEQ_FIRST:     return "first";
    case EDGE_SEQ_IN_ORDER:  return "in_order";
    case EDGE_SEQ_DUPLICATE: return "duplicate";
    case EDGE_SEQ_REORDER:   return "reorder";
    case EDGE_SEQ_GAP:       return "gap";
    default:                 return "unknown";
    }
}
