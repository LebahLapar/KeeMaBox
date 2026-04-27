# PROJECT PLAN: Smart Delivery Box IoT (Refreshed Workflow)

## 1. Project Overview
Membangun sistem kotak penerima paket pintar yang aman dengan fitur validasi nomor resi dan perekaman video otomatis saat kurir berinteraksi dengan alat. Sistem ini beralih dari model "deteksi otomatis" ke model "validasi berbasis izin pemilik" untuk keamanan maksimal.

## 2. Tech Stack & Infrastructure
### Hardware
* **Main MCU:** ESP32 Devkit V1 (Upgrade dari ESP8266 di jurnal).
* **Camera Module:** ESP32-CAM (Khusus streaming/capture frame).
* **Sensors:** Magnetic Reed Switch (MC-38) untuk deteksi pintu.
* **Actuators:** Solenoid Door Lock 12V + Relay Module + Buzzer + LED.
* **Power:** Adaptor 12V + Step-down LM2596 (12V to 5V).

### Software & Backend
* **Web Framework:** Flask (Python).
* **Database:** MongoDB (NoSQL).
* **Communication:** MQTT (untuk kontrol gembok) & HTTP (untuk upload frame gambar).
* **Multimedia:** Library PyAV (untuk kompilasi frame menjadi video MP4).
* **Notification:** Telegram Bot API (menggunakan library `requests` di Python untuk mengirim pesan HTTP POST langsung ke API Telegram).
* **Tunneling:** Cloudflare Tunnel atau Ngrok (untuk mengekspos localhost ke publik agar bisa diakses kurir via QR Code).

## 3. System Workflow (The Logic)
1. **Pendaftaran Paket:** Pemilik mendaftarkan nomor resi di Dashboard Web.
2. **Kedatangan Kurir:** Kurir scan QR Code di kotak, diarahkan ke website untuk input nomor resi.
3. **Validasi:** Server mengecek database. Jika resi cocok, server mengirim notifikasi instan via **Telegram Bot** ke HP pemilik (berisi info paket dan tautan untuk membuka kotak).
4. **Otorisasi:** Pemilik membuka tautan dari Telegram dan menekan tombol "Unlock" di web -> Server mengirim pesan MQTT 'Publish'.
5. **Eksekusi:** ESP32 'Subscribe' MQTT -> Menerima perintah dan menyalakan Relay (Solenoid Terbuka).
6. **Monitoring:** Saat pintu terbuka (deteksi Magnetic Switch), ESP32-CAM mengirim frame gambar secara kontinu ke server via HTTP POST.
7. **Kompilasi:** Setelah pintu tertutup, Server menyatukan kumpulan frame gambar menjadi satu video MP4.

## 4. Hardware Pin Mapping (ESP32 Devkit V1)
### 4.1 Jalur Power (12V)
* **Adaptor 12V** masuk ke **Jack DC Female**.
* Dari Jack DC Female (Positif/Merah):
    * Ke pin **IN+** pada Step-Down LM2596.
    * Ke pin **COM** (tengah) pada bagian *output* modul Relay.
* Dari Jack DC Female (Negatif/Hitam):
    * Ke pin **IN-** pada Step-Down LM2596.
    * Langsung ke kabel Negatif/Hitam pada **Solenoid Door Lock**.

### 4.2 Jalur Step-Down (5V ke Otak Sistem)
*(Pastikan baut Step-Down sudah diputar hingga output tepat 5.0V sebelum menyambung)*
* Dari pin **OUT+ (5V)** Step-Down:
    * Ke pin **VIN** (atau 5V) di ESP32.
    * Ke pin **5V** di ESP32-CAM.
    * Ke pin **VCC** di modul Relay.
* Dari pin **OUT- (GND)** Step-Down:
    * Sambungkan ke semua pin **GND** di ESP32, ESP32-CAM, dan Relay (Common Ground).

### 4.3 Jalur Aktuator (Solenoid & Relay)
* Kabel Positif/Merah dari **Solenoid Door Lock** masuk ke pin **NO** (Normally Open) pada modul Relay.
* Pin **IN** (Input logika) modul Relay dihubungkan ke **GPIO 19** pada ESP32.

### 4.4 Jalur Sensor & Indikator
* **Magnetic Switch Sensor:** Satu kabel ke **GPIO 4** (ESP32), satu kabel lagi ke **GND**. *(Setting di kode: `INPUT_PULLUP`)*.
* **Buzzer:** Pin Positif (+) ke **GPIO 18** (ESP32), pin Negatif (-) ke **GND**.
* **LED Merah:** Kaki Positif (+) lewat resistor 220 ohm ke **GPIO 5** (ESP32), kaki Negatif (-) ke **GND**.
* **LED Biru:** Kaki Positif (+) lewat resistor 220 ohm ke **GPIO 21** (ESP32), kaki Negatif (-) ke **GND**.

### 4.5 Jalur Komunikasi Antar-ESP
* Dari **GPIO 22** (ESP32 Utama) dihubungkan ke salah satu pin input digital di **ESP32-CAM** (misalnya GPIO 13 atau 14, tergantung ketersediaan pin pada board kamera Anda) sebagai sinyal pemicu (*trigger*) mulai/berhenti merekam gambar.

## 5. Directory Structure Reference
```text
/KeeMaBox
├── /backend
│   ├── app.py (Flask Server)
│   ├── models.py (MongoDB Schema)
│   ├── /static/uploads (Storage Frame & Video)
│   └── /templates (Web Interface)
├── /firmware
│   ├── esp32_main_logic (Arduino/C++)
│   └── esp32_cam_stream (Arduino/C++)
├── /tunnel
│   └── config.yml (Cloudflare/Ngrok config)
└── README.md
```

## 6. Action Plan for AI Instructions

### Phase 1: Infrastructure & Database (Proxmox/Docker)
* Buat struktur `docker-compose.yml` untuk mendeploy sistem secara terpisah: Service `mongodb` (Database) dan Service `web` (Flask Server).
* Setup Cloudflare Tunnel (bisa via container `cloudflared` terpisah atau langsung di OS) untuk mengekspos port Flask ke domain publik.
* Setup koneksi MongoDB di Flask untuk menyimpan koleksi `packages` dengan field: `resi_number`, `status`, `video_path`.

### Phase 2: Backend (Flask), Cloud MQTT & Notification
* Buat server Flask dengan rute: `/` (dashboard), `/kurir` (halaman input resi), dan `/api/upload_frame`.
* Integrasikan Cloud MQTT menggunakan library `paho-mqtt` di Python. Gunakan kredensial berikut:
  * **Host:** `broker.avisha.id`
  * **Port:** `1883`
  * **Username:** `barka` (Password disesuaikan dengan milik pengguna).
  * **Topic Publish:** `barka/kontrol` (Kirim payload "OPEN" dari web untuk membuka kunci).
* Implementasikan logika Telegram Bot API menggunakan library `requests` di Python untuk mengirim notifikasi ke pemilik saat nomor resi yang diinput kurir dinyatakan valid.

### Phase 3: Frontend UI/UX (Web Dashboard via Jinja2 & Tailwind)
* Gunakan **Jinja2 Templates** (letakkan di dalam folder `/backend/templates`) dan integrasikan **Tailwind CSS** (via CDN) untuk desain yang *clean*, modern, dan *mobile-first*.
* **Halaman Owner Dashboard (Terhubung ke rute `/`):**
  * Tampilkan tabel/kartu daftar paket secara dinamis berdasarkan data dari koleksi MongoDB (Nama Barang, Nomor Resi, Status).
  * Buat tombol **"Buka Pintu"**. Gunakan Vanilla JavaScript (`fetch()`) agar saat diklik, halaman menembak endpoint backend secara asinkron untuk men-trigger publish MQTT "OPEN".
  * Sediakan tombol **"Cek Video"** pada paket yang sudah sampai, yang akan memunculkan modal/layer berisi pemutar video `.mp4`.
* **Halaman Portal Kurir (Terhubung ke rute `/kurir`):**
  * Buat halaman minimalis yang hanya fokus menampilkan form input untuk **"Nomor Resi"**.
  * Saat *form* disubmit (POST ke backend), tampilkan indikator *loading*. Jika backend membalas validasi sukses, ubah tampilan menjadi pesan "Kotak Berhasil Dibuka!" agar kurir tahu sistem merespons.

### Phase 4: Firmware ESP32 (Main Logic)
* Buat kode ESP32 untuk koneksi WiFi dan koneksi ke Cloud MQTT (`broker.avisha.id` port `1883`) menggunakan library `PubSubClient`.
* Set ESP32 untuk **Subscribe** ke topic: `barka/kontrol`.
* Buat logika aktuator: Jika menerima pesan "OPEN" di topic tersebut, aktifkan Relay (Solenoid Terbuka) dan nyalakan Buzzer sesaat.
* Gunakan interupsi/pembacaan state pada pin Magnetic Switch (GPIO 4) untuk men-trigger status pintu (Terbuka/Tertutup) dan mengirimkan sinyal ke ESP32-CAM via GPIO 22.

### Phase 5: ESP32-CAM & Video Processing
* Program ESP32-CAM untuk menangkap gambar (capture) setiap 200ms SAAT menerima sinyal pintu terbuka dari ESP32 Utama.
* Kirim frame gambar tersebut menggunakan HTTP POST langsung ke URL publik Web Server (hasil tunneling Cloudflare).
* Buat fungsi di Python (Flask backend) menggunakan library `PyAV` untuk menampung frame gambar di `/static/uploads` dan langsung mengkompilasinya menjadi satu file video `.mp4` saat pintu ditutup.
---
