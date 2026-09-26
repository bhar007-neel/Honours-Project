# Week 2: Technical Background Summary

A short summary of the concepts each part of the project relies on, with the
primary references used.

## 1. FreeRTOS architecture and scheduling

- **Tasks** are independent functions with their own stack and priority. A
  task is Running, Ready, Blocked (waiting for time or an event), or
  Suspended.
- **Scheduling** is fixed-priority and preemptive. The highest-priority Ready
  task always runs; equal priorities time-slice on each tick. The tick
  (1 kHz here) drives delays and timeouts.
- **Periodic tasks** should use `xTaskDelayUntil`, which wakes relative to the
  previous release time. `vTaskDelay` drifts because it counts from "now".
- **Inter-task communication:** queues copy data between tasks. Binary
  semaphores signal events, typically from an interrupt. Mutexes protect
  shared resources and apply **priority inheritance** to limit priority
  inversion. Task notifications are the lightest signalling mechanism.
- **Interrupts** may only call `...FromISR` APIs, and only if their NVIC
  priority is numerically at or above
  `configMAX_SYSCALL_INTERRUPT_PRIORITY`. The ISR should do minimal work and
  defer the rest to a task.
- **Rate-monotonic priority assignment** gives shorter-period tasks higher
  priority. It is optimal among fixed-priority schemes for independent
  periodic tasks.
- **STM32H5 specifics:** FreeRTOS comes from ST's X-CUBE-FREERTOS pack, using
  the `ARM_CM33_NTZ` port when TrustZone is disabled. SysTick belongs to the
  kernel, so the HAL timebase must move to a hardware timer such as TIM6.

References: [FreeRTOS kernel documentation](https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/00-Developer-docs),
[X-CUBE-FREERTOS](https://www.st.com/en/embedded-software/x-cube-freertos.html),
DigiKey "Introduction to RTOS" video series.

## 2. QNX architecture and real-time features

- **Microkernel:** only scheduling, IPC, timers, and interrupt dispatch run in
  the kernel (`procnto`). Drivers, file systems, and the network stack are
  separate user-space processes, so a faulty driver cannot corrupt the kernel.
- **Message passing** is the core IPC. `MsgSend` blocks the client until the
  server calls `MsgReceive` and then `MsgReply`. It is synchronous, copies
  data directly between address spaces, and needs no shared buffers.
- **Priority inheritance through messages:** a server runs at the priority of
  the client it is serving. This prevents a low-priority client from delaying
  a high-priority one through a shared server.
- **Pulses** are small non-blocking notifications. Timers, interrupts, and
  other processes can deliver them to a channel, which is the idiomatic way
  to drive periodic threads.
- **Scheduling:** 256 priority levels (unprivileged processes up to 63),
  per-thread FIFO, round-robin, or sporadic policies, and SMP across the Pi's
  four cores.
- **POSIX API:** threads, timers, sockets, and termios are standard, so the
  receiver code is portable. QNX-specific calls (`ChannelCreate`,
  `ClockCycles`, `name_attach`) are used where they add real-time value.
- **Resource managers** are user processes that expose a path such as
  `/dev/ser1`, which clients open with ordinary `open`/`read`/`write`.

References: Elad Lahav, *Introduction to the QNX RTOS with Raspberry Pi*
(free); [QNX SDP 8.0 documentation](https://www.qnx.com/developers/docs/8.0/);
QNX Quick Start Target Image for Raspberry Pi guide.

## 3. Communication options between STM32 and Raspberry Pi

- **UART:** 3 wires, and both sides are 3.3 V. Typical rate is 115200 baud to
  about 1 Mbaud. Simplest, easy to debug, and it is point-to-point with
  framing left to software. **Chosen first.**
- **UDP over Ethernet:** both boards have Ethernet and a switch is available.
  It supports 100 Mbit/s and more. Realistic IoT networking, but it needs an
  STM32 network stack (LwIP or FreeRTOS-Plus-TCP). **Week 5 candidate.**
- **TCP over Ethernet:** built-in reliability, but retransmissions add
  unbounded latency, which suits real-time data poorly.
- **SPI:** fast, but the Pi must act as master and needs a driver. There is
  no framing or error detection.
- **I2C:** slow, meant for on-board peripherals. This is how a real sensor
  would connect to the STM32.
- **CAN:** robust and deterministic, but needs transceivers (and a HAT on the
  Pi).

In every case the application protocol stays the same: a start marker,
version, type, sequence number, timestamp, length, payload, and CRC-16. Only
the byte transport underneath changes.

## 4. Ada/SPARK and formal verification

- **Ada** is a strongly typed language with range-constrained types (for
  example, `type Centi_Celsius is range -32_768 .. 32_767`). The compiler
  inserts run-time checks for every constraint.
- **SPARK** is a formally analysable subset of Ada. GNATprove performs
  **flow analysis** (no uninitialized reads, and declared dependencies match
  the code) and **proof** (verification conditions discharged by SMT solvers
  such as CVC5, Z3, and Alt-Ergo).
- **Contracts:** `Pre` (what callers must guarantee), `Post` (what the
  subprogram guarantees), and `Contract_Cases` (complete, disjoint behaviour
  cases).
- **Assurance levels** (AdaCore): Stone (valid SPARK), Bronze (flow analysis),
  Silver (absence of run-time errors), Gold (key functional properties
  proven), Platinum (full functional correctness). The project targets
  **Gold for one function**: the reading acceptance gate.
- **Integration:** SPARK code can export C-callable functions. Running it on
  QNX requires GNAT Pro for QNX, which is why the QNX/Ada licence question is
  open.

References: [AdaCore Introduction to SPARK](https://learn.adacore.com/courses/intro-to-spark/index.html),
[SPARK User's Guide](https://docs.adacore.com/spark2014-docs/html/ug/),
[Alire](https://alire.ada.dev/).
