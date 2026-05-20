# SPEDI BOAT Firmware v15.0 - Complete Fix

## 🎯 Changelog v15.0

### GPS M8U Fixes:
- ✅ UBX binary parser dengan ACK/NAK detection
- ✅ Serial connection verification sebelum konfigurasi
- ✅ Auto-fallback ke 9600 baud jika 38400 gagal
- ✅ Factory reset function untuk modul stuck
- ✅ Detailed diagnostic logging (satelit, HDOP, NMEA)
- ✅ LED blink pattern untuk status GPS

### Ultrasonic Sensor Fixes:
- ✅ Sensor **HANYA aktif di MODE_AUTO**
- ✅ Tidak ganggu manual control (MODE_MANUAL/IDLE)
- ✅ Kedua sensor bekerja simultan
- ✅ Fuzzy logic decision making

### Fuzzy Logic Implementation:
**Boat Dimensions**: 80cm (L) x 25cm (W)

**Distance Thresholds**:
- DEKAT (NEAR): < 50cm
- SEDANG (MEDIUM): 50-100cm  
- JAUH (FAR): > 100cm

**5 Fuzzy Rules**:
1. Kiri DEKAT + Kanan JAUH → Belok KANAN KERAS (servo 135°)
2. Kiri JAUH + Kanan DEKAT → Belok KIRI KERAS (servo 45°)
3. Kiri DEKAT + Kanan DEKAT → MUNDUR (speed -130)
4. Kiri SEDANG + Kanan SEDANG → PELAN + CENTER (speed 100)
5. Kiri JAUH + Kanan JAUH → NORMAL SPEED (speed 180)

## 📦 Hardware Requirements

- ESP32-S3 Dev Module
- NEO-M8U GPS (dengan antena eksternal aktif)
- 2x HC-SR04 Ultrasonic Sensor
- BTS7960 Motor Driver
- Servo MG996R
- Buzzer aktif
- LED indicator

## 🔌 Pin Configuration

| Component | Pin ESP32-S3 |
|-----------|--------------|
| GPS RX | GPIO 16 |
| GPS TX | GPIO 17 |
| Motor RPWM | GPIO 5 |
| Motor LPWM | GPIO 6 |
| Motor R_EN | GPIO 7 |
| Motor L_EN | GPIO 15 |
| Servo | GPIO 4 |
| Ultrasonic Left TRIG | GPIO 10 |
| Ultrasonic Left ECHO | GPIO 11 |
| Ultrasonic Right TRIG | GPIO 12 |
| Ultrasonic Right ECHO | GPIO 13 |
| Buzzer | GPIO 21 |
| LED Status | GPIO 38 |

## 📡 Network Configuration

```cpp
SSID: "WikWek"
Password: "11334455"
MQTT Broker: "ballast.proxy.rlwy.net:29053"
MQTT User: "device"
MQTT Pass: "spedi2026"
```

## 🚀 Upload Instructions

1. Install Arduino IDE 2.x
2. Install ESP32 board support (v3.0.0+)
3. Install libraries:
   - WiFi (built-in)
   - PubSubClient
   - ArduinoJson (v7.x)
   - ESP32Servo
   - TinyGPSPlus
   - Preferences (built-in)

4. Board Settings:
   - Board: "ESP32S3 Dev Module"
   - USB CDC On Boot: "Enabled"
   - Flash Size: "8MB"
   - Partition Scheme: "Default 4MB with spiffs"
   - Upload Speed: "921600"

5. Upload firmware
6. Open Serial Monitor (115200 baud)

## 🧪 Testing Procedure

### 1. GPS Lock Test (Outdoor):
```
Expected output:
[GPS] T+  10s | SEARCHING | Sat: 0 | HDOP:99.9
[GPS] T+  45s | WEAK SAT  | Sat: 3 | HDOP:8.2
[GPS] T+ 120s | GOOD FIX  | Sat: 8 | HDOP:1.8
[GPS] *** GPS TERKUNCI! ***
```

### 2. Manual Control Test:
```
- Kirim joystick command via MQTT
- Motor harus bergerak
- Servo harus belok
- Ultrasonic TIDAK aktif (tidak ada log obstacle)
```

### 3. Autopilot Test:
```
- Kirim route dengan 2+ waypoints
- Kapal mulai navigasi otomatis
- Ultrasonic AKTIF (ada log obstacle detection)
- Fuzzy logic bekerja saat ada halangan
```

## 🐛 Troubleshooting

### GPS Tidak Lock:
1. Cek antena eksternal terpasang ke U.FL connector
2. Pastikan outdoor terbuka (langit terlihat 180°)
3. Tunggu 5-10 menit untuk cold start
4. Cek Serial Monitor untuk diagnostic log

### Ultrasonic Aktif Saat Manual:
- Bug fixed di v15.0
- Sensor hanya aktif di MODE_AUTO

### Motor Tidak Bergerak:
1. Cek EN pin (R_EN=7, L_EN=15) HIGH
2. Cek power supply motor driver (12V/5A)
3. Cek wiring RPWM/LPWM

## 📊 Performance Metrics

- GPS Lock Time: 30-120 detik (outdoor)
- Ultrasonic Scan Rate: 60ms per cycle
- Servo Update Rate: 12ms (smooth)
- Motor Ramp Rate: 25ms per step
- MQTT Telemetry: 2 detik per publish

## 📝 Version History

- v14.5: GPS fix attempts (5 fixes)
- v15.0: Complete rewrite dengan UBX parser + Fuzzy logic

## 👨‍💻 Author

SPEDI BOAT Project
ESP32-S3 + NEO-M8U + Fuzzy Logic
