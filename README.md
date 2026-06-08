<div align="center">

# 📦 KeeMaBox

### Smart Delivery Box IoT — Receipt Validation-Based Smart Package Box

*An automated package receiver box system with receipt number validation, owner authorization via Telegram, remote padlock control (MQTT), and video recording as proof of package delivery.*

<br>

![Status](https://img.shields.io/badge/status-Phase%205%20(Active)-blueviolet?style=for-the-badge)
![Platform](https://img.shields.io/badge/platform-IoT%20%7C%20ESP32-00979D?style=for-the-badge&logo=espressif&logoColor=white)
![Backend](https://img.shields.io/badge/backend-Flask%20%2B%20MongoDB-3776AB?style=for-the-badge&logo=python&logoColor=white)
![License](https://img.shields.io/badge/license-Private-lightgrey?style=for-the-badge)

</div>

---

## 📑 Table of Contents

- [About the Project](#-about-the-project)
- [Key Features](#-key-features)
- [System Architecture](#-system-architecture)
- [System Workflow](#-system-workflow)
- [Tech Stack](#-tech-stack)
- [Directory Structure](#-directory-structure)
- [Hardware & Wiring](#-hardware--wiring)
- [Backend Configuration (Flask)](#-backend-configuration-flask)
- [ESP32 Firmware](#-esp32-firmware)
- [Differences Between Two Main Firmware Variants](#-differences-between-two-main-firmware-variants)
- [Installation & Running](#-installation--running)
- [Security Notes](#-security-notes)
- [Roadmap / Progress](#-roadmap--progress)

---

## 🎯 About the Project

**KeeMaBox** is a *Smart Delivery Box* system designed to securely receive packages without the owner's presence at the location. Unlike "auto-detection" models, this project adheres to an **"owner-permission-based validation"** philosophy for maximum security:

> The box door **will never** open automatically. The box only opens after the owner **consciously presses a button** from the dashboard, triggered by a Telegram notification when the courier enters a valid receipt number.

Every open door interaction is automatically recorded by the camera and compiled into an MP4 video as proof of package reception.

---

## ✨ Key Features

| Feature | Description |
|---------|-------------|
| 🔐 **Receipt Validation** | Courier enters the receipt number; the system verifies it against the registered package database. |
| 📲 **Telegram Notification** | Owner receives an instant notification when a valid receipt is validated by the courier. |
| 🚪 **Remote Padlock Control** | Owner opens the solenoid lock from the dashboard via MQTT (`OPEN`). |
| 🎥 **Auto Recording** | ESP32-CAM records frames while the door is open, then compiles them into an MP4. |
| 🚨 **Anti-Theft Alarm** | Buzzer + LED sounds if the door is forced open without system authorization. |
| ⏱️ **Time Tolerance** | Solenoid opens for 20 seconds, then automatically locks back. |
| 🖥️ **Owner Dashboard** | Package CRUD, status monitoring, proof video player — modern glassmorphism design. |
| 🛡️ **Security Hardening** | Secret URLs, fake 404 traps, timing-safe auth, input validation (OWASP Top 10 guidelines). |

---

## 🏗️ System Architecture

```
                                  ┌─────────────────────────┐
                                  │  Courier (Phone/Browser)│
                                  │  scan QR → input receipt│
                                  └───────────┬─────────────┘
                                              │ HTTP POST /api/validasi_resi
                                              ▼
┌──────────────┐  notification   ┌────────────────────────────────────┐
│    Owner     │ ◀────Telegram───│         FLASK BACKEND (Docker)     │
│(Phone/Tg)    │                 │  ┌──────────┐   ┌────────────────┐ │
│              │    open door    │  │ MongoDB  │   │ PyAV (compile) │ │
│  Dashboard   │ ───────────────▶│  │ packages │   │  JPG → MP4     │ │
└──────────────┘  /api/buka_pintu│  └──────────┘   └────────────────┘ │
                                 └─────┬──────────────────────▲───────┘
                          MQTT publish │ "OPEN"               │ HTTP POST
                       (broker.avisha) │                      │ frame.jpg
                                       ▼                      │
                          ┌────────────────────┐    GPIO 22   │  ┌─────────────┐
                          │  ESP32 Main Logic  │═════trigger══▶  │  ESP32-CAM  │
                          │ Relay│Reed│Buzzer│L│              │  │  (OV2640)   │
                          └────────┬───────────┘              └──┴─────────────┘
                                   │ Relay (Active-Low)
                                   ▼
                          ┌────────────────────┐
                          │ Solenoid Door Lock │
                          └────────────────────┘
```

**Two microcontrollers work separately:**
- **ESP32 DevKit V1 (Main Logic)** — the brain of the system: MQTT connection, relay/solenoid control, read door sensor, alarm, and send trigger signals to the camera via GPIO 22.
- **ESP32-CAM (OV2640)** — camera: reads trigger signals on GPIO 13, captures frames every 200ms, and sends them to the backend via HTTP.

---

## 🔄 System Workflow

1. **Package Registration** — Owner registers `nama_barang` (item name) + `resi_number` (receipt number) on the Dashboard (status: `pending`).
2. **Courier Arrival** — Courier scans the QR Code on the box → directed to Courier Portal → inputs receipt number.
3. **Validation** — Server checks MongoDB. If receipt matches → status changes to `validated` + sends a **Telegram notification** to the owner.
4. **Authorization** — Owner opens Dashboard, presses the **"OPEN DOOR"** button → server **publishes MQTT** `OPEN` to topic `barka/kontrol`.
5. **Execution** — ESP32 Main (subscribed to topic) receives `OPEN`:
   - Buzzer beeps 2x → Relay ON (solenoid opens) → GPIO 22 HIGH (camera trigger) → **20-second** timer starts.
6. **Recording** — ESP32-CAM detects HIGH trigger → captures frames every **200ms** → sends to `/api/upload_frame`.
7. **Locking** — After 20 seconds, Relay OFF (locked), GPIO 22 LOW. If the door is still open → reminder alarm buzzer blinks every 500ms until the door is closed.
8. **Compilation** — When trigger is LOW, ESP32-CAM calls `/api/compile_video` → server stitches all JPG frames into an MP4 (5 FPS) via **PyAV**, then attaches it to the related package in the database.

---

## 🧰 Tech Stack

### Hardware
- **ESP32 DevKit V1** — main controller (upgrade from ESP8266 in reference journal)
- **ESP32-CAM (AI-Thinker / OV2640)** — camera module
- **Magnetic Reed Switch (MC-38)** — door status detection
- **Solenoid Door Lock 12V + Relay Module** — padlock actuator
- **Buzzer + LED (Red & Blue)** — indicator & alarm
- **12V Adapter + LM2596 Step-down** — power supply (12V → 5V)

### Software & Backend
| Component | Technology |
|-----------|------------|
| Web Framework | **Flask** (Python 3.9) |
| Database | **MongoDB** (NoSQL) |
| Control Comm | **MQTT** (`paho-mqtt` / `PubSubClient`) |
| Image Upload | **HTTP** (Multipart POST) |
| Video Processing | **PyAV** (+ Pillow, NumPy) |
| Notification | **Telegram Bot API** (via `requests`) |
| Frontend | **Jinja2 + Tailwind CSS (CDN)** + Vanilla JS |
| Deployment | **Docker + Docker Compose** |
| Tunneling | Cloudflare Tunnel / Ngrok (optional) |

---

## 📂 Directory Structure

```text
KeeMaBox/
├── backend/                          # Flask Backend (containerized)
│   ├── app.py                        # Main server: routes, MQTT, Tg, video
│   ├── requirements.txt              # Python dependencies
│   ├── Dockerfile                    # Image with PyAV dependencies
│   ├── static/uploads/               # .mp4 video storage
│   │   └── temp_frames/              # Temporary .jpg frame storage
│   └── templates/                    # Jinja2 templates
│       ├── kurir.html                # Public portal (receipt input)
│       ├── login.html                # Admin login page
│       └── index.html                # Owner dashboard
│
├── firmware/
│   └── esp32_main_logic/             # Main ESP32 firmware (relay, sensor, alarm)
│       └── esp32_main_logic.ino
│
├── esp32_cam/                        # ESP32-CAM firmware (capture & upload)
│   └── esp32_cam.ino
│
├── esp32_main_logic_copy/            # Backup of main firmware
│
├── reference/                        # Reference materials (journals, wiring, notes)
│
├── docker-compose.yml                # Web + mongo service orchestration
├── .env                              # Secret variables (NOT committed)
├── PROJECT_PLAN.md                   # Full project plan
└── README.md                         # This document
```

---

## 🔌 Hardware & Wiring

### Pin Mapping — ESP32 DevKit V1 (Main Logic)

| Component | GPIO Pin | Mode | Description |
|-----------|:--------:|------|-------------|
| Relay (Solenoid) | **19** | OUTPUT | **Active-Low**: `LOW` = ON (open), `HIGH` = OFF (locked) |
| Magnetic Reed Switch | **4** | INPUT_PULLUP | `HIGH` = door open, `LOW` = door closed |
| Buzzer | **18** | OUTPUT | Confirmation & alarm |
| Red LED | **5** | OUTPUT | Door open / alarm indicator (via 220Ω resistor) |
| Blue LED | **21** | OUTPUT | Network connection status (via 220Ω resistor) |
| Camera Trigger | **22** | OUTPUT | HIGH signal → ESP32-CAM starts recording |

### Pin Trigger — ESP32-CAM

| Component | GPIO Pin | Mode | Description |
|-----------|:--------:|------|-------------|
| Trigger Input | **13** | INPUT_PULLDOWN | Receives signal from GPIO 22 of ESP32 Main |

> ⚠️ **Common Ground is mandatory**: connect all GNDs (ESP32, ESP32-CAM, Relay, Step-down) to the same point so the trigger signal logic between boards is read correctly.

### Power Routing
- **12V** Adapter → DC Jack → `IN+/IN-` LM2596 Step-down **and** to `COM` relay + GND solenoid.
- Step-down output **5V** → `VIN` ESP32, `5V` ESP32-CAM, `VCC` Relay (ensure calibrated to exactly 5.0V).
- Solenoid (+) → **NO** relay pin; relay `IN` → GPIO 19 ESP32.

---

## ⚙️ Backend Configuration (Flask)

All sensitive credentials are fetched from **environment variables** (`.env` file), not hardcoded.

| Variable | Function | Default / Notes |
|----------|----------|-----------------|
| `SECRET_KEY` | Flask session key | **Must** be changed to a long random string |
| `MONGO_URI` | MongoDB connection | `mongodb://db:27017/smart_box` (Docker) |
| `ADMIN_USERNAME` | Admin login username | — |
| `ADMIN_PASSWORD` | Admin login password | **Must** be set via env |
| `MQTT_PASSWORD` | MQTT broker password | broker: `broker.avisha.id:1883`, user `barka` |
| `TELEGRAM_BOT_TOKEN` | Telegram bot token | from @BotFather |
| `TELEGRAM_CHAT_ID` | Destination chat ID | — |
| `TZ` | Timezone | `Asia/Jakarta` |

**Fixed MQTT Configuration:**
- Broker: `broker.avisha.id` | Port: `1883` | User: `barka`
- Control Topic: `barka/kontrol` | Open Payload: `OPEN`

### `packages` Document Schema (MongoDB)
```jsonc
{
  "_id": ObjectId,
  "nama_barang": "string",
  "resi_number": "string",          // unique
  "status": "pending | validated",
  "created_at": ISODate,
  "validated_at": ISODate,          // when courier validates
  "video_url": "/static/uploads/video_xxx.mp4"  // after compilation
}
```

---

## 💾 ESP32 Firmware

### `esp32_main_logic.ino` — The Brain
Written in **procedural Arduino** style (non-blocking with `millis()`), using **PubSubClient by Nick O'Leary**. Main state machine:

- **`mqttCallback()`** — on receiving `OPEN`: activates `sesiAman` (safe session), beeps 2x, sets camera trigger HIGH, turns relay ON, starts 20s timer.
- **`handleRelayTimer()`** — after 20 seconds: turns relay OFF, sets camera trigger LOW, enters monitoring phase if door is still open.
- **`handleMonitoring()`** — buzzer blinks 500ms as long as the door remains open post-timer.
- **`handleDoorSensor()`** — **anti-theft alarm**: if door opens without active `sesiAman` → buzzer + red LED blinks rapidly (150ms). The `sesiAman` variable prevents race conditions between authorized opening and theft detection.

### `esp32_cam.ino` — The Camera
Libraries: `esp_camera.h`, `WiFi.h`, `HTTPClient.h`. VGA resolution, JPEG quality 12.

- **Standby** → reads GPIO 13.
- **HIGH** → captures + HTTP multipart POST to `/api/upload_frame` every 200ms (sent in 1024-byte chunks for stability).
- **LOW** (after HIGH) → calls `/api/compile_video`, returns to standby.

> ⚙️ **Before uploading:** adjust `server_host` / `compile_url` in `esp32_cam.ino` with your backend IP, and WiFi/MQTT credentials in both firmwares.

---

## 🔄 Differences Between Two Main Firmware Variants

There are **two versions** of the ESP32 Main firmware in this repo. Both are **100% identical** except for the `OPEN` command execution block within the `mqttCallback()` function. The difference lies in the **turn-on sequence of components** when the door is opened.

| Aspect | `firmware/esp32_main_logic/` (Stable) | `esp32_main_logic_copy/` (Experimental) |
|--------|---------------------------------------|-----------------------------------------|
| **Strategy** | Immediate sequence | **Software Sequencing / Staggered Start** |
| **Order on `OPEN`** | Beep → Cam Trigger → LED → Relay | **Cam Trigger → wait 1.5s → Beep → LED → Relay** |
| **Delay before relay**| None (immediate) | `delay(1500)` after camera trigger |
| **Purpose** | Fast response | Prevents *voltage sag* / ESP32-CAM crash |

### Core Logic Difference

**Stable Version** — components turn on sequentially without delay. The relay (heavy inductive load) is pulled almost simultaneously with the ESP32-CAM initializing:

```cpp
beepBuzzer(2, 150, 100);              // 1. Beep first
digitalWrite(CAM_TRIGGER_PIN, HIGH);  // 2. Camera trigger
digitalWrite(LED_RED_PIN, HIGH);      // 3. LED
digitalWrite(RELAY_PIN, LOW);         // 4. Relay ON (immediate)
```

**Copy Version (Experimental)** — implements a *staggered start*. The camera is activated **first** while voltage is clean, given a 1.5s delay to stabilize, **then** the solenoid (heavy load) is pulled last:

```cpp
digitalWrite(CAM_TRIGGER_PIN, HIGH);  // 1. Camera FIRST (light load)
delay(1500);                          // 2. Wait for camera to stabilize
beepBuzzer(2, 150, 100);              // 3. Beep
digitalWrite(LED_RED_PIN, HIGH);      // 4. LED
digitalWrite(RELAY_PIN, LOW);         // 5. Relay ON (heavy load, LAST)
```

### Why does the Copy Version Exist?
A 12V solenoid draws a large current (inductive spike) when activated. If the ESP32-CAM is booting/initializing the camera at the same time, a *voltage sag* can cause the ESP32-CAM to **reset or crash**, failing to record. The copy version fixes this by ensuring the camera runs stably **before** pulling the solenoid.

> 📝 **Minor note:** the copy version also fixes the `Serial.printf` timer format from `%d` to `%lu` (matching `unsigned long` type). The rest — WiFi, MQTT, `reconnect()`, relay timer, monitoring, and anti-theft alarm — **remains unchanged** in both variants.

---

## 🚀 Installation & Running

### 1. Backend (Docker — Recommended)

```bash
# From project root, create .env file first (see configuration table)
docker-compose up --build
```

Running services:
- **`keemabox_mongo`** — MongoDB on port `27017`
- **`keemabox_flask`** — Flask on port `5000`

Dashboard access: `http://localhost:5000/dca-dooa-001`

### 2. Backend (Manual / without Docker)

```bash
# PyAV requires system FFmpeg dependencies (libav*)
pip install -r backend/requirements.txt
export MONGO_URI="mongodb://localhost:27017/smart_box"
export SECRET_KEY="..." ADMIN_PASSWORD="..." MQTT_PASSWORD="..."
python backend/app.py
```

### 3. Firmware
1. Open `firmware/esp32_main_logic/esp32_main_logic.ino` in **Arduino IDE**.
2. Install **PubSubClient** library (by Nick O'Leary) via Library Manager.
3. Adjust WiFi SSID/password & MQTT credentials, then upload to ESP32 DevKit V1.
4. Open `esp32_cam/esp32_cam.ino`, adjust `server_host`/Backend IP, upload to ESP32-CAM (board: *AI Thinker ESP32-CAM*).

> 📡 **Tunneling (optional):** for courier access via QR from the internet, expose port 5000 with Cloudflare Tunnel or Ngrok.

---

## 🔒 Security Notes

This project implements several hardening practices following the **OWASP Top 10**:

- **A01 (Access Control)** — `SameSite=Lax` cookies; admin dashboard & API use *fake 404 traps* (returning 404, not redirecting, if not logged in) so page existence is not exposed.
- **A02 (Crypto Failures)** — `SECRET_KEY` & all passwords must come from environment variables, not hardcoded.
- **A03 (Injection)** — Input validation on endpoints; frame upload only accepts `.jpg/.jpeg` extensions.
- **A07 (Auth Failures)** — `hmac.compare_digest()` to prevent *timing attacks*; `session.clear()` on login to prevent *session fixation*; `HttpOnly` cookies.
- **Security through Obscurity** — Non-obvious login/dashboard URLs (`dca-dooa-xxx`).

> 🛡️ **Production recommendation:** add rate-limiting to `/api/validasi_resi` & login, restrict hardware endpoints (`upload_frame`/`compile_video`) with API keys/IP allowlists, and use HTTPS (via tunnel/reverse proxy). MQTT should upgrade to TLS (port 8883).

---

## 🗺️ Roadmap / Progress

| Phase | Description | Status |
|-------|-------------|:------:|
| **Phase 1** | Infrastructure & Database (Docker, MongoDB) | ✅ Done |
| **Phase 2** | Flask Backend, MQTT, Telegram Notifications | ✅ Done |
| **Phase 3** | Frontend Dashboard & Courier Portal (Tailwind) | ✅ Done |
| **Phase 4** | ESP32 Main Firmware (relay, sensor, alarm) | ✅ Done |
| **Phase 5** | ESP32-CAM & Video Processing (PyAV) | 🔄 Active |

**Currently Running:**
- `sesiAman` logic (anti-race-condition alarm) & non-blocking relay timer.
- `/api/upload_frame` & `/api/compile_video` endpoints (PyAV @ 5 FPS) + auto-attach video to package.
- ESP32-CAM firmware capture & upload via HTTP multipart.

---

<div align="center">

**KeeMaBox** &copy; 2026 — Smart Delivery Box IoT

*Built with Flask, MongoDB, MQTT & ESP32*

</div>
