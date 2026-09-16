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

## Planned repository layout

```text
firmware/stm32/       STM32CubeIDE and FreeRTOS sensing-node code
supervisor/qnx/       QNX C/C++ supervisory-node code
verification/spark/   Ada/SPARK validation component and proof configuration
shared/protocol/      Protocol specification and portable shared definitions
tests/                Host-side protocol and integration tests
docs/                 Setup, architecture, decisions, experiments, and report evidence
```

Directories will be introduced as their corresponding components are started,
keeping each change small and explainable.

## Development roadmap

1. Configure toolchains and source control.
2. Build the STM32/FreeRTOS sensing node.
3. Build the Raspberry Pi/QNX supervisory node.
4. Define and stabilize bidirectional communication.
5. Add structured logging, monitoring, validation, and CI.
6. Prove one critical validation rule with SPARK.
7. Measure latency, jitter, reliability, and abnormal-condition behavior.
8. Produce the report, presentation, and demonstration.


