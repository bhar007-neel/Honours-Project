# Week 3 — STM32 / FreeRTOS sensing node

| | |
|---|---|
| **Focus** | A FreeRTOS application that samples on time, frames the reading, and reports its own jitter |
| **Board** | NUCLEO-H563ZI, TrustZone off |
| **Theory behind this week** | [Week 2 background, §1 FreeRTOS](week-02-background.md). The applied form is written below |

## What was done

The sensing-node application is written and compiles with STM32CubeIDE's GNU Arm toolchain 14.3. It is four tasks, a board layer, a portable protocol, a simulated sensor, and timing statistics that the PC can test without the board.

- **SensorTask** (priority 4) wakes with `xTaskDelayUntil`, reads the simulator, and queues the sample without blocking. It times its own period, jitter, and execution with the DWT cycle counter.
- **CommTask** (priority 3) blocks on that queue and transmits each sample on USART3, the ST-LINK virtual COM port.
- **ControlTask** (priority 2) waits on a binary semaphore from the B1 interrupt. Each press cycles the sensor period through 100, 50, 20, 10, and 200 ms, and toggles LD2.
- **HeartbeatTask** (priority 1) runs every 1 s, toggles LD1, and sends health: jitter, overruns, dropped samples, stack high-water marks, and free heap.
- The UART is shared under a mutex. Transmit-complete is a task notification from the UART interrupt, so the sender blocks the task and does not spin on the CPU.
- LD3 latches after a dropped sample or an overrun, and stays lit after a fatal error (stack overflow or a failed heap allocation).

The portable C (protocol, simulator, timing stats) has a host unit-test program. Flashing the board, the serial capture, and the rate-sweep measurements are the remaining bench work. Week 2's compile check passed; the board was not attached for a flash test.

## Theory applied this week

### Release, period, jitter, overrun

A periodic task is **released** when it becomes ready to do one job. The **period** is the gap between two releases. **Jitter** is the absolute difference between that measured gap and the nominal period. An **overrun** is a release later than nominal plus tolerance (`APP_OVERRUN_TOLERANCE_PCT`, 10%). **Execution time** is release to the end of that job.

`shared/common/rt_stats` computes those from raw cycle counts. On this board the counter is `DWT->CYCCNT`. Unsigned subtraction absorbs a single wrap. At 250 MHz that covers periods up to about 17 seconds, which is far longer than any period this node uses. The definitions live in `rt_stats.h` so the report and the firmware use the same words.

### Why `xTaskDelayUntil`

SensorTask must hold a configured period (FR1). `vTaskDelay(100 ms)` would start after the sample and the queue send, so the true period would be 100 ms plus that work, and it would drift. `xTaskDelayUntil` schedules the next wake from the previous wake time. If the job finishes early, the extra wait absorbs the slack. If the job runs long, the next release is already late and `rt_stats` records the overrun.

### Rate-monotonic priorities

Shorter period, higher priority. SensorTask is the fastest, so it sits above CommTask, ControlTask, and the 1 s heartbeat. CommTask is still above the heartbeat so a status line cannot crowd out a sample. The button task sits in the middle: a human press is sporadic and must not delay sampling.

The inversion experiment in the bench list swaps `APP_PRIO_SENSOR` and `APP_PRIO_COMM`. At 10–20 ms the UART holds CommTask for a large fraction of the period. If that task outranks the sampler, the sampler waits, and the jitter numbers should grow. That is the Week 2 priority-inversion story, measured.

### The four synchronisation objects

| Object | From | To | Why this object |
|---|---|---|---|
| Queue of 8 samples | SensorTask | CommTask | The sample must be copied, and SensorTask must not block if CommTask is behind. A full queue increments `samples_dropped` and SensorTask continues |
| Mutex `link` | CommTask and HeartbeatTask | the UART | Two tasks share one transmitter. Inheritance keeps a heartbeat holder from delaying CommTask behind an unrelated medium-priority task |
| Binary semaphore | B1 ISR | ControlTask | The ISR only gives the semaphore. Debounce and the period change run in task context |
| Task notification | USART3 transmit-complete ISR | the task inside `board_link_write` | The task sleeps until the shift register finishes |

USART3 and EXTI line 13 call `FromISR` functions, so their NVIC preemption priority must be numerically 5 or higher (6 is the example used). A more urgent interrupt (a smaller number) is not allowed to call the FreeRTOS API.

### What the LEDs mean

| LED | Pin | Meaning |
|---|---|---|
| LD1 green | PB0 | Heartbeat, toggled once per second |
| LD2 yellow | PF4 | Toggles when B1 changes the sample period |
| LD3 red | PG4 | Latched on a dropped sample or an overrun. Solid after `board_fatal` (stack overflow or heap exhaustion) |

## Code written this week

### Firmware (`firmware/stm32/app/`)

CubeIDE still generates the clock, pin, and kernel startup. These files are the application. Generated code only needs a call to `app_init()`.

| File | Role |
|---|---|
| `app.h` | `app_init` and `app_on_button_isr` |
| `app.c` | The four tasks, the queue, the link mutex, the button semaphore, text and binary transmit |
| `app_config.h` | Periods, priorities, stack sizes, queue length, text-versus-binary switch |
| `app_hooks.c` | `vApplicationStackOverflowHook` and `vApplicationMallocFailedHook`, both of which call `board_fatal` |
| `board.h` | LED, UART write, cycle counter, and fatal-stop interface. No HAL types leak out |
| `board_nucleo_h563zi.c` | NUCLEO-H563ZI implementation: LD1–LD3, DWT, interrupt-driven USART3, B1 callback |

`APP_OUTPUT_TEXT` defaults to 1 (readable lines on a terminal). Set it to 0 for binary edge frames aimed at the Pi and at `tools/edge_monitor.py`.

### Shared C, also tested on the PC

| File | Role |
|---|---|
| `shared/protocol/edge_protocol.h` | Frame layout, sensor and status payloads, decoder events |
| `shared/protocol/edge_protocol.c` | CRC-16/CCITT-FALSE, encode, streaming decode, sensor and status payload codecs |
| `shared/common/sensor_sim.h` | Simulator interface |
| `shared/common/sensor_sim.c` | Deterministic BME280-like temperature, humidity, and pressure |
| `shared/common/rt_stats.h` | Period, jitter, overrun, and execution-time definitions |
| `shared/common/rt_stats.c` | Cycle-count implementation of those statistics |
| `tests/host/test_main.c` | Host tests: CRC, encode/decode, resync after corruption, sensor payload, timing stats |
| `tests/host/Makefile` | Host build |
| `tests/host/run_tests.ps1` | Windows runner using MSYS2 GCC |
| `tools/edge_monitor.py` | Independent Python decoder for a live serial port or a saved binary capture |

### Frame layout

Multi-byte fields are little-endian, written byte by byte so padding and CPU endianness cannot change the wire format.

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | SOF0 `0xA5` |
| 1 | 1 | SOF1 `0x5A` |
| 2 | 1 | Version (`1`) |
| 3 | 1 | Type: `0x01` sensor, `0x02` status, `0x10` command (reserved for Week 5) |
| 4 | 2 | Sequence |
| 6 | 4 | Sender uptime, milliseconds |
| 10 | 1 | Payload length, 0–64 |
| 11 | N | Payload |
| 11+N | 2 | CRC-16/CCITT-FALSE over bytes `[2, 11+N)` |

A sensor payload is 8 bytes: `int16` centi-degrees, `uint16` centi-percent, `uint32` pascals. A status payload is 20 bytes of health counters. Overhead is 13 bytes, so a sensor frame is 21 bytes.

The decoder is streaming. `edge_decoder_feed` accepts whatever chunk the UART or a file just delivered. On a bad version, length, or CRC it rescans from the byte after the failed start marker, so a good frame buried in garbage is still recovered.

### Host tests

```powershell
powershell -ExecutionPolicy Bypass -File tests\host\run_tests.ps1
```

## Bring-up on the board

The compile succeeded from the command-line Arm toolchain. These steps are the flash-and-observe pass, still to do with the NUCLEO board connected.

### 1. CubeIDE project

1. **File > New > STM32 Project**, **Board Selector**, `NUCLEO-H563ZI`.
2. Project name `edge-node`. Untick *Use default location* and set it to `<repo>/firmware/stm32/edge-node`.
3. TrustZone: **without TrustZone**. Initialize peripherals to their default mode: **Yes**.

### 2. `.ioc` settings

**FreeRTOS** (X-CUBE-FREERTOS):

1. **Software Packs > Select Components**. Under `STMicroelectronics.X-CUBE-FREERTOS`, select the kernel, the **CMSIS-RTOS2** interface, and **heap_4**. Install the pack if CubeIDE asks.
2. Enable it under **Middleware and Software Packs > X-CUBE-FREERTOS**, then set:
   - `TOTAL_HEAP_SIZE`: at least 16384
   - `CHECK_FOR_STACK_OVERFLOW`: option 2
   - `USE_MALLOC_FAILED_HOOK`: enabled
   - `USE_MUTEXES`: enabled
   - `QUEUE_REGISTRY_SIZE`: 8
   - Include `xTaskDelayUntil`, `uxTaskGetStackHighWaterMark`, and `vTaskDelay`

**Pins and interrupts:**

- **SYS > Timebase Source:** `TIM6`. SysTick stays with the kernel.
- **USART3:** asynchronous, 115200 8N1, PD8 TX and PD9 RX. Enable *USART3 global interrupt*. If the board template gave the virtual COM port to the BSP COM driver, disable that so CubeMX generates `huart3`.
- **PC13 (B1):** `GPIO_EXTI13`, rising edge. Enable *EXTI Line13 interrupt*.
- **PB0, PF4, PG4:** `GPIO_Output` (LD1–LD3; the board template usually presets these).
- **NVIC:** USART3 and EXTI13 preemption priority **5 or higher numerically** (6 is a safe example). Tick *Uses FreeRTOS functions* so CubeMX enforces the `FromISR` rule.

Save and generate.

### 3. Point the project at this source

**Project > Properties > C/C++ General > Paths and Symbols**, all configurations:

1. **Source Location > Link Folder**, *Link to folder in the file system*:
   - `PROJECT_LOC/../app`
   - `PROJECT_LOC/../../../shared/protocol`
   - `PROJECT_LOC/../../../shared/common`
2. **Includes > GNU C:** the same three paths.
3. For binary frames, add the symbol `APP_OUTPUT_TEXT=0`. The default is readable text.

In the generated FreeRTOS init file (`app_freertos.c` or `freertos.c`):

```c
/* USER CODE BEGIN Includes */
#include "app.h"
/* USER CODE END Includes */

/* USER CODE BEGIN RTOS_THREADS */
app_init();
/* USER CODE END RTOS_THREADS */
```

The generated `defaultTask` can be deleted in the `.ioc` editor, or left idle. If CubeMX emitted non-weak `vApplicationStackOverflowHook` or `vApplicationMallocFailedHook`, delete those copies. `app_hooks.c` provides both.

### 4. Flash and watch

1. Build, then **Run > Debug** to flash over ST-LINK.
2. Open the ST-LINK COM port at 115200 8N1. Device Manager lists it as "STMicroelectronics STLink Virtual COM Port". CubeIDE's **Console > Command Shell Console > Serial Port**, or PuTTY, both work.

Expected text:

```text
BOOT edge-node protocol=1 core_mhz=250
SENSOR seq=1 t_ms=100 temp_c=18.87 hum_pct=46.51 press_pa=101171
...
STATUS seq=11 t_ms=1000 period_ms=100 avg_period_us=100000 max_jitter_us=3 overruns=0 ...
```

3. LD1 blinks once per second. Press B1: LD2 toggles, and the next STATUS line shows the new `period_ms`.

Binary mode: build with `APP_OUTPUT_TEXT=0`, then `python tools/edge_monitor.py --port COM5`. That script needs `pyserial`.

## Measurements still to record

Write the STATUS line for each case into the project log.

| Experiment | What to capture |
|---|---|
| Rate sweep | Press B1 through 100, 50, 20, 10, and 200 ms. Record `avg_period_us`, `max_jitter_us`, `overruns`, `max_exec_us`, and `dropped` |
| Text versus binary at 10 ms | A text sensor line is about 85 bytes, roughly 7.4 ms at 115200 baud, so the UART is nearly full and `dropped` should rise while SensorTask stays on time. Repeat with 21-byte binary frames (about 1.8 ms) |
| Priority inversion | Swap `APP_PRIO_SENSOR` and `APP_PRIO_COMM` in `app_config.h`. Compare jitter at 10–20 ms, when transmission keeps CommTask busy |
| Resource margins | `stack_min_words` and `heap_free` over several minutes. Shrink a stack with a large margin. Grow one that sits under about 50 words |
| CubeIDE FreeRTOS views | **Window > Show View > FreeRTOS**. The task list and the queue registry (`samples`, `link`) show state and fill |

The 100 ms jitter target from Week 1 is at most 1 ms, with zero overruns. The STATUS line is the measurement that confirms or revises that target.
