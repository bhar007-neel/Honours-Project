#include "main.h"
#include "stm32h5xx_it.h"

extern UART_HandleTypeDef huart3;

void TIM6_IRQHandler(void)
{
    if ((TIM6->SR & TIM_SR_UIF) != 0u) {
        TIM6->SR = (uint32_t)~TIM_SR_UIF;
        HAL_IncTick();
    }
}

void USART3_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart3);
}

void EXTI13_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_13);
}
