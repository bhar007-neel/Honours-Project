# Week 4: Raspberry Pi 4 / QNX Platform

Goal: an operational QNX development platform on the Pi. That means a basic
application, tests of processes, threads, timing, and system services, and an
initial data-reception application.

## Programs (`supervisor/qnx/src/`)

- **`qnx_sysinfo`** is the basic application. It prints OS, CPU count, clock
  tick, cycle-counter rate, and scheduling limits, and measures how
  accurately `nanosleep` honours a 1 ms request.
- **`rt_threads`** tests threads and timing. It runs three `SCHED_FIFO`
  threads (2 ms, 10 ms, and 50 ms periods; priorities 30, 20, and 10), each
  driven by a timer pulse on its own channel. It reports period jitter,
  overruns, and execution time. `-l` adds a CPU hog on every core at
  priority 8.
- **`msg_ipc`** tests processes and IPC. It spawns a client process that
  sends 1000 sensor readings through `MsgSend`. It reports round-trip time
  and shows the server inheriting the client's priority.
- **`edge_rx`** is the data-reception application. It decodes edge frames
  from a serial port (`-s`), UDP (`-u`), or a file or stdin (`-f`), and
  prints key=value lines and an error summary.
- **`edge_tx`** simulates the sensing node. It produces the same frames as
  the STM32, optionally corrupting every Nth one (`-e`).

## 1. Prepare the Pi

1. In QNX Software Center, install `com.qnx.qnx800.quickstart.rpi4`. Write
   its image to the microSD card with Raspberry Pi Imager (**Use custom**).
2. Connect the Pi to the Ethernet switch, together with your PC. Boot it and
   find its IP address from your router, or from the serial console (USB-TTL
   adapter on GPIO14/15, 115200 baud).
3. `ssh qnxuser@<pi-ip>`, using the credentials from the Quick Start guide.

## 2. Build and deploy from Windows

In `cmd`:

```bat
%USERPROFILE%\qnx800\qnxsdp-env.bat
cd "<repo>\supervisor\qnx"
make
make deploy PI=qnxuser@<pi-ip>
```

The binaries go to `supervisor/qnx/build/aarch64le/`, and `make deploy` copies
them to the Pi's home directory with Windows' built-in `scp`.

## 3. Run and record

On the Pi:

```sh
./qnx_sysinfo
./rt_threads -d 30
./rt_threads -d 30 -l
./msg_ipc -n 5000
./msg_ipc -n 5000 -p 40
```

Record for the report:

- **`qnx_sysinfo`:** `clock.tick_ns`, `clock.cycles_per_sec`, and the
  min/avg/max of the requested 1 ms sleep. The overshoot shows the timer
  granularity.
- **`rt_threads`,** without and with `-l`: `max_us`, `jitter_us`, and
  `overruns` for each thread. With `-l`, all four cores are busy, yet
  priority-30 threads should keep nearly the same jitter. That is
  priority-based preemption.
- **`msg_ipc`:** `rtt_min/avg/max_us`, and `handling_priority` matching the
  client's `-p` value. That is priority inheritance through messages.

## 4. Data reception without the STM32

Pipe the simulator into the receiver: 200 frames at 10 ms, every 25th
corrupted.

```sh
./edge_tx -o - -n 200 -p 10 -e 25 | ./edge_rx -f -
```

Expected summary: `frames_ok=192 bad_crc=8`, with the good frames decoded.
Over UDP, in two SSH sessions:

```sh
./edge_rx -u 5000                              # session 1, Ctrl+C for the summary
./edge_tx -u 127.0.0.1:5000 -n 500 -e 50       # session 2
```

The UDP test also works across the switch: run `edge_rx` on the Pi and send
from another QNX or Linux machine.

## 5. Data reception from the STM32 (preview of Week 5)

The ST-LINK virtual COM port connects the STM32 to the PC, not to the Pi.
For a direct link, wire a spare STM32 USART to a Pi UART: TX to RX, RX to TX,
and GND to GND. Both sides are 3.3 V. Then list the Pi's serial devices with
`ls /dev/ser*` and run:

```sh
./edge_rx -s /dev/serN -b 115200
```

Until then, capture the STM32's binary output on the PC:
`python tools/edge_monitor.py --port COM5 --save run.bin`. Copy the file to
the Pi and replay it with `./edge_rx -f run.bin`. This exercises the same
decoder with real firmware data.
