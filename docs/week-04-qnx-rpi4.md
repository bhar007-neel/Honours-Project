# Week 4 — Raspberry Pi 4 / QNX

| | |
|---|---|
| **Focus** | A QNX supervisory platform: system info, real-time threads, message passing, and a frame receiver |
| **Target** | `aarch64le`, QNX SDP 8.0, Raspberry Pi 4 |
| **Theory behind this week** | [Week 2 background, §2 QNX](week-02-background.md). The applied form is written below |

## What was done

Five QNX programs are written, and `qcc` cross-compiles all of them with `-Wall -Wextra -Werror`. They cover the Week 4 brief: a basic application, exercises for processes, threads, timing, and IPC, and a first data-reception path that speaks the same frames as the STM32.

The programs have not been run on the Pi yet. That waits on the Quick Start image from the Week 2 checklist (`com.qnx.qnx800.quickstart.rpi4`). Until the board boots, the receiver can still be exercised by replaying a capture taken on the PC.

## Theory applied this week

### What a microkernel changes for this node

The supervisor's network stack, serial driver, and these five programs are processes. The kernel schedules them and moves messages. A bug in `edge_rx` takes down `edge_rx`. It does not take down the scheduler. That is the property the Week 2 microkernel section is aiming at, and it is why the receiver is a separate program rather than a driver linked into the kernel.

### Periodic threads from timer pulses

`rt_threads` does not poll the clock. Each worker creates a channel, arms a timer, and blocks in `MsgReceive`. The timer posts a pulse when the period expires, the thread wakes, does one job, and blocks again. Three `SCHED_FIFO` threads run together:

| Thread | Period | Priority |
|---|---|---|
| Fast | 2 ms | 30 |
| Medium | 10 ms | 20 |
| Slow | 50 ms | 10 |

Each thread reports period jitter, overruns, and execution time through the same `rt_stats` definitions as the STM32. `-l` adds a CPU hog on every core at priority 8. All four cores go busy. The priority-30 thread should keep nearly the same jitter, because a priority-8 hog is always the one that gets preempted. That is the measurement behind "fixed-priority preemptive scheduling" on a four-core Pi.

`qnx_sysinfo` is the simpler clock check. It prints the OS identity, CPU count, tick length, cycle-counter rate, and scheduling limits, then times a 1 ms `nanosleep`. Min, average, and max of that sleep show the timer granularity: the request is 1 ms, and the overshoot is how coarse the clock actually is.

### Synchronous messages and inherited priority

`msg_ipc` is two processes. The parent registers the name `edge_ipc_demo`, spawns itself again as the client, and the client sends sensor readings with `MsgSend`. The server replies with a range-check verdict. The client measures round-trip time.

While the server handles that message it runs at the client's priority. Raising the client with `-p 40` should raise `handling_priority` to 40. A mutex on a shared buffer is not involved. The inheritance is a property of the message path, which is the QNX-specific point of the exercise.

### The same frames, a different operating system

`edge_rx` and `edge_tx` link `shared/protocol/edge_protocol.c`. The streaming decoder, CRC, and payload layout are the Week 3 code, compiled with `qcc` and no FreeRTOS headers. `edge_tx` also links the Week 3 sensor simulator, so the Pi can generate the stream before the STM32 is wired to it. `edge_tx -e N` corrupts every Nth frame. The receiver should count those as `bad_crc` and keep decoding the good ones, which is NFR3 (resynchronise without a restart) on QNX.

Sequence tracking in `edge_rx` is the start of FR7: lost, duplicated, and out-of-order frames become counters, separate from CRC failures.

## Code written this week

Build file: `supervisor/qnx/Makefile`. It calls `qcc -Vgcc_ntoaarch64le`, includes `shared/protocol` and `shared/common`, and can `scp` the binaries to the Pi.

| File | Program | What it demonstrates |
|---|---|---|
| `supervisor/qnx/src/qnx_sysinfo.c` | `qnx_sysinfo` | OS, CPU count, tick, `ClockCycles` rate, scheduling limits, and 1 ms `nanosleep` accuracy |
| `supervisor/qnx/src/rt_threads.c` | `rt_threads` | Three FIFO threads on timer pulses (2 ms / 10 ms / 50 ms). `-l` loads every core at priority 8. Uses `rt_stats` |
| `supervisor/qnx/src/msg_ipc.c` | `msg_ipc` | A spawned client sends readings with `MsgSend`. Prints round-trip time and the priority the server inherited |
| `supervisor/qnx/src/edge_rx.c` | `edge_rx` | Decodes edge frames from a serial port (`-s`), UDP (`-u`), or a file or stdin (`-f`). Prints key=value lines, sequence faults, and an error summary |
| `supervisor/qnx/src/edge_tx.c` | `edge_tx` | Generates the same frames as the STM32, including the simulated sensor. `-e` corrupts every Nth frame |

No new shared protocol code this week. `edge_rx` links `edge_protocol.c`. `edge_tx` links `edge_protocol.c` and `sensor_sim.c`. `rt_threads` links `rt_stats.c`.

## Build and deploy

In `cmd`, after the SDP environment script from Week 2:

```bat
%USERPROFILE%\qnx800\qnxsdp-env.bat
cd "<repo>\supervisor\qnx"
make
make deploy PI=qnxuser@<pi-ip>
```

Binaries land in `supervisor/qnx/build/aarch64le/`. `make deploy` copies them to the Pi user's home directory with Windows `scp`. The default host in the Makefile is `qnxuser@qnxpi.local`.

## Run on the Pi, and what to record

Boot steps, once the Quick Start image is flashed:

1. In QNX Software Center, install `com.qnx.qnx800.quickstart.rpi4`. Write the image with Raspberry Pi Imager (**Use custom**).
2. Put the Pi and the PC on the Ethernet switch. Find the address from the router, or from a USB-TTL console on GPIO14/15 at 115200 baud.
3. `ssh qnxuser@<pi-ip>` with the Quick Start credentials.

```sh
./qnx_sysinfo
./rt_threads -d 30
./rt_threads -d 30 -l
./msg_ipc -n 5000
./msg_ipc -n 5000 -p 40
```

| Program | Record for the report |
|---|---|
| `qnx_sysinfo` | `clock.tick_ns`, `clock.cycles_per_sec`, and min / avg / max of the requested 1 ms sleep. The overshoot is the timer granularity |
| `rt_threads` without `-l` | `max_us`, `jitter_us`, and `overruns` for each thread. This is the idle baseline |
| `rt_threads` with `-l` | The same three numbers under load. Priority 30 should stay close to the idle jitter |
| `msg_ipc` | `rtt_min_us`, `rtt_avg_us`, `rtt_max_us`, and `handling_priority`. The handling priority should equal the client's `-p` |

These jitter figures are the Week 1 "QNX timing" row. They set the supervisor baseline before any STM32 traffic is added.

## Receive frames before the STM32 is attached

Two hundred frames at a 10 ms period, every 25th corrupted:

```sh
./edge_tx -o - -n 200 -p 10 -e 25 | ./edge_rx -f -
```

Expected summary: `frames_ok=192` and `bad_crc=8`, with the good frames decoded.

UDP, in two SSH sessions:

```sh
./edge_rx -u 5000
./edge_tx -u 127.0.0.1:5000 -n 500 -e 50
```

`edge_rx` prints the summary when you interrupt it. The same UDP test works across the switch: run `edge_rx` on the Pi and send from another QNX or Linux machine.

## Receive frames from the STM32

The ST-LINK virtual COM port connects the STM32 to the PC, not to the Pi. A direct link is a spare STM32 USART wired to a Pi UART: TX to RX, RX to TX, GND to GND. Both sides are 3.3 V. Then:

```sh
ls /dev/ser*
./edge_rx -s /dev/serN -b 115200
```

Until that cable exists, capture binary output on the PC and replay it:

```powershell
python tools/edge_monitor.py --port COM5 --save run.bin
```

Copy `run.bin` to the Pi and run `./edge_rx -f run.bin`. The decoder is the same one the live UART will use. The STM32 must be built with `APP_OUTPUT_TEXT=0` for that capture. Text mode is for a human terminal, not for `edge_rx`.
