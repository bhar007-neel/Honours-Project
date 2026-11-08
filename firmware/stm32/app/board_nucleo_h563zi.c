#include "board.h"

#include "main.h"

#include "FreeRTOS.h"
#include "task.h"

#include "app.h"
#include "app_config.h"

extern UART_HandleTypeDef APP_LINK_UART_HANDLE;
extern UART_HandleTypeDef hlpuart1;

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} led_pin_t;

static const led_pin_t k_leds[] = {
    [BOARD_LED_GREEN]  = {GPIOB, GPIO_PIN_0},
    [BOARD_LED_YELLOW] = {GPIOF, GPIO_PIN_4},
    [BOARD_LED_RED]    = {GPIOG, GPIO_PIN_4},
};

#define USER_BUTTON_PIN GPIO_PIN_13 /* B1 on PC13, high when pressed */

static TaskHandle_t volatile s_tx_waiter;

/* One-producer (UART IRQ), one-consumer (the task in board_link_read). */
#define BOARD_RX_CAP 256u
static uint8_t s_rx_ring[BOARD_RX_CAP];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;
static TaskHandle_t volatile s_rx_waiter;
static uint8_t s_rx_byte;
static volatile bool s_rx_rearm;

static void rx_push_from_isr(uint8_t byte, BaseType_t *woken)
{
    const uint16_t head = s_rx_head;
    const uint16_t next = (uint16_t)((head + 1u) % BOARD_RX_CAP);
    if (next == s_rx_tail) {
        return; /* full: drop this byte; the decoder resyncs after the gap */
    }
    s_rx_ring[head] = byte;
    s_rx_head = next;

    TaskHandle_t waiter = s_rx_waiter;
    if (waiter != NULL) {
        vTaskNotifyGiveFromISR(waiter, woken);
    }
}

static size_t rx_drain(uint8_t *out, size_t cap)
{
    size_t n = 0;
    uint16_t tail = s_rx_tail;
    while (n < cap && tail != s_rx_head) {
        out[n] = s_rx_ring[tail];
        n = n + 1u;
        tail = (uint16_t)((tail + 1u) % BOARD_RX_CAP);
    }
    s_rx_tail = tail;
    return n;
}

static void rx_arm(void)
{
    if (APP_LINK_UART_HANDLE.RxState != HAL_UART_STATE_READY) {
        return;
    }
    if (HAL_UART_Receive_IT(&APP_LINK_UART_HANDLE, &s_rx_byte, 1) != HAL_OK) {
        s_rx_rearm = true;
    } else {
        s_rx_rearm = false;
    }
}

void board_init(void)
{
#if defined(DCB)
    DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
#else
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
#endif
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    for (size_t i = 0; i < sizeof k_leds / sizeof k_leds[0]; ++i) {
        HAL_GPIO_WritePin(k_leds[i].port, k_leds[i].pin, GPIO_PIN_RESET);
    }
}

void board_led_set(board_led_t led, bool on)
{
    HAL_GPIO_WritePin(k_leds[led].port, k_leds[led].pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void board_led_toggle(board_led_t led)
{
    HAL_GPIO_TogglePin(k_leds[led].port, k_leds[led].pin);
}

bool board_link_write(const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    if (len == 0u || len > UINT16_MAX) {
        return false;
    }

    /* D1/D0, the jumper wires to the Pi. The USB cable still gets the same bytes below. */
    (void)HAL_UART_Transmit(&hlpuart1, (uint8_t *)data, (uint16_t)len, timeout_ms);

    /* Drop a completion that arrived after a previous timeout. */
    (void)ulTaskNotifyTake(pdTRUE, 0);
    s_tx_waiter = xTaskGetCurrentTaskHandle();

    if (HAL_UART_Transmit_IT(&APP_LINK_UART_HANDLE, (uint8_t *)data, (uint16_t)len) != HAL_OK) {
        s_tx_waiter = NULL;
        return false;
    }

    const bool done = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeout_ms)) > 0u;
    s_tx_waiter = NULL;
    if (!done) {
        (void)HAL_UART_AbortTransmit(&APP_LINK_UART_HANDLE);
    }
    return done;
}

void board_link_rx_start(void)
{
    rx_arm();
}

size_t board_link_read(uint8_t *out, size_t cap, uint32_t timeout_ms)
{
    if (out == NULL || cap == 0u) {
        return 0u;
    }
    if (s_rx_rearm) {
        rx_arm();
    }

    /* The waiter is published before the drain so a byte that arrives
     * between an empty drain and the wait is not lost. */
    s_rx_waiter = xTaskGetCurrentTaskHandle();
    size_t n = rx_drain(out, cap);
    if (n == 0u && timeout_ms > 0u) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeout_ms));
        n = rx_drain(out, cap);
    }
    s_rx_waiter = NULL;
    return n;
}

uint32_t board_cycles(void)
{
    return DWT->CYCCNT;
}

uint32_t board_cycles_per_us(void)
{
    return SystemCoreClock / 1000000u;
}

void board_fatal(void)
{
    __disable_irq();
    HAL_GPIO_WritePin(k_leds[BOARD_LED_RED].port, k_leds[BOARD_LED_RED].pin, GPIO_PIN_SET);
    for (;;) {
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    TaskHandle_t waiter = s_tx_waiter;
    if (huart == &APP_LINK_UART_HANDLE && waiter != NULL) {
        BaseType_t woken = pdFALSE;
        vTaskNotifyGiveFromISR(waiter, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart != &APP_LINK_UART_HANDLE) {
        return;
    }
    BaseType_t woken = pdFALSE;
    rx_push_from_isr(s_rx_byte, &woken);
    if (HAL_UART_Receive_IT(huart, &s_rx_byte, 1) != HAL_OK) {
        s_rx_rearm = true;
    }
    portYIELD_FROM_ISR(woken);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart != &APP_LINK_UART_HANDLE) {
        return;
    }
    /* Framing and overrun errors end the current reception. Arm the next byte. */
    if (HAL_UART_Receive_IT(huart, &s_rx_byte, 1) != HAL_OK) {
        s_rx_rearm = true;
    }
}

void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == USER_BUTTON_PIN) {
        app_on_button_isr();
    }
}
