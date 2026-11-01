#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configUSE_PREEMPTION                       1
#define configUSE_IDLE_HOOK                        0
#define configUSE_TICK_HOOK                        0
#define configCPU_CLOCK_HZ                         250000000u
#define configTICK_RATE_HZ                         1000u
#define configMAX_PRIORITIES                       8
#define configMINIMAL_STACK_SIZE                   128
#define configTOTAL_HEAP_SIZE                      (32u * 1024u)
#define configMAX_TASK_NAME_LEN                    16
#define configUSE_16_BIT_TICKS                     0
#define configUSE_MUTEXES                          1
#define configUSE_COUNTING_SEMAPHORES              1
#define configQUEUE_REGISTRY_SIZE                  8
#define configUSE_PORT_OPTIMISED_TASK_SELECTION    0
#define configSUPPORT_DYNAMIC_ALLOCATION           1
#define configSUPPORT_STATIC_ALLOCATION            0
#define configCHECK_FOR_STACK_OVERFLOW             2
#define configUSE_MALLOC_FAILED_HOOK               1

#define configENABLE_MPU                           0
#define configENABLE_FPU                           0
#define configENABLE_MVE                           0
#define configENABLE_TRUSTZONE                     0
#define configRUN_FREERTOS_SECURE_ONLY             1

/* 4 priority bits. Syscalls are allowed from priority 5 and above (numerically). */
#define configPRIO_BITS                                4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY        15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY   5
#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

#define INCLUDE_vTaskDelay                         1
#define INCLUDE_xTaskDelayUntil                    1
#define INCLUDE_uxTaskGetStackHighWaterMark        1

#define configASSERT(x) do { if ((x) == 0) { for (;;) {} } } while (0)

#endif /* FREERTOS_CONFIG_H */
