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
    uint32_t rng_state; /* state of the noise generator; never zero */
} sensor_sim_t;

/* Stores seed in the simulator. A seed of 0 is replaced, because the noise
 * generator would stay stuck at zero forever. */
void sensor_sim_init(sensor_sim_t *sim, uint32_t seed);

/* Writes one temperature, humidity, and pressure sample into out.
 * now_ms picks the position on the slow temperature and pressure waves.
 * The same seed and the same now_ms always produce the same reading. */
void sensor_sim_read(sensor_sim_t *sim, uint32_t now_ms, edge_sensor_payload_t *out);

#endif /* SENSOR_SIM_H */
