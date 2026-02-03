# 🔒 Secure OTA Firmware Update System

### STM32F407 + ESP32 | CDAC ACTS PG-Diploma in DESD Capstone Project

A professional-grade **Over-The-Air (OTA) firmware update system** for STM32F407 microcontrollers using ESP32 as a WiFi bridge. Implements dual-bank flash architecture, SHA-256 cryptographic verification, and delta patching for bandwidth-optimized secure updates.

---

## 📋 Table of Contents

- [Architecture Overview](#architecture-overview)
- [Features](#features)
- [DESD Curriculum Mapping](#desd-curriculum-mapping)
- [System Components](#system-components)
- [Memory Layout](#memory-layout)
- [Setup & Build Instructions](#setup--build-instructions)
- [OTA Update Flow](#ota-update-flow)
- [Security Design](#security-design)
- [Team Members](#team-members)

---

## 🏗️ Architecture Overview

```
┌─────────────┐    HTTPS/TLS    ┌─────────────┐    UART     ┌─────────────────┐
│  Flask OTA   │◄──────────────►│   ESP32      │◄──────────►│   STM32F407     │
│  Server      │   WiFi/Cloud   │   WiFi       │  115200    │   Target MCU    │
│              │                │   Bridge     │  8N1       │                 │
│ - Firmware   │                │              │            │ ┌─────────────┐ │
│   Storage    │                │ - HTTPS      │            │ │ Bootloader  │ │
│ - Delta      │                │   Client     │            │ │ (Bank Start)│ │
│   Patch Gen  │                │ - UART       │            │ ├─────────────┤ │
│ - SHA-256    │                │   Bridge     │            │ │ App FW      │ │
│   Hashing    │                │ - Chunk      │            │ │ (Bank A/B)  │ │
│              │                │   Transfer   │            │ │ Dual-Bank   │ │
└─────────────┘                └─────────────┘            │ └─────────────┘ │
                                                           └─────────────────┘
```

## ✨ Features

- **Dual-Bank Flash Architecture** — Fail-safe A/B partitioning with automatic rollback
- **SHA-256 Cryptographic Verification** — Protects against both corruption and tampering
- **Delta Patching (bsdiff)** — Up to 95%+ bandwidth reduction by sending only changed pages
- **HTTPS/TLS Secure Transport** — Encrypted firmware download with certificate validation
- **Chunked UART Transfer** — Reliable ESP32-to-STM32 data transfer with ACK/NACK flow control
- **Automatic Rollback** — Reverts to previous firmware on verification failure
- **Version Management** — Semantic versioning with server-side tracking
- **Boot Flag State Machine** — Persistent metadata survives power cycles

## 🎓 DESD Curriculum Mapping

| DESD Area | Project Implementation |
|---|---|
| **Microcontroller Programming** | STM32 HAL, flash programming, UART drivers, bootloader, interrupt handling, VTOR relocation |
| **Data Structures & Algorithms** | SHA-256 hashing, binary diff for delta patches, chunked transfer protocol, state machines |
| **Embedded Operating Systems** | Memory partitioning, dual-bank management, boot sequence, watchdog integration, FreeRTOS concepts |
| **IoT Connectivity** | ESP32 WiFi, HTTPS/TLS, client-server architecture, REST API, remote device management |

## 🧩 System Components

| Component | Language | Platform | Description |
|---|---|---|---|
| `server/` | Python (Flask) | PC/Cloud | Firmware hosting, delta patch generation, SHA-256 hashing |
| `esp32/` | C++ (Arduino) | ESP32 DevKit | WiFi connectivity, HTTPS download, UART bridge |
| `stm32-bootloader/` | C (HAL) | STM32F407 | Boot management, bank switching, firmware verification |
| `stm32-app/` | C (HAL) | STM32F407 | OTA reception, flash programming, patch reconstruction |

## 💾 Memory Layout

```
STM32F407 Flash (1MB = 0x08000000 - 0x080FFFFF)

┌────────────────────────────────────────────────┐ 0x08000000
│           BOOTLOADER (64KB)                    │
│           Sectors 0-3 (4 x 16KB)              │
├────────────────────────────────────────────────┤ 0x08010000
│           APPLICATION BANK A (448KB)           │
│           Sector 4 (64KB) +                    │
│           Sectors 5-7 (3 x 128KB)             │
├────────────────────────────────────────────────┤ 0x08080000
│           APPLICATION BANK B (448KB)           │
│           Sectors 8-11 (4 x 128KB)            │
│           (Mirror of Bank A for OTA)           │
├────────────────────────────────────────────────┤ 0x080E0000
│           METADATA / FLAGS (128KB)             │
│           Sector 11 (Boot flags, versions)     │
└────────────────────────────────────────────────┘ 0x080FFFFF
```

## 🚀 Setup & Build Instructions

### Prerequisites
- **STM32CubeIDE** (v1.12+) for STM32 firmware development
- **Arduino IDE** (v2.0+) with ESP32 board support
- **Python 3.8+** with pip
- **STM32F407 Discovery Board** + **ESP32 DevKit**
- Jumper wires for UART connection (TX, RX, GND)

### 1. Server Setup
```bash
cd server/
pip install -r requirements.txt
python ota_server.py
# Server runs on http://0.0.0.0:5000
```

### 2. ESP32 Firmware
- Open `esp32/esp32_ota_bridge.ino` in Arduino IDE
- Update WiFi credentials and server URL in `esp32/config.h`
- Select ESP32 Dev Module board, upload

### 3. STM32 Bootloader
- Import `stm32-bootloader/` into STM32CubeIDE
- Build and flash via ST-Link (flashed ONCE, protected)

### 4. STM32 Application
- Import `stm32-app/` into STM32CubeIDE
- Build to generate `.bin` file
- Upload `.bin` to server for OTA distribution

### Hardware Connections
```
ESP32           STM32F407 Discovery
─────           ───────────────────
GPIO17 (TX2) ──► PA3 (USART2_RX)
GPIO16 (RX2) ◄── PA2 (USART2_TX)
GND ──────────── GND
```

## 🔄 OTA Update Flow

```
 1. Device running from Bank A (active)
          │
 2. ESP32 polls server for new firmware version
          │
 3. Server generates delta patch (changed pages only) + SHA-256
          │
 4. ESP32 downloads patch via HTTPS/TLS
          │
 5. ESP32 sends patch to STM32 via UART (chunked, with ACK)
          │
 6. STM32 reconstructs full firmware in Bank B
    ├── Unchanged pages: copied from Bank A
    └── Changed pages: written from patch data
          │
 7. STM32 computes SHA-256 of Bank B, compares with expected hash
          │
 8. If MATCH: Set UPDATE_PENDING flag, System Reset
          │
 9. Bootloader verifies Bank B, switches active bank, boots Bank B
          │
10. New firmware running! Old firmware safe in Bank A as fallback
```

## 🔐 Security Design

| Layer | Mechanism | Protection |
|---|---|---|
| **Transport** | HTTPS/TLS 1.2 | Encryption in transit, server authentication |
| **Integrity** | SHA-256 Hash | Detects corruption AND tampering |
| **Availability** | Dual-Bank + Rollback | Prevents bricking, always-bootable |
| **Future** | ECDSA Signatures | Firmware authenticity verification |

### Why SHA-256 over CRC?
CRC detects **accidental** errors only. An attacker can craft malicious firmware matching any CRC in seconds. SHA-256 is **cryptographically secure** — finding a collision requires ~2^128 operations (computationally infeasible).

## 👥 Team Members

| Member | Responsibility |
|---|---|
| Member 1 | Cloud Infrastructure & Server |
| Member 2 | ESP32 WiFi Bridge |
| Member 3 | STM32 Bootloader |
| Member 4 | STM32 Application Firmware |
| Member 5 | System Integration & Testing |

*CDAC ACTS PG-Diploma in DESD — Centre for Development of Advanced Computing*

---

## 📜 License

MIT License — See [LICENSE](LICENSE) for details.

## 📚 References

- STM32F407 Reference Manual (RM0090)
- ESP32 Technical Reference Manual
- NIST FIPS 180-4 (SHA-256 Standard)
- STM32 AN2606 — System Memory Boot Mode
- STM32 AN4657 — STM32 In-Application Programming
