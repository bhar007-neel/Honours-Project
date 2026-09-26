# Week 2: Development Environment and Version Control

Last verified: 2026-09-26

## Objective

Install and configure the development environments required by each target, then
establish a reproducible Git workflow. Installation is complete only when a small
program can be built and run or flashed with the corresponding toolchain.

## Current status

- **Version control:** Git is installed and configured; the repository uses `main`.
- **STM32:** STM32CubeIDE 2.2.0 is installed in `C:\ST\STM32CubeIDE_2.2.0`.
  Its bundled GNU Arm toolchain 14.3 compiles `firmware/stm32/app` cleanly.
  The board build/flash/debug smoke test is pending until the board is
  connected.
- **QNX:** SDP 8.0 is installed in `%USERPROFILE%\qnx800`. `qcc` for
  `aarch64le` builds all five programs in `supervisor/qnx` with `-Werror`.
  Running them requires the Pi 4 Quick Start image.
- **Ada/SPARK:** the Alire installer has been downloaded but not run, so
  `alr` is not on `PATH`. The proof target is `verification/spark/edge_gate`.
- **Host C compiler:** MSYS2 GCC 15.2 builds and runs `tests/host`.
- **Editor:** Cursor or VS Code is installed and can open this repository.

Tools bundled inside an IDE may not appear on the normal terminal `PATH`. The
functional checks above are the source of truth.

## 2. STM32 and FreeRTOS

Install the current Windows release of
[STM32CubeIDE](https://www.st.com/en/development-tools/stm32cubeide.html).
Version 2.2.0 was current when this guide was verified. A full installer is
preferred because an IDE-only update may not update ST-LINK drivers and servers.

First validation:

1. Install the `STM32CubeH5` MCU package when STM32CubeIDE requests it.
2. Connect the NUCLEO-H563ZI through its integrated ST-LINK USB connector.
3. Create a project for `NUCLEO-H563ZI`.
4. Build and flash the generated or board LED-toggle example.
5. Debug the program and confirm the expected LED behavior.
6. Record the IDE version, STM32CubeH5 version, ST-LINK firmware version, and
   test result in the project log.

FreeRTOS configuration begins only after the basic build/flash/debug path works.

## 3. QNX SDP 8.0

QNX installation requires account and license interaction:

1. Create or sign in to a [myQNX account](https://www.qnx.com/getqnx).
2. Accept and deploy the free individual non-commercial QNX SDP 8.0 license.
3. Install QNX Software Center.
4. Install QNX SDP 8.0.
5. Install package `com.qnx.qnx800.quickstart.rpi4`.
6. Flash its image to the microSD card using Raspberry Pi Imager.
7. Boot the board, connect to it, and run a cross-compiled Hello World program.

Current QNX Quick Start guidance recommends:

- Raspberry Pi 4 or 5 with **8 GB RAM or more**;
- a high-quality microSD card of **at least 32 GB**.

These current values supersede the older 4 GB Pi and 16 GB card suggestions in
the original project guide. The Pi 4 1 GB model is explicitly unsupported.

## 4. Ada and SPARK

Install [Alire for Windows](https://alire.ada.dev/docs/getting-started). Use its
installer-created PowerShell shortcut so `alr` is present on `PATH`. Allow Alire
to install MSYS2 when prompted unless a working MSYS2 installation already
exists.

Inside an Alire SPARK project:

```powershell
alr with gnatprove
alr gnatprove
```

Alire supplies compatible GNAT toolchains for Windows. GNATprove should be a
project dependency so the required version is reproducible.

The project's smoke test is `verification/spark/edge_gate`:

```powershell
cd verification\spark\edge_gate
alr with gnatprove      # first time only; records the dependency in alire.toml
alr build
.\bin\edge_gate_demo.exe
alr gnatprove
```

Success means `gnatprove` reports every check as proved, including the
`Contract_Cases` of `Check` and the overflow-free subtraction in
`Temperature_Step_Ok`. Save the output in `docs/evidence/` for the
verification chapter.

## 5. Week 2 completion checklist

- [x] Initialize the local Git repository on `main`.
- [x] Configure Git author identity.
- [x] Add project scope, architecture, and working principles to `README.md`.
- [x] Create the private GitHub repository and add it as `origin`.
- [x] Make and push the initial commit.
- [x] Install STM32CubeIDE 2.2.0.
- [ ] Complete the STM32 build/flash/debug smoke test on the board.
- [x] Obtain the QNX license and install QNX SDP 8.0.
- [x] Cross-compile QNX programs with `qcc` (`supervisor/qnx`).
- [ ] Install the Raspberry Pi 4 QNX Quick Start image package and boot the Pi.
- [ ] Run the Alire installer and prove `verification/spark/edge_gate`.
- [x] Write the technical background summary (`week-02-background.md`).
- [ ] Record exact installed versions and smoke-test evidence.

