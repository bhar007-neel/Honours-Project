# Week 2: Development Environment and Version Control

Last verified: 2026-09-15

## Objective

Install and configure the development environments required by each target, then
establish a reproducible Git workflow. Installation is complete only when a small
program can be built and run or flashed with the corresponding toolchain.

## Current status

- **Version control:** Git is installed and configured; the repository uses `main`.
- **STM32:** STM32CubeIDE 2.2.0 was not detected. Verification will build and
  flash the board's LED example.
- **QNX:** QNX Software Center and SDP 8.0 were not detected. Verification will
  cross-compile and run Hello World.
- **Ada/SPARK:** Alire and GNATprove were not detected. Verification will prove
  the sample project's contracts.
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

## 5. Week 2 completion checklist

-  Initialize the local Git repository on `main`.
-  Configure Git author identity.
-  Add project scope, architecture, and working principles to `README.md`.
-  Create the private GitHub repository and add it as `origin`.
- Make and push the initial commit.
- Install STM32CubeIDE and complete a build/flash/debug smoke test.
-  Obtain the QNX license and install QNX SDP 8.0.
- Install the Raspberry Pi 4 QNX Quick Start image package.
-  Install Alire and run a GNATprove sample.
-  Record exact installed versions and smoke-test evidence.

