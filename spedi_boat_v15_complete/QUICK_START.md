# 🚀 QUICK START GUIDE - SPEDI BOAT v15.0

## ⚡ 3 Masalah Utama yang Diperbaiki

### 1. GPS Tidak Lock (20+ menit)
**Root Cause**: Konfigurasi UBX gagal tanpa verifikasi
**Fix**: 
- ACK/NAK detection untuk setiap command
- Auto-fallback ke 9600 jika 38400 gagal
- Serial connection test sebelum config

### 2. Ultrasonic Aktif Saat Manual Control
**Root Cause**: `processAvoidance()` dipanggil di semua mode
**Fix**:
```cpp
// SEBELUM (v14.5):
void loop() {
  processAvoidance();  // ❌ Selalu aktif!
}

// SESUDAH (v15.0):
void loop() {
  if (S.mode == MODE_AUTO) {  // ✅ Hanya di autopilot
    processAvoidance();
  }
}
```

### 3. Tidak Ada Fuzzy Logic untuk 2 Sensor
**Root Cause**: Hanya ada if-else sederhana
**Fix**: Implementasi 5 fuzzy rules

---

## 📋 Checklist Sebelum Upload

### Hardware:
- [ ] Antena GPS eksternal terpasang ke U.FL connector
- [ ] Kabel antena tidak tertekuk tajam (radius min 2cm)
- [ ] Power supply 5V/3A (jangan dari USB laptop)
- [ ] Kedua ultrasonic sensor terpasang (kiri & kanan)

### Software:
- [ ] Arduino IDE 2.x installed
- [ ] ESP32 board support v3.0.0+
- [ ] Libraries installed (lihat README.md)

### Testing Location:
- [ ] **OUTDOOR TERBUKA** (lapangan/atap)
- [ ] Langit terlihat 180° (tidak ada gedung/pohon)
- [ ] Jarak minimal 5 meter dari dinding/obstacle

---

## 🔧 Upload Procedure

### 1. Buka Arduino IDE
```
File → Open → spedi_boat_v15_complete.ino
```

### 2. Board Settings
```
Tools → Board → ESP32S3 Dev Module
Tools → USB CDC On Boot → Enabled
Tools → Flash Size → 8MB
Tools → Upload Speed → 921600
Tools → Port → (pilih COM port ESP32)
```

### 3. Upload
```
Sketch → Upload (Ctrl+U)
```

### 4. Open Serial Monitor
```
Tools → Serial Monitor
Baud Rate: 115200
```

---

## 📊 Expected Output (Normal Boot)

```
=== SPEDI BOAT v15.0-COMPLETE-FIX — BOOT ===
[INIT] Pin OK — EN HIGH, Sonar ready
[MOTOR] LEDC ch2(RPWM) & ch3(LPWM) @ 16000Hz OK
[SERVO] Timer 0, 50Hz, pin 4 — center 1450µs OK

[GPS] Testing serial connection @ 9600 baud...
[GPS] ✓ GPS module responding! (received NMEA data)
[GPS] Mengirim konfigurasi UBX (NEO-M8U)...
[GPS] CFG-PRT: upgrade baud ke 38400... ACK ✓
[GPS] HardwareSerial1 re-init @ 38400 baud OK
[GPS] NAV5: dynModel=SEA... ACK ✓
[GPS] CFG-RATE: 5Hz... ACK ✓
[GPS] NMEA: GGA+RMC+GSA ON | GSV OFF... ACK ✓
[GPS] Konfigurasi tersimpan ke flash GPS... ACK ✓

[WIFI] Menghubungkan ke: WikWek
[WIFI] Terhubung! IP: 192.168.x.x
[MQTT] Terhubung ke broker.

[SYSTEM] Siap. Menunggu GPS lock...

[GPS] T+  10s | SEARCHING | Sat: 0 | HDOP:99.9 | Lat:0.000000 Lng:0.000000
[GPS] T+  45s | WEAK SAT  | Sat: 3 | HDOP:8.2  | Lat:-2.953xxx Lng:104.748xxx
[GPS] T+ 120s | GOOD FIX  | Sat: 8 | HDOP:1.8  | Lat:-2.953923 Lng:104.748214

[GPS] ============================================
[GPS]  *** GPS TERKUNCI! (NEO-M8U) ***
[GPS]  Lat       : -2.95392300
[GPS]  Lng       : 104.74821400
[GPS]  Satelit   : 8
[GPS]  HDOP      : 1.80
[GPS]  Quality   : 3/3
[GPS]  Waktu lock: 120 detik
[GPS] ============================================

[SYSTEM] Mode: IDLE | GPS: LOCKED | Ultrasonic: INACTIVE
```

---

## 🐛 Troubleshooting

### GPS Tidak Ada Output
```
[GPS] Testing serial connection @ 9600 baud...
[GPS] ✗ No response after 5 seconds!
[GPS] ERROR: GPS module not responding
```

**Solusi**:
1. Cek wiring TX/RX (TX modul → RX ESP32, silang!)
2. Cek power GPS (LED merah harus nyala)
3. Coba swap GPIO 16 ↔ 17

### GPS Stuck di "SEARCHING"
```
[GPS] T+ 300s | SEARCHING | Sat: 0 | HDOP:99.9
```

**Solusi**:
1. **CEK ANTENA!** Kabel U.FL harus terpasang kuat
2. Pindah ke outdoor terbuka
3. Tunggu 10 menit (cold start bisa lama)

### Ultrasonic Aktif Saat Manual
```
[JOY] Lurus | thr=0.80 | spd=204
[FUZZY] Kiri:45cm Kanan:60cm → Rule 4: PELAN  ← ❌ TIDAK SEHARUSNYA!
```

**Solusi**: Upload firmware v15.0 (bug sudah fixed)

### Motor Tidak Bergerak
```
[MOTOR] LEDC ch2(RPWM) & ch3(LPWM) @ 16000Hz OK
[JOY] Lurus | thr=1.00 | spd=255
(motor diam)
```

**Solusi**:
1. Cek EN pin (R_EN=7, L_EN=15) harus HIGH
2. Cek power motor driver (12V/5A minimum)
3. Test manual: `digitalWrite(R_EN_PIN, HIGH);`

---

## 📞 Support

Jika masih ada masalah setelah ikuti troubleshooting:
1. Screenshot Serial Monitor (full boot log)
2. Foto wiring GPS (close-up konektor U.FL)
3. Konfirmasi lokasi testing (indoor/outdoor)

---

## 🎯 Next Steps

Setelah GPS lock berhasil:
1. Test manual control via joystick
2. Test autopilot dengan 2 waypoints
3. Test fuzzy logic (taruh obstacle di kiri/kanan)
4. Monitor telemetry di dashboard

**Target Performance**:
- GPS Lock: < 2 menit (outdoor)
- Manual control: instant response
- Autopilot: smooth navigation
- Fuzzy logic: smart obstacle avoidance
