# 📝 CODE CHANGES SUMMARY - v14.5 → v15.0

## 🎯 3 Perubahan Utama

### 1. GPS M8U - UBX Parser dengan ACK Verification

#### ❌ SEBELUM (v14.5):
```cpp
void sendUBXCmd(uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t len) {
  // Kirim UBX command
  gpsSerial.write(0xB5); gpsSerial.write(0x62);
  gpsSerial.write(cls);  gpsSerial.write(id);
  // ... kirim payload ...
  gpsSerial.flush();
  
  // ❌ TIDAK ADA VERIFIKASI! Tidak tahu berhasil atau tidak
}

void configureGPS() {
  sendUBXCmd(0x06, 0x00, cfgPrt38400, 20);  // Upgrade baud
  delay(100);  // ❌ Blind delay, tidak tahu modul sudah switch atau belum
  gpsSerial.end();
  gpsSerial.begin(38400);  // ❌ Bisa gagal jika modul reject command
}
```

**Masalah**:
- Jika modul reject CFG-PRT → modul tetap di 9600, ESP32 switch ke 38400 → komunikasi putus!
- Tidak ada cara tahu konfigurasi berhasil atau tidak

#### ✅ SESUDAH (v15.0):
```cpp
// 1. Tambah UBX Parser dengan state machine
struct UbxParser {
  UbxDecodeState state;
  bool ackReceived;
  bool nakReceived;
  uint8_t waitForClass;
  uint8_t waitForId;
  
  bool decode(uint8_t byte) {
    // State machine: SYNC1 → SYNC2 → CLASS → ID → LEN → PAYLOAD → CRC
    // Jika terima ACK/NAK → set flag
  }
};

// 2. Kirim UBX dengan wait ACK
void sendUBXCmd(uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t len, bool waitAck = true) {
  // Kirim command
  gpsSerial.write(0xB5); gpsSerial.write(0x62);
  // ... kirim payload ...
  
  if (waitAck) {
    ubxParser.ackExpected = true;
    ubxParser.waitForClass = cls;
    ubxParser.waitForId = id;
    
    // ✅ Tunggu ACK/NAK dengan timeout
    unsigned long start = millis();
    while (millis() - start < 1000) {
      while (gpsSerial.available()) {
        uint8_t byte = gpsSerial.read();
        ubxParser.decode(byte);  // Parse UBX binary
        gps.encode(byte);        // Parse NMEA juga
      }
      if (ubxParser.ackReceived || ubxParser.nakReceived) break;
    }
    
    if (ubxParser.ackReceived) {
      Serial.printf("[UBX] Command 0x%02X/0x%02X: ACK ✓\\n", cls, id);
      return true;
    } else if (ubxParser.nakReceived) {
      Serial.printf("[UBX] Command 0x%02X/0x%02X: NAK ✗\\n", cls, id);
      return false;
    } else {
      Serial.printf("[UBX] Command 0x%02X/0x%02X: TIMEOUT ⏱\\n", cls, id);
      return false;
    }
  }
}

// 3. Konfigurasi dengan verifikasi
void configureGPS() {
  // ✅ Test koneksi dulu
  if (!testGpsConnection()) {
    Serial.println("[GPS] ERROR: Module not responding!");
    return false;
  }
  
  // ✅ Upgrade baud dengan ACK verification
  if (sendUBXCmd(0x06, 0x00, cfgPrt38400, 20, true)) {
    delay(200);  // Tunggu modul reboot
    gpsSerial.end();
    gpsSerial.begin(38400);
    
    // ✅ Test koneksi di baud baru
    if (testGpsConnection()) {
      Serial.println("[GPS] Baud 38400 OK!");
      S.gpsBaudCurrent = 2;
    } else {
      // ✅ Fallback ke 9600
      Serial.println("[GPS] Baud 38400 gagal, fallback ke 9600");
      gpsSerial.end();
      gpsSerial.begin(9600);
      S.gpsBaudCurrent = 1;
    }
  }
  
  // ✅ Kirim konfigurasi lain dengan ACK check
  sendUBXCmd(0x06, 0x24, nav5, 36, true);   // NAV5
  sendUBXCmd(0x06, 0x08, rate, 6, true);    // RATE
  // ... dst ...
}

// 4. Test koneksi GPS
bool testGpsConnection() {
  Serial.println("[GPS] Testing connection...");
  unsigned long start = millis();
  bool gotData = false;
  
  while (millis() - start < 5000) {
    if (gpsSerial.available()) {
      char c = gpsSerial.read();
      if (c == '$' || c == 0xB5) {  // NMEA atau UBX
        gotData = true;
        break;
      }
    }
  }
  
  if (gotData) {
    Serial.println("[GPS] ✓ Module responding!");
    return true;
  } else {
    Serial.println("[GPS] ✗ No response!");
    return false;
  }
}
```

**Keuntungan**:
- ✅ Tahu pasti setiap command berhasil atau tidak
- ✅ Auto-fallback jika upgrade baud gagal
- ✅ Tidak ada komunikasi putus
- ✅ Diagnostic log detail

---

### 2. Ultrasonic - Hanya Aktif di MODE_AUTO

#### ❌ SEBELUM (v14.5):
```cpp
void loop() {
  updateGpsLockFSM();
  updateBuzzer();
  updateServoSmooth();
  updateMotorPhysics();
  
  // ❌ processAvoidance() dipanggil SELALU, tidak peduli mode!
  processAvoidance();  // Ini ganggu manual control!
  
  switch (S.mode) {
    case MODE_MANUAL:
      if (millis() - S.lastCommand > JOYSTICK_TIMEOUT) S.targetSpeed = 0;
      break;  // ❌ Tapi processAvoidance() sudah jalan di atas!
      
    case MODE_AUTO:
      updateAutopilot();
      break;
      
    case MODE_IDLE:
    default:
      S.targetSpeed = 0;
      break;
  }
  
  mqttClient.loop();
  publishTelemetry();
}
```

**Masalah**:
- Saat manual control, ultrasonic tetap aktif
- Jika ada obstacle, servo/motor di-override oleh avoidance
- User tidak bisa kontrol penuh

#### ✅ SESUDAH (v15.0):
```cpp
void loop() {
  updateGpsLockFSM();
  updateBuzzer();
  updateServoSmooth();
  updateMotorPhysics();
  
  // ✅ Mode FSM dengan ultrasonic hanya di AUTO
  switch (S.mode) {
    case MODE_MANUAL:
      // ✅ Manual control: ultrasonic TIDAK aktif
      if (millis() - S.lastCommand > JOYSTICK_TIMEOUT) {
        S.targetSpeed = 0;
      }
      // Tidak ada processAvoidance() di sini!
      break;
      
    case MODE_AUTO:
      // ✅ Autopilot: ultrasonic AKTIF
      if (processAvoidance()) {
        // Obstacle detected, avoidance aktif
        // updateAutopilot() akan di-skip
      } else {
        // No obstacle, autopilot normal
        updateAutopilot();
      }
      break;
      
    case MODE_IDLE:
    default:
      // ✅ Idle: ultrasonic TIDAK aktif
      S.targetSpeed = 0;
      break;
  }
  
  mqttClient.loop();
  publishTelemetry();
}
```

**Keuntungan**:
- ✅ Manual control tidak terganggu obstacle avoidance
- ✅ Autopilot tetap aman dengan avoidance
- ✅ Idle mode hemat power (sensor tidak scan terus)

---

### 3. Fuzzy Logic - 5 Rules untuk 2 Sensor

#### ❌ SEBELUM (v14.5):
```cpp
bool processAvoidance() {
  int dL = readFilteredDist(TRIG_LEFT_PIN,  ECHO_LEFT_PIN,  leftBuf);
  int dR = readFilteredDist(TRIG_RIGHT_PIN, ECHO_RIGHT_PIN, rightBuf);
  
  // ❌ Simple if-else, tidak optimal
  if (dL < CRITICAL_DIST && dR < CRITICAL_DIST) {
    S.targetSpeed = -AVOID_SPEED;
    setServoTarget(SERVO_CENTER);
    S.isAvoiding = true;
  } else if (dL < OBSTACLE_DIST) {
    S.targetSpeed = AVOID_SPEED;
    setServoTarget(SERVO_MAX_RIGHT);  // ❌ Selalu belok maksimal
    S.isAvoiding = true;
  } else if (dR < OBSTACLE_DIST) {
    S.targetSpeed = AVOID_SPEED;
    setServoTarget(SERVO_MAX_LEFT);   // ❌ Selalu belok maksimal
    S.isAvoiding = true;
  } else {
    S.isAvoiding = false;
  }
  
  return S.isAvoiding;
}
```

**Masalah**:
- Tidak ada gradasi (hanya belok maksimal atau tidak)
- Tidak consider kombinasi 2 sensor
- Tidak ada "medium" decision

#### ✅ SESUDAH (v15.0):
```cpp
// 1. Fuzzy classification
enum FuzzyDistance { FUZZY_NEAR, FUZZY_MEDIUM, FUZZY_FAR };

FuzzyDistance classifyDistance(int distCm) {
  if (distCm < 50)        return FUZZY_NEAR;    // < 50cm = DEKAT
  else if (distCm < 100)  return FUZZY_MEDIUM;  // 50-100cm = SEDANG
  else                    return FUZZY_FAR;     // > 100cm = JAUH
}

// 2. Fuzzy logic dengan 5 rules
bool processAvoidance() {
  // Baca kedua sensor
  int dL = readFilteredDist(TRIG_LEFT_PIN,  ECHO_LEFT_PIN,  leftBuf);
  int dR = readFilteredDist(TRIG_RIGHT_PIN, ECHO_RIGHT_PIN, rightBuf);
  
  // Klasifikasi fuzzy
  FuzzyDistance fuzzyLeft  = classifyDistance(dL);
  FuzzyDistance fuzzyRight = classifyDistance(dR);
  
  // ✅ 5 Fuzzy Rules
  if (fuzzyLeft == FUZZY_NEAR && fuzzyRight == FUZZY_FAR) {
    // Rule 1: Kiri DEKAT + Kanan JAUH → Belok KANAN KERAS
    S.targetSpeed = AVOID_SPEED;
    setServoTarget(SERVO_MAX_RIGHT);  // 135°
    S.isAvoiding = true;
    Serial.printf("[FUZZY] Rule 1: Kiri:%dcm Kanan:%dcm → BELOK KANAN KERAS\\n", dL, dR);
    
  } else if (fuzzyLeft == FUZZY_FAR && fuzzyRight == FUZZY_NEAR) {
    // Rule 2: Kiri JAUH + Kanan DEKAT → Belok KIRI KERAS
    S.targetSpeed = AVOID_SPEED;
    setServoTarget(SERVO_MAX_LEFT);   // 45°
    S.isAvoiding = true;
    Serial.printf("[FUZZY] Rule 2: Kiri:%dcm Kanan:%dcm → BELOK KIRI KERAS\\n", dL, dR);
    
  } else if (fuzzyLeft == FUZZY_NEAR && fuzzyRight == FUZZY_NEAR) {
    // Rule 3: Kiri DEKAT + Kanan DEKAT → MUNDUR
    S.targetSpeed = -AVOID_SPEED;
    setServoTarget(SERVO_CENTER);     // 90°
    S.isAvoiding = true;
    Serial.printf("[FUZZY] Rule 3: Kiri:%dcm Kanan:%dcm → MUNDUR\\n", dL, dR);
    
  } else if (fuzzyLeft == FUZZY_MEDIUM && fuzzyRight == FUZZY_MEDIUM) {
    // Rule 4: Kiri SEDANG + Kanan SEDANG → PELAN + CENTER
    S.targetSpeed = APPROACH_SPEED;   // Speed rendah
    setServoTarget(SERVO_CENTER);     // 90°
    S.isAvoiding = true;
    Serial.printf("[FUZZY] Rule 4: Kiri:%dcm Kanan:%dcm → PELAN\\n", dL, dR);
    
  } else if (fuzzyLeft == FUZZY_FAR && fuzzyRight == FUZZY_FAR) {
    // Rule 5: Kiri JAUH + Kanan JAUH → NORMAL SPEED
    S.isAvoiding = false;
    // Tidak ubah speed/servo, biarkan autopilot kontrol
    
  } else {
    // Mixed case (e.g., NEAR + MEDIUM): pilih yang paling aman
    if (fuzzyLeft == FUZZY_NEAR || fuzzyRight == FUZZY_NEAR) {
      // Ada yang NEAR → treat sebagai Rule 3 (mundur)
      S.targetSpeed = -AVOID_SPEED;
      setServoTarget(SERVO_CENTER);
      S.isAvoiding = true;
      Serial.printf("[FUZZY] Mixed (NEAR): Kiri:%dcm Kanan:%dcm → MUNDUR\\n", dL, dR);
    } else {
      // Semua MEDIUM/FAR → treat sebagai Rule 4 (pelan)
      S.targetSpeed = APPROACH_SPEED;
      setServoTarget(SERVO_CENTER);
      S.isAvoiding = true;
      Serial.printf("[FUZZY] Mixed (MEDIUM): Kiri:%dcm Kanan:%dcm → PELAN\\n", dL, dR);
    }
  }
  
  cachedDistLeft  = dL;
  cachedDistRight = dR;
  S.smartMoveActive = S.isAvoiding;
  
  return S.isAvoiding;
}
```

**Keuntungan**:
- ✅ 5 rules cover semua kombinasi sensor
- ✅ Gradasi decision (keras/pelan/mundur)
- ✅ Lebih smooth dan predictable
- ✅ Kalibrasi untuk kapal 80cm x 25cm

---

## 📊 Comparison Table

| Feature | v14.5 | v15.0 |
|---------|-------|-------|
| **GPS UBX ACK** | ❌ Blind send | ✅ Wait & verify |
| **GPS Fallback** | ❌ Stuck jika gagal | ✅ Auto-fallback 9600 |
| **GPS Diagnostic** | ❌ Minimal log | ✅ Detail log + status |
| **Ultrasonic Mode** | ❌ Selalu aktif | ✅ Hanya di AUTO |
| **Fuzzy Logic** | ❌ Simple if-else | ✅ 5 rules |
| **Obstacle Decision** | ❌ Binary (belok/tidak) | ✅ Gradasi (keras/pelan/mundur) |
| **Manual Control** | ❌ Terganggu sensor | ✅ Full control |
| **Code Maintainability** | ⚠️ Monolithic | ✅ Modular |

---

## 🎯 Testing Checklist

### GPS Lock Test:
```
✅ Outdoor terbuka
✅ Antena eksternal terpasang
✅ Serial Monitor menunjukkan ACK untuk setiap UBX command
✅ GPS lock dalam 2-5 menit
✅ LED biru berkedip 1x/detik setelah lock
```

### Manual Control Test:
```
✅ Kirim joystick command via MQTT
✅ Motor bergerak sesuai throttle
✅ Servo belok sesuai steering
✅ TIDAK ADA log "[FUZZY]" di Serial Monitor
✅ Ultrasonic tidak ganggu control
```

### Autopilot + Fuzzy Test:
```
✅ Kirim route dengan 2+ waypoints
✅ Kapal mulai navigasi
✅ Taruh obstacle di kiri → kapal belok kanan
✅ Taruh obstacle di kanan → kapal belok kiri
✅ Taruh obstacle di kiri+kanan → kapal mundur
✅ Serial Monitor menunjukkan "[FUZZY] Rule X"
```

---

## 📞 Next Actions

1. **Upload firmware v15.0**
2. **Test GPS lock outdoor** (target: < 2 menit)
3. **Test manual control** (pastikan ultrasonic tidak aktif)
4. **Test autopilot + fuzzy** (taruh obstacle, lihat decision)
5. **Report hasil** (screenshot Serial Monitor + video test)

Jika ada masalah, kirim:
- Full boot log (Serial Monitor)
- Foto wiring GPS (close-up U.FL)
- Video test (manual + autopilot)
