<div align="center">

# 📦 KeeMaBox

### Smart Delivery Box IoT — Kotak Paket Pintar Berbasis Validasi Resi

*Sistem kotak penerima paket otomatis dengan validasi nomor resi, otorisasi pemilik via Telegram, kontrol gembok jarak jauh (MQTT), dan perekaman video bukti penerimaan paket.*

<br>

![Status](https://img.shields.io/badge/status-Phase%205%20(Aktif)-blueviolet?style=for-the-badge)
![Platform](https://img.shields.io/badge/platform-IoT%20%7C%20ESP32-00979D?style=for-the-badge&logo=espressif&logoColor=white)
![Backend](https://img.shields.io/badge/backend-Flask%20%2B%20MongoDB-3776AB?style=for-the-badge&logo=python&logoColor=white)
![License](https://img.shields.io/badge/license-Private-lightgrey?style=for-the-badge)

</div>

---

## 📑 Daftar Isi

- [Tentang Proyek](#-tentang-proyek)
- [Fitur Utama](#-fitur-utama)
- [Arsitektur Sistem](#-arsitektur-sistem)
- [Alur Kerja Sistem](#-alur-kerja-sistem)
- [Tech Stack](#-tech-stack)
- [Struktur Direktori](#-struktur-direktori)
- [Perangkat Keras & Wiring](#-perangkat-keras--wiring)
- [Konfigurasi Backend (Flask)](#-konfigurasi-backend-flask)
- [Firmware ESP32](#-firmware-esp32)
- [Perbedaan Dua Varian Firmware Main](#-perbedaan-dua-varian-firmware-main)
- [Instalasi & Menjalankan](#-instalasi--menjalankan)
- [Catatan Keamanan](#-catatan-keamanan)
- [Roadmap / Progress](#-roadmap--progress)

---

## 🎯 Tentang Proyek

**KeeMaBox** adalah sistem *Smart Delivery Box* yang dirancang untuk menerima paket secara aman tanpa kehadiran pemilik di lokasi. Berbeda dari model "deteksi otomatis", proyek ini menganut filosofi **"validasi berbasis izin pemilik"** untuk keamanan maksimal:

> Pintu kotak **tidak akan pernah** terbuka otomatis. Kotak hanya terbuka setelah pemilik **secara sadar menekan tombol** dari dashboard, dipicu oleh notifikasi Telegram ketika kurir memasukkan nomor resi yang valid.

Setiap interaksi pintu terbuka direkam otomatis oleh kamera dan dikompilasi menjadi video MP4 sebagai bukti penerimaan paket.

---

## ✨ Fitur Utama

| Fitur | Deskripsi |
|-------|-----------|
| 🔐 **Validasi Resi** | Kurir memasukkan nomor resi; sistem memverifikasi terhadap database paket terdaftar. |
| 📲 **Notifikasi Telegram** | Pemilik menerima notifikasi instan saat resi valid divalidasi oleh kurir. |
| 🚪 **Kontrol Gembok Jarak Jauh** | Pemilik membuka solenoid lock dari dashboard via MQTT (`OPEN`). |
| 🎥 **Perekaman Otomatis** | ESP32-CAM merekam frame saat pintu terbuka, lalu dikompilasi menjadi MP4. |
| 🚨 **Alarm Anti-Bobol** | Buzzer + LED berbunyi jika pintu dibuka paksa tanpa izin sistem. |
| ⏱️ **Toleransi Waktu** | Solenoid terbuka 20 detik, lalu otomatis mengunci kembali. |
| 🖥️ **Dashboard Owner** | CRUD paket, monitoring status, pemutar video bukti — desain glassmorphism modern. |
| 🛡️ **Hardening Keamanan** | URL rahasia, fake 404 trap, timing-safe auth, validasi input (mengacu OWASP Top 10). |

---

## 🏗️ Arsitektur Sistem

```
                                  ┌─────────────────────────┐
                                  │   Kurir (HP / Browser)  │
                                  │  scan QR → input resi   │
                                  └───────────┬─────────────┘
                                              │ HTTP POST /api/validasi_resi
                                              ▼
┌──────────────┐   notifikasi    ┌────────────────────────────────────┐
│   Pemilik    │ ◀────Telegram───│         BACKEND FLASK (Docker)       │
│ (HP/Telegram)│                 │  ┌──────────┐   ┌────────────────┐  │
│              │   buka pintu    │  │ MongoDB  │   │ PyAV (compile) │  │
│  Dashboard   │ ───────────────▶│  │ packages │   │  JPG → MP4     │  │
└──────────────┘  /api/buka_pintu│  └──────────┘   └────────────────┘  │
                                 └─────┬──────────────────────▲────────┘
                          MQTT publish │ "OPEN"               │ HTTP POST
                       (broker.avisha) │                      │ frame.jpg
                                       ▼                      │
                          ┌────────────────────┐    GPIO 22   │  ┌─────────────┐
                          │  ESP32 Main Logic   │═════trigger══▶  │  ESP32-CAM  │
                          │ Relay│Reed│Buzzer│LED│              │  │  (OV2640)   │
                          └────────┬───────────┘                └─────────────┘
                                   │ Relay (Active-Low)
                                   ▼
                          ┌────────────────────┐
                          │ Solenoid Door Lock  │
                          └────────────────────┘
```

**Dua mikrokontroler bekerja terpisah:**
- **ESP32 DevKit V1 (Main Logic)** — otak sistem: koneksi MQTT, kontrol relay/solenoid, baca sensor pintu, alarm, dan mengirim sinyal trigger ke kamera lewat GPIO 22.
- **ESP32-CAM (OV2640)** — kamera: membaca sinyal trigger di GPIO 13, memotret frame setiap 200ms, dan mengirimnya ke backend via HTTP.

---

## 🔄 Alur Kerja Sistem

1. **Pendaftaran Paket** — Pemilik mendaftarkan `nama_barang` + `resi_number` di Dashboard (status: `pending`).
2. **Kedatangan Kurir** — Kurir scan QR Code di kotak → diarahkan ke Portal Kurir → input nomor resi.
3. **Validasi** — Server mengecek MongoDB. Jika resi cocok → status diubah ke `validated` + kirim **notifikasi Telegram** ke pemilik.
4. **Otorisasi** — Pemilik membuka Dashboard, menekan tombol **"BUKA PINTU"** → server **publish MQTT** `OPEN` ke topic `barka/kontrol`.
5. **Eksekusi** — ESP32 Main (subscribe topic) menerima `OPEN`:
   - Buzzer beep 2x → Relay ON (solenoid terbuka) → GPIO 22 HIGH (trigger kamera) → timer **20 detik** mulai.
6. **Perekaman** — ESP32-CAM mendeteksi trigger HIGH → memotret frame setiap **200ms** → kirim ke `/api/upload_frame`.
7. **Penguncian** — Setelah 20 detik, Relay OFF (terkunci), GPIO 22 LOW. Jika pintu masih terbuka → buzzer alarm pengingat berkedip tiap 500ms sampai pintu ditutup.
8. **Kompilasi** — Saat trigger LOW, ESP32-CAM memanggil `/api/compile_video` → server merajut semua frame JPG menjadi MP4 (5 FPS) via **PyAV**, lalu melampirkannya ke paket terkait di database.

---

## 🧰 Tech Stack

### Hardware
- **ESP32 DevKit V1** — main controller (upgrade dari ESP8266 di jurnal referensi)
- **ESP32-CAM (AI-Thinker / OV2640)** — modul kamera
- **Magnetic Reed Switch (MC-38)** — deteksi status pintu
- **Solenoid Door Lock 12V + Relay Module** — aktuator gembok
- **Buzzer + LED (Merah & Biru)** — indikator & alarm
- **Adaptor 12V + Step-down LM2596** — catu daya (12V → 5V)

### Software & Backend
| Komponen | Teknologi |
|----------|-----------|
| Web Framework | **Flask** (Python 3.9) |
| Database | **MongoDB** (NoSQL) |
| Komunikasi Kontrol | **MQTT** (`paho-mqtt` / `PubSubClient`) |
| Upload Gambar | **HTTP** (Multipart POST) |
| Video Processing | **PyAV** (+ Pillow, NumPy) |
| Notifikasi | **Telegram Bot API** (via `requests`) |
| Frontend | **Jinja2 + Tailwind CSS (CDN)** + Vanilla JS |
| Deployment | **Docker + Docker Compose** |
| Tunneling | Cloudflare Tunnel / Ngrok (opsional) |

---

## 📂 Struktur Direktori

```text
KeeMaBox/
├── backend/                          # Backend Flask (containerized)
│   ├── app.py                        # Server utama: routes, MQTT, Telegram, video
│   ├── requirements.txt              # Dependency Python
│   ├── Dockerfile                    # Image dengan dependensi PyAV
│   ├── static/uploads/               # Storage video .mp4
│   │   └── temp_frames/              # Storage sementara frame .jpg
│   └── templates/                    # Jinja2 templates
│       ├── kurir.html                # Portal publik (input resi)
│       ├── login.html                # Halaman login admin
│       └── index.html                # Dashboard owner
│
├── firmware/
│   └── esp32_main_logic/             # Firmware ESP32 utama (relay, sensor, alarm)
│       └── esp32_main_logic.ino
│
├── esp32_cam/                        # Firmware ESP32-CAM (capture & upload)
│   └── esp32_cam.ino
│
├── esp32_main_logic_copy/            # Salinan cadangan firmware utama
│
├── reference/                        # Bahan referensi (jurnal, wiring, catatan)
│
├── docker-compose.yml                # Orkestrasi service web + mongo
├── .env                              # Variabel rahasia (TIDAK di-commit)
├── PROJECT_PLAN.md                   # Rencana proyek lengkap
└── README.md                         # Dokumen ini
```

---

## 🔌 Perangkat Keras & Wiring

### Pin Mapping — ESP32 DevKit V1 (Main Logic)

| Komponen | Pin GPIO | Mode | Keterangan |
|----------|:--------:|------|------------|
| Relay (Solenoid) | **19** | OUTPUT | **Active-Low**: `LOW` = ON (terbuka), `HIGH` = OFF (terkunci) |
| Magnetic Reed Switch | **4** | INPUT_PULLUP | `HIGH` = pintu terbuka, `LOW` = pintu tertutup |
| Buzzer | **18** | OUTPUT | Konfirmasi & alarm |
| LED Merah | **5** | OUTPUT | Indikator pintu terbuka / alarm (lewat resistor 220Ω) |
| LED Biru | **21** | OUTPUT | Status koneksi jaringan (lewat resistor 220Ω) |
| Camera Trigger | **22** | OUTPUT | Sinyal HIGH → ESP32-CAM mulai merekam |

### Pin Trigger — ESP32-CAM

| Komponen | Pin GPIO | Mode | Keterangan |
|----------|:--------:|------|------------|
| Trigger Input | **13** | INPUT_PULLDOWN | Menerima sinyal dari GPIO 22 ESP32 Main |

> ⚠️ **Common Ground wajib**: hubungkan semua GND (ESP32, ESP32-CAM, Relay, Step-down) ke titik yang sama agar logika sinyal trigger antar-board terbaca benar.

### Jalur Power
- Adaptor **12V** → Jack DC → `IN+/IN-` Step-down LM2596 **dan** ke `COM` relay + GND solenoid.
- Step-down output **5V** → `VIN` ESP32, `5V` ESP32-CAM, `VCC` Relay (pastikan dikalibrasi tepat 5.0V).
- Solenoid (+) → pin **NO** relay; relay `IN` → GPIO 19 ESP32.

---

## ⚙️ Konfigurasi Backend (Flask)

Semua kredensial sensitif diambil dari **environment variables** (file `.env`), tidak di-hardcode.

| Variabel | Fungsi | Default / Catatan |
|----------|--------|-------------------|
| `SECRET_KEY` | Kunci sesi Flask | **Wajib** diganti string random panjang |
| `MONGO_URI` | Koneksi MongoDB | `mongodb://db:27017/smart_box` (Docker) |
| `ADMIN_USERNAME` | Username login admin | — |
| `ADMIN_PASSWORD` | Password login admin | **Wajib** di-set via env |
| `MQTT_PASSWORD` | Password broker MQTT | broker: `broker.avisha.id:1883`, user `barka` |
| `TELEGRAM_BOT_TOKEN` | Token bot Telegram | dari @BotFather |
| `TELEGRAM_CHAT_ID` | Chat ID tujuan notifikasi | — |
| `TZ` | Zona waktu | `Asia/Jakarta` (WIB) |

**Konfigurasi MQTT tetap:**
- Broker: `broker.avisha.id` | Port: `1883` | User: `barka`
- Topic kontrol: `barka/kontrol` | Payload buka: `OPEN`

### Skema Dokumen `packages` (MongoDB)
```jsonc
{
  "_id": ObjectId,
  "nama_barang": "string",
  "resi_number": "string",          // unik
  "status": "pending | validated",
  "created_at": ISODate,
  "validated_at": ISODate,          // saat kurir validasi
  "video_url": "/static/uploads/video_xxx.mp4"  // setelah kompilasi
}
```

---

## 💾 Firmware ESP32

### `esp32_main_logic.ino` — Otak Sistem
Ditulis gaya **prosedural Arduino** (non-blocking dengan `millis()`), library **PubSubClient by Nick O'Leary**. State machine utama:

- **`mqttCallback()`** — saat menerima `OPEN`: aktifkan `sesiAman`, beep 2x, trigger kamera HIGH, relay ON, mulai timer 20 detik.
- **`handleRelayTimer()`** — setelah 20 detik: relay OFF, kamera trigger LOW, masuk fase monitoring jika pintu masih terbuka.
- **`handleMonitoring()`** — buzzer berkedip 500ms selama pintu masih terbuka pasca-timer.
- **`handleDoorSensor()`** — **alarm anti-bobol**: jika pintu terbuka tanpa `sesiAman` aktif → buzzer + LED merah berkedip cepat (150ms). Variabel `sesiAman` mencegah *race condition* antara pembukaan sah dan deteksi bobol.

### `esp32_cam.ino` — Kamera
Library: `esp_camera.h`, `WiFi.h`, `HTTPClient.h`. Resolusi VGA, JPEG quality 12.

- **Standby** → baca GPIO 13.
- **HIGH** → potret + HTTP multipart POST ke `/api/upload_frame` setiap 200ms (dikirim per-chunk 1024 byte agar stabil).
- **LOW** (setelah HIGH) → panggil `/api/compile_video`, kembali standby.

> ⚙️ **Sebelum upload:** sesuaikan `server_host` / `compile_url` di `esp32_cam.ino` dengan IP backend, dan kredensial WiFi/MQTT di kedua firmware.

---

## � Perbedaan Dua Varian Firmware Main

Terdapat **dua versi** firmware ESP32 Main di repo ini. Keduanya **identik 100%** kecuali pada blok eksekusi perintah `OPEN` di dalam fungsi `mqttCallback()`. Perbedaannya terletak pada **urutan penyalaan komponen** saat pintu dibuka.

| Aspek | `firmware/esp32_main_logic/` (Stabil) | `esp32_main_logic_copy/` (Eksperimental) |
|-------|---------------------------------------|-------------------------------------------|
| **Strategi** | Urutan langsung (immediate) | **Software Sequencing / Staggered Start** |
| **Urutan saat `OPEN`** | Beep → Trigger kamera → LED → Relay | **Trigger kamera → jeda 1.5s → Beep → LED → Relay** |
| **Jeda sebelum relay** | Tidak ada (langsung) | `delay(1500)` setelah trigger kamera |
| **Tujuan** | Respons cepat | Mencegah *voltage sag* / crash ESP32-CAM |

### Inti Perbedaan Logika

**Versi Stabil** — komponen dinyalakan berurutan tanpa jeda. Relay (beban induktif berat) bisa ditarik hampir bersamaan dengan saat ESP32-CAM sedang inisialisasi:

```cpp
beepBuzzer(2, 150, 100);              // 1. Beep dulu
digitalWrite(CAM_TRIGGER_PIN, HIGH);  // 2. Trigger kamera
digitalWrite(LED_RED_PIN, HIGH);      // 3. LED
digitalWrite(RELAY_PIN, LOW);         // 4. Relay ON (langsung)
```

**Versi Copy (Eksperimental)** — menerapkan *staggered start*. Kamera diaktifkan **lebih dulu** saat tegangan listrik masih bersih, diberi jeda 1.5 detik agar stabil, **baru** solenoid (beban berat) ditarik terakhir:

```cpp
digitalWrite(CAM_TRIGGER_PIN, HIGH);  // 1. Kamera DULUAN (beban ringan)
delay(1500);                          // 2. Tunggu kamera stabil
beepBuzzer(2, 150, 100);              // 3. Beep
digitalWrite(LED_RED_PIN, HIGH);      // 4. LED
digitalWrite(RELAY_PIN, LOW);         // 5. Relay ON (beban berat, TERAKHIR)
```

### Mengapa Versi Copy Ada?
Solenoid 12V menarik arus besar (lonjakan induktif) saat aktif. Jika ESP32-CAM sedang booting/inisialisasi kamera pada saat bersamaan, *drop tegangan* (voltage sag) dapat membuat ESP32-CAM **reset atau crash** sehingga gagal merekam. Versi copy mengatasi ini dengan memastikan kamera sudah berjalan stabil **sebelum** solenoid ditarik.

> 📝 **Catatan minor:** versi copy juga memperbaiki format `Serial.printf` timer dari `%d` menjadi `%lu` (sesuai tipe `unsigned long`). Sisanya — WiFi, MQTT, `reconnect()`, timer relay, monitoring, dan alarm anti-bobol — **tidak berubah** di kedua varian.

---

## �🚀 Instalasi & Menjalankan

### 1. Backend (Docker — direkomendasikan)

```bash
# Dari root proyek, buat file .env terlebih dahulu (lihat tabel konfigurasi)
docker-compose up --build
```

Service yang dijalankan:
- **`keemabox_mongo`** — MongoDB di port `27017`
- **`keemabox_flask`** — Flask di port `5000`

Akses dashboard: `http://localhost:5000/dca-dooa-001`

### 2. Backend (manual / tanpa Docker)

```bash
# PyAV butuh dependensi sistem FFmpeg (libav*)
pip install -r backend/requirements.txt
export MONGO_URI="mongodb://localhost:27017/smart_box"
export SECRET_KEY="..." ADMIN_PASSWORD="..." MQTT_PASSWORD="..."
python backend/app.py
```

### 3. Firmware
1. Buka `firmware/esp32_main_logic/esp32_main_logic.ino` di **Arduino IDE**.
2. Install library **PubSubClient** (by Nick O'Leary) via Library Manager.
3. Sesuaikan SSID/password WiFi & kredensial MQTT, lalu upload ke ESP32 DevKit V1.
4. Buka `esp32_cam/esp32_cam.ino`, sesuaikan `server_host`/IP backend, upload ke ESP32-CAM (board: *AI Thinker ESP32-CAM*).

> 📡 **Tunneling (opsional):** untuk akses kurir via QR dari internet, ekspos port 5000 dengan Cloudflare Tunnel atau Ngrok.

---

## 🔒 Catatan Keamanan

Proyek ini menerapkan beberapa praktik hardening mengacu **OWASP Top 10**:

- **A01 (Access Control)** — Cookie `SameSite=Lax`; dashboard & API admin memakai *fake 404 trap* (mengembalikan 404, bukan redirect, jika belum login) agar keberadaan halaman tidak terekspos.
- **A02 (Crypto Failures)** — `SECRET_KEY` & semua password wajib dari environment variable, bukan hardcode.
- **A03 (Injection)** — Validasi input di endpoint; upload frame hanya menerima ekstensi `.jpg/.jpeg`.
- **A07 (Auth Failures)** — `hmac.compare_digest()` untuk cegah *timing attack*; `session.clear()` saat login untuk cegah *session fixation*; cookie `HttpOnly`.
- **Security through Obscurity** — URL login/dashboard tidak obvious (`dca-dooa-xxx`).

> 🛡️ **Rekomendasi produksi:** tambahkan rate-limiting pada `/api/validasi_resi` & login, batasi endpoint hardware (`upload_frame`/`compile_video`) dengan API key/IP allowlist, dan gunakan HTTPS (via tunnel/reverse proxy). MQTT sebaiknya naik ke TLS (port 8883).

---

## 🗺️ Roadmap / Progress

| Fase | Deskripsi | Status |
|------|-----------|:------:|
| **Phase 1** | Infrastruktur & Database (Docker, MongoDB) | ✅ Selesai |
| **Phase 2** | Backend Flask, MQTT, Notifikasi Telegram | ✅ Selesai |
| **Phase 3** | Frontend Dashboard & Portal Kurir (Tailwind) | ✅ Selesai |
| **Phase 4** | Firmware ESP32 Main (relay, sensor, alarm) | ✅ Selesai |
| **Phase 5** | ESP32-CAM & Video Processing (PyAV) | 🔄 Aktif |

**Sudah berjalan:**
- Logika `sesiAman` (anti race-condition alarm) & timer relay non-blocking.
- Endpoint `/api/upload_frame` & `/api/compile_video` (PyAV @ 5 FPS) + auto-attach video ke paket.
- Firmware ESP32-CAM capture & upload via HTTP multipart.

---

<div align="center">

**KeeMaBox** &copy; 2026 — Smart Delivery Box IoT

*Dibangun dengan Flask, MongoDB, MQTT & ESP32*

</div>
