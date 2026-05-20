# ✅ IMPLEMENTATION CHECKLIST

Print atau bookmark halaman ini untuk tracking progress Anda!

---

## 📦 FASE 1: PERSIAPAN (15 menit)

### Hardware Check:
- [ ] GPS NEO-M8U ada
- [ ] Antena eksternal ada (kabel + kotak)
- [ ] Kabel antena terpasang ke U.FL connector
- [ ] Tekan konektor U.FL sampai bunyi "klik"
- [ ] 2x Ultrasonic HC-SR04 terpasang
- [ ] Power supply 5V/3A (bukan dari USB laptop!)
- [ ] ESP32-S3 Dev Module
- [ ] Kabel USB data (bukan charging-only)

### Software Check:
- [ ] Arduino IDE 2.x installed
- [ ] ESP32 board support v3.0.0+ installed
- [ ] Library WiFi installed (built-in)
- [ ] Library PubSubClient installed
- [ ] Library ArduinoJson v7.x installed
- [ ] Library ESP32Servo installed
- [ ] Library TinyGPSPlus installed
- [ ] Library Preferences installed (built-in)

### Dokumentasi:
- [ ] Baca QUICK_START.md (5 menit)
- [ ] Baca PATCH_INSTRUCTIONS.md (10 menit)
- [ ] Siapkan firmware v14.5 Anda

---

## 🔧 FASE 2: APPLY PATCH (20 menit)

### PATCH 1: GPS UBX Parser
- [ ] Buka firmware v14.5 di Arduino IDE
- [ ] Copy struct `UbxParser` dari PATCH_INSTRUCTIONS.md
- [ ] Paste setelah `#include` statements
- [ ] Copy fungsi `sendUBXCmd()` baru
- [ ] Replace fungsi `sendUBXCmd()` lama
- [ ] Copy fungsi `testGpsConnection()`
- [ ] Paste sebelum `configureGPS()`
- [ ] Copy fungsi `configureGPS()` baru
- [ ] Replace fungsi `configureGPS()` lama
- [ ] **Compile** → pastikan no error

### PATCH 2: Ultrasonic Mode Fix
- [ ] Cari fungsi `loop()`
- [ ] Cari baris `processAvoidance();` di luar switch
- [ ] **HAPUS** baris tersebut (jika ada)
- [ ] Copy switch statement dari PATCH_INSTRUCTIONS.md
- [ ] Replace switch statement lama
- [ ] **Compile** → pastikan no error

### PATCH 3: Fuzzy Logic
- [ ] Copy enum `FuzzyDistance` dari PATCH_INSTRUCTIONS.md
- [ ] Paste setelah enum `DeviceMode`
- [ ] Copy fungsi `fuzzyDistToString()`
- [ ] Paste setelah `modeToString()`
- [ ] Copy fungsi `classifyDistance()`
- [ ] Paste setelah `fuzzyDistToString()`
- [ ] Copy fungsi `processAvoidance()` baru
- [ ] Replace fungsi `processAvoidance()` lama
- [ ] **Compile** → pastikan no error

### Final Check:
- [ ] Compile sukses (no error, no warning)
- [ ] Sketch size < 1MB (cek di output compile)
- [ ] Save firmware sebagai `spedi_boat_v15_patched.ino`

---

## 📤 FASE 3: UPLOAD (5 menit)

### Board Settings:
- [ ] Tools → Board → ESP32S3 Dev Module
- [ ] Tools → USB CDC On Boot → Enabled
- [ ] Tools → Flash Size → 8MB
- [ ] Tools → Partition Scheme → Default 4MB with spiffs
- [ ] Tools → Upload Speed → 921600
- [ ] Tools → Port → (pilih COM port ESP32)

### Upload:
- [ ] Klik Upload (Ctrl+U)
- [ ] Tunggu "Hard resetting via RTS pin..."
- [ ] Upload sukses (100%)
- [ ] ESP32 reboot otomatis

### Serial Monitor:
- [ ] Tools → Serial Monitor
- [ ] Baud Rate: 115200
- [ ] Lihat boot log

---

## 🧪 FASE 4: TEST GPS LOCK (10 menit)

### Persiapan:
- [ ] **Bawa kapal ke OUTDOOR TERBUKA**
- [ ] Langit terlihat 180° (tidak ada gedung/pohon)
- [ ] Jarak 5+ meter dari dinding
- [ ] Antena GPS tidak tertutup metal/tangan

### Test:
- [ ] Nyalakan ESP32
- [ ] Buka Serial Monitor
- [ ] Cek boot log:
  ```
  [GPS] Testing connection...
  [GPS] ✓ Module responding!
  [UBX] 0x06/0x00: ACK ✓
  [UBX] 0x06/0x24: ACK ✓
  ```
- [ ] Tunggu GPS lock (target < 2 menit)
- [ ] Cek log:
  ```
  [GPS] T+ 120s | GOOD FIX | Sat: 8 | HDOP:1.8
  [GPS] *** GPS TERKUNCI! ***
  ```
- [ ] Cek LED biru berkedip 1x/detik
- [ ] Screenshot Serial Monitor

### Jika Gagal:
- [ ] Cek antena U.FL (lepas & pasang ulang)
- [ ] Tunggu 10 menit (cold start bisa lama)
- [ ] Pindah lokasi lebih terbuka
- [ ] Cek Serial Monitor untuk error message

---

## 🎮 FASE 5: TEST MANUAL CONTROL (5 menit)

### Persiapan:
- [ ] Pastikan WiFi terhubung
- [ ] Pastikan MQTT connected
- [ ] Buka dashboard control

### Test:
- [ ] Kirim joystick command (throttle 50%)
- [ ] Motor harus bergerak
- [ ] Kirim steering command (kiri/kanan)
- [ ] Servo harus belok
- [ ] Cek Serial Monitor:
  ```
  [JOY] Lurus | thr=0.50 | spd=127
  ```
- [ ] **PASTIKAN TIDAK ADA log `[FUZZY]`** ← PENTING!
- [ ] Video test 30 detik

### Jika Ultrasonic Masih Aktif:
- [ ] Cek PATCH 2 sudah di-apply
- [ ] Cek tidak ada `processAvoidance()` di luar switch
- [ ] Re-compile & re-upload

---

## 🚢 FASE 6: TEST AUTOPILOT (10 menit)

### Persiapan:
- [ ] GPS sudah lock
- [ ] Buat route dengan 2+ waypoints
- [ ] Siapkan obstacle (kardus/botol)

### Test:
- [ ] Kirim route via MQTT
- [ ] Kapal mulai navigasi
- [ ] Cek Serial Monitor:
  ```
  [NAV] Rute dimulai: 3 waypoint
  [NAV] Navigasi ke WP 1/3
  ```
- [ ] Taruh obstacle di kiri kapal
- [ ] Kapal harus belok kanan
- [ ] Cek log:
  ```
  [FUZZY] Rule 1: L:35cm(DEKAT) R:150cm(JAUH) → KANAN KERAS
  ```
- [ ] Taruh obstacle di kanan kapal
- [ ] Kapal harus belok kiri
- [ ] Cek log:
  ```
  [FUZZY] Rule 2: L:150cm(JAUH) R:40cm(DEKAT) → KIRI KERAS
  ```
- [ ] Taruh obstacle di kiri+kanan
- [ ] Kapal harus mundur
- [ ] Cek log:
  ```
  [FUZZY] Rule 3: L:30cm(DEKAT) R:35cm(DEKAT) → MUNDUR
  ```
- [ ] Video test 1 menit

---

## 📊 FASE 7: VERIFICATION (5 menit)

### GPS Performance:
- [ ] Lock time < 2 menit (outdoor)
- [ ] Satelit ≥ 8
- [ ] HDOP < 2.5
- [ ] Position update 5Hz
- [ ] LED biru berkedip 1x/detik

### Manual Control:
- [ ] Joystick response instant
- [ ] Motor smooth (no jerk)
- [ ] Servo smooth (no stepping)
- [ ] Ultrasonic TIDAK aktif
- [ ] No log `[FUZZY]` saat manual

### Autopilot:
- [ ] Navigasi ke waypoint smooth
- [ ] Ultrasonic AKTIF
- [ ] Fuzzy logic 5 rules bekerja:
  - [ ] Rule 1: Kiri DEKAT + Kanan JAUH → Kanan keras
  - [ ] Rule 2: Kiri JAUH + Kanan DEKAT → Kiri keras
  - [ ] Rule 3: Kiri DEKAT + Kanan DEKAT → Mundur
  - [ ] Rule 4: Kiri SEDANG + Kanan SEDANG → Pelan
  - [ ] Rule 5: Kiri JAUH + Kanan JAUH → Normal
- [ ] Obstacle avoidance smart

### Telemetry:
- [ ] MQTT publish setiap 2 detik
- [ ] Data GPS valid (lat/lng/sat/hdop)
- [ ] Data sensor valid (obstacle_left/right)
- [ ] Mode correct (idle/manual/auto)

---

## 📝 FASE 8: DOKUMENTASI (10 menit)

### Screenshot:
- [ ] Serial Monitor boot log (full)
- [ ] Serial Monitor GPS lock
- [ ] Serial Monitor manual control
- [ ] Serial Monitor autopilot + fuzzy
- [ ] Dashboard telemetry

### Video:
- [ ] GPS lock process (2 menit)
- [ ] Manual control test (30 detik)
- [ ] Autopilot navigation (1 menit)
- [ ] Fuzzy logic avoidance (1 menit)

### Report:
- [ ] GPS lock time: ___ detik
- [ ] Satelit count: ___
- [ ] HDOP: ___
- [ ] Manual control: ✅ / ❌
- [ ] Ultrasonic mode fix: ✅ / ❌
- [ ] Fuzzy logic: ✅ / ❌
- [ ] Issues found: ___

---

## 🎯 SUCCESS CRITERIA

Anda **LULUS** jika semua ini ✅:

### Critical (WAJIB):
- [x] GPS lock < 2 menit outdoor
- [x] Manual control smooth tanpa gangguan ultrasonic
- [x] Autopilot navigasi ke waypoint
- [x] Fuzzy logic 5 rules bekerja

### Important (SANGAT DIANJURKAN):
- [x] Satelit ≥ 8
- [x] HDOP < 2.5
- [x] Telemetry publish normal
- [x] No crash/reboot

### Nice to Have (BONUS):
- [ ] GPS lock < 1 menit
- [ ] Satelit ≥ 12
- [ ] HDOP < 1.5
- [ ] Dead reckoning aktif (M8U IMU)

---

## 🆘 TROUBLESHOOTING CHECKLIST

### GPS Tidak Lock:
- [ ] Cek antena U.FL terpasang kuat
- [ ] Cek outdoor terbuka (langit 180°)
- [ ] Tunggu 10 menit (cold start)
- [ ] Cek Serial Monitor untuk ACK ✓
- [ ] Cek LED merah nyala (power)

### Ultrasonic Masih Aktif Saat Manual:
- [ ] Cek PATCH 2 sudah di-apply
- [ ] Cek `processAvoidance()` hanya di MODE_AUTO
- [ ] Re-compile & re-upload

### Fuzzy Logic Tidak Bekerja:
- [ ] Cek PATCH 3 sudah di-apply
- [ ] Cek fungsi `classifyDistance()` ada
- [ ] Cek threshold (50cm, 100cm)
- [ ] Test dengan obstacle nyata

### Motor Tidak Bergerak:
- [ ] Cek EN pin HIGH (R_EN=7, L_EN=15)
- [ ] Cek power motor driver (12V/5A)
- [ ] Cek wiring RPWM/LPWM

---

## 📞 SUPPORT

Jika stuck di fase manapun, kirim:
1. **Fase berapa** yang stuck
2. **Error message** (jika ada)
3. **Screenshot** Serial Monitor
4. **Video** (jika behavior aneh)

Saya akan bantu debug dalam 24 jam!

---

## 🏆 COMPLETION

Setelah semua checklist ✅, Anda berhasil:
- ✅ Fix GPS M8U lock issue
- ✅ Fix ultrasonic mode issue
- ✅ Implement fuzzy logic 5 rules
- ✅ Deploy firmware v15.0

**CONGRATULATIONS! 🎉**

Next: Tuning parameter, add features, atau deploy production!

---

**Print checklist ini dan centang satu per satu!**
**Good luck! 🚀**
