#include "sensor_gate.h"

/* Same decision order as Sensor_Gate.Check in sensor_gate.adb. */
sensor_gate_verdict_t sensor_gate_check(const edge_sensor_payload_t *reading)
{
    const int16_t temperature = reading->temperature_centi_c;
    const uint16_t humidity = reading->humidity_centi_pct;
    const uint32_t pressure = reading->pressure_pa;

    if (temperature < SENSOR_GATE_MIN_TEMP_CENTI_C || temperature > SENSOR_GATE_MAX_TEMP_CENTI_C) {
        return SENSOR_GATE_TEMPERATURE_OUT_OF_RANGE;
    }
    if (humidity > SENSOR_GATE_MAX_HUM_CENTI_PCT) {
        return SENSOR_GATE_HUMIDITY_OUT_OF_RANGE;
    }
    if (pressure < SENSOR_GATE_MIN_PRESS_PA || pressure > SENSOR_GATE_MAX_PRESS_PA) {
        return SENSOR_GATE_PRESSURE_OUT_OF_RANGE;
    }
    return SENSOR_GATE_ACCEPTED;
}

/* Mirrors Temperature_Step_Ok. int32 holds every int16 difference. */
bool sensor_gate_temperature_step_ok(int16_t previous_centi_c, int16_t current_centi_c,
                                    int16_t max_step_centi_c)
{
    if (max_step_centi_c < 0) {
        return false;
    }
    const int32_t difference = (int32_t)current_centi_c - (int32_t)previous_centi_c;
    const int32_t magnitude = (difference < 0) ? -difference : difference;
    return magnitude <= (int32_t)max_step_centi_c;
}

const char *sensor_gate_verdict_name(sensor_gate_verdict_t verdict)
{
    switch (verdict) {
    case SENSOR_GATE_ACCEPTED:                 return "accepted";
    case SENSOR_GATE_TEMPERATURE_OUT_OF_RANGE: return "temperature_out_of_range";
    case SENSOR_GATE_HUMIDITY_OUT_OF_RANGE:    return "humidity_out_of_range";
    case SENSOR_GATE_PRESSURE_OUT_OF_RANGE:    return "pressure_out_of_range";
    default:                                   return "unknown";
    }
}
