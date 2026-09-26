/*
 * Thin board layer for the NUCLEO-H563ZI. Everything that touches the HAL or
 * Cortex-M registers lives behind this interface so the application logic
 * stays portable.
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    BOARD_LED_GREEN,  /* LD1, PB0: heartbeat              */
    BOARD_LED_YELLOW, /* LD2, PF4: toggles on rate change */
    BOARD_LED_RED     /* LD3, PG4: latched error          */
} board_led_t;

/* Call once before the scheduler starts. Enables the DWT cycle counter. */
void board_init(void);

void board_led_set(board_led_t led, bool on);
void board_led_toggle(board_led_t led);

/*
 * Transmits over the link UART using interrupts and blocks the calling task
 * (not the CPU) until done. Callers must serialize access, e.g. with a mutex.
 */
bool board_link_write(const uint8_t *data, size_t len, uint32_t timeout_ms);

uint32_t board_cycles(void);
uint32_t board_cycles_per_us(void);

/* Unrecoverable error: lights LD3 and halts with interrupts disabled. */
void board_fatal(void) __attribute__((noreturn));

#endif /* BOARD_H */
