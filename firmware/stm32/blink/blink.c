/*
 * Hardware smoke test for the NUCLEO-H563ZI.
 *
 * Turns the three user LEDs on one at a time: green LD1 (PB0), yellow LD2
 * (PF4), red LD3 (PG4). No HAL and no FreeRTOS. The sensing-node application
 * needs a CubeIDE project before it can be flashed.
 */
#include <stdint.h>

#define RCC_AHB2ENR (*(volatile uint32_t *)0x44020C8Cu)

#define GPIOB_MODER (*(volatile uint32_t *)0x42020400u)
#define GPIOB_BSRR  (*(volatile uint32_t *)0x42020418u)
#define GPIOF_MODER (*(volatile uint32_t *)0x42021400u)
#define GPIOF_BSRR  (*(volatile uint32_t *)0x42021418u)
#define GPIOG_MODER (*(volatile uint32_t *)0x42021800u)
#define GPIOG_BSRR  (*(volatile uint32_t *)0x42021818u)

#define STACK_TOP 0x20040000u

static void delay(void)
{
    for (volatile uint32_t i = 0; i < 800000u; ++i) {
    }
}

static void pin_output(volatile uint32_t *moder, unsigned pin)
{
    const uint32_t shift = pin * 2u;
    *moder = (*moder & ~(3u << shift)) | (1u << shift);
}

static void run(void)
{
    RCC_AHB2ENR |= (1u << 1) | (1u << 5) | (1u << 6);
    pin_output(&GPIOB_MODER, 0);
    pin_output(&GPIOF_MODER, 4);
    pin_output(&GPIOG_MODER, 4);

    for (;;) {
        GPIOB_BSRR = 1u << 0;
        delay();
        GPIOB_BSRR = 1u << 16;

        GPIOF_BSRR = 1u << 4;
        delay();
        GPIOF_BSRR = 1u << 20;

        GPIOG_BSRR = 1u << 4;
        delay();
        GPIOG_BSRR = 1u << 20;
    }
}

void Reset_Handler(void);
void Default_Handler(void)
{
    for (;;) {
    }
}

__attribute__((used, section(".isr_vector")))
const uint32_t vector_table[] = {
    STACK_TOP,
    (uint32_t)Reset_Handler,
    (uint32_t)Default_Handler,
    (uint32_t)Default_Handler,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    (uint32_t)Default_Handler,
    (uint32_t)Default_Handler,
};

void Reset_Handler(void)
{
    run();
}
