#include "sensor_sim.h"

#define TEMP_BASE_CENTI_C   2200
#define TEMP_SWING_CENTI_C  300
#define TEMP_PERIOD_MS      60000u
#define TEMP_NOISE          20

#define HUM_BASE_CENTI_PCT  4500
#define HUM_NOISE           30

#define PRESS_BASE_PA       101325
#define PRESS_SWING_PA      150
#define PRESS_PERIOD_MS     300000u
#define PRESS_NOISE         5

/* Advances the noise generator by one step and returns the new value.
 * The same state always produces the same next value, so a run can be repeated. */
static uint32_t xorshift32(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/* Returns one random offset in the range -amplitude .. +amplitude.
 * Each call consumes one step of the simulator's generator. */
static int32_t noise(sensor_sim_t *sim, int32_t amplitude)
{
    return (int32_t)(xorshift32(&sim->rng_state) % (uint32_t)(2 * amplitude + 1)) - amplitude;
}

/* Returns a point on a triangle wave that rises from -amplitude to +amplitude
 * and back again, once every period_ms. now_ms selects where on that wave we are. */
static int32_t triangle(uint32_t now_ms, uint32_t period_ms, int32_t amplitude)
{
    const uint32_t phase = now_ms % period_ms;
    const uint32_t half = period_ms / 2u;
    const uint32_t rise = (phase < half) ? phase : period_ms - phase;
    return (int32_t)(((int64_t)rise * 2 * amplitude) / half) - amplitude;
}

/* Saves the seed that noise() will start from. See sensor_sim.h. */
void sensor_sim_init(sensor_sim_t *sim, uint32_t seed)
{
    /* xorshift must never hold zero. */
    sim->rng_state = (seed != 0u) ? seed : 0x6D2B79F5u;
}

/* Fills out with one sample: a slow temperature wave, humidity that falls when
 * temperature rises, and a much slower pressure wave. Each field then gets a
 * small noise() offset. See sensor_sim.h. */
void sensor_sim_read(sensor_sim_t *sim, uint32_t now_ms, edge_sensor_payload_t *out)
{
    const int32_t temp_offset = triangle(now_ms, TEMP_PERIOD_MS, TEMP_SWING_CENTI_C);

    /* Relative humidity falls as temperature rises, as it does indoors. */
    const int32_t humidity = HUM_BASE_CENTI_PCT - temp_offset / 2 + noise(sim, HUM_NOISE);

    out->temperature_centi_c = (int16_t)(TEMP_BASE_CENTI_C + temp_offset + noise(sim, TEMP_NOISE));
    out->humidity_centi_pct = (uint16_t)humidity;
    out->pressure_pa = (uint32_t)(PRESS_BASE_PA + triangle(now_ms, PRESS_PERIOD_MS, PRESS_SWING_PA) +
                                  noise(sim, PRESS_NOISE));
}
