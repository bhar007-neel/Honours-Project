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

## 1. Git and GitHub

The local repository is initialized on `main`. Git author identity is configured.

After creating an empty **private** GitHub repository, connect and publish it:

```powershell
git remote add origin <YOUR_GITHUB_REPOSITORY_URL>
git push -u origin main
```

Do not initialize the GitHub repository with a README, license, or `.gitignore`;
those files are maintained locally.

Recommended daily workflow:

```powershell
git status
git switch -c feature/short-description
# Make and verify one focused change.
git add <specific-files>
git commit -m "type: concise description"
git push -u origin feature/short-description
```

Use prefixes such as `docs`, `build`, `feat`, `fix`, and `test`. Never commit
licenses, credentials, generated binaries, IDE workspaces, or private keys.

## 2. STM32 and FreeRTOS

Install the current Windows release of
[STM32CubeIDE](https://www.st.com/en/development-tools/stm32cubeide.html).
Version 2.2.0 was current when this guide was verified. A full installer is
preferred because an IDE-only update may not update ST-LINK drivers and servers.

First validation:

1. Connect the NUCLEO-L552ZE-Q through its ST-LINK USB connector.
2. Create a project for `NUCLEO-L552ZE-Q`.
3. Build and flash the generated or board example.
4. Debug the program and confirm the expected LED behavior.
5. Record the IDE version, firmware version, and test result in the project log.

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

- [x] Initialize the local Git repository on `main`.
- [x] Configure Git author identity.
- [x] Add project scope, architecture, and working principles to `README.md`.
- [ ] Create the private GitHub repository and add it as `origin`.
- [ ] Make and push the initial commit.
- [ ] Confirm which hardware is supplied by the university.
- [ ] Install STM32CubeIDE and complete a build/flash/debug smoke test.
- [ ] Obtain the QNX license and install QNX SDP 8.0.
- [ ] Install the Raspberry Pi 4 QNX Quick Start image package.
- [ ] Install Alire and run a GNATprove sample.
- [ ] Record exact installed versions and smoke-test evidence.

## Official references

- [STM32CubeIDE](https://www.st.com/en/development-tools/stm32cubeide.html)
- [QNX Everywhere non-commercial access](https://www.qnx.com/products/everywhere/)
- [QNX Raspberry Pi Quick Start Target Image](https://www.qnx.com/developers/docs/qnxeverywhere/com.qnx.doc.target_images/topic/qsti/intro.html)
- [QNX SDP 8.0 Raspberry Pi 4 release notes](https://www.qnx.com/developers/docs/BSP8.0/com.qnx.doc.bsp.releasenotes/topic/rel_sdp80.bsp.broadcom.rpi4.bcm2711.html)
- [Alire getting started](https://alire.ada.dev/docs/getting-started)
- [GNATprove package in Alire](https://alire.ada.dev/crates/gnatprove)

