#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>

// ==========================================
// Konfigurasi WiFi
// ==========================================
const char* ssid = "Wifi Rian";
const char* password = "admin123";

// ==========================================
// Konfigurasi Server (Backend)
// ==========================================
const char* server_host = "192.168.0.112";
const int server_port = 5000;
const char* upload_path = "/api/upload_frame";
const char* compile_url = "http://192.168.0.112:5000/api/compile_video";

// ==========================================
// Konfigurasi Pin
// ==========================================
#define TRIGGER_PIN 13

// ==========================================
// Pin Camera Model AI-Thinker (OV2640)
// ==========================================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ==========================================
// Variabel Kontrol
// ==========================================
unsigned long lastCaptureTime = 0;
const int captureInterval = 200; // 200 milidetik
bool isRecording = false;        // Status apakah sedang merekam

void setup() {
  Serial.begin(115200);
  Serial.println();

  // 1. Inisialisasi Pin Trigger (Input Pulldown agar tidak floating)
  pinMode(TRIGGER_PIN, INPUT_PULLDOWN);

  // 2. Inisialisasi WiFi
  WiFi.begin(ssid, password);
  Serial.print("Menghubungkan ke WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi terhubung!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // 3. Inisialisasi Kamera
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  // Konfigurasi Kualitas dan Resolusi (VGA, Quality 12)
  if(psramFound()){
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12; // Semakin kecil = makin bagus (0-63). 12 sangat optimal.
    config.fb_count = 1;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Inisialisasi kamera gagal dengan error 0x%x", err);
    return;
  }
  Serial.println("Kamera OV2640 siap!");
}

void loop() {
  bool currentTriggerState = digitalRead(TRIGGER_PIN);

  // Jika GPIO 13 HIGH (Pintu Dibuka)
  if (currentTriggerState == HIGH) {
    if (!isRecording) {
      Serial.println("Pintu DIBUKA. Mulai merekam frame...");
      isRecording = true;
    }

    // Mengambil dan upload frame setiap 200ms (Non-blocking)
    if (millis() - lastCaptureTime >= captureInterval) {
      lastCaptureTime = millis();
      captureAndUpload();
    }
  } 
  // Jika GPIO 13 berubah dari HIGH ke LOW (Pintu Ditutup)
  else if (currentTriggerState == LOW && isRecording) {
    Serial.println("Pintu DITUTUP. Selesai merekam.");
    isRecording = false;
    compileVideo();
  }

  // Standby jika LOW
  delay(10); // Delay kecil untuk stabilitas CPU
}

// ==========================================
// Fungsi Mengambil & Mengirim Frame (Multipart POST)
// ==========================================
void captureAndUpload() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi terputus!");
    return;
  }

  // 1. Ambil Gambar
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Gagal mengambil frame dari kamera!");
    return;
  }

  // 2. Persiapkan Client & HTTP Multipart Body
  WiFiClient client;
  if (client.connect(server_host, server_port)) {
    String boundary = "----ESP32CamBoundary";
    String head = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"frame\"; filename=\"frame.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--" + boundary + "--\r\n";
    
    uint32_t contentLength = head.length() + fb->len + tail.length();
    
    // Kirim HTTP Headers
    client.println(String("POST ") + upload_path + " HTTP/1.1");
    client.println(String("Host: ") + server_host + ":" + String(server_port));
    client.println("Content-Type: multipart/form-data; boundary=" + boundary);
    client.println("Content-Length: " + String(contentLength));
    client.println();
    
    // Kirim Header Data Multipart
    client.print(head);
    
    // Kirim Payload Gambar (Dibagi dalam potongan 1024 byte agar stabil)
    uint8_t *fbBuf = fb->buf;
    size_t fbLen = fb->len;
    for (size_t n=0; n<fbLen; n=n+1024) {
      if (n+1024 < fbLen) {
        client.write(fbBuf, 1024);
        fbBuf += 1024;
      }
      else if (fbLen%1024>0) {
        size_t remainder = fbLen%1024;
        client.write(fbBuf, remainder);
      }
    }   
    
    // Kirim Tail Data Multipart
    client.print(tail);
    
    // Kosongkan dan baca buffer response dari server (non-blocking cek singkat)
    while (client.connected()) {
      String line = client.readStringUntil('\n');
      if (line == "\r") break;
    }
    client.stop();
    Serial.println("✓ Frame terkirim!");
  } else {
    Serial.println("✗ Koneksi ke server gagal (Upload)!");
  }
  
  // 3. Bebaskan memori agar tidak crash
  esp_camera_fb_return(fb);
}

// ==========================================
// Fungsi Memicu Kompilasi Video di Backend
// ==========================================
void compileVideo() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi terputus, tidak bisa kompilasi!");
    return;
  }
  
  HTTPClient http;
  http.begin(compile_url);
  http.addHeader("Content-Type", "application/json");
  
  int httpResponseCode = http.POST("{}"); // Kirim POST kosong (atau empty JSON)
  
  if (httpResponseCode > 0) {
    Serial.printf("Perintah kompilasi terkirim. Respons Server: %d\n", httpResponseCode);
  } else {
    Serial.printf("Gagal mengirim perintah kompilasi. Error: %s\n", http.errorToString(httpResponseCode).c_str());
  }
  
  http.end();
}
