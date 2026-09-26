#include "board.h"

#include "main.h"

#include "FreeRTOS.h"
#include "task.h"

#include "app.h"
#include "app_config.h"

extern UART_HandleTypeDef APP_LINK_UART_HANDLE;

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

void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == USER_BUTTON_PIN) {
        app_on_button_isr();
    }
}
