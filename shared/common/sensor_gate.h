/*
 * C copy of the acceptance rule in verification/spark/edge_gate.
 *
 * The SPARK package is what GNATprove proves. This file is what the QNX
 * receiver runs. The limits and the order of the checks match sensor_gate.ads:
 * temperature, then humidity, then pressure. The first failure is the verdict.
 */
#ifndef SENSOR_GATE_H
#define SENSOR_GATE_H

#include <stdbool.h>
#include <stdint.h>

#include "edge_protocol.h"

/* BME280 operating window, in the same integer units as the wire payload. */
#define SENSOR_GATE_MIN_TEMP_CENTI_C   (-4000) /* -40.00 degC */
#define SENSOR_GATE_MAX_TEMP_CENTI_C   8500    /*  85.00 degC */
#define SENSOR_GATE_MAX_HUM_CENTI_PCT  10000   /* 100.00 %RH  */
#define SENSOR_GATE_MIN_PRESS_PA       30000u  /* 300 hPa     */
#define SENSOR_GATE_MAX_PRESS_PA       110000u /* 1100 hPa    */

/* Largest temperature jump still treated as plausible, 1.00 degC.
 * The same limit is used by edge_gate_demo. */
#define SENSOR_GATE_MAX_STEP_CENTI_C   100

typedef enum {
    SENSOR_GATE_ACCEPTED = 0,
    SENSOR_GATE_TEMPERATURE_OUT_OF_RANGE,
    SENSOR_GATE_HUMIDITY_OUT_OF_RANGE,
    SENSOR_GATE_PRESSURE_OUT_OF_RANGE
} sensor_gate_verdict_t;

/* Returns Accepted only when every field is inside its limits.
 * Otherwise names the first field that is out of range. */
sensor_gate_verdict_t sensor_gate_check(const edge_sensor_payload_t *reading);

/* True when |current - previous| is at most max_step_centi_c.
 * The subtraction is done in a wider integer so the extreme int16 pair
 * (-32768 and 32767) cannot overflow. */
bool sensor_gate_temperature_step_ok(int16_t previous_centi_c, int16_t current_centi_c,
                                    int16_t max_step_centi_c);

/* "accepted", "temperature_out_of_range", "humidity_out_of_range",
 * "pressure_out_of_range", or "unknown". For logs. */
const char *sensor_gate_verdict_name(sensor_gate_verdict_t verdict);

#endif /* SENSOR_GATE_H */
