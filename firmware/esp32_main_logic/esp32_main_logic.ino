// =============================================================
// KeeMaBox — ESP32 Main Logic (Phase 4)
// Board  : ESP32 DevKit V1
// Fungsi : Kontrol gembok solenoid via MQTT, sensor pintu,
//          trigger sinyal ke ESP32-CAM, dan alarm anti-bobol.
// =============================================================
// Library yang dibutuhkan (install via Arduino Library Manager):
//   - WiFi.h         : Built-in ESP32 Board Package
//   - PubSubClient   : "PubSubClient" by Nick O'Leary
// =============================================================

#include <WiFi.h>
#include <PubSubClient.h>  // Library: PubSubClient by Nick O'Leary

// =============================================================
// KONFIGURASI PIN
// =============================================================
#define RELAY_PIN        19  // Output : Relay → Solenoid Door Lock
#define SENSOR_PIN        4  // Input  : Magnetic Reed Switch (INPUT_PULLUP)
#define BUZZER_PIN       18  // Output : Buzzer aktif
#define LED_RED_PIN       5  // Output : LED Merah (kotak terbuka / alarm)
#define LED_BLUE_PIN     21  // Output : LED Biru  (status koneksi jaringan)
#define CAM_TRIGGER_PIN  22  // Output : Sinyal HIGH ke ESP32-CAM → mulai rekam

// =============================================================
// KONFIGURASI JARINGAN — Ganti sesuai WiFi kamu
// =============================================================
const char* WIFI_SSID     = "Wifi Rian";
const char* WIFI_PASSWORD = "admin123";

// =============================================================
// KONFIGURASI MQTT
// =============================================================
const char* MQTT_BROKER    = "broker.avisha.id";
const int   MQTT_PORT      = 1883;
const char* MQTT_USERNAME  = "barka";
const char* MQTT_PASSWORD  = "10kw6989";  // Samakan dengan .env
const char* MQTT_CLIENT_ID = "keemabox-esp32-main";
const char* MQTT_TOPIC_SUB = "barka/kontrol";

// =============================================================
// KONFIGURASI WAKTU (non-blocking via millis)
// =============================================================
const unsigned long RELAY_DURATION_MS   = 7000;  // Durasi solenoid terbuka (7 detik)
const unsigned long MQTT_RETRY_DELAY_MS = 5000;  // Jeda antar percobaan reconnect MQTT
const unsigned long BLINK_FAST_MS       =  150;  // Interval blink alarm (ms)
const unsigned long BLINK_SLOW_MS       =  500;  // Interval blink saat connecting (ms)

// =============================================================
// VARIABEL STATE SISTEM
// =============================================================

// --- State Relay & Kamera ---
bool          relayActive    = false;  // true = solenoid sedang aktif (terbuka)
unsigned long relayStartTime = 0;      // Timestamp saat relay diaktifkan

// --- State Alarm ---
bool          alarmActive    = false;  // true = alarm anti-bobol menyala
bool          sesiAman       = false;  // true = pintu boleh terbuka tanpa alarm
                                       //        (dicabut saat pintu ditutup kembali)

// --- State Koneksi ---
unsigned long lastMqttRetry  = 0;      // Timestamp percobaan reconnect MQTT terakhir

// --- State Blink Non-Blocking ---
unsigned long lastBlueBlink  = 0;
unsigned long lastRedBlink   = 0;
bool          blueLedState   = false;
bool          redLedState    = false;

// =============================================================
// OBJEK WiFi & MQTT
// =============================================================
WiFiClient   espClient;
PubSubClient mqtt(espClient);

// =============================================================
// FUNGSI: Beep Buzzer
// Digunakan untuk konfirmasi perintah atau tanda sistem siap.
// =============================================================
void beepBuzzer(int kali, int durasiMs, int jedaMs) {
    for (int i = 0; i < kali; i++) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(durasiMs);
        digitalWrite(BUZZER_PIN, LOW);
        if (i < kali - 1) delay(jedaMs);
    }
}

// =============================================================
// FUNGSI: Koneksi WiFi
// Blocking hanya di setup(). Menggunakan setAutoReconnect(true)
// sehingga di loop() reconnect ditangani oleh library.
// =============================================================
void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.printf("[WiFi] Menghubungkan ke SSID: %s\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    // Blink LED Biru selama proses connecting
    while (WiFi.status() != WL_CONNECTED) {
        digitalWrite(LED_BLUE_PIN, !digitalRead(LED_BLUE_PIN));
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    digitalWrite(LED_BLUE_PIN, HIGH);  // Solid = Connected
    Serial.printf("[WiFi] Terhubung! IP: %s\n", WiFi.localIP().toString().c_str());
}

// =============================================================
// FUNGSI: MQTT Callback
// Otomatis dipanggil oleh mqtt.loop() saat pesan masuk.
// =============================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    // Konversi payload byte array ke String
    String pesan = "";
    for (unsigned int i = 0; i < length; i++) {
        pesan += (char)payload[i];
    }

    Serial.printf("[MQTT] Pesan diterima | Topic: %s | Payload: %s\n", topic, pesan.c_str());

    // ── Perintah BUKA PINTU ──────────────────────────────────
    if (pesan == "OPEN") {

        // Cegah eksekusi ganda jika relay sudah aktif
        if (relayActive) {
            Serial.println("[MQTT] Perintah OPEN diabaikan — relay sedang aktif.");
            return;
        }

        Serial.println("[OPEN] Perintah diterima. Memulai sesi buka pintu...");
        sesiAman = true;  // Mulai sesi aman — alarm tidak akan menyala selama sesi ini
        Serial.println("[Sesi] Sesi aman AKTIF.");

        // Reset alarm jika sebelumnya aktif
        if (alarmActive) {
            alarmActive = false;
            digitalWrite(BUZZER_PIN,   LOW);
            digitalWrite(LED_RED_PIN,  LOW);
            Serial.println("[Alarm] Alarm direset oleh perintah OPEN.");
        }

        // 1. Konfirmasi bunyi: Beep 2x
        Serial.println("[Buzzer] Beep 2x");
        beepBuzzer(2, 150, 100);  // 2 beep, 150ms nyala, 100ms jeda

        // 2. Aktifkan trigger → ESP32-CAM mulai merekam
        digitalWrite(CAM_TRIGGER_PIN, HIGH);
        Serial.println("[CAM] Trigger HIGH → ESP32-CAM mulai merekam");

        // 3. Nyalakan LED Merah sebagai indikator pintu terbuka
        digitalWrite(LED_RED_PIN, HIGH);

        // 4. Aktifkan Relay → Solenoid terbuka (Active-Low: LOW = Relay ON)
        digitalWrite(RELAY_PIN, LOW);
        relayActive    = true;
        relayStartTime = millis();
        Serial.printf("[Relay] ON → Solenoid terbuka. Timer %d detik dimulai.\n",
                      RELAY_DURATION_MS / 1000);
    }
}

// =============================================================
// FUNGSI: Koneksi & Reconnect MQTT
// Dipanggil dari loop() — non-blocking dengan rate limiting.
// =============================================================
void connectMQTT() {
    if (mqtt.connected()) return;
    if (millis() - lastMqttRetry < MQTT_RETRY_DELAY_MS) return;

    lastMqttRetry = millis();
    Serial.printf("[MQTT] Menghubungkan ke %s:%d ...\n", MQTT_BROKER, MQTT_PORT);

    if (mqtt.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD)) {
        Serial.println("[MQTT] Connected!");
        mqtt.subscribe(MQTT_TOPIC_SUB);
        Serial.printf("[MQTT] Subscribe ke topic: %s\n", MQTT_TOPIC_SUB);
        digitalWrite(LED_BLUE_PIN, HIGH);  // Solid = MQTT ready
    } else {
        Serial.printf("[MQTT] Gagal terhubung. rc=%d | Coba lagi dalam %ds...\n",
                      mqtt.state(), MQTT_RETRY_DELAY_MS / 1000);
    }
}

// =============================================================
// FUNGSI: Handle Timer Relay (non-blocking)
// Cek setiap loop apakah durasi 7 detik sudah terlampaui.
// =============================================================
void handleRelayTimer() {
    if (!relayActive) return;

    if (millis() - relayStartTime >= RELAY_DURATION_MS) {
        // Waktu habis — kunci kembali (Active-Low: HIGH = Relay OFF)
        digitalWrite(RELAY_PIN,       HIGH);
        digitalWrite(LED_RED_PIN,     LOW);
        digitalWrite(CAM_TRIGGER_PIN, LOW);
        relayActive = false;

        Serial.println("[Relay] OFF → Solenoid terkunci.");
        Serial.println("[CAM]   Trigger LOW → ESP32-CAM berhenti merekam.");
        Serial.println("[Sistem] Sesi buka pintu selesai.");

        // Beep 1x tanda gembok terkunci kembali
        beepBuzzer(1, 100, 0);
    }
}

// =============================================================
// FUNGSI: Handle Sensor Pintu & Alarm Anti-Bobol (non-blocking)
//
// Wiring: Magnetic Switch antara SENSOR_PIN dan GND.
// Logika INPUT_PULLUP:
//   LOW  → Saklar tertutup (magnet menempel) → Pintu TERTUTUP
//   HIGH → Saklar terbuka  (magnet menjauh)  → Pintu TERBUKA
//
// Alarm aktif jika pintu terbuka TANPA relay diaktifkan sistem
// (indikasi percobaan bobol).
// =============================================================
void handleDoorSensor() {
    bool pintuTerbuka = (digitalRead(SENSOR_PIN) == HIGH);

    if (pintuTerbuka && !sesiAman) {
        // ── KONDISI BOBOL: pintu terbuka tanpa sesi aman aktif ──
        if (!alarmActive) {
            alarmActive = true;
            Serial.println("[ALARM] PERINGATAN! Pintu terbuka tanpa izin sistem!");
        }

        // Blink LED Merah + Buzzer cepat (non-blocking)
        if (millis() - lastRedBlink >= BLINK_FAST_MS) {
            lastRedBlink = millis();
            redLedState  = !redLedState;
            digitalWrite(LED_RED_PIN, redLedState);
            digitalWrite(BUZZER_PIN,  redLedState);  // Buzzer ikut blink = bunyi terputus-putus cepat
        }

    } else if (!pintuTerbuka && alarmActive) {
        // ── Pintu tertutup + alarm aktif → Reset alarm & akhiri sesi ──
        alarmActive = false;
        sesiAman    = false;
        digitalWrite(LED_RED_PIN, LOW);
        digitalWrite(BUZZER_PIN,  LOW);
        Serial.println("[Alarm] Alarm direset. Pintu kembali tertutup.");
        Serial.println("[Sesi]  Sesi aman berakhir.");
    }

    // ── Kondisi independen: pintu ditutup & relay idle → akhiri sesi aman ──
    // Guard !relayActive mencegah race condition: sesiAman tidak di-reset
    // saat pintu belum sempat terbuka (relay baru saja aktif).
    if (!pintuTerbuka && sesiAman && !relayActive) {
        sesiAman = false;
        Serial.println("[Sesi] Sesi aman berakhir. Pintu ditutup dan relay idle.");
    }
}

// =============================================================
// SETUP
// =============================================================
void setup() {
    Serial.begin(115200);
    delay(200);  // Beri waktu Serial Monitor siap
    Serial.println("\n========================================");
    Serial.println("  KeeMaBox ESP32 — Booting...");
    Serial.println("========================================");

    // ── Inisialisasi Pin Output ──
    pinMode(RELAY_PIN,       OUTPUT);
    pinMode(BUZZER_PIN,      OUTPUT);
    pinMode(LED_RED_PIN,     OUTPUT);
    pinMode(LED_BLUE_PIN,    OUTPUT);
    pinMode(CAM_TRIGGER_PIN, OUTPUT);

    // ── Inisialisasi Pin Input ──
    pinMode(SENSOR_PIN, INPUT_PULLUP);

    // ── Matikan semua output di kondisi awal (safety) ──
    // RELAY Active-Low: HIGH = Relay mati = Solenoid TERKUNCI (kondisi aman)
    digitalWrite(RELAY_PIN,       HIGH);
    digitalWrite(BUZZER_PIN,      LOW);
    digitalWrite(LED_RED_PIN,     LOW);
    digitalWrite(LED_BLUE_PIN,    LOW);
    digitalWrite(CAM_TRIGGER_PIN, LOW);
    Serial.println("[Setup] Semua pin diinisialisasi. Output dalam kondisi OFF.");

    // ── Koneksi WiFi ──
    connectWiFi();

    // ── Setup MQTT ──
    mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    mqtt.setCallback(mqttCallback);
    mqtt.setKeepAlive(60);
    connectMQTT();  // Koneksi pertama kali

    // ── Sistem Siap ──
    Serial.println("========================================");
    Serial.println("  KeeMaBox Siap Beroperasi.");
    Serial.printf("  MQTT Topic: %s\n", MQTT_TOPIC_SUB);
    Serial.println("========================================\n");
    beepBuzzer(1, 300, 0);  // Beep 1x panjang = sistem siap
}

// =============================================================
// LOOP
// =============================================================
void loop() {

    // ── 1. Cek status WiFi ──
    if (WiFi.status() != WL_CONNECTED) {
        // Blink LED Biru non-blocking saat WiFi terputus
        if (millis() - lastBlueBlink >= BLINK_SLOW_MS) {
            lastBlueBlink = millis();
            blueLedState  = !blueLedState;
            digitalWrite(LED_BLUE_PIN, blueLedState);
        }
        // WiFi akan reconnect otomatis via setAutoReconnect(true)
        // Skip operasi MQTT hingga WiFi kembali
        return;
    }

    // ── 2. Cek & reconnect MQTT jika terputus ──
    if (!mqtt.connected()) {
        digitalWrite(LED_BLUE_PIN, LOW);  // Matikan LED saat MQTT disconnect
        connectMQTT();
    }

    // ── 3. Proses incoming MQTT message ──
    mqtt.loop();

    // ── 4. Handle timer penutupan relay (non-blocking) ──
    handleRelayTimer();

    // ── 5. Baca sensor pintu & handle alarm ──
    handleDoorSensor();
}
