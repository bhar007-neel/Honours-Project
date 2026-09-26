/*
 * Simulated BME280-style environment sensor.
 *
 * Produces deterministic, slowly varying temperature, humidity, and pressure
 * with small noise, so the whole pipeline can be exercised before a physical
 * sensor is wired. Portable C: no HAL or RTOS dependencies.
 */
#ifndef SENSOR_SIM_H
#define SENSOR_SIM_H

#include <stdint.h>

#include "edge_protocol.h"

typedef struct {
    uint32_t rng_state;
} sensor_sim_t;

void sensor_sim_init(sensor_sim_t *sim, uint32_t seed);

/* now_ms drives the slow waveforms; the same seed and times give the same data. */
void sensor_sim_read(sensor_sim_t *sim, uint32_t now_ms, edge_sensor_payload_t *out);

#endif /* SENSOR_SIM_H */
