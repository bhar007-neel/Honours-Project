# Week 2 — Technical background

| | |
|---|---|
| **Focus** | The theory each later week builds on |
| **Companion** | [Week 2: environment setup](week-02-setup.md) |
| **Code that embodies this theory** | The SPARK gate in `verification/spark/edge_gate` (this week). FreeRTOS and QNX programs follow in Weeks 3 and 4 |

This note is the study summary for the four bodies of theory the project rests on: FreeRTOS scheduling, the QNX microkernel, the link between the boards, and SPARK proof. Each section ends with where the repository uses the idea.

## 1. FreeRTOS

FreeRTOS is the kernel on the sensing node. Week 3's four tasks are a direct use of this section.

### Tasks and states

A **task** is a C function with its own stack and priority. At any moment it is in one of four states:

| State | Meaning |
|---|---|
| Running | The scheduler has given this task the CPU |
| Ready | It could run, and a higher-priority task is running instead |
| Blocked | It is waiting for time or for an event (queue, semaphore, notification) |
| Suspended | It was explicitly suspended and will not run until resumed |

Blocking is what makes a periodic task cheap. `SensorTask` does not spin until the next sample. It blocks, the CPU runs something else, and the kernel wakes it.

### Fixed-priority preemptive scheduling

The highest-priority Ready task always runs. A higher-priority task that becomes Ready preempts a lower one immediately. Tasks that share a priority time-slice on each tick. The tick here is 1 kHz, so delays and timeouts are in steps of 1 ms.

**Rate-monotonic assignment** gives the shorter period the higher priority. Among fixed-priority schemes it is optimal for independent periodic tasks. Week 3 follows that rule: SensorTask (100 ms, later as fast as 10 ms) is priority 4, CommTask is 3, ControlTask is 2, HeartbeatTask (1 s) is 1.

### Waking on time

`vTaskDelay` starts the wait from "now", so the period stretches by whatever the task just spent working. `xTaskDelayUntil` wakes relative to the previous release, so a short job does not make the next job late. Periodic tasks in this project use `xTaskDelayUntil`.

### How tasks talk

| Mechanism | What it is for | Where Week 3 uses it |
|---|---|---|
| Queue | Copies a value from one task to another | Sensor samples travel to CommTask |
| Binary semaphore | An event signal, often from an interrupt | The B1 button ISR wakes ControlTask |
| Mutex | Protects a shared resource. **Priority inheritance** raises the holder's priority to the waiter's, which limits priority inversion | CommTask and HeartbeatTask share the UART |
| Task notification | The lightest signal, directed at one task | The UART transmit-complete interrupt wakes the sender |

**Priority inversion** is the failure mode the mutex is there to limit: a low-priority task holds a resource, a high-priority task waits for it, and a medium-priority task runs in between and stretches the wait. Week 3 includes an experiment that swaps SensorTask and CommTask priorities so this effect shows up in the jitter numbers.

### Interrupts

An interrupt may call only the `...FromISR` APIs, and only when its NVIC priority number is at or above `configMAX_SYSCALL_INTERRUPT_PRIORITY` (a higher number means a less urgent interrupt). The ISR does the minimum — give a semaphore, notify a task — and returns. The task does the rest. On the NUCLEO board, USART3 and EXTI13 are set to preemption priority 5 or numerically higher for this reason.

### STM32H5 specifics

ST ships the kernel through the X-CUBE-FREERTOS pack. With TrustZone off, the port is `ARM_CM33_NTZ`. SysTick belongs to the kernel, so the HAL timebase moves to a hardware timer (TIM6). CubeIDE generates the startup; the application enters through `app_init()`.

Timing measurements use the DWT cycle counter, not the tick. A tick is 1 ms. Cycle counts resolve jitter well below that. `shared/common/rt_stats` turns those counts into period, jitter, overrun, and execution time. An **overrun** here means a release later than the nominal period plus a tolerance (10% in the firmware config).

References: [FreeRTOS kernel documentation](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/00-Developer-docs), [X-CUBE-FREERTOS](https://www.st.com/en/embedded-software/x-cube-freertos.html), DigiKey "Introduction to RTOS".

## 2. QNX

QNX SDP 8.0 is the operating system on the supervisory node. Week 4's five programs are the exercises for this section.

### Microkernel

Only scheduling, inter-process communication, timers, and interrupt dispatch run in the kernel (`procnto`). Drivers, file systems, and the network stack are ordinary user-space processes. A faulty driver can crash itself. It does not get to corrupt the kernel.

### Message passing

`MsgSend` is the core IPC. The client blocks until the server has `MsgReceive`d the message and `MsgReply`d. The copy goes between the two address spaces, so there is no shared buffer to protect. Because the exchange is synchronous, the time from send to reply is a real round-trip measurement. `msg_ipc` records that time.

**Priority inheritance through messages:** while a server handles a client, the server runs at that client's priority. A low-priority client cannot hold a shared server and delay a high-priority client behind it. `msg_ipc` prints the priority the server actually ran at, which should match the client's `-p` argument.

### Pulses, clocks, and threads

A **pulse** is a small non-blocking notice. Timers and interrupts deliver pulses to a channel. A thread blocks in `MsgReceive` on that channel and wakes when the pulse arrives. That is the usual way to drive a periodic QNX thread, and it is what `rt_threads` does.

Scheduling offers 256 priority levels. Unprivileged processes use priorities up to 63. Each thread can be FIFO, round-robin, or sporadic. The Pi has four cores, so several high-priority threads can run truly in parallel. `rt_threads -l` fills every core with a low-priority hog. The priority-30 thread should keep nearly the same jitter, which is the demonstration that priority preemption still wins when the machine is busy.

`ClockCycles` reads a free-running cycle counter. `qnx_sysinfo` prints how many cycles per second that counter advances, and how far a 1 ms `nanosleep` overshoots. The overshoot is the timer granularity.

### POSIX and resource managers

Threads, timers, sockets, and termios are POSIX, so the receiver can stay close to ordinary C. QNX-specific calls (`ChannelCreate`, `ClockCycles`, `name_attach`) are used where they add a real-time property the POSIX call does not.

A **resource manager** is a user process that publishes a path such as `/dev/ser1`. Clients open it with `open`, `read`, and `write`. The serial port `edge_rx` listens on is one of those paths.

References: Elad Lahav, *Introduction to the QNX RTOS with Raspberry Pi* (free); [QNX SDP 8.0 documentation](https://www.qnx.com/developers/docs/8.0/); QNX Quick Start Target Image for Raspberry Pi guide.

## 3. The link between the boards

The application protocol stays the same on every transport: a start marker, version, type, sequence number, timestamp, length, payload, and CRC-16. Only the byte pipe underneath changes.

| Transport | What it gives this project | Decision |
|---|---|---|
| UART | Three wires, 3.3 V on both sides, typically 115200 baud up to about 1 Mbaud. Point-to-point. Framing is the software protocol's job | First link |
| UDP over Ethernet | Both boards have Ethernet, and a switch is on the bench. 100 Mbit/s and above. Needs an STM32 TCP/IP stack (LwIP or FreeRTOS-Plus-TCP) | Week 5 candidate |
| TCP over Ethernet | The stack retransmits lost data. Those retries make latency unbounded, which fights a real-time sample stream | Not used for samples |
| SPI | Fast. The Pi would be master and would need a driver. No framing and no error detection of its own | Not the inter-node link |
| I2C | Meant for on-board peripherals, and slow for a node-to-node stream | How a real sensor would attach to the STM32 |
| CAN | Robust and deterministic. Needs transceivers, and a HAT on the Pi | Out of scope |

CRC-16/CCITT-FALSE covers the frame from the version byte through the payload. It detects every burst error up to 16 bits long. The two start bytes (`0xA5 0x5A`) are how a streaming decoder finds the next frame after garbage. Sequence numbers are a different check: they catch loss, duplication, and reordering, which a CRC cannot see because those frames are individually well formed.

A sensor frame on the wire is 21 bytes. At 115200 baud that is about 1.8 ms. A text status line is much longer, which is why Week 3 compares text mode with binary mode when the sample period drops to 10 ms.

## 4. Ada and SPARK

This is the theory behind `verification/spark/edge_gate`, the code written in Week 2.

### Ada's checks

Ada is strongly typed. A range-constrained type such as `type Centi_Celsius is range -32_768 .. 32_767` makes the compiler insert a run-time check on every assignment that might leave that range. Integer units (centi-degrees, centi-percent, pascals) match the C payload, so every value the decoder can produce is a value SPARK can name.

### What SPARK adds

SPARK is a subset of Ada that GNATprove can analyse.

- **Flow analysis** checks that nothing is read before it is written, and that the data dependencies you declared match the code.
- **Proof** turns contracts into verification conditions and sends them to SMT solvers (CVC5, Z3, Alt-Ergo). A proved check is an argument about all inputs, not a sample of them.

Contracts used on `Check`:

| Contract | Meaning on the gate |
|---|---|
| `Post` | The result is `Accepted` exactly when every field is inside its limits |
| `Contract_Cases` | The cases are complete and disjoint. The first failing field names the rejection reason: temperature, then humidity, then pressure |

`Temperature_Step_Ok` has its own postcondition: the absolute step is within the limit. The subtraction is done in a wider type because `Current - Previous` can fall outside `Centi_Celsius`. Removing that conversion is exactly the overflow GNATprove is there to catch.

### Assurance levels

AdaCore's scale, from least to most:

| Level | What has been shown |
|---|---|
| Stone | The code is valid SPARK |
| Bronze | Flow analysis passes |
| Silver | No run-time errors (overflow, range, and similar) |
| Gold | The important functional properties are proved |
| Platinum | Full functional correctness of the unit |

The project targets **Gold for one function**: `Sensor_Gate.Check`. Silver comes along with it, because a proof of the contracts includes absence of run-time errors in that code.

### Where the proved code runs

SPARK can export a C-callable function. Executing that function on the Pi needs GNAT Pro for QNX, which is the open licence question from Week 1. Until that is settled, the proof runs on the PC, and the QNX receiver can apply the same rule in C. The proof still covers the rule. It does not, by itself, prove that the C copy was transcribed faithfully. That transcription is a review item if the C mirror is the one that runs on the Pi.

References: [AdaCore Introduction to SPARK](https://learn.adacore.com/courses/intro-to-spark/index.html), [SPARK User's Guide](https://docs.adacore.com/spark2014-docs/html/ug/), [Alire](https://alire.ada.dev/).
