# DevSecOps-Oriented Real-Time Edge IoT Platform

CSI4900 honours project supervised by Prof. M. A. Ibrahim.

## Goal

Build and evaluate a two-node real-time IoT platform:

- an STM32/FreeRTOS sensing node collects sensor readings;
- an Ada/SPARK validation gate checks a critical safety or security rule;
- a Raspberry Pi/QNX supervisory node receives, validates, logs, and monitors data;
- DevSecOps practices provide automated checks, structured logs, runtime monitoring,
  and evidence for the final evaluation.


## Target hardware

- STM32 NUCLEO-H563ZI with its integrated ST-LINK debugger/programmer
- Raspberry Pi 4 Model B with a 32 GB or larger microSD card
- Five-port Gigabit Ethernet switch and three to four Ethernet cables
- USB-to-serial/UART adapter and male-to-female jumper wires
- Breadboard, LEDs, resistors, and other basic test components

The STM32H563ZI is an Arm Cortex-M33 MCU with TrustZone, 2 MB of flash, 640 KB of
RAM, and a maximum 250 MHz clock. The project uses STM32CubeH5 support rather
than the STM32L5 package named in the original planning guide.

## Repository layout

```text
shared/protocol/          Edge frame format: encoder, streaming decoder, CRC-16
shared/common/            Portable sensor simulator and real-time timing statistics
firmware/stm32/app/       FreeRTOS application and NUCLEO-H563ZI board layer
supervisor/qnx/           QNX programs: system info, RT threads, IPC, frame RX/TX
verification/spark/       Ada/SPARK acceptance gate (Alire crate)
tests/host/               PC unit tests for the portable C code
tools/                    PC utilities (Python frame monitor)
docs/                     Requirements, background, weekly guides, and evidence
```

## Quick commands

```powershell
# Host unit tests (MSYS2 GCC)
powershell -ExecutionPolicy Bypass -File tests\host\run_tests.ps1

# QNX programs for the Pi (in cmd, after %USERPROFILE%\qnx800\qnxsdp-env.bat)
cd supervisor\qnx && make

# SPARK proof (after installing Alire)
cd verification\spark\edge_gate; alr with gnatprove; alr gnatprove
```

## Weekly documents

- [Week 1: Requirements and architecture](docs/week-01-requirements.md)
- [Week 2: Environment setup](docs/week-02-setup.md) and
  [technical background](docs/week-02-background.md)
- [Week 3: STM32/FreeRTOS sensing node](docs/week-03-stm32-freertos.md)
- [Week 4: Raspberry Pi 4/QNX platform](docs/week-04-qnx-rpi4.md)

## Development roadmap

1. Configure toolchains and source control.
2. Build the STM32/FreeRTOS sensing node.
3. Build the Raspberry Pi/QNX supervisory node.
4. Define and stabilize bidirectional communication.
5. Add structured logging, monitoring, validation, and CI.
6. Prove one critical validation rule with SPARK.
7. Measure latency, jitter, reliability, and abnormal-condition behavior.
8. Produce the report, presentation, and demonstration.


