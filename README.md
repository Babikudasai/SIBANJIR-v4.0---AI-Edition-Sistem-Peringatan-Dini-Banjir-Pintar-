# 🌊 SIBANJIR v4.0 - AI Edition (Sistem Peringatan Dini Banjir Pintar)

![ESP32](https://img.shields.io/badge/ESP32-Dual%20Core-red.svg)
![Blynk](https://img.shields.io/badge/Blynk-IoT%20Ready-brightgreen.svg)
![AI](https://img.shields.io/badge/AI-Random%20Forest-blue.svg)
![OpenWeather](https://img.shields.io/badge/API-OpenWeatherMap-orange.svg)

Selamat datang di *repository* **SIBANJIR v4.0** by **Fitra Ajax Technologies**! 🚀

Project ini bukan sekadar alarm banjir biasa. Ini adalah **Sistem Peringatan Dini Banjir Kelas Industri** yang dirancang menggunakan mikrokontroler ESP32. Di versi 4.0 ini, kita udah *upgrade* gila-gilaan dengan memasukkan **Edge AI (Mini Random Forest)** dan narik data langsung dari satelit via **OpenWeatherMap API**. Hasilnya? Prediksi banjir yang jauh lebih presisi dan anti *false-alarm*!

---

## ✨ Fitur Keren (Highlight)

*   🧠 **Edge AI (Mini Random Forest):** Punya algoritma "Decision Tree" cerdas yang nge-gabungin data dari sensor fisik lokal dan data cuaca satelit buat nebak potensi banjir.
*   📡 **Live Cloud Sync via Blynk IoT:** Pantau kondisi air, curah hujan, dan status AI langsung dari HP kamu di mana aja.
*   🌤️ **Satelit Cuaca Terintegrasi:** Narik data cuaca *real-time* dari OpenWeatherMap (JSON Parsing).
*   🚀 **Arsitektur Dual-Core (FreeRTOS):** 
    *   *Core 1:* Fokus 100% ngebaca sensor, ngitung AI, putar Servo, & bunyiin Buzzer (Anti nge-lag!).
    *   *Core 0:* Khusus ngurusin WiFi, Blynk, dan API cuaca. Kalo WiFi putus, alarm darurat tetap jalan lancar jaya!
*   📟 **Dual Monitor Output:** Output tampil di LCD 1602 (dengan sistem *auto-scroll* halaman) dan Serial Monitor di PC.
*   🔄 **OTA (Over-The-Air) Ready:** Bisa update *codingan* lewat WiFi, gak perlu repot colok kabel USB lagi!

---

## 🛠️ Alat & Bahan (Hardware)

1.  **ESP32 DevKit V1** (Otak utamanya)
2.  **Sensor Jarak Ultrasonik (HC-SR04)** + 2 Resistor untuk pembagi tegangan di pin Echo.
3.  **Sensor Air (Water Level)**
4.  **Sensor Hujan (Raindrops Module)** 
5.  **Motor Servo SG90** (Untuk simulasi palang pintu air)
6.  **Buzzer Pasif/Aktif 5V** (Sirine)
7.  **LCD 1602 + Modul I2C** (Layar monitor lokal)
8.  **Kabel Jumper** (Male-to-Female, Male-to-Male)

---

## 🔌 Skema Pinout & Detail Warna Kabel

Buat anak SMK/TKJ/RPL yang mau ngerakit, tolong perhatiin tabel *wiring* di bawah ini. Biasakan pakai standar warna kabel yang benar biar gampang kalau ada *troubleshooting*!

| Komponen Hardware | Pin Hardware | Pin ESP32 (Code) | Warna Kabel Disarankan | Keterangan / Fungsi |
| :--- | :--- | :--- | :--- | :--- |
| **LCD 1602 (I2C)** | `SDA` | **D21** | 🟪 **Ungu** | Jalur Data (Wajib sesuai kode!) |
| | `SCL` | **D22** | ⬜ **Abu-abu** | Jalur Clock (Wajib sesuai kode!) |
| | `VCC` | **VIN / 5V** | 🟥 Merah | Tegangan operasi LCD |
| | `GND` | **GND** | ⬛ Hitam | Ground |
| **Sensor Jarak (HC-SR04)** | `TRIG` | **D4** | 🟦 Biru / Bebas | Pemicu gelombang suara |
| | `ECHO` | **D18** | 🟩 Hijau / Bebas | *Penerima pantulan (Wajib pakai resistor penurun tegangan ke 3.3V)* |
| **Sensor Hujan (Raindrops)**| `AO` | **D32** | 🟨 Kuning | Baca intensitas air (Analog) |
| | `DO` | **D27** | 🟧 Orange | Deteksi hujan rintik (Digital) |
| **Sensor Water Level** | `Signal (S)` | **D33** | 🟫 Coklat / Bebas | Baca ketinggian air (Analog) |
| **Motor Servo** | `Signal / PWM` | **D13** | 🟨 Kuning (dari Servo) | Buka/tutup gerbang (85 derajat) |
| **Buzzer** | `Positif (+)` | **D25** | 🟥 Merah / Bebas | Bunyi putus-putus/panjang (Native PWM channel 15) |
| **Semua Komponen** | `VCC` | **3V3 / VIN** | 🟥 Merah | Pastikan cek kebutuhan Voltase tiap sensor! |
| **Semua Komponen** | `GND` | **GND** | ⬛ Hitam | Satukan semua Ground |

> ⚠️ **TIPS HARDWARE PENTING:** 
> Kalo layar LCD kamu nyala tapi teksnya **BLANK** (kosong), ambil obeng plus/kembang kecil, putar perlahan kotak biru (Trimpot) di modul I2C belakang layar sampai tulisannya muncul!

---

## 📱 Konfigurasi Blynk IoT (Datastreams)

Biar HP kamu bisa nerima data dari ESP32, buat *Virtual Pins* di Datastreams Blynk persis seperti ini:

*   **V1** : `Integer` ➔ Water Level Gauge (0 - 4095)
*   **V2** : `Integer` ➔ Raindrops Sensor (0 - 4095)
*   **V3** : `Integer` ➔ Jarak Air Ultrasonik (0 - 999 cm)
*   **V4** : `String` ➔ Status Bahaya (Aman/Waspada/Bahaya)
*   **V5** : `String` ➔ Kondisi Cuaca Fisik (Hujan/Cerah)
*   **V6** : `Double` ➔ Intensitas Curah Hujan (0 - 255)
*   **V7** : `String` ➔ **AI Prediksi Banjir** (Terkendali/Potensi Naik/Banjir Tinggi)
*   **V8** : `String` ➔ **Cuaca Satelit API** (Misal: "awan pecah", "hujan ringan")

---

## 💻 Persiapan Instalasi Software

Sebelum *upload* kodenya ke ESP32, pastiin kamu udah *install* *Library* ini di Arduino IDE (*Sketch* -> *Include Library* -> *Manage Libraries*):

1.  `LiquidCrystal I2C` (by Frank de Brabander)
2.  `ESP32Servo` (by Kevin Harrington) - *Jangan pakai library servo bawaan Arduino!*
3.  `Blynk` (by Volodymyr Shymanskyy)
4.  `ArduinoJson` (by Benoit Blanchon) - *Buat nge-parse data OpenWeather*

**Langkah Upload:**
1. *Clone* atau *Download* repository ini.
2. Buka file `.ino`.
3. Edit bagian kredensial di atas sendiri (*Auth Token Blynk, WiFi SSID, Password, & API Key OpenWeather*).
4. *Colok* ESP32, pilih board **DOIT ESP32 DEVKIT V1**, lalu klik **Upload**.

---

## 🧠 Gimana Cara Kerja AI-nya? (Penjelasan Buat Geek)

Sistem AI di sini pakai logika **Ensemble Decision Trees (Mini Random Forest)**. Kita bikin 3 "Pohon Keputusan" di dalam kode:
1.  **Tree 1 (Lokal):** Nganalisa 100% data fisik (kalo sensor air kerendam + jarak air dekat = Bahaya).
2.  **Tree 2 (Satelit):** Nganalisa awan di atas (kalo API OpenWeather bilang lagi badai + sensor air mulai naik sedikit = Bahaya).
3.  **Tree 3 (Gabungan/Anomali):** *Cross-check*! (Kalo API cuaca bilang cerah, tapi sensor ultrasonik bilang air naik drastis, AI bakal nebak ada banjir kiriman).

Ketiga pohon ini bakal ngelakuin *Voting* (Majority Vote). Hasil suara terbanyak yang akan ditampilin di layar sebagai Prediksi AI! Keren kan? 😎

---

### 👨‍💻 Dikembangkan Oleh
**Fitra Ajax Technologies**  
*Study Group IoT 2026*

*Jika nemu bug atau mau nambah fitur, feel free buat pull request atau buka Issues ya!*
