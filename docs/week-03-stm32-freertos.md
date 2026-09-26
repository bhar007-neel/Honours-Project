# Week 3: STM32/FreeRTOS Sensing Node

Goal: a working FreeRTOS prototype on the NUCLEO-H563ZI with multiple real-time
tasks, measured priorities and timing, and simulated sensor acquisition.

## What the code does

The application lives in `firmware/stm32/app/`. The files CubeIDE generates
only need a single call to `app_init()`.

- **SensorTask** (priority 4, period 100 ms). Wakes with `xTaskDelayUntil`,
  reads the simulated sensor, and queues the sample without blocking. It
  measures its own period, jitter, and execution time with the DWT cycle
  counter.
- **CommTask** (priority 3, event-driven). Blocks on the sample queue and
  transmits each sample on USART3, the ST-LINK virtual COM port.
- **ControlTask** (priority 2, sporadic). Woken by the B1 button interrupt
  through a binary semaphore. Cycles the sensor period through 100, 50, 20,
  10, and 200 ms, and toggles LD2.
- **HeartbeatTask** (priority 1, period 1 s). Toggles LD1 and sends a status
  report: jitter, overruns, dropped samples, and stack and heap margins.
- **Synchronization:** a queue (sensor to comm), a mutex around the UART
  (CommTask and HeartbeatTask share it), a binary semaphore (ISR to
  ControlTask), and a task notification (UART transmit-complete interrupt to
  the transmitting task).
- **LEDs:** LD1 green is the heartbeat. LD2 yellow toggles on a rate change.
  LD3 red latches after a dropped sample or overrun, and stays solid after a
  fatal error (stack overflow or out of heap).

Portable pieces (`shared/protocol`, `shared/common`) are unit-tested on the PC
with `tests/host/run_tests.ps1`.

## 1. Create the CubeIDE project

1. **File > New > STM32 Project**, **Board Selector**, choose `NUCLEO-H563ZI`.
2. Project name `edge-node`. Untick *Use default location* and set it to
   `<repo>/firmware/stm32/edge-node`.
3. When asked about TrustZone, choose **without TrustZone**. When asked to
   initialize peripherals to their default mode, choose **Yes**.

## 2. Configure in the `.ioc` editor

**FreeRTOS** (from the X-CUBE-FREERTOS pack):

1. **Software Packs > Select Components**. Under `STMicroelectronics.X-CUBE-FREERTOS`
   (install it if prompted), select the FreeRTOS kernel with the
   **CMSIS-RTOS2** interface and **heap_4**.
2. Enable it under **Middleware and Software Packs > X-CUBE-FREERTOS**. Then set:
   - `TOTAL_HEAP_SIZE`: at least 16384
   - `CHECK_FOR_STACK_OVERFLOW`: option 2
   - `USE_MALLOC_FAILED_HOOK`: enabled
   - `USE_MUTEXES`: enabled
   - `QUEUE_REGISTRY_SIZE`: 8
   - Include parameters `xTaskDelayUntil`, `uxTaskGetStackHighWaterMark`, and
     `vTaskDelay`: enabled

**System and peripherals:**

- **SYS > Timebase Source:** `TIM6`. SysTick must be left to the kernel.
- **USART3:** Asynchronous, 115200 8N1, pins PD8 (TX) and PD9 (RX). In
  **NVIC Settings**, enable *USART3 global interrupt*. If the board template
  assigned the virtual COM port to the BSP COM driver instead, disable it
  there so CubeMX generates `huart3`.
- **PC13 (B1):** `GPIO_EXTI13`, rising edge. Enable *EXTI Line13 interrupt*
  in the NVIC.
- **PB0, PF4, PG4:** `GPIO_Output` (LD1-LD3; normally preset by the board
  template).
- **NVIC priorities:** USART3 and EXTI13 must have a preemption priority of
  **5 or higher numerically**, for example 6. They call FreeRTOS `FromISR`
  functions, and a lower number (more urgent) breaks the kernel. CubeMX
  enforces this when *Uses FreeRTOS functions* is ticked.

Save to generate the code.

## 3. Add the application code

**Project > Properties > C/C++ General > Paths and Symbols**, All
configurations:

1. **Source Location > Link Folder**, *Link to folder in the file system*.
   Add these three folders:
   - `PROJECT_LOC/../app`
   - `PROJECT_LOC/../../../shared/protocol`
   - `PROJECT_LOC/../../../shared/common`
2. **Includes > GNU C:** add the same three paths as workspace-relative or
   file-system paths.
3. For binary output (for the Pi), add the symbol `APP_OUTPUT_TEXT=0` under
   **Symbols**. The default is readable text.

Start the application in the generated FreeRTOS init file (`app_freertos.c`
or `freertos.c`):

```c
/* USER CODE BEGIN Includes */
#include "app.h"
/* USER CODE END Includes */

/* USER CODE BEGIN RTOS_THREADS */
app_init();
/* USER CODE END RTOS_THREADS */
```

You can delete the generated `defaultTask` in the `.ioc` editor, or leave it
idle. If CubeMX generated non-weak `vApplicationStackOverflowHook` or
`vApplicationMallocFailedHook`, delete them. `app_hooks.c` provides both.

## 4. Build, flash, and observe

1. Build (hammer icon), then **Run > Debug** to flash over ST-LINK.
2. Open a serial terminal on the ST-LINK COM port (Device Manager shows
   "STMicroelectronics STLink Virtual COM Port") at 115200 8N1. Use CubeIDE's
   **Console > Command Shell Console > Serial Port**, or PuTTY. Expected
   output:

   ```text
   BOOT edge-node protocol=1 core_mhz=250
   SENSOR seq=1 t_ms=100 temp_c=18.87 hum_pct=46.51 press_pa=101171
   ...
   STATUS seq=11 t_ms=1000 period_ms=100 avg_period_us=100000 max_jitter_us=3 overruns=0 ...
   ```

3. LD1 should blink once per second. Press B1 and confirm LD2 toggles and
   `period_ms` changes in the next STATUS line.

For binary output, build with `APP_OUTPUT_TEXT=0` and decode on the PC:
`python tools/edge_monitor.py --port COM5`. This needs `pyserial`.

## 5. Experiments to record

Record the `STATUS` values for each case in the project log:

- **Rate sweep:** press B1 through 100, 50, 20, 10, and 200 ms. Note
  `avg_period_us`, `max_jitter_us`, `overruns`, `max_exec_us`, and `dropped`.
  In text mode a sensor line is about 85 bytes, roughly 7.4 ms at 115200
  baud. So at 10 ms the UART is nearly saturated, and `dropped` shows
  CommTask falling behind while SensorTask stays on time.
- **Text vs binary:** repeat the 10 ms case with `APP_OUTPUT_TEXT=0`
  (21-byte frames, about 1.8 ms each).
- **Priority inversion experiment:** swap `APP_PRIO_SENSOR` and
  `APP_PRIO_COMM` in `app_config.h`. Compare jitter at 10-20 ms, when
  transmission keeps the higher-priority CommTask busy.
- **Resource margins:** `stack_min_words` and `heap_free` over several
  minutes. Shrink stacks if the margin is large; grow them if it is under
  about 50 words.
- **CubeIDE FreeRTOS views:** **Window > Show View > FreeRTOS**. The task list
  and the queue registry (`samples`, `link`) show task states and queue
  fill.
