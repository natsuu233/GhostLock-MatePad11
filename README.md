# GhostLock-DBY

> Research-only exploit chain for GhostLock, tested on Huawei MatePad 11 2021 (DBY).
> **Not a universal Huawei exploit. Use only on devices you own or are authorized to test.**

## ⚠️ Warning

- This project is for **security research and authorized testing only**.
- Running it may **brick your device**, cause data loss, or leave the system unstable.
- Temporary root is not persistence; do not use it on a daily-driver device.
- Do not download or run “adapted” exploit code from unknown sources. Review code with AI/manual audit before running.
- You are responsible for complying with local laws and regulations.

## Overview

GhostLock (CVE-2026-43499, as referenced in the docs) is a long-standing kernel vulnerability class. This repository contains a research exploit chain tested on:

| Device | System | Kernel | Status |
| --- | --- | --- | --- |
| Huawei MatePad 11 2021 (DBY) | HarmonyOS 4.2.0.213 | 4.19.157 | Tested in the write-up |

Other devices/versions require porting and offset calibration. No compatibility is guaranteed.

## Docs

The full write-up is in [`docs/GhostLock-DBY教程.md`](docs/GhostLock-DBY教程.md).  
I documented the process as comprehensively as possible so that you can adapt it to your device. Feel free to use AI for translation.

## Build / Run
See the docs for environment setup, kernel build, initramfs, QEMU lab, and device-side steps.
This README intentionally does not provide a one-command “universal root” path.

## Credits
Author: natsuu233

Thanks to DeepSeek / GLM and the referenced official write-ups.

See docs/ for the full reference list.

## Contact
Open an issue for porting questions. Do not include personal device identifiers, firmware dumps, or sensitive logs in issues.
