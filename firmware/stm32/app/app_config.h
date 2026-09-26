/*
 * Tunable parameters for the sensing-node application.
 *
 * Priorities follow rate-monotonic ordering: the shorter the period (or the
 * tighter the reaction requirement), the higher the priority.
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "FreeRTOS.h"

/* 1 = human-readable lines for a PC serial monitor; 0 = binary edge frames for the Pi. */
#ifndef APP_OUTPUT_TEXT
#define APP_OUTPUT_TEXT 1
#endif

/* CubeMX-generated handle of the UART wired to the ST-LINK virtual COM port. */
#define APP_LINK_UART_HANDLE huart3
#define APP_LINK_TX_TIMEOUT_MS 50u

#define APP_SENSOR_PERIOD_MS_DEFAULT 100u
#define APP_HEARTBEAT_PERIOD_MS      1000u
#define APP_BUTTON_DEBOUNCE_MS       50u
#define APP_OVERRUN_TOLERANCE_PCT    10u
#define APP_SENSOR_SEED              0x1234ABCDu

/* Sensor periods cycled by the user button (B1). */
#define APP_SENSOR_PERIOD_STEPS_MS { 100u, 50u, 20u, 10u, 200u }

#define APP_SAMPLE_QUEUE_LEN 8u

#define APP_PRIO_SENSOR    (tskIDLE_PRIORITY + 4)
#define APP_PRIO_COMM      (tskIDLE_PRIORITY + 3)
#define APP_PRIO_CONTROL   (tskIDLE_PRIORITY + 2)
#define APP_PRIO_HEARTBEAT (tskIDLE_PRIORITY + 1)

/* Stack sizes in words (4 bytes each); snprintf in text mode needs the larger stacks. */
#define APP_STACK_SENSOR    256u
#define APP_STACK_COMM      512u
#define APP_STACK_CONTROL   192u
#define APP_STACK_HEARTBEAT 512u

#endif /* APP_CONFIG_H */
