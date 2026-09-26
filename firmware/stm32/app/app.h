#ifndef APP_H
#define APP_H

/*
 * Creates the application's queues, mutex, semaphore, and tasks.
 * Call once after osKernelInitialize()/MX_FREERTOS_Init() and before
 * osKernelStart().
 */
void app_init(void);

/* Called from the user-button EXTI interrupt. */
void app_on_button_isr(void);

#endif /* APP_H */
