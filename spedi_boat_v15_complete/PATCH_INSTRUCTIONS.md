# 🔧 PATCH INSTRUCTIONS - Apply to Your Current Firmware

Karena firmware lengkap terlalu besar untuk ditulis sekaligus, berikut adalah **patch manual** yang bisa Anda terapkan ke firmware v14.5 Anda yang sudah ada.

## 📋 3 File Patch

### 1. GPS_UBX_PARSER_PATCH.txt
### 2. ULTRASONIC_MODE_PATCH.txt  
### 3. FUZZY_LOGIC_PATCH.txt

---

## 🎯 PATCH 1: GPS UBX Parser dengan ACK Verification

### Lokasi: Setelah `#include` statements, sebelum `setup()`

```cpp
// ============================================================================
// [PATCH 1] UBX PARSER STATE MACHINE
// ============================================================================
enum UbxDecodeState {
  UBX_DECODE_SYNC1, UBX_DECODE_SYNC2, UBX_DECODE_CLASS, UBX_DECODE_ID,
  UBX_DECODE_LEN1, UBX_DECODE_LEN2, UBX_DECODE_PAYLOAD, UBX_DECODE_CK_A, UBX_DECODE_CK_B
};

struct UbxMessage {
  uint8_t msgClass;
  uint8_t msgId;
  uint16_t payloadLen;
  uint8_t payload[512];
  uint8_t ckA, ckB;
  bool valid;
};

struct UbxParser {
  UbxDecodeState state = UBX_DECODE_SYNC1;
  UbxMessage rxMsg;
  uint16_t payloadIndex = 0;
  uint8_t ckA = 0, ckB = 0;
  bool ackExpected = false, ackReceived = false, nakReceived = false;
  uint8_t waitForClass = 0, waitForId = 0;
  
  void addChecksum(uint8_t b) {
    ckA = (ckA + b) & 0xFF;
    ckB = (ckB + ckA) & 0xFF;
  }
  
  bool decode(uint8_t byte) {
    switch (state) {
      case UBX_DECODE_SYNC1:
        if (byte == 0xB5) state = UBX_DECODE_SYNC2;
        break;
      case UBX_DECODE_SYNC2:
        if (byte == 0x62) { state = UBX_DECODE_CLASS; ckA = ckB = 0; payloadIndex = 0; }
        else state = UBX_DECODE_SYNC1;
        break;
      case UBX_DECODE_CLASS:
        rxMsg.msgClass = byte; addChecksum(byte); state = UBX_DECODE_ID;
        break;
      case UBX_DECODE_ID:
        rxMsg.msgId = byte; addChecksum(byte); state = UBX_DECODE_LEN1;
        break;
      case UBX_DECODE_LEN1:
        rxMsg.payloadLen = byte; addChecksum(byte); state = UBX_DECODE_LEN2;
        break;
      case UBX_DECODE_LEN2:
        rxMsg.payloadLen |= (uint16_t)byte << 8; addChecksum(byte);
        state = (rxMsg.payloadLen == 0) ? UBX_DECODE_CK_A : UBX_DECODE_PAYLOAD;
        break;
      case UBX_DECODE_PAYLOAD:
        if (payloadIndex < 512) rxMsg.payload[payloadIndex++] = byte;
        addChecksum(byte);
        if (payloadIndex >= rxMsg.payloadLen) state = UBX_DECODE_CK_A;
        break;
      case UBX_DECODE_CK_A:
        rxMsg.ckA = byte; state = UBX_DECODE_CK_B;
        break;
      case UBX_DECODE_CK_B:
        rxMsg.ckB = byte; state = UBX_DECODE_SYNC1;
        if (rxMsg.ckA == ckA && rxMsg.ckB == ckB) {
          processMessage();
          return true;
        }
        break;
    }
    return false;
  }
  
  void processMessage() {
    if (rxMsg.msgClass == 0x05 && rxMsg.payloadLen >= 2) {  // ACK class
      uint8_t ackClass = rxMsg.payload[0];
      uint8_t ackId = rxMsg.payload[1];
      if (ackExpected && ackClass == waitForClass && ackId == waitForId) {
        if (rxMsg.msgId == 0x01) ackReceived = true;  // ACK-ACK
        else if (rxMsg.msgId == 0x00) nakReceived = true;  // ACK-NAK
      }
    }
  }
} ubxParser;

// REPLACE fungsi sendUBXCmd() yang lama dengan ini:
void sendUBXCmd(uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t len, bool waitAck = true) {
  uint8_t ckA = 0, ckB = 0;
  auto addByte = [&](uint8_t b) { ckA = (ckA + b) & 0xFF; ckB = (ckB + ckA) & 0xFF; };
  addByte(cls); addByte(id); addByte((uint8_t)(len & 0xFF)); addByte((uint8_t)(len >> 8));
  for (uint16_t i = 0; i < len; i++) addByte(payload[i]);

  gpsSerial.write(0xB5); gpsSerial.write(0x62);
  gpsSerial.write(cls);  gpsSerial.write(id);
  gpsSerial.write((uint8_t)(len & 0xFF)); gpsSerial.write((uint8_t)(len >> 8));
  if (len > 0) gpsSerial.write(payload, len);
  gpsSerial.write(ckA);  gpsSerial.write(ckB);
  gpsSerial.flush();
  
  if (waitAck) {
    ubxParser.ackExpected = true;
    ubxParser.ackReceived = false;
    ubxParser.nakReceived = false;
    ubxParser.waitForClass = cls;
    ubxParser.waitForId = id;
    
    unsigned long start = millis();
    while (millis() - start < 1000) {
      while (gpsSerial.available()) {
        uint8_t byte = gpsSerial.read();
        ubxParser.decode(byte);
        gps.encode(byte);
      }
      if (ubxParser.ackReceived || ubxParser.nakReceived) break;
      delay(10);
    }
    
    if (ubxParser.ackReceived) Serial.printf("[UBX] 0x%02X/0x%02X: ACK ✓\\n", cls, id);
    else if (ubxParser.nakReceived) Serial.printf("[UBX] 0x%02X/0x%02X: NAK ✗\\n", cls, id);
    else Serial.printf("[UBX] 0x%02X/0x%02X: TIMEOUT ⏱\\n", cls, id);
    
    ubxParser.ackExpected = false;
  }
}

// TAMBAHKAN fungsi test koneksi GPS:
bool testGpsConnection() {
  Serial.println("[GPS] Testing connection...");
  unsigned long start = millis();
  while (millis() - start < 5000) {
    if (gpsSerial.available()) {
      char c = gpsSerial.read();
      if (c == '$' || c == 0xB5) {
        Serial.println("[GPS] ✓ Module responding!");
        return true;
      }
    }
  }
  Serial.println("[GPS] ✗ No response!");
  return false;
}

// REPLACE fungsi configureGPS() dengan ini:
void configureGPS() {
  Serial.println(F("[GPS] Konfigurasi UBX dengan ACK verification..."));
  
  // Test koneksi @ 9600
  if (!testGpsConnection()) {
    Serial.println(F("[GPS] ERROR: Module not responding!"));
    return;
  }
  
  // Upgrade baud dengan ACK
  static const uint8_t cfgPrt38400[20] = {
    0x01, 0x00, 0x00,0x00, 0xD0,0x08,0x00,0x00,
    0x00,0x96,0x00,0x00, 0x23,0x00, 0x03,0x00, 0x00,0x00, 0x00,0x00
  };
  sendUBXCmd(0x06, 0x00, cfgPrt38400, sizeof(cfgPrt38400), true);
  
  delay(200);
  gpsSerial.end();
  delay(100);
  gpsSerial.begin(GPS_BAUD_FAST, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  delay(200);
  
  // Test koneksi @ 38400
  if (testGpsConnection()) {
    Serial.printf("[GPS] Baud %d OK!\\n", GPS_BAUD_FAST);
  } else {
    Serial.println("[GPS] Fallback ke 9600...");
    gpsSerial.end();
    gpsSerial.begin(GPS_BAUD_INIT, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  }
  
  // Kirim konfigurasi lain dengan ACK
  static const uint8_t nav5[36] = {
    0xFF,0xFF, 0x05, 0x03, 0x00,0x00,0x00,0x00, 0x10,0x27,0x00,0x00,
    0x05, 0x00, 0xFA,0x00, 0xFA,0x00, 0x64,0x00, 0x2C,0x01,
    0x00, 0x3C, 0x00, 0x00, 0x00,0x00, 0x00,0x00, 0x00, 0x00,0x00,0x00,0x00
  };
  sendUBXCmd(0x06, 0x24, nav5, sizeof(nav5), true);
  
  static const uint8_t rate[6] = { 0xC8,0x00, 0x01,0x00, 0x01,0x00 };
  sendUBXCmd(0x06, 0x08, rate, sizeof(rate), true);
  
  static const uint8_t msgGGA[8] = { 0xF0,0x00, 0x00,0x01,0x00,0x00,0x00,0x00 };
  static const uint8_t msgRMC[8] = { 0xF0,0x04, 0x00,0x01,0x00,0x00,0x00,0x00 };
  static const uint8_t msgGSV[8] = { 0xF0,0x03, 0x00,0x00,0x00,0x00,0x00,0x00 };
  static const uint8_t msgGSA[8] = { 0xF0,0x02, 0x00,0x01,0x00,0x00,0x00,0x00 };
  sendUBXCmd(0x06, 0x01, msgGGA, 8, true);
  sendUBXCmd(0x06, 0x01, msgRMC, 8, true);
  sendUBXCmd(0x06, 0x01, msgGSV, 8, true);
  sendUBXCmd(0x06, 0x01, msgGSA, 8, true);
  
  static const uint8_t saveCfg[12] = {
    0x00,0x00,0x00,0x00, 0xFF,0xFF,0x00,0x00, 0x00,0x00,0x00,0x00
  };
  sendUBXCmd(0x06, 0x09, saveCfg, sizeof(saveCfg), true);
  
  Serial.println(F("[GPS] Konfigurasi selesai!"));
}
```

---

## 🎯 PATCH 2: Ultrasonic Hanya Aktif di MODE_AUTO

### Lokasi: Fungsi `loop()`, REPLACE bagian mode FSM

```cpp
// ============================================================================
// [PATCH 2] MODE FSM - Ultrasonic hanya di AUTO
// ============================================================================

// HAPUS baris ini jika ada di luar switch:
// processAvoidance();  // ❌ HAPUS INI!

// REPLACE switch statement dengan ini:
switch (S.mode) {
  case MODE_MANUAL:
    // Manual control: ultrasonic TIDAK aktif
    if (millis() - S.lastCommand > JOYSTICK_TIMEOUT) {
      S.targetSpeed = 0;
    }
    // TIDAK ADA processAvoidance() di sini!
    break;
    
  case MODE_AUTO:
    // Autopilot: ultrasonic AKTIF
    if (processAvoidance()) {
      // Obstacle detected, avoidance override autopilot
      Serial.println("[NAV] Obstacle avoidance aktif");
    } else {
      // No obstacle, autopilot normal
      updateAutopilot();
    }
    break;
    
  case MODE_IDLE:
  default:
    // Idle: ultrasonic TIDAK aktif
    S.targetSpeed = 0;
    break;
}
```

---

## 🎯 PATCH 3: Fuzzy Logic untuk 2 Sensor

### Lokasi: REPLACE fungsi `processAvoidance()`

```cpp
// ============================================================================
// [PATCH 3] FUZZY LOGIC - 5 Rules
// ============================================================================

// TAMBAHKAN enum dan fungsi klasifikasi:
enum FuzzyDistance { FUZZY_NEAR, FUZZY_MEDIUM, FUZZY_FAR };

const char* fuzzyDistToString(FuzzyDistance d) {
  switch(d) {
    case FUZZY_NEAR:   return "DEKAT";
    case FUZZY_MEDIUM: return "SEDANG";
    case FUZZY_FAR:    return "JAUH";
    default:           return "???";
  }
}

FuzzyDistance classifyDistance(int distCm) {
  if (distCm < 50)        return FUZZY_NEAR;    // < 50cm
  else if (distCm < 100)  return FUZZY_MEDIUM;  // 50-100cm
  else                    return FUZZY_FAR;     // > 100cm
}

// REPLACE fungsi processAvoidance() dengan ini:
bool processAvoidance() {
  // Hanya scan jika sudah waktunya
  if (millis() - S.lastSonarRead < SONAR_INTERVAL) return S.isAvoiding;
  S.lastSonarRead = millis();

  // Baca kedua sensor
  int dL = readFilteredDist(TRIG_LEFT_PIN,  ECHO_LEFT_PIN,  leftBuf);
  int dR = readFilteredDist(TRIG_RIGHT_PIN, ECHO_RIGHT_PIN, rightBuf);
  bufIdx = (bufIdx + 1) % FILTER_SAMPLES;

  // Klasifikasi fuzzy
  FuzzyDistance fuzzyLeft  = classifyDistance(dL);
  FuzzyDistance fuzzyRight = classifyDistance(dR);

  // 5 Fuzzy Rules
  if (fuzzyLeft == FUZZY_NEAR && fuzzyRight == FUZZY_FAR) {
    // Rule 1: Kiri DEKAT + Kanan JAUH → Belok KANAN KERAS
    S.targetSpeed = AVOID_SPEED;
    setServoTarget(SERVO_MAX_RIGHT);
    S.isAvoiding = true;
    Serial.printf("[FUZZY] Rule 1: L:%dcm(%s) R:%dcm(%s) → KANAN KERAS\\n",
      dL, fuzzyDistToString(fuzzyLeft), dR, fuzzyDistToString(fuzzyRight));
    
  } else if (fuzzyLeft == FUZZY_FAR && fuzzyRight == FUZZY_NEAR) {
    // Rule 2: Kiri JAUH + Kanan DEKAT → Belok KIRI KERAS
    S.targetSpeed = AVOID_SPEED;
    setServoTarget(SERVO_MAX_LEFT);
    S.isAvoiding = true;
    Serial.printf("[FUZZY] Rule 2: L:%dcm(%s) R:%dcm(%s) → KIRI KERAS\\n",
      dL, fuzzyDistToString(fuzzyLeft), dR, fuzzyDistToString(fuzzyRight));
    
  } else if (fuzzyLeft == FUZZY_NEAR && fuzzyRight == FUZZY_NEAR) {
    // Rule 3: Kiri DEKAT + Kanan DEKAT → MUNDUR
    S.targetSpeed = -AVOID_SPEED;
    setServoTarget(SERVO_CENTER);
    S.isAvoiding = true;
    Serial.printf("[FUZZY] Rule 3: L:%dcm(%s) R:%dcm(%s) → MUNDUR\\n",
      dL, fuzzyDistToString(fuzzyLeft), dR, fuzzyDistToString(fuzzyRight));
    
  } else if (fuzzyLeft == FUZZY_MEDIUM && fuzzyRight == FUZZY_MEDIUM) {
    // Rule 4: Kiri SEDANG + Kanan SEDANG → PELAN
    S.targetSpeed = APPROACH_SPEED;
    setServoTarget(SERVO_CENTER);
    S.isAvoiding = true;
    Serial.printf("[FUZZY] Rule 4: L:%dcm(%s) R:%dcm(%s) → PELAN\\n",
      dL, fuzzyDistToString(fuzzyLeft), dR, fuzzyDistToString(fuzzyRight));
    
  } else if (fuzzyLeft == FUZZY_FAR && fuzzyRight == FUZZY_FAR) {
    // Rule 5: Kiri JAUH + Kanan JAUH → NORMAL
    S.isAvoiding = false;
    // Tidak ubah speed/servo
    
  } else {
    // Mixed case: pilih yang paling aman
    if (fuzzyLeft == FUZZY_NEAR || fuzzyRight == FUZZY_NEAR) {
      S.targetSpeed = -AVOID_SPEED;
      setServoTarget(SERVO_CENTER);
      S.isAvoiding = true;
      Serial.printf("[FUZZY] Mixed(NEAR): L:%dcm R:%dcm → MUNDUR\\n", dL, dR);
    } else {
      S.targetSpeed = APPROACH_SPEED;
      setServoTarget(SERVO_CENTER);
      S.isAvoiding = true;
      Serial.printf("[FUZZY] Mixed(MED): L:%dcm R:%dcm → PELAN\\n", dL, dR);
    }
  }

  cachedDistLeft  = dL;
  cachedDistRight = dR;
  S.smartMoveActive = S.isAvoiding;
  
  return S.isAvoiding;
}
```

---

## 📝 Cara Apply Patch

1. **Buka firmware v14.5 Anda di Arduino IDE**
2. **Apply PATCH 1** (GPS UBX Parser):
   - Copy-paste kode PATCH 1 setelah `#include` statements
   - Replace fungsi `sendUBXCmd()` dan `configureGPS()`
3. **Apply PATCH 2** (Ultrasonic Mode):
   - Cari fungsi `loop()`
   - Hapus `processAvoidance()` di luar switch
   - Replace switch statement
4. **Apply PATCH 3** (Fuzzy Logic):
   - Copy-paste enum dan fungsi klasifikasi
   - Replace fungsi `processAvoidance()`
5. **Compile & Upload**

---

## ✅ Verification

Setelah upload, cek Serial Monitor:

### GPS Lock:
```
[GPS] Testing connection...
[GPS] ✓ Module responding!
[UBX] 0x06/0x00: ACK ✓
[UBX] 0x06/0x24: ACK ✓
[UBX] 0x06/0x08: ACK ✓
```

### Manual Control (Ultrasonic OFF):
```
[JOY] Lurus | thr=0.80 | spd=204
(TIDAK ADA log [FUZZY])
```

### Autopilot (Ultrasonic ON):
```
[NAV] Navigasi ke WP 1/3
[FUZZY] Rule 1: L:35cm(DEKAT) R:150cm(JAUH) → KANAN KERAS
```

---

## 🆘 Jika Patch Gagal

Kirim ke saya:
1. Error message saat compile
2. Screenshot kode yang sudah di-patch
3. Versi Arduino IDE & ESP32 board support

Saya akan bantu debug!
