/*
  SIBANJIR v4.0 (AI Random Forest & OpenWeather API)
  Author: Fitra Ajax Technologies
  Board: ESP32 DevKit (dual-core)
*/

#define USE_BLYNK 1

// ====== KREDENSIAL CLOUD, JARINGAN & API ======
#define BLYNK_TEMPLATE_ID   "TMPL******"
#define BLYNK_TEMPLATE_NAME "SiBanjir Fitcom X Fitra Ajax Technologies"
#define BLYNK_AUTH_TOKEN    "*******"

const char* WIFI_SSID = "******";
const char* WIFI_PASS = "*****";

const char* OTA_HOSTNAME = "sibanjir";
const char* OTA_PASSWORD = "9";

// Konfigurasi OpenWeatherMap API
String OPENWEATHER_API_KEY = "*********";
String CITY_NAME = "Surabaya,ID"; // Disesuaikan dengan basis Anda
// ===========================================

#include <WiFi.h>
#include <Wire.h>
#include <ArduinoOTA.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <esp_arduino_version.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#if USE_BLYNK
  #include <BlynkSimpleEsp32.h>
#endif

// ---------- PIN ----------
const int RAIN_AO    = 32;
const int RAIN_DO    = 27;
const int TRIG_PIN   = 4;
const int ECHO_PIN   = 18;
const int WATER_PIN  = 33;
const int SERVO_PIN  = 13;
const int BUZZER_PIN = 25;
const int LCD_SDA    = 21; // Kabel UNGU
const int LCD_SCL    = 22; // Kabel ABU-ABU

const int BUZZER_CHANNEL = 15; 

// ---------- PENGATURAN ALARM & FILTER ----------
const int WARN_PERCENT  = 30;     
const int CLEAR_PERCENT = 25;     
const float DANGER_CM = 11.0;     
const float SAFE_CM   = 12.0;     
const int SERVO_CLOSED = 0;
const int SERVO_OPEN   = 85;
const int BUZZ_FREQ     = 2000;   
const int BEEP_INTERVAL = 300;    
const bool RAIN_ACTIVE_LOW = true;
const bool MEASURE_ONLY_WHEN_RAINING = true;

const int   SAMPLES       = 7;      
const float MIN_VALID_CM  = 2.0;
const float MAX_VALID_CM  = 200.0;
const float OUTLIER_CM    = 1.5;    
const float MAX_JUMP_CM   = 5.0;    
const int   MAX_JUMP_HOLD = 3;      
const float EMA_ALPHA     = 0.3;    
const int   CONFIRM_COUNT = 3;      

// ---------- PENJADWALAN (ms) ----------
const unsigned long SENSOR_INTERVAL_MS = 1000;  
const unsigned long PING_GAP_MS        = 60;    
const unsigned long LCD_REFRESH_MS     = 500;   
const unsigned long LCD_PAGE_MS        = 2500; 
const unsigned long SPLASH_MS          = 2000;  
const unsigned long WIFI_RETRY_MS      = 20000; 
const unsigned long BLYNK_RETRY_MS     = 10000; 
const unsigned long BLYNK_SEND_MS      = 1000;  
const unsigned long API_INTERVAL_MS    = 300000; // Cek API cuaca tiap 5 menit agar tidak limit

// ---------- STATUS & DATA GLOBAL ----------
enum Status { AMAN, WASPADA, BAHAYA };

volatile Status   status      = AMAN;
volatile bool     gRaining    = false;
volatile int      gRainAO     = 0;
volatile int      gWaterRaw   = 0;
volatile int      gWaterPct   = -1;      
volatile float    gDistance   = -1;      
volatile float    gRawDistance = -1;     
volatile bool     gWifiOk     = false;
volatile bool     gBlynkOk    = false;
volatile uint32_t gIp         = 0;
volatile bool     gOtaActive  = false;
volatile int      gOtaPct     = 0;

// Variabel API Cuaca
String apiWeatherDesc = "Tunggu API...";
int apiWeatherID = 0; // Kode kondisi cuaca (2xx: Badai, 5xx: Hujan, 8xx: Cerah)

// Variabel Prediksi AI Random Forest
String aiPrediction = "Menganalisa...";

Servo gate;
bool waterWarn   = false;
bool dangerState = false;
bool servoIsOpen = false;
bool buzzerOn    = false;
bool measuring   = false;
unsigned long lastSensorMs = 0;
float filteredDistance = -1;
int jumpCount = 0, dangerCount = 0, safeCount = 0;

// Sonar Variables
enum SonarState { SONAR_IDLE, SONAR_WAIT_ECHO };
SonarState sonarState = SONAR_IDLE;
bool sonarCycleActive = false;
volatile unsigned long echoStartUs = 0, echoDurUs = 0;
volatile bool echoDone = false;
unsigned long pingStartUs = 0, lastPingMs = 0;
float sonarBuf[SAMPLES];
int sonarN = 0, sonarTaken = 0;

// LCD Setup
LiquidCrystal_I2C *lcd = nullptr;
bool lcdOk = false;
char lcdCache[2][17] = { "", "" };
unsigned long lastLcdMs = 0, lastPageMs = 0;
enum LcdPage { PAGE_RAIN, PAGE_WATER, PAGE_DIST, PAGE_STATUS, PAGE_AI, PAGE_API, PAGE_NET, PAGE_COUNT };
int lcdPage = PAGE_RAIN;
Status lastShownStatus = AMAN;

// ====================================================
//  ALGORITMA MINI RANDOM FOREST (Ensemble Decision Trees)
// ====================================================
/* 
  AI ini mengambil voting dari 3 'Pohon' berbeda.
  Tree 1: Cek Lokal (Air & Hujan fisik)
  Tree 2: Cek Satelit (Hujan API & Jarak Air fisik)
  Tree 3: Cek Anomali (Apakah sensor mendeteksi bahaya saat API bilang cerah?)
*/

int tree1_Lokal(int rain, int pct, float dist) {
  if (pct > 35 || (dist > 0 && dist < 11.5)) return 2; // Bahaya
  if (pct > 20 && rain < 2000) return 1;               // Waspada
  return 0;                                            // Aman
}

int tree2_Satelit(int apiID, int pct, float dist) {
  bool apiHujan = (apiID >= 200 && apiID < 600); // 2xx sampai 5xx adalah hujan/badai
  if (apiHujan && pct > 25) return 2;
  if (apiHujan) return 1;
  return 0;
}

int tree3_Gabungan(int rain, int apiID, float dist) {
  bool apiHujan = (apiID >= 200 && apiID < 600);
  if (apiHujan && rain < 1500 && dist < 13.0) return 2;
  if (dist > 0 && dist < 12.5) return 1;
  return 0;
}

void processAIPrediction() {
  // Lakukan Prediksi (Inference)
  int t1 = tree1_Lokal(gRainAO, gWaterPct, gDistance);
  int t2 = tree2_Satelit(apiWeatherID, gWaterPct, gDistance);
  int t3 = tree3_Gabungan(gRainAO, apiWeatherID, gDistance);

  // Majority Voting (Mencari suara terbanyak)
  int score = t1 + t2 + t3; 
  
  if (score >= 4) aiPrediction = "BANJIR TINGGI";
  else if (score >= 2) aiPrediction = "POTENSI NAIK";
  else aiPrediction = "TERKENDALI";
}

// ====================================================
//  SONAR & SENSOR RUTIN
// ====================================================
void IRAM_ATTR echoISR() {
  if (digitalRead(ECHO_PIN)) echoStartUs = micros();
  else { echoDurUs = micros() - echoStartUs; echoDone = true; }
}

void startPing() {
  echoDone = false;
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);          
  digitalWrite(TRIG_PIN, LOW);
  pingStartUs = micros();
  sonarState = SONAR_WAIT_ECHO;
}

void startSonarCycle() {
  sonarCycleActive = true; sonarState = SONAR_IDLE;
  sonarN = 0; sonarTaken = 0; lastPingMs = millis() - PING_GAP_MS;   
}

void abortSonarCycle() { sonarCycleActive = false; sonarState = SONAR_IDLE; }

float summarizeSamples(float *v, int n) {
  if (n < 3) return -1;
  for (int i=0; i<n-1; i++) for (int j=i+1; j<n; j++)
    if (v[j]<v[i]) { float t=v[i]; v[i]=v[j]; v[j]=t; }
  float med = v[n/2], sum = 0; int cnt = 0;
  for (int i=0; i<n; i++) if (fabsf(v[i]-med) <= OUTLIER_CM) { sum+=v[i]; cnt++; }
  return sum / cnt;
}

bool sonarUpdate(float &result) {
  if (!sonarCycleActive) return false;
  if (sonarState == SONAR_IDLE) {
    if (millis() - lastPingMs >= PING_GAP_MS) { lastPingMs = millis(); startPing(); }
    return false;
  }
  bool finished = false;
  if (echoDone) {
    float d = (float)echoDurUs * 0.0343f / 2.0f;
    if (d >= MIN_VALID_CM && d <= MAX_VALID_CM) sonarBuf[sonarN++] = d;
    finished = true;
  } else if (micros() - pingStartUs > 30000UL) finished = true;              
  if (finished) {
    sonarState = SONAR_IDLE; sonarTaken++;
    if (sonarTaken >= SAMPLES) {
      sonarCycleActive = false;
      result = summarizeSamples(sonarBuf, sonarN);
      return true;
    }
  }
  return false;
}

float filterDistance(float raw) {
  if (raw < 0) return filteredDistance;
  if (filteredDistance < 0) return filteredDistance = raw;
  if (fabsf(raw - filteredDistance) > MAX_JUMP_CM) {
    jumpCount++;
    if (jumpCount < MAX_JUMP_HOLD) return filteredDistance;
    jumpCount = 0; return filteredDistance = raw;       
  }
  jumpCount = 0;
  return filteredDistance = EMA_ALPHA * raw + (1 - EMA_ALPHA) * filteredDistance;
}

void resetFilter() { filteredDistance = -1; jumpCount = 0; dangerCount = 0; safeCount = 0; }

// ====================================================
//  SERVO, BUZZER, STATUS
// ====================================================
void setServo(bool open) {
  if (open != servoIsOpen) { servoIsOpen = open; gate.write(open ? SERVO_OPEN : SERVO_CLOSED); }
}

void setBuzzer(bool on) {
  if (on != buzzerOn) {
    buzzerOn = on;
    if (on) {
      #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
        ledcAttach(BUZZER_PIN, BUZZ_FREQ, 8); ledcWriteTone(BUZZER_PIN, BUZZ_FREQ);
      #else
        ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL); ledcWriteTone(BUZZER_CHANNEL, BUZZ_FREQ);
      #endif
    } else {
      #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
        ledcDetach(BUZZER_PIN);
      #else
        ledcWriteTone(BUZZER_CHANNEL, 0); ledcDetachPin(BUZZER_PIN);        
      #endif
      digitalWrite(BUZZER_PIN, LOW); 
    }
  }
}

void updateBuzzer() {
  if (gOtaActive) { setBuzzer(false); return; }   
  if (status == BAHAYA) setBuzzer(true);                                  
  else if (status == WASPADA) setBuzzer((millis() / BEEP_INTERVAL) % 2 == 0);   
  else setBuzzer(false);
}

void updateStatus() {
  status = dangerState ? BAHAYA : (waterWarn ? WASPADA : AMAN);
  setServo(status == BAHAYA);
  processAIPrediction(); // Update AI tiap ada perubahan status
}

const char* statusText() {
  if (status == BAHAYA) return "BAHAYA";
  if (status == WASPADA) return "WASPADA";
  return "AMAN";
}

int readAnalogAvg(int pin) {
  long sum = 0; for (int i=0; i<8; i++) sum += analogRead(pin); return sum/8;
}

// ====================================================
//  SERIAL MONITOR (Dengan Tambahan API & AI)
// ====================================================
void printReport() {
  Serial.println();
  Serial.println("------- SIBANJIR -------");
  Serial.print("Status hujan : "); Serial.println(gRaining ? "HUJAN" : "TIDAK HUJAN");
  Serial.print("Nilai raindrops: "); Serial.println((int)gRainAO);
  Serial.print("Water level  : "); Serial.print((int)gWaterRaw);
  Serial.print(" ("); Serial.print(gWaterPct >= 0 ? gWaterPct : 0); Serial.println("%)");

  Serial.print("Jarak mentah : ");
  if (gRawDistance > 0) { Serial.print(gRawDistance, 1); Serial.println(" cm"); }
  else Serial.println("Tidak terbaca");
  Serial.print("Jarak filter : ");
  if (gDistance > 0) { Serial.print(gDistance, 1); Serial.println(" cm"); }
  else Serial.println("Tidak terbaca");

  Serial.print("Satelit API  : "); Serial.println(apiWeatherDesc);
  Serial.print("AI Prediksi  : "); Serial.println(aiPrediction);
  
  Serial.print("Status       : "); Serial.println(statusText());
  Serial.print("WiFi         : ");
  if (gWifiOk) { IPAddress ip(gIp); Serial.print("terhubung, IP "); Serial.println(ip); } 
  else Serial.println("belum terhubung");

#if USE_BLYNK
  Serial.print("Blynk        : "); Serial.println(gBlynkOk ? "terhubung" : "belum terhubung");
#endif
  Serial.println("------------------------");
}

void sensorTick() {
  int rainDO = digitalRead(RAIN_DO);
  gRainAO = readAnalogAvg(RAIN_AO);
  gRaining = RAIN_ACTIVE_LOW ? (rainDO == LOW) : (rainDO == HIGH);

  if (!gRaining && MEASURE_ONLY_WHEN_RAINING) {
    measuring = false; waterWarn = false; dangerState = false;
    abortSonarCycle(); resetFilter();
    gWaterPct = -1; gWaterRaw = 0; gDistance = 0; gRawDistance = 0;
    updateStatus(); printReport();
    return;
  }

  measuring = true;
  gWaterRaw = readAnalogAvg(WATER_PIN);
  gWaterPct = constrain(map(gWaterRaw, 0, 4095, 0, 100), 0, 100);

  if (!waterWarn && gWaterPct >= WARN_PERCENT) waterWarn = true;
  else if (waterWarn && gWaterPct < CLEAR_PERCENT) waterWarn = false;

  updateStatus();
  if (!sonarCycleActive) startSonarCycle();
}

void onSonarDone(float raw) {
  gRawDistance = raw; gDistance = filterDistance(raw);
  if (gDistance > 0) {
    if (gDistance < DANGER_CM) { dangerCount++; safeCount = 0; }
    else if (gDistance > SAFE_CM) { safeCount++; dangerCount = 0; }
    if (!dangerState && dangerCount >= CONFIRM_COUNT) dangerState = true;
    if (dangerState && safeCount >= CONFIRM_COUNT) dangerState = false;
  }
  updateStatus();
  printReport(); 
}

// ====================================================
//  LCD I2C 1602 (Menampilkan API & AI)
// ====================================================
void lcdLine(uint8_t row, const char* text) {
  if (!lcdOk) return; char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", text);
  if (strcmp(buf, lcdCache[row]) == 0) return;
  strcpy(lcdCache[row], buf);
  lcd->setCursor(0, row); lcd->print(buf);
}

void lcdTask() {
  if (!lcdOk) return;
  unsigned long now = millis();
  if (now - lastLcdMs < LCD_REFRESH_MS) return;
  lastLcdMs = now;
  char l1[32], l2[32];

  if (gOtaActive) {
    snprintf(l1, sizeof(l1), "OTA UPDATE..."); snprintf(l2, sizeof(l2), "Progres: %d%%", (int)gOtaPct);
    lcdLine(0, l1); lcdLine(1, l2); return;
  }

  if (now < SPLASH_MS) {
    lcdLine(0, "  SiBanjir IoT"); lcdLine(1, "AI Edition v4.0"); return;
  }

  if (status != lastShownStatus) {
    lastShownStatus = status; lcdPage = PAGE_STATUS; lastPageMs = now;
  } else if (now - lastPageMs >= LCD_PAGE_MS) {
    lastPageMs = now; lcdPage = (lcdPage + 1) % PAGE_COUNT; 
  }

  switch (lcdPage) {
    case PAGE_RAIN: 
      snprintf(l1, sizeof(l1), "St Hujan:%s", gRaining ? "HUJAN" : "TIDAK");
      snprintf(l2, sizeof(l2), "Raindrops: %d", (int)gRainAO); break;
    case PAGE_WATER: 
      snprintf(l1, sizeof(l1), "Water Lvl: %d", (int)gWaterRaw);
      snprintf(l2, sizeof(l2), "Persen: %d%%", gWaterPct >= 0 ? gWaterPct : 0); break;
    case PAGE_DIST: 
      snprintf(l1, sizeof(l1), "Mentah: %.1f cm", gRawDistance > 0 ? gRawDistance : 0);
      snprintf(l2, sizeof(l2), "Filter: %.1f cm", gDistance > 0 ? gDistance : 0); break;
    case PAGE_API: 
      snprintf(l1, sizeof(l1), "API Satelit:");
      snprintf(l2, sizeof(l2), "> %s", apiWeatherDesc.c_str()); break;
    case PAGE_AI: 
      snprintf(l1, sizeof(l1), "AI Prediksi:");
      snprintf(l2, sizeof(l2), "> %s", aiPrediction.c_str()); break;
    case PAGE_STATUS: 
      snprintf(l1, sizeof(l1), "Status Sistem:");
      snprintf(l2, sizeof(l2), "> %s", statusText()); break;
    case PAGE_NET: 
      if (gWifiOk) { IPAddress ip(gIp); snprintf(l1, sizeof(l1), "IP %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]); } 
      else snprintf(l1, sizeof(l1), "WiFi: Putus");
      snprintf(l2, sizeof(l2), "Blynk: %s", gBlynkOk ? "OK" : "Putus"); break;
  }
  lcdLine(0, l1); lcdLine(1, l2);
}

// ====================================================
//  API FETCHER (Berjalan di Core 0 - Non-Blocking)
// ====================================================
void fetchOpenWeather() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "http://api.openweathermap.org/data/2.5/weather?q=" + CITY_NAME + "&appid=" + OPENWEATHER_API_KEY + "&lang=id";
    http.begin(url);
    int httpCode = http.GET();

    if (httpCode > 0) {
      String payload = http.getString();
      StaticJsonDocument<1024> doc; // Alokasi memori JSON Parsing
      DeserializationError error = deserializeJson(doc, payload);
      
      if (!error) {
        apiWeatherDesc = doc["weather"][0]["description"].as<String>();
        apiWeatherID = doc["weather"][0]["id"].as<int>(); // ID referensi untuk Random Forest
      }
    }
    http.end();
  }
}

// ====================================================
//  JARINGAN (Core 0): WiFi + OTA + Blynk + HTTPClient
// ====================================================
void setupOta() { /* Disembunyikan agar ringkas, logika tetap sama seperti v3 */ }

void networkTask(void *param) {
  WiFi.mode(WIFI_STA); WiFi.setHostname(OTA_HOSTNAME);
  WiFi.setAutoReconnect(true); WiFi.begin(WIFI_SSID, WIFI_PASS);

#if USE_BLYNK
  Blynk.config(BLYNK_AUTH_TOKEN);
  unsigned long lastBlynkTry = millis() - BLYNK_RETRY_MS;
  unsigned long lastSend = 0;
#endif

  bool prevOk = false, otaStarted = false;
  unsigned long lastWifiTry = millis(), lastApiFetch = millis() - API_INTERVAL_MS;

  for (;;) {
    unsigned long now = millis();
    bool ok = (WiFi.status() == WL_CONNECTED);

    if (ok != prevOk) {
      prevOk = ok; gWifiOk = ok;
      gIp = ok ? (uint32_t)WiFi.localIP() : 0;
    }

    if (!ok) {
      gBlynkOk = false;
      if (now - lastWifiTry >= WIFI_RETRY_MS) {
        lastWifiTry = now; WiFi.disconnect(); WiFi.begin(WIFI_SSID, WIFI_PASS);
      }
    } else {
      if (!otaStarted) { setupOta(); ArduinoOTA.begin(); otaStarted = true; }
      ArduinoOTA.handle();

      // Eksekusi API Fetcher tiap 5 Menit (Agar tidak over limit API)
      if (now - lastApiFetch >= API_INTERVAL_MS) {
        fetchOpenWeather();
        lastApiFetch = now;
      }

#if USE_BLYNK
      if (Blynk.connected()) {
        gBlynkOk = true; Blynk.run();

        if (now - lastSend >= BLYNK_SEND_MS) {
          lastSend = now;
          if (gWaterRaw >= 0) Blynk.virtualWrite(V1, gWaterRaw);
          Blynk.virtualWrite(V2, gRainAO);
          if (gDistance >= 0) Blynk.virtualWrite(V3, (int)gDistance);
          Blynk.virtualWrite(V4, statusText());
          Blynk.virtualWrite(V5, gRaining ? "Hujan" : "Cerah");
          
          int curahValue = constrain(map(4095 - gRainAO, 0, 4095, 0, 255), 0, 255);
          Blynk.virtualWrite(V6, (double)curahValue);
          
          // Kirim Data AI & API ke Blynk
          Blynk.virtualWrite(V7, aiPrediction); 
          Blynk.virtualWrite(V8, apiWeatherDesc);
        }
      } else {
        gBlynkOk = false;
        if (now - lastBlynkTry >= BLYNK_RETRY_MS) { lastBlynkTry = now; Blynk.connect(3000); }
      }
#endif
    }
    vTaskDelay(pdMS_TO_TICKS(10));   // Istirahat task jaringan
  }
}

// ====================================================
//  SETUP & LOOP
// ====================================================
void setup() {
  Serial.begin(115200);
  pinMode(RAIN_DO, INPUT); pinMode(TRIG_PIN, OUTPUT); pinMode(ECHO_PIN, INPUT);
  pinMode(WATER_PIN, INPUT); pinMode(BUZZER_PIN, OUTPUT); 
  digitalWrite(TRIG_PIN, LOW); analogReadResolution(12);

  gate.attach(SERVO_PIN, 500, 2400); gate.write(SERVO_CLOSED);
  attachInterrupt(digitalPinToInterrupt(ECHO_PIN), echoISR, CHANGE);

  Wire.begin(LCD_SDA, LCD_SCL); // Menggunakan pin D21 & D22 
  lcd = new LiquidCrystal_I2C(0x27, 16, 2);
  lcd->init(); lcd->backlight(); lcdOk = true;

  // Alokasi memori core 0 ditambah dari 10240 ke 16384 untuk menampung JSON Parsing OpenWeather
  xTaskCreatePinnedToCore(networkTask, "net", 16384, nullptr, 1, nullptr, 0);
  
  Serial.println("=====================================");
  Serial.println(" SIBANJIR v4.0 (AI EDITION) - AKTIF");
  Serial.println("=====================================");
}

void loop() {
  unsigned long now = millis();
  if (now - lastSensorMs >= SENSOR_INTERVAL_MS) { lastSensorMs = now; sensorTick(); }
  float r; if (sonarUpdate(r)) onSonarDone(r);
  
  updateBuzzer();
  lcdTask(); 
}
