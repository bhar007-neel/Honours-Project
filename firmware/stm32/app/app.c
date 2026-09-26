/*
 * Sensing-node application: four FreeRTOS tasks.
 *
 *   SensorTask     periodic (100 ms default), highest priority. Samples the
 *                  sensor at a fixed rate with xTaskDelayUntil and measures its
 *                  own period jitter with the DWT cycle counter.
 *   CommTask       event-driven. Blocks on the sample queue, formats each
 *                  sample, and transmits it over the link UART.
 *   ControlTask    sporadic. Woken by the user-button interrupt through a
 *                  binary semaphore; cycles the sensor period.
 *   HeartbeatTask  periodic (1 s), lowest priority. Blinks LD1 and reports
 *                  health: dropped samples, jitter, stack and heap margins.
 *
 * Synchronization: queue (SensorTask -> CommTask), mutex (CommTask and
 * HeartbeatTask share the UART), binary semaphore (ISR -> ControlTask).
 */
#include "app.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "app_config.h"
#include "board.h"
#include "edge_protocol.h"
#include "rt_stats.h"
#include "sensor_sim.h"

typedef struct {
    uint32_t timestamp_ms;
    edge_sensor_payload_t reading;
} sample_t;

enum { TASK_SENSOR, TASK_COMM, TASK_CONTROL, TASK_HEARTBEAT, TASK_COUNT };

static QueueHandle_t s_sample_queue;
static SemaphoreHandle_t s_link_mutex;
static SemaphoreHandle_t s_button_sem;
static TaskHandle_t s_tasks[TASK_COUNT];

static volatile uint32_t s_sensor_period_ms = APP_SENSOR_PERIOD_MS_DEFAULT;
static volatile uint32_t s_samples_dropped; /* written only by SensorTask */
static rt_stats_t s_sensor_stats;           /* written only by SensorTask */

/* Guarded by s_link_mutex. */
static uint16_t s_sequence;
static uint32_t s_frames_sent;
static uint32_t s_link_errors;

static uint32_t uptime_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static uint16_t clamp_u16(uint32_t v)
{
    return (v > UINT16_MAX) ? UINT16_MAX : (uint16_t)v;
}

/* Takes the link and returns the sequence number for the message about to be sent. */
static uint16_t link_begin(void)
{
    (void)xSemaphoreTake(s_link_mutex, portMAX_DELAY);
    return s_sequence++;
}

static void link_write_and_end(const uint8_t *data, size_t len)
{
    if (len > 0u && board_link_write(data, len, APP_LINK_TX_TIMEOUT_MS)) {
        ++s_frames_sent;
    } else {
        ++s_link_errors;
    }
    (void)xSemaphoreGive(s_link_mutex);
}

#if APP_OUTPUT_TEXT
static size_t text_len(int n, size_t cap)
{
    if (n < 0) {
        return 0;
    }
    return ((size_t)n >= cap) ? cap - 1u : (size_t)n;
}
#else
static void link_write_frame_and_end(uint8_t type, uint16_t seq, uint32_t timestamp_ms,
                                     const uint8_t *payload, uint8_t payload_len)
{
    edge_frame_t frame;
    frame.type = type;
    frame.sequence = seq;
    frame.timestamp_ms = timestamp_ms;
    frame.payload_len = payload_len;
    memcpy(frame.payload, payload, payload_len);

    uint8_t wire[EDGE_MAX_FRAME];
    link_write_and_end(wire, edge_frame_encode(&frame, wire, sizeof wire));
}
#endif

static void send_sensor(const sample_t *s)
{
    const uint16_t seq = link_begin();
#if APP_OUTPUT_TEXT
    const int32_t t = s->reading.temperature_centi_c;
    const uint32_t t_abs = (uint32_t)(t < 0 ? -t : t);
    char line[112];
    const int n = snprintf(line, sizeof line,
                           "SENSOR seq=%u t_ms=%lu temp_c=%s%lu.%02lu hum_pct=%u.%02u press_pa=%lu\r\n",
                           (unsigned)seq, (unsigned long)s->timestamp_ms, (t < 0) ? "-" : "",
                           (unsigned long)(t_abs / 100u), (unsigned long)(t_abs % 100u),
                           (unsigned)(s->reading.humidity_centi_pct / 100u),
                           (unsigned)(s->reading.humidity_centi_pct % 100u),
                           (unsigned long)s->reading.pressure_pa);
    link_write_and_end((const uint8_t *)line, text_len(n, sizeof line));
#else
    uint8_t payload[EDGE_SENSOR_PAYLOAD_LEN];
    edge_sensor_encode(&s->reading, payload);
    link_write_frame_and_end(EDGE_MSG_SENSOR, seq, s->timestamp_ms, payload, sizeof payload);
#endif
}

static uint16_t min_stack_free_words(void)
{
    UBaseType_t min_free = UINT16_MAX;
    for (size_t i = 0; i < TASK_COUNT; ++i) {
        const UBaseType_t free_words = uxTaskGetStackHighWaterMark(s_tasks[i]);
        if (free_words < min_free) {
            min_free = free_words;
        }
    }
    return clamp_u16((uint32_t)min_free);
}

static void send_status(void)
{
    const uint16_t seq = link_begin();
    const uint32_t now = uptime_ms();

    edge_status_payload_t st;
    st.frames_sent = s_frames_sent;
    st.samples_dropped = s_samples_dropped;
    st.sensor_period_ms = clamp_u16(s_sensor_period_ms);
    st.max_jitter_us = clamp_u16(s_sensor_stats.max_jitter_us);
    st.deadline_misses = clamp_u16(s_sensor_stats.overruns);
    st.min_stack_free_words = min_stack_free_words();
    st.free_heap_bytes = (uint32_t)xPortGetFreeHeapSize();

#if APP_OUTPUT_TEXT
    char line[224];
    const int n = snprintf(line, sizeof line,
                           "STATUS seq=%u t_ms=%lu period_ms=%u avg_period_us=%lu "
                           "max_jitter_us=%u overruns=%u max_exec_us=%lu dropped=%lu "
                           "sent=%lu link_errors=%lu stack_min_words=%u heap_free=%lu\r\n",
                           (unsigned)seq, (unsigned long)now, (unsigned)st.sensor_period_ms,
                           (unsigned long)rt_stats_avg_period_us(&s_sensor_stats),
                           (unsigned)st.max_jitter_us, (unsigned)st.deadline_misses,
                           (unsigned long)s_sensor_stats.max_exec_us,
                           (unsigned long)st.samples_dropped, (unsigned long)st.frames_sent,
                           (unsigned long)s_link_errors, (unsigned)st.min_stack_free_words,
                           (unsigned long)st.free_heap_bytes);
    link_write_and_end((const uint8_t *)line, text_len(n, sizeof line));
#else
    uint8_t payload[EDGE_STATUS_PAYLOAD_LEN];
    edge_status_encode(&st, payload);
    link_write_frame_and_end(EDGE_MSG_STATUS, seq, now, payload, sizeof payload);
#endif
}

static void sensor_task(void *arg)
{
    (void)arg;
    sensor_sim_t sim;
    sensor_sim_init(&sim, APP_SENSOR_SEED);

    uint32_t period_ms = s_sensor_period_ms;
    rt_stats_init(&s_sensor_stats, period_ms * 1000u, APP_OVERRUN_TOLERANCE_PCT,
                  board_cycles_per_us());
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        (void)xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(period_ms));
        rt_stats_on_release(&s_sensor_stats, board_cycles());

        sample_t sample;
        sample.timestamp_ms = uptime_ms();
        sensor_sim_read(&sim, sample.timestamp_ms, &sample.reading);

        /* Never block the highest-priority task: a full queue means CommTask is behind. */
        if (xQueueSend(s_sample_queue, &sample, 0) != pdPASS) {
            s_samples_dropped = s_samples_dropped + 1u;
        }

        rt_stats_on_complete(&s_sensor_stats, board_cycles());

        const uint32_t requested = s_sensor_period_ms;
        if (requested != period_ms) {
            period_ms = requested;
            rt_stats_reset(&s_sensor_stats, period_ms * 1000u, APP_OVERRUN_TOLERANCE_PCT);
            last_wake = xTaskGetTickCount();
        }
    }
}

static void comm_task(void *arg)
{
    (void)arg;
    for (;;) {
        sample_t sample;
        if (xQueueReceive(s_sample_queue, &sample, portMAX_DELAY) == pdPASS) {
            send_sensor(&sample);
        }
    }
}

static void control_task(void *arg)
{
    (void)arg;
    static const uint32_t steps_ms[] = APP_SENSOR_PERIOD_STEPS_MS;
    size_t step = 0;

    for (;;) {
        (void)xSemaphoreTake(s_button_sem, portMAX_DELAY);

        /* Debounce: let the contacts settle, then discard the bounce edges. */
        vTaskDelay(pdMS_TO_TICKS(APP_BUTTON_DEBOUNCE_MS));
        while (xSemaphoreTake(s_button_sem, 0) == pdPASS) {
        }

        step = (step + 1u) % (sizeof steps_ms / sizeof steps_ms[0]);
        s_sensor_period_ms = steps_ms[step];
        board_led_toggle(BOARD_LED_YELLOW);
    }
}

static void heartbeat_task(void *arg)
{
    (void)arg;

#if APP_OUTPUT_TEXT
    {
        (void)link_begin();
        char line[96];
        const int n = snprintf(line, sizeof line, "BOOT edge-node protocol=%u core_mhz=%lu\r\n",
                               (unsigned)EDGE_PROTOCOL_VERSION,
                               (unsigned long)board_cycles_per_us());
        link_write_and_end((const uint8_t *)line, text_len(n, sizeof line));
    }
#endif

    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        (void)xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_HEARTBEAT_PERIOD_MS));
        board_led_toggle(BOARD_LED_GREEN);
        if (s_samples_dropped > 0u || s_sensor_stats.overruns > 0u) {
            board_led_set(BOARD_LED_RED, true);
        }
        send_status();
    }
}

static void create_task(TaskFunction_t fn, const char *name, uint16_t stack_words,
                        UBaseType_t priority, size_t slot)
{
    if (xTaskCreate(fn, name, stack_words, NULL, priority, &s_tasks[slot]) != pdPASS) {
        board_fatal();
    }
}

void app_init(void)
{
    board_init();

    s_sample_queue = xQueueCreate(APP_SAMPLE_QUEUE_LEN, sizeof(sample_t));
    s_link_mutex = xSemaphoreCreateMutex();
    s_button_sem = xSemaphoreCreateBinary();
    if (s_sample_queue == NULL || s_link_mutex == NULL || s_button_sem == NULL) {
        board_fatal();
    }
    vQueueAddToRegistry(s_sample_queue, "samples");
    vQueueAddToRegistry(s_link_mutex, "link");

    create_task(sensor_task, "Sensor", APP_STACK_SENSOR, APP_PRIO_SENSOR, TASK_SENSOR);
    create_task(comm_task, "Comm", APP_STACK_COMM, APP_PRIO_COMM, TASK_COMM);
    create_task(control_task, "Control", APP_STACK_CONTROL, APP_PRIO_CONTROL, TASK_CONTROL);
    create_task(heartbeat_task, "Heartbeat", APP_STACK_HEARTBEAT, APP_PRIO_HEARTBEAT,
                TASK_HEARTBEAT);
}

void app_on_button_isr(void)
{
    if (s_button_sem == NULL) {
        return;
    }
    BaseType_t woken = pdFALSE;
    (void)xSemaphoreGiveFromISR(s_button_sem, &woken);
    portYIELD_FROM_ISR(woken);
}
