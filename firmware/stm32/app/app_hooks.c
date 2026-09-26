/*
 * FreeRTOS failure hooks. Require configCHECK_FOR_STACK_OVERFLOW = 2 and
 * configUSE_MALLOC_FAILED_HOOK = 1. If CubeMX also generates non-weak
 * versions of these functions, delete the generated ones.
 */
#include "FreeRTOS.h"
#include "task.h"

#include "board.h"

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name; /* inspect in the debugger to see which task overflowed */
    board_fatal();
}

void vApplicationMallocFailedHook(void)
{
    board_fatal();
}
