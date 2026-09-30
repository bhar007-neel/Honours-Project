# Week 2 — Development environment

| | |
|---|---|
| **Focus** | A toolchain that can build each target, plus Git as the record of the work |
| **Recorded** | 2026-09-26 |
| **Theory** | Written separately in [Week 2: technical background](week-02-background.md) |

Installation counts as done only when a small program builds, and runs or flashes, with that toolchain. Tools bundled inside an IDE may be absent from the normal terminal `PATH`. The checks below are the source of truth.

## What was done

Git, the host C compiler, STM32CubeIDE, and QNX SDP 8.0 are installed. The QNX programs cross-compile. The SPARK acceptance gate is written and waiting on Alire.

| Area | Result |
|---|---|
| Version control | Git is installed and configured. The repository uses `main`. The private GitHub repository is `origin`, and the initial commit is pushed |
| Project documents | Scope, architecture, and working principles are in `README.md`. This week's theory summary is `docs/week-02-background.md` |
| Host C compiler | MSYS2 GCC 15.2 builds and runs `tests/host` |
| STM32 | STM32CubeIDE 2.2.0 is installed in `C:\ST\STM32CubeIDE_2.2.0`. Its bundled GNU Arm toolchain 14.3 compiles `firmware/stm32/app` cleanly |
| QNX | SDP 8.0 is installed in `%USERPROFILE%\qnx800`. `qcc` for `aarch64le` builds all five programs in `supervisor/qnx` with `-Werror` |
| Editor | Cursor or VS Code opens this repository |
| Ada/SPARK | The Alire installer is downloaded. `alr` is not on `PATH` yet, so the proof has not been run |

## Still open

- Flash and debug a board smoke test on the NUCLEO-H563ZI. The compile is clean; the board was not connected for this check.
- Install `com.qnx.qnx800.quickstart.rpi4`, flash the microSD card, boot the Pi, and run a cross-compiled program on it.
- Run the Alire installer, then `alr build` and `alr gnatprove` in `verification/spark/edge_gate`.
- Copy exact versions and smoke-test logs into `docs/evidence/` once those runs exist.

## Code written this week

The week's program is the SPARK gate: the function Week 1 reserved for a proof (FR10). It mirrors the sensor fields in `shared/protocol/edge_protocol.h`. GNATprove has not been run, because Alire is not installed yet.

| File | Role |
|---|---|
| `verification/spark/edge_gate/alire.toml` | Alire crate manifest. Crate name `edge_gate`, executable `edge_gate_demo` |
| `verification/spark/edge_gate/edge_gate.gpr` | GNAT project file |
| `verification/spark/edge_gate/src/sensor_gate.ads` | SPARK specification: range types, `Check` with `Post` and `Contract_Cases`, and `Temperature_Step_Ok` |
| `verification/spark/edge_gate/src/sensor_gate.adb` | Body of `Check` and the wide subtraction that keeps the step test overflow-free |
| `verification/spark/edge_gate/src/edge_gate_demo.adb` | Small driver: in-range, out-of-range, boundary, and extreme step cases |

Limits in `sensor_gate.ads` are the BME280 operating window: temperature −40.00 °C to 85.00 °C, humidity up to 100.00 %RH, pressure 300 hPa to 1100 hPa. Values are stored in the same integer units as the wire format (centi-degrees, centi-percent, pascals).

### Proof commands, once Alire is installed

Install [Alire for Windows](https://alire.ada.dev/docs/getting-started) and use the PowerShell shortcut it creates, so `alr` is on `PATH`. Allow Alire to install MSYS2 unless a working MSYS2 install is already present.

```powershell
cd verification\spark\edge_gate
alr with gnatprove
alr build
.\bin\edge_gate_demo.exe
alr gnatprove
```

Success means `gnatprove` reports every check proved, including the `Contract_Cases` of `Check` and the overflow-free subtraction in `Temperature_Step_Ok`. Save that output under `docs/evidence/` for the verification chapter. GNATprove stays a project dependency so the solver version is reproducible.

## STM32 toolchain notes

[STM32CubeIDE](https://www.st.com/en/development-tools/stm32cubeide.html) 2.2.0 was current when this was checked. Prefer the full installer so ST-LINK drivers and servers update with the IDE.

When the board is on the desk:

1. Install the `STM32CubeH5` MCU package when CubeIDE asks for it.
2. Connect the NUCLEO-H563ZI through the ST-LINK USB connector.
3. Create a project for `NUCLEO-H563ZI`, or open the application described in the Week 3 note.
4. Build, flash, and debug. Confirm the expected LED behaviour.
5. Record the IDE version, STM32CubeH5 version, ST-LINK firmware version, and the result.

FreeRTOS configuration belongs to Week 3, after this flash path works. The application source is already in `firmware/stm32/app/` and compiles with the IDE's Arm toolchain.

## QNX toolchain notes

QNX needs an account and a licence:

1. Create or sign in to a [myQNX account](https://www.qnx.com/getqnx).
2. Accept the free individual non-commercial QNX SDP 8.0 licence and deploy it.
3. Install QNX Software Center, then QNX SDP 8.0.
4. Install package `com.qnx.qnx800.quickstart.rpi4`.
5. Flash that image to the microSD card with Raspberry Pi Imager.
6. Boot the board, connect, and run a cross-compiled Hello World. The five programs in `supervisor/qnx` are that Hello World, plus the real-time and protocol demos. Week 4 describes how to build and run them.

Current Quick Start guidance asks for a Raspberry Pi 4 or 5 with **8 GB RAM or more**, and a high-quality microSD card of **at least 32 GB**. The Pi 4 1 GB model is unsupported. These figures replace the older 4 GB Pi and 16 GB card notes in the original project guide.

Cross-compile check, in `cmd` after the SDP environment script:

```bat
%USERPROFILE%\qnx800\qnxsdp-env.bat
cd "<repo>\supervisor\qnx"
make
```

## Week 2 checklist

- [x] Initialize the local Git repository on `main`
- [x] Configure the Git author identity
- [x] Add project scope, architecture, and working principles to `README.md`
- [x] Create the private GitHub repository and add it as `origin`
- [x] Make and push the initial commit
- [x] Install STM32CubeIDE 2.2.0
- [ ] Complete the STM32 build / flash / debug smoke test on the board
- [x] Obtain the QNX licence and install QNX SDP 8.0
- [x] Cross-compile the QNX programs with `qcc` (`supervisor/qnx`)
- [ ] Install the Raspberry Pi 4 QNX Quick Start image and boot the Pi
- [x] Write the SPARK gate (`verification/spark/edge_gate`)
- [ ] Run the Alire installer and prove the gate
- [x] Write the technical background ([week-02-background.md](week-02-background.md))
- [ ] Record exact installed versions and smoke-test evidence
