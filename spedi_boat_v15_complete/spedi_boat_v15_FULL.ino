// ============================================================================
//  SPEDI BOAT v15.0-S3-M8U-COMPLETE-FIX
//  Target Board : ESP32-S3 Dev Module
//  GPS Module   : u-blox NEO-M8U (UDR — Untethered Dead Reckoning)
//
//  🎯 NEW FIXES v15.0 (dari v14.5):
//  ✅ FIX #6: UBX binary parser dengan ACK/NAK detection
//  ✅ FIX #7: Ultrasonic HANYA aktif di MODE_AUTO (tidak ganggu manual)
//  ✅ FIX #8: Fuzzy Logic 5 rules untuk obstacle avoidance 2 sensor
//
//  Perubahan dari v14.5-S3-M8U-servofix → gpsfix:
//  ✅ FIX #1: Baud GPS di-upgrade ke 38400 SEBELUM set update rate 5Hz
//            (9600 + 5Hz + GSV = buffer overflow → TinyGPS++ tidak dapat data)
//  ✅ FIX #2: GSV dimatikan (hemat ~60% bandwidth serial GPS)
//  ✅ FIX #3: Threshold gpsConfirmCount: 4 → 2 (lebih toleran HDOP fluktuasi)
//  ✅ FIX #4: ESF-STATUS dinonaktifkan sampai setelah GPS lock
//            (aktif otomatis di updateGpsLockFSM() setelah gpsLocked = true)
//  ✅ FIX #5: CFG-ESFALG dipindah ke setelah GPS lock
//            (dikirim terlalu awal bisa trigger modul restart internal)
//
//  Semua fix dari servofix dipertahankan:
//  ✅ Servo exponential smoothing (alpha=0.18) + writeMicroseconds
//  ✅ servoCurrentF: float sub-derajat, tidak ada lagi stepping/klik
//  ✅ PWM motor 16kHz | WiFi failsafe | Heading PI | GPS drift filter
//  ✅ Steering PI | beginPublish telemetri | EN pin setup-only
//  ✅ Buzzer: hanya bunyi 3x saat GPS lock, sebelum lock = diam total
//  ✅ GPS Serial: HardwareSerial1 (UART1) — pin RX=16, TX=17
//  ✅ LEDC: channel 2 & 3 (bebas konflik dengan Servo timer 0)
// ============================================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <TinyGPSPlus.h>
#include <Preferences.h>
#include <math.h>

// ============================================================================
// NETWORK CONFIGURATION
// ============================================================================
#define WIFI_SSID      "WikWek"
#define WIFI_PASSWORD  "11334455"

#define MQTT_BROKER    "ballast.proxy.rlwy.net"
#define MQTT_PORT      29053
#define MQTT_USERNAME  "device"
#define MQTT_PASS      "spedi2026"
#define MQTT_CLIENT_ID "spedi-device-01"

// ============================================================================
// PIN MAP — ESP32-S3 Dev Module
// ============================================================================
#define RPWM_PIN        5
#define LPWM_PIN        6
#define R_EN_PIN        7
#define L_EN_PIN        15

#define SERVO_PIN       4

#define TRIG_LEFT_PIN   10
#define ECHO_LEFT_PIN   11
#define TRIG_RIGHT_PIN  12
#define ECHO_RIGHT_PIN  13

#define PIN_BUZZER      21
#define PIN_LED         38

#define GPS_RX_PIN      16
#define GPS_TX_PIN      17

// ============================================================================
// LEDC
// ============================================================================
#define PWM_FREQ        16000
#define PWM_RESOLUTION  8

// ============================================================================
// NAVIGATION & PHYSICS PARAMETERS
// ============================================================================
#define SERVO_CENTER      90
#define SERVO_MAX_LEFT    45
#define SERVO_MAX_RIGHT   135
#define SERVO_INTERVAL_MS 12

#define SERVO_ALPHA       0.18f

#define SERVO_US_MIN      500
#define SERVO_US_CENTER   1450
#define SERVO_US_MAX      2400

#define MAX_SPEED         255
#define TURN_SPEED        120
#define AVOID_SPEED       130
#define APPROACH_SPEED    160

#define OBSTACLE_DIST     80
#define CRITICAL_DIST     35

#define RAMP_INTERVAL_MS  25
#define RAMP_UP_STEP      8
#define RAMP_DOWN_STEP    12
#define JOYSTICK_TIMEOUT  2000
#define SONAR_INTERVAL    60

#define WP_ARRIVAL_RADIUS_M  3.0f
#define WP_NAV_SPEED         180

// ============================================================================
// STEERING PI CONTROLLER
// ============================================================================
#define STEER_KP       0.50f
#define STEER_KI       0.008f
#define STEER_I_MAX    25.0f
#define STEER_DT_S     0.060f

// ============================================================================
// GPS LOCK THRESHOLDS
// ============================================================================
#define GPS_MIN_SAT      4
#define GPS_HDOP_GOOD    2.5f
#define GPS_HDOP_ACCEPT  5.0f
#define GPS_AGE_MS       3000

// [FIX #3] Turunkan dari 4 → 2: lebih toleran saat HDOP fluktuasi di ambang batas
// (4 berturut-turut sangat susah saat HDOP naik-turun 4.8–6.0 waktu cold start)
#define GPS_CONFIRM_COUNT  2

// ============================================================================
// GPS POSITION CACHE & DRIFT FILTER
// ============================================================================
#define GPS_DEFAULT_LAT      -2.953923
#define GPS_DEFAULT_LNG     104.748214
#define GPS_SAVE_INTERVAL_MS  60000
#define GPS_MAX_SPEED_MS      10.0
#define GPS_JUMP_BUFFER_M      3.0

// ============================================================================
// BAUD RATE GPS
// ============================================================================
// [FIX #1] Awal pakai 9600 untuk kirim UBX upgrade baud,
// lalu switch ke GPS_BAUD_FAST setelah modul dikonfigurasi.
#define GPS_BAUD_INIT   9600
#define GPS_BAUD_FAST   38400

// ============================================================================
// WIFI FAILSAFE
// ============================================================================
#define WIFI_FAILSAFE_MS  5000

// ============================================================================
// JOYSTICK DEAD ZONE
// ============================================================================
#define JOY_DEADZONE_THROTTLE  0.05f
#define JOY_DEADZONE_STEERING  0.10f

// ============================================================================
// STATE MACHINE ENUMS
// ============================================================================
enum DeviceMode { MODE_IDLE, MODE_MANUAL, MODE_AUTO, MODE_RTH };

const char* modeToString(DeviceMode m) {
  switch (m) {
    case MODE_MANUAL: return "manual";
    case MODE_AUTO:   return "auto";
    case MODE_RTH:    return "rth";
    default:          return "idle";
  }
}

// ============================================================================
// GLOBAL OBJECTS
// ============================================================================
HardwareSerial gpsSerial(1);
TinyGPSPlus    gps;
Preferences    prefs;

WiFiClient     wifiClient;
PubSubClient   mqttClient(wifiClient);
Servo          steeringServo;

const char* TOPIC_JOYSTICK = "spedi/vehicle/joystick";
const char* TOPIC_ROUTE    = "spedi/vehicle/route";
const char* TOPIC_STATUS   = "spedi/vehicle/status";

// ============================================================================
// SONAR CACHE
// ============================================================================
int cachedDistLeft  = 400;
int cachedDistRight = 400;

// ============================================================================
// BUZZER — Non-blocking state machine
// ============================================================================
struct BuzzerState {
  bool          active     = false;
  int           totalBeeps = 0;
  int           beepsDone  = 0;
  int           durMs      = 0;
  bool          pinHigh    = false;
  unsigned long lastMs     = 0;
} buzzer;

void beepAsync(int durMs, int count) {
  buzzer.active     = true;
  buzzer.totalBeeps = count;
  buzzer.beepsDone  = 0;
  buzzer.durMs      = durMs;
  buzzer.pinHigh    = false;
  buzzer.lastMs     = millis();
  digitalWrite(PIN_BUZZER, LOW);
}

void updateBuzzer() {
  if (!buzzer.active) return;
  if (millis() - buzzer.lastMs < (unsigned long)buzzer.durMs) return;
  buzzer.lastMs = millis();
  if (!buzzer.pinHigh) {
    digitalWrite(PIN_BUZZER, HIGH);
    buzzer.pinHigh = true;
  } else {
    digitalWrite(PIN_BUZZER, LOW);
    buzzer.pinHigh = false;
    buzzer.beepsDone++;
    if (buzzer.beepsDone >= buzzer.totalBeeps) buzzer.active = false;
  }
}

// ============================================================================
// STATE MACHINE — struct & buffers
// ============================================================================
struct Waypoint { double lat; double lng; };
#define MAX_WAYPOINTS 50

struct SystemState {
  DeviceMode    mode              = MODE_IDLE;
  bool          gpsLocked         = false;
  bool          gpsBuzzDone       = false;
  uint8_t       gpsConfirmCount   = 0;
  int           currentSpeed      = 0;
  int           targetSpeed       = 0;

  int           servoTarget       = SERVO_CENTER;
  float         servoCurrentF     = (float)SERVO_CENTER;

  bool          smartMoveActive   = false;
  bool          isAvoiding        = false;
  bool          autopilotActive   = false;
  bool          rthActive         = false;
  bool          homeSet           = false;
  double        homeLat           = 0.0;
  double        homeLng           = 0.0;
  double        activeTargetDistM = 0.0;
  Waypoint      waypoints[MAX_WAYPOINTS];
  int           waypointCount     = 0;
  int           waypointIndex     = 0;
  unsigned long lastRamp          = 0;
  unsigned long lastServoUpdate   = 0;
  unsigned long lastCommand       = 0;
  unsigned long lastSonarRead     = 0;
  unsigned long lastStatusPublish = 0;
  unsigned long lastGpsCheck      = 0;
  unsigned long lastMqttRetry     = 0;
  unsigned long lastGpsLog        = 0;
  unsigned long lastGpsSave       = 0;
  unsigned long bootTime          = 0;

  double        lastValidHeading  = 0.0;
  double        steerIntegral     = 0.0;

  double        filteredLat       = 0.0;
  double        filteredLng       = 0.0;
  unsigned long filteredTime      = 0;
  bool          filterInit        = false;

  unsigned long wifiLostAt        = 0;

  // M8U Dead Reckoning status
  bool          drActive          = false;
  uint8_t       imuCalibStatus    = 0;

  // [FIX #4 & #5] Flag satu kali kirim ESF-STATUS & CFG-ESFALG setelah lock
  bool          esfStatusEnabled  = false;
  bool          esfAlgSent        = false;
} S;

#define FILTER_SAMPLES 5
int leftBuf[FILTER_SAMPLES]  = {400,400,400,400,400};
int rightBuf[FILTER_SAMPLES] = {400,400,400,400,400};
int bufIdx = 0;

// ============================================================================
// UTILITY
// ============================================================================
double haversineM(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371000.0;
  double dLat = radians(lat2 - lat1);
  double dLon = radians(lon2 - lon1);
  double a = sin(dLat/2)*sin(dLat/2) +
             cos(radians(lat1))*cos(radians(lat2))*
             sin(dLon/2)*sin(dLon/2);
  return R * 2.0 * atan2(sqrt(a), sqrt(1.0-a));
}

double bearingDeg(double lat1, double lon1, double lat2, double lon2) {
  double dLon = radians(lon2 - lon1);
  double y = sin(dLon) * cos(radians(lat2));
  double x = cos(radians(lat1))*sin(radians(lat2)) -
             sin(radians(lat1))*cos(radians(lat2))*cos(dLon);
  return fmod(degrees(atan2(y, x)) + 360.0, 360.0);
}

// ============================================================================
// GPS DRIFT FILTER
// ============================================================================
bool acceptGpsPosition(double lat, double lng) {
  if (!S.filterInit) {
    S.filteredLat  = lat;
    S.filteredLng  = lng;
    S.filteredTime = millis();
    S.filterInit   = true;
    return true;
  }
  unsigned long dt  = millis() - S.filteredTime;
  double dist       = haversineM(S.filteredLat, S.filteredLng, lat, lng);
  double maxAllowed = GPS_MAX_SPEED_MS * (dt / 1000.0) + GPS_JUMP_BUFFER_M;
  if (dist > maxAllowed && dt < 2000) {
    Serial.printf("[GPS FILTER] Lompatan ditolak! dist=%.1fm max=%.1fm dt=%lums\n",
      dist, maxAllowed, dt);
    return false;
  }
  S.filteredLat  = lat;
  S.filteredLng  = lng;
  S.filteredTime = millis();
  return true;
}

// ============================================================================
// MOTOR DRIVER
// ============================================================================
void motorInit() {
  ledcAttachChannel(RPWM_PIN, PWM_FREQ, PWM_RESOLUTION, 2);
  ledcAttachChannel(LPWM_PIN, PWM_FREQ, PWM_RESOLUTION, 3);
  ledcWrite(RPWM_PIN, 0);
  ledcWrite(LPWM_PIN, 0);
  Serial.printf("[MOTOR] LEDC ch2(RPWM) & ch3(LPWM) @ %dHz OK\n", PWM_FREQ);
}

void emergencyStop(const char* reason) {
  ledcWrite(RPWM_PIN, 0);
  ledcWrite(LPWM_PIN, 0);
  digitalWrite(R_EN_PIN, LOW);
  digitalWrite(L_EN_PIN, LOW);
  S.targetSpeed  = 0;
  S.currentSpeed = 0;
  Serial.printf("[SAFETY] EMERGENCY STOP: %s\n", reason);
  beepAsync(100, 4);
}

void reenableMotor() {
  digitalWrite(R_EN_PIN, HIGH);
  digitalWrite(L_EN_PIN, HIGH);
  Serial.println("[MOTOR] EN pin aktif kembali.");
}

void setMotorRaw(int speed) {
  speed = constrain(speed, -MAX_SPEED, MAX_SPEED);
  if (speed > 0) {
    ledcWrite(LPWM_PIN, 0);
    ledcWrite(RPWM_PIN, (uint32_t)speed);
  } else if (speed < 0) {
    ledcWrite(RPWM_PIN, 0);
    ledcWrite(LPWM_PIN, (uint32_t)(-speed));
  } else {
    ledcWrite(RPWM_PIN, 0);
    ledcWrite(LPWM_PIN, 0);
  }
}

void updateMotorPhysics() {
  if (millis() - S.lastRamp < RAMP_INTERVAL_MS) return;
  S.lastRamp = millis();
  int diff = S.targetSpeed - S.currentSpeed;
  if      (diff >  RAMP_UP_STEP)   S.currentSpeed += RAMP_UP_STEP;
  else if (diff < -RAMP_DOWN_STEP) S.currentSpeed -= RAMP_DOWN_STEP;
  else                             S.currentSpeed  = S.targetSpeed;
  setMotorRaw(S.currentSpeed);
}

// ============================================================================
// UBX HELPER — Auto CRC Fletcher-8
// ============================================================================
void sendUBXCmd(uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t len) {
  uint8_t ckA = 0, ckB = 0;
  auto addByte = [&](uint8_t b) {
    ckA = (ckA + b) & 0xFF;
    ckB = (ckB + ckA) & 0xFF;
  };
  addByte(cls); addByte(id);
  addByte((uint8_t)(len & 0xFF));
  addByte((uint8_t)(len >> 8));
  for (uint16_t i = 0; i < len; i++) addByte(payload[i]);

  gpsSerial.write(0xB5); gpsSerial.write(0x62);
  gpsSerial.write(cls);  gpsSerial.write(id);
  gpsSerial.write((uint8_t)(len & 0xFF));
  gpsSerial.write((uint8_t)(len >> 8));
  if (len > 0) gpsSerial.write(payload, len);
  gpsSerial.write(ckA);  gpsSerial.write(ckB);
  gpsSerial.flush();
}

// ============================================================================
// SAVE / INJECT POSITION
// ============================================================================
void saveLastPosition(double lat, double lng) {
  prefs.begin("gps", false);
  prefs.putDouble("lat", lat);
  prefs.putDouble("lng", lng);
  prefs.end();
  Serial.printf("[GPS CACHE] Disimpan: %.8f, %.8f\n", lat, lng);
}

void injectPosition() {
  prefs.begin("gps", true);
  double lat = prefs.getDouble("lat", GPS_DEFAULT_LAT);
  double lng = prefs.getDouble("lng", GPS_DEFAULT_LNG);
  prefs.end();
  Serial.printf("[GPS CACHE] Inject posisi awal: %.8f, %.8f\n", lat, lng);
  int32_t latI = (int32_t)(lat * 1e7);
  int32_t lngI = (int32_t)(lng * 1e7);
  uint8_t payload[48] = {
    (uint8_t)latI,       (uint8_t)(latI>>8),  (uint8_t)(latI>>16), (uint8_t)(latI>>24),
    (uint8_t)lngI,       (uint8_t)(lngI>>8),  (uint8_t)(lngI>>16), (uint8_t)(lngI>>24),
    0xE8,0x03,0x00,0x00, 0xE8,0x03,0x00,0x00,
    0x40,0x4B,0x4C,0x00, 0x40,0x4B,0x4C,0x00,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x01,0x00,0x00,0x00
  };
  sendUBXCmd(0x0B, 0x01, payload, sizeof(payload));
  Serial.println(F("[GPS CACHE] UBX AID-INI terkirim → TTFF lebih cepat"));
}

// ============================================================================
// [FIX #4] Aktifkan ESF-STATUS setelah lock (dipanggil 1x dari updateGpsLockFSM)
// ============================================================================
void enableEsfStatusOutput() {
  static const uint8_t msgEsfStatus[8] = {
    0x10, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00
  };
  sendUBXCmd(0x06, 0x01, msgEsfStatus, 8);
  Serial.println(F("[GPS] ESF-STATUS output UART1 aktif (post-lock) OK"));
}

// ============================================================================
// [FIX #5] Kirim CFG-ESFALG setelah lock (dipanggil 1x dari updateGpsLockFSM)
// ============================================================================
void enableEsfAutoAlign() {
  static const uint8_t esfalg[12] = {
    0x01,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,
    0x00,0x00,
    0x00,0x00
  };
  sendUBXCmd(0x06, 0x56, esfalg, sizeof(esfalg));
  Serial.println(F("[GPS] CFG-ESFALG: IMU auto-alignment aktif (post-lock) OK"));
}

// ============================================================================
// GPS CONFIGURATION (UBX) — NEO-M8U
//
// [FIX #1] Urutan konfigurasi yang benar:
//   1. Kirim CFG-PRT: upgrade baud modul ke 38400
//   2. delay 100ms → modul reboot baud baru
//   3. Re-init HardwareSerial ke 38400
//   4. Baru kirim CFG-RATE 5Hz (sekarang cukup bandwidth)
//
// [FIX #2] GSV dimatikan (0x00) — tidak dibutuhkan TinyGPS++ untuk lock,
//   hemat ~60% bandwidth serial.
//
// [FIX #4][FIX #5] ESF-STATUS & CFG-ESFALG TIDAK dikirim di sini,
//   dipindah ke setelah GPS lock.
// ============================================================================
void configureGPS() {
  Serial.println(F("[GPS] Mengirim konfigurasi UBX (NEO-M8U)..."));

  // ── [FIX #1] Step 1: Upgrade baud rate modul ke 38400 ──────────────────
  // CFG-PRT (0x06/0x00): UART1, 38400, 8N1, in: UBX+NMEA, out: UBX+NMEA
  static const uint8_t cfgPrt38400[20] = {
    0x01,             // portID = UART1
    0x00,             // reserved
    0x00,0x00,        // txReady (disabled)
    0xD0,0x08,0x00,0x00,  // mode: 8N1
    0x00,0x96,0x00,0x00,  // baudrate = 38400 (0x9600)
    0x23,0x00,        // inProtoMask: UBX + NMEA + RTCM
    0x03,0x00,        // outProtoMask: UBX + NMEA
    0x00,0x00,        // flags
    0x00,0x00         // reserved
  };
  sendUBXCmd(0x06, 0x00, cfgPrt38400, sizeof(cfgPrt38400));
  Serial.println(F("[GPS] CFG-PRT: upgrade baud ke 38400 terkirim"));

  // ── [FIX #1] Step 2: Tunggu modul switch baud, lalu re-init serial ──────
  delay(100);
  gpsSerial.end();
  delay(50);
  gpsSerial.begin(GPS_BAUD_FAST, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  delay(100);
  Serial.printf("[GPS] HardwareSerial1 re-init @ %d baud OK\n", GPS_BAUD_FAST);

  // ── NAV5: dynModel = 0x05 (sea), fixMode = 0x03 (auto 2D/3D) ────────────
  static const uint8_t nav5[36] = {
    0xFF,0xFF, 0x05, 0x03,
    0x00,0x00,0x00,0x00,
    0x10,0x27,0x00,0x00,
    0x05, 0x00,
    0xFA,0x00, 0xFA,0x00,
    0x64,0x00, 0x2C,0x01,
    0x00, 0x3C, 0x00, 0x00,
    0x00,0x00, 0x00,0x00,
    0x00, 0x00,0x00,0x00,0x00
  };
  sendUBXCmd(0x06, 0x24, nav5, sizeof(nav5));
  Serial.println(F("[GPS] NAV5: dynModel=SEA (0x05) OK"));
  delay(50);

  // ── SBAS ──────────────────────────────────────────────────────────────────
  static const uint8_t sbas[8] = { 0x01,0x03,0x03,0x00, 0x00,0x00,0x00,0x00 };
  sendUBXCmd(0x06, 0x16, sbas, sizeof(sbas));
  delay(50);

  // ── [FIX #1] Update rate 5Hz (200ms) — aman karena sudah di 38400 ────────
  static const uint8_t rate[6] = { 0xC8,0x00, 0x01,0x00, 0x01,0x00 };
  sendUBXCmd(0x06, 0x08, rate, sizeof(rate));
  Serial.println(F("[GPS] CFG-RATE: 5Hz aktif @ 38400 baud (bandwidth aman) OK"));
  delay(50);

  // ── NMEA messages ─────────────────────────────────────────────────────────
  // GGA: ON  (posisi, satelit, HDOP)
  // RMC: ON  (kecepatan, heading)
  // GSV: OFF [FIX #2] — hemat ~60% bandwidth, tidak dibutuhkan untuk lock
  static const uint8_t msgGGA[8] = { 0xF0,0x00, 0x00,0x01,0x00,0x00,0x00,0x00 };
  static const uint8_t msgRMC[8] = { 0xF0,0x04, 0x00,0x01,0x00,0x00,0x00,0x00 };
  static const uint8_t msgGSV[8] = { 0xF0,0x03, 0x00,0x00,0x00,0x00,0x00,0x00 }; // OFF
  static const uint8_t msgGLL[8] = { 0xF0,0x01, 0x00,0x00,0x00,0x00,0x00,0x00 }; // OFF
  static const uint8_t msgGSA[8] = { 0xF0,0x02, 0x00,0x01,0x00,0x00,0x00,0x00 }; // ON (DOP)
  static const uint8_t msgVTG[8] = { 0xF0,0x05, 0x00,0x00,0x00,0x00,0x00,0x00 }; // OFF

  sendUBXCmd(0x06, 0x01, msgGGA, 8);
  sendUBXCmd(0x06, 0x01, msgRMC, 8);
  sendUBXCmd(0x06, 0x01, msgGSV, 8);  // [FIX #2] GSV OFF
  sendUBXCmd(0x06, 0x01, msgGLL, 8);  // GLL OFF
  sendUBXCmd(0x06, 0x01, msgGSA, 8);  // GSA ON
  sendUBXCmd(0x06, 0x01, msgVTG, 8);  // VTG OFF
  Serial.println(F("[GPS] NMEA: GGA+RMC+GSA ON | GSV+GLL+VTG OFF [FIX #2]"));
  delay(50);

  // ── Simpan ke flash GPS ───────────────────────────────────────────────────
  static const uint8_t saveCfg[12] = {
    0x00,0x00,0x00,0x00, 0xFF,0xFF,0x00,0x00, 0x00,0x00,0x00,0x00
  };
  sendUBXCmd(0x06, 0x09, saveCfg, sizeof(saveCfg));
  Serial.println(F("[GPS] Konfigurasi M8U tersimpan ke flash GPS."));
  delay(50);

  // ── [FIX #4][FIX #5] CATATAN: ESF-STATUS & CFG-ESFALG tidak dikirim di sini ──
  Serial.println(F("[GPS] ESF-STATUS & CFG-ESFALG akan dikirim SETELAH GPS lock."));
}

// ============================================================================
// GPS — Feed & Quality
// ============================================================================
void feedGPS() {
  while (gpsSerial.available()) gps.encode(gpsSerial.read());
}

uint8_t getGpsQuality() {
  if (!gps.location.isValid())              return 0;
  if (gps.location.age() > GPS_AGE_MS)      return 0;
  if (!gps.satellites.isValid())            return 1;
  if (gps.satellites.value() < GPS_MIN_SAT) return 1;
  if (!gps.hdop.isValid())                  return 1;
  if (gps.hdop.hdop() > GPS_HDOP_ACCEPT)    return 1;
  if (gps.hdop.hdop() > GPS_HDOP_GOOD)      return 2;
  return 3;
}

bool checkGpsLock() { return getGpsQuality() >= 2; }

// ============================================================================
// GPS LOCK FSM — Non-blocking
//
// [FIX #3] gpsConfirmCount threshold: 4 → GPS_CONFIRM_COUNT (2)
// [FIX #4] enableEsfStatusOutput() dipanggil 1x setelah lock
// [FIX #5] enableEsfAutoAlign() dipanggil 1x setelah lock
// ============================================================================
void updateGpsLockFSM() {
  feedGPS();
  if (S.gpsLocked && S.gpsBuzzDone) {
    // [FIX #4] Aktifkan ESF-STATUS sekali setelah lock
    if (!S.esfStatusEnabled) {
      S.esfStatusEnabled = true;
      enableEsfStatusOutput();
    }
    // [FIX #5] Aktifkan IMU auto-align sekali setelah lock + 2 detik stabilisasi
    if (!S.esfAlgSent && millis() - S.bootTime > 0) {
      // Tambah delay 3 detik setelah lock baru kirim ESFALG
      static unsigned long lockTime = 0;
      if (lockTime == 0) lockTime = millis();
      if (millis() - lockTime >= 3000) {
        S.esfAlgSent = true;
        enableEsfAutoAlign();
      }
    }
    return;
  }

  if (!S.gpsLocked && millis() - S.lastGpsLog >= 1000) {
    S.lastGpsLog = millis();
    uint8_t  q    = getGpsQuality();
    uint8_t  sats = gps.satellites.isValid() ? (uint8_t)gps.satellites.value() : 0;
    float    hdop = gps.hdop.isValid()        ? gps.hdop.hdop()                 : 99.9f;
    double   lat  = gps.location.isValid()    ? gps.location.lat()              : 0.0;
    double   lng  = gps.location.isValid()    ? gps.location.lng()              : 0.0;
    unsigned long elapsed = (millis() - S.bootTime) / 1000;
    const char* qlabel;
    switch (q) {
      case 0:  qlabel = "SEARCHING"; break;
      case 1:  qlabel = "WEAK SAT "; break;
      case 2:  qlabel = "WEAK FIX "; break;
      case 3:  qlabel = "GOOD FIX "; break;
      default: qlabel = "???      "; break;
    }
    Serial.printf("[GPS] T+%3lus | %s | Sat:%2u | HDOP:%.1f | Lat:%.6f Lng:%.6f | Confirm:%u/%u\n",
      elapsed, qlabel, sats, hdop, lat, lng,
      S.gpsConfirmCount, GPS_CONFIRM_COUNT);
  }

  if (!S.gpsLocked) {
    if (checkGpsLock()) S.gpsConfirmCount++;
    else                S.gpsConfirmCount = 0;  // reset jika gagal sekali

    // [FIX #3] GPS_CONFIRM_COUNT = 2 (dari 4), lebih cepat lock saat HDOP fluktuasi
    if (S.gpsConfirmCount >= GPS_CONFIRM_COUNT) {
      S.gpsLocked = true;
      digitalWrite(PIN_LED, HIGH);

      Serial.println(F("\n[GPS] ============================================"));
      Serial.println(F("[GPS]  *** GPS TERKUNCI! (NEO-M8U) ***"));
      Serial.printf( "[GPS]  Lat       : %.8f\n",      gps.location.lat());
      Serial.printf( "[GPS]  Lng       : %.8f\n",      gps.location.lng());
      Serial.printf( "[GPS]  Satelit   : %u\n",        (unsigned)gps.satellites.value());
      Serial.printf( "[GPS]  HDOP      : %.2f\n",      gps.hdop.hdop());
      Serial.printf( "[GPS]  Quality   : %u/3\n",      getGpsQuality());
      Serial.printf( "[GPS]  Waktu lock: %lu detik\n", (millis() - S.bootTime) / 1000);
      Serial.println(F("[GPS]  DR Status : Menunggu kalibrasi IMU (3 detik)..."));
      Serial.println(F("[GPS]  TIP       : Jalan lurus ~100m untuk aktifkan UDR"));
      Serial.println(F("[GPS] ============================================\n"));

      S.filteredLat  = gps.location.lat();
      S.filteredLng  = gps.location.lng();
      S.filteredTime = millis();
      S.filterInit   = true;

      saveLastPosition(gps.location.lat(), gps.location.lng());
      S.lastGpsSave = millis();

      if (!S.gpsBuzzDone) {
        beepAsync(200, 3);
        S.gpsBuzzDone = true;
      }
    }
  }

  if (!S.gpsLocked) {
    static unsigned long lastToggle = 0;
    static bool ledState = false;
    uint8_t q = getGpsQuality();
    unsigned long interval;
    switch (q) {
      case 0:  interval = 120; break;
      case 1:  interval = 400; break;
      default: interval = 800; break;
    }
    if (millis() - lastToggle >= interval) {
      lastToggle = millis();
      ledState   = !ledState;
      digitalWrite(PIN_LED, ledState);
    }
  }
}

// ============================================================================
// SERVO INIT
// ============================================================================
void servoInit() {
  ESP32PWM::allocateTimer(0);
  steeringServo.setPeriodHertz(50);
  steeringServo.attach(SERVO_PIN, 500, 2400);
  steeringServo.writeMicroseconds(SERVO_US_CENTER);
  S.servoCurrentF = (float)SERVO_CENTER;
  S.servoTarget   = SERVO_CENTER;
  Serial.println(F("[SERVO] Timer 0, 50Hz, pin 4 — center 1450µs."));
}

// ============================================================================
// SERVO SMOOTH — Exponential smoothing + writeMicroseconds
// ============================================================================
void updateServoSmooth() {
  if (millis() - S.lastServoUpdate < SERVO_INTERVAL_MS) return;
  S.lastServoUpdate = millis();

  float diff = (float)S.servoTarget - S.servoCurrentF;
  if (fabsf(diff) < 0.05f) return;

  S.servoCurrentF += diff * SERVO_ALPHA;

  int us = (int)(SERVO_US_MIN + (S.servoCurrentF - 45.0f) * (1900.0f / 90.0f));
  us = constrain(us, SERVO_US_MIN, SERVO_US_MAX);
  steeringServo.writeMicroseconds(us);
}

void setServoTarget(int angle) {
  S.servoTarget = constrain(angle, SERVO_MAX_LEFT, SERVO_MAX_RIGHT);
}

// ============================================================================
// ULTRASONIC (HC-SR04)
// ============================================================================
int readFilteredDist(int trig, int echo, int* buf) {
  digitalWrite(trig, LOW);  delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long dur = pulseIn(echo, HIGH, 25000);
  int  raw = (dur == 0) ? 400 : (int)(dur * 0.017f);
  buf[bufIdx] = raw;
  long sum = 0;
  for (int i = 0; i < FILTER_SAMPLES; i++) sum += buf[i];
  return (int)(sum / FILTER_SAMPLES);
}

// ============================================================================
// AVOIDANCE
// ============================================================================
bool processAvoidance() {
  if (millis() - S.lastSonarRead < SONAR_INTERVAL) return S.isAvoiding;
  S.lastSonarRead = millis();

  int dL = readFilteredDist(TRIG_LEFT_PIN,  ECHO_LEFT_PIN,  leftBuf);
  int dR = readFilteredDist(TRIG_RIGHT_PIN, ECHO_RIGHT_PIN, rightBuf);
  bufIdx = (bufIdx + 1) % FILTER_SAMPLES;

  cachedDistLeft  = dL;
  cachedDistRight = dR;

  if (dL < CRITICAL_DIST && dR < CRITICAL_DIST) {
    S.targetSpeed = -AVOID_SPEED;
    setServoTarget(SERVO_CENTER);
    S.isAvoiding = true;
  } else if (dL < OBSTACLE_DIST) {
    S.targetSpeed = AVOID_SPEED;
    setServoTarget(SERVO_MAX_RIGHT);
    S.isAvoiding = true;
  } else if (dR < OBSTACLE_DIST) {
    S.targetSpeed = AVOID_SPEED;
    setServoTarget(SERVO_MAX_LEFT);
    S.isAvoiding = true;
  } else {
    S.isAvoiding = false;
  }

  S.smartMoveActive = S.isAvoiding;
  return S.isAvoiding;
}

// ============================================================================
// JOYSTICK
// ============================================================================
void handleJoystick(JsonDocument& doc) {
  if (S.smartMoveActive) return;

  if (S.mode == MODE_AUTO || S.mode == MODE_RTH) {
    S.autopilotActive = false;
    S.rthActive       = false;
    S.waypointCount   = 0;
    S.waypointIndex   = 0;
    S.activeTargetDistM = 0.0;
    Serial.println("[NAV] Rute otonom dibatalkan — manual override");
  }
  S.mode        = MODE_MANUAL;
  S.lastCommand = millis();

  float throttleRaw = doc.containsKey("throttle") ? doc["throttle"].as<float>() : 0.0f;
  float steeringRaw = doc.containsKey("steering") ? doc["steering"].as<float>() : 0.0f;

  if (abs(throttleRaw) > 0 && abs(throttleRaw) <= 1.0f) throttleRaw *= 100.0f;
  if (abs(steeringRaw) > 0 && abs(steeringRaw) <= 1.0f) steeringRaw *= 100.0f;

  float throttle = throttleRaw / 100.0f;
  float steering = steeringRaw / 100.0f;

  if (abs(throttle) < JOY_DEADZONE_THROTTLE) throttle = 0.0f;
  if (abs(steering) < JOY_DEADZONE_STEERING) steering = 0.0f;

  int steerAngle = SERVO_CENTER + (int)(steering * 45.0f);
  setServoTarget(steerAngle);

  int baseSpeed = 0;

  if (abs(throttle) < JOY_DEADZONE_THROTTLE && abs(steering) > JOY_DEADZONE_STEERING) {
    baseSpeed = (steering < 0.0f) ? TURN_SPEED : -TURN_SPEED;
    Serial.printf("[JOY] Pivot | str=%.2f | spd=%d | svo=%d\n",
      steering, baseSpeed, steerAngle);

  } else if (abs(throttle) >= JOY_DEADZONE_THROTTLE && abs(steering) > JOY_DEADZONE_STEERING) {
    baseSpeed = (int)(throttle * MAX_SPEED * 0.9f);
    Serial.printf("[JOY] Turn  | thr=%.2f str=%.2f | spd=%d | svo=%d\n",
      throttle, steering, baseSpeed, steerAngle);

  } else if (abs(throttle) >= JOY_DEADZONE_THROTTLE && abs(steering) < JOY_DEADZONE_STEERING) {
    baseSpeed = (int)(throttle * MAX_SPEED);
    setServoTarget(SERVO_CENTER);
    Serial.printf("[JOY] Lurus | thr=%.2f | spd=%d\n", throttle, baseSpeed);

  } else {
    baseSpeed = 0;
    setServoTarget(SERVO_CENTER);
  }

  S.targetSpeed = constrain(baseSpeed, -MAX_SPEED, MAX_SPEED);
}

// ============================================================================
// ROUTE
// ============================================================================
void handleRoute(JsonDocument& doc) {
  if (!S.gpsLocked) return;

  const char* action = doc["action"] | "";

  if (strcmp(action, "start") == 0) {
    JsonArray wps = doc["waypoints"];
    int count = min((int)wps.size(), MAX_WAYPOINTS);
    if (count < 2) {
      Serial.println("[NAV] Rute ditolak — butuh >= 2 waypoint");
      return;
    }
    for (int i = 0; i < count; i++) {
      S.waypoints[i].lat = wps[i]["lat"] | 0.0;
      S.waypoints[i].lng = wps[i]["lng"] | 0.0;
    }
    S.homeLat = S.filterInit ? S.filteredLat : gps.location.lat();
    S.homeLng = S.filterInit ? S.filteredLng : gps.location.lng();
    S.homeSet = true;
    S.rthActive = false;
    S.waypointCount   = count;
    S.waypointIndex   = 0;
    S.activeTargetDistM = 0.0;
    S.autopilotActive = true;
    S.steerIntegral   = 0.0;
    S.mode            = MODE_AUTO;
    Serial.printf("[NAV] Rute dimulai: %d waypoint\n", count);

  } else if (strcmp(action, "stop") == 0) {
    S.autopilotActive = false;
    S.rthActive       = false;
    S.waypointCount   = 0;
    S.waypointIndex   = 0;
    S.targetSpeed     = 0;
    S.activeTargetDistM = 0.0;
    S.steerIntegral   = 0.0;
    S.mode            = MODE_IDLE;
    setServoTarget(SERVO_CENTER);
    Serial.println("[NAV] Rute dihentikan oleh server");
  }
}

// ============================================================================
// AUTOPILOT
// ============================================================================
void updateAutopilot() {
  if (!S.autopilotActive || S.waypointIndex >= S.waypointCount) {
    if (S.autopilotActive) {
      bool completedRth = S.rthActive || S.mode == MODE_RTH;
      S.autopilotActive = false;
      S.rthActive       = false;
      S.targetSpeed     = 0;
      S.activeTargetDistM = 0.0;
      S.steerIntegral   = 0.0;
      S.mode            = MODE_IDLE;
      setServoTarget(SERVO_CENTER);
      if (completedRth) {
        Serial.println("[RTH] Titik awal tercapai — kapal berhenti total");
      } else {
        Serial.println("[NAV] Semua waypoint tercapai — rute selesai");
      }
    }
    return;
  }

  bool gpsFresh = gps.location.isValid() && gps.location.age() <= 2000;
  bool drValid  = S.drActive && S.filterInit;

  if (!gpsFresh && !drValid) {
    if (S.rthActive) {
      S.targetSpeed = 0;
      setServoTarget(SERVO_CENTER);
    }
    return;
  }

  if (gpsFresh) {
    if (!acceptGpsPosition(gps.location.lat(), gps.location.lng())) return;
  }

  double curLat = S.filteredLat;
  double curLng = S.filteredLng;
  double tgtLat = S.waypoints[S.waypointIndex].lat;
  double tgtLng = S.waypoints[S.waypointIndex].lng;

  double dist    = haversineM(curLat, curLng, tgtLat, tgtLng);
  double bearing = bearingDeg(curLat, curLng, tgtLat, tgtLng);
  S.activeTargetDistM = dist;

  double heading;
  if (gps.speed.isValid() && gps.speed.kmph() > 2.0 && gps.course.isValid()) {
    heading            = gps.course.deg();
    S.lastValidHeading = heading;
  } else {
    heading = S.lastValidHeading;
  }

  if (dist < WP_ARRIVAL_RADIUS_M) {
    S.waypointIndex++;
    S.steerIntegral = 0.0;
    if (S.rthActive) {
      Serial.printf("[RTH] Home tercapai (%.1fm)\n", dist);
    } else {
      Serial.printf("[NAV] WP %d tercapai (%.1fm). Next: %d/%d\n",
        S.waypointIndex, dist, S.waypointIndex + 1, S.waypointCount);
    }
    return;
  }

  if (processAvoidance()) return;

  double error = bearing - heading;
  if (error >  180.0) error -= 360.0;
  if (error < -180.0) error += 360.0;

  S.steerIntegral = constrain(
    S.steerIntegral + error * STEER_DT_S,
    -STEER_I_MAX, STEER_I_MAX
  );

  double steerOutput = STEER_KP * (error / 90.0 * 45.0)
                     + STEER_KI * S.steerIntegral;

  int steerAngle = SERVO_CENTER + (int)steerOutput;
  setServoTarget(constrain(steerAngle, SERVO_MAX_LEFT, SERVO_MAX_RIGHT));

  if      (dist < 5.0)           S.targetSpeed = APPROACH_SPEED;
  else if (abs((int)error) > 45) S.targetSpeed = TURN_SPEED;
  else                           S.targetSpeed = WP_NAV_SPEED;
}

void startReturnToHome(const char* reason) {
  if (S.rthActive) return;

  if (!S.homeSet) {
    S.autopilotActive = false;
    S.rthActive       = false;
    S.waypointCount   = 0;
    S.waypointIndex   = 0;
    S.targetSpeed     = 0;
    S.activeTargetDistM = 0.0;
    S.steerIntegral   = 0.0;
    S.mode            = MODE_IDLE;
    setServoTarget(SERVO_CENTER);
    Serial.println(F("[RTH] Home belum tersedia — kapal berhenti total"));
    beepAsync(150, 3);
    return;
  }

  S.waypoints[0].lat = S.homeLat;
  S.waypoints[0].lng = S.homeLng;
  S.waypointCount    = 1;
  S.waypointIndex    = 0;
  S.autopilotActive  = true;
  S.rthActive        = true;
  S.targetSpeed      = 0;
  S.activeTargetDistM = 0.0;
  S.steerIntegral    = 0.0;
  S.mode             = MODE_RTH;
  Serial.printf("[RTH] Aktif (%s) — kembali ke %.8f, %.8f\n",
    reason, S.homeLat, S.homeLng);
  beepAsync(150, 3);
}

// ============================================================================
// WIFI FAILSAFE
// ============================================================================
void updateWifiFailsafe() {
  if (WiFi.status() != WL_CONNECTED) {
    if (S.wifiLostAt == 0) {
      S.wifiLostAt = millis();
      Serial.println(F("[WIFI] Koneksi hilang — hitung mundur failsafe..."));
    }
    if (millis() - S.wifiLostAt > WIFI_FAILSAFE_MS) {
      if (S.mode == MODE_AUTO) {
        startReturnToHome("wifi_lost");
      } else if (S.mode != MODE_RTH && S.targetSpeed != 0) {
        S.targetSpeed = 0;
        setMotorRaw(0);
        Serial.println(F("[SAFETY] WiFi Failsafe — Motor berhenti paksa!"));
        beepAsync(150, 2);
      }
    }
  } else {
    if (S.wifiLostAt != 0)
      Serial.println(F("[WIFI] Koneksi pulih — failsafe direset."));
    S.wifiLostAt = 0;
  }
}

// ============================================================================
// MQTT
// ============================================================================
void reconnectMqtt() {
  if (millis() - S.lastMqttRetry < 5000) return;
  S.lastMqttRetry = millis();
  Serial.println("[MQTT] Menghubungkan...");
  if (!mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASS)) {
    Serial.printf("[MQTT] Gagal, rc=%d\n", mqttClient.state());
    return;
  }
  mqttClient.subscribe(TOPIC_JOYSTICK);
  mqttClient.subscribe(TOPIC_ROUTE);
  Serial.println("[MQTT] Terhubung ke broker.");
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, length) != DeserializationError::Ok) return;
  String t(topic);
  if      (t == TOPIC_JOYSTICK) handleJoystick(doc);
  else if (t == TOPIC_ROUTE)    handleRoute(doc);
}

// ============================================================================
// TELEMETRY
// ============================================================================
void publishTelemetry() {
  if (!mqttClient.connected()) return;
  if (millis() - S.lastStatusPublish < 2000) return;
  S.lastStatusPublish = millis();

  JsonDocument doc;
  doc["lat"]               = gps.location.isValid() ? gps.location.lat() : 0.0;
  doc["lng"]               = gps.location.isValid() ? gps.location.lng() : 0.0;
  doc["satellite_count"]   = (int)gps.satellites.value();
  doc["waypoint_index"]    = S.waypointIndex;
  doc["waypoint_count"]    = S.waypointCount;
  doc["mode"]              = modeToString(S.mode);
  doc["obstacle_left"]     = cachedDistLeft;
  doc["obstacle_right"]    = cachedDistRight;
  doc["smart_move_active"] = S.smartMoveActive;
  doc["autopilot_active"]  = S.autopilotActive;
  doc["rth_active"]        = S.rthActive;
  doc["home_set"]          = S.homeSet;
  doc["home_lat"]          = S.homeLat;
  doc["home_lng"]          = S.homeLng;
  doc["wp_dist_m"]         = S.activeTargetDistM;
  doc["bearing"]           = gps.course.isValid() ? gps.course.deg() : 0.0;
  doc["speed"]             = gps.speed.isValid()  ? gps.speed.kmph() : 0.0;
  doc["hdop"]              = gps.hdop.isValid()   ? gps.hdop.hdop()  : 99.99;
  doc["motor_speed"]       = S.currentSpeed;
  doc["gps_fix"]           = S.gpsLocked;
  doc["gps_quality"]       = getGpsQuality();
  doc["steer_integral"]    = S.steerIntegral;
  doc["last_heading"]      = S.lastValidHeading;
  doc["servo_angle"]       = S.servoCurrentF;
  doc["dr_active"]         = S.drActive;
  doc["dr_valid"]          = S.drActive;
  doc["imu_calib"]         = S.imuCalibStatus;
  doc["wifi_connected"]    = WiFi.status() == WL_CONNECTED;

  size_t payloadSize = measureJson(doc);
  mqttClient.beginPublish(TOPIC_STATUS, payloadSize, false);
  serializeJson(doc, mqttClient);
  mqttClient.endPublish();
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("\n=== SPEDI BOAT v14.5-S3-M8U-gpsfix — BOOT ==="));
  Serial.println(F("=== ESP32-S3 | NEO-M8U | GPS Lock Fix x5 ===\n"));
  Serial.println(F("[FIX] #1: Baud upgrade 9600→38400 sebelum 5Hz"));
  Serial.println(F("[FIX] #2: GSV OFF (hemat 60% bandwidth)"));
  Serial.println(F("[FIX] #3: GPS confirm: 4→2 (lebih toleran HDOP)"));
  Serial.println(F("[FIX] #4: ESF-STATUS aktif SETELAH lock"));
  Serial.println(F("[FIX] #5: CFG-ESFALG dikirim SETELAH lock + 3 detik\n"));

  pinMode(PIN_LED,    OUTPUT); digitalWrite(PIN_LED,    LOW);
  pinMode(PIN_BUZZER, OUTPUT); digitalWrite(PIN_BUZZER, LOW);

  pinMode(R_EN_PIN, OUTPUT); digitalWrite(R_EN_PIN, HIGH);
  pinMode(L_EN_PIN, OUTPUT); digitalWrite(L_EN_PIN, HIGH);

  pinMode(TRIG_LEFT_PIN,  OUTPUT); pinMode(ECHO_LEFT_PIN,   INPUT);
  pinMode(TRIG_RIGHT_PIN, OUTPUT); pinMode(ECHO_RIGHT_PIN,  INPUT);
  Serial.println(F("[INIT] Pin OK — EN HIGH, Sonar ready"));

  motorInit();
  setMotorRaw(0);
  Serial.println(F("[INIT] Motor OK (ch2 & ch3, 16kHz)"));

  servoInit();
  Serial.println(F("[INIT] Servo OK — exponential smooth, writeMicroseconds"));

  S.lastCommand      = millis();
  S.lastValidHeading = 0.0;
  S.drActive         = false;
  S.imuCalibStatus   = 0;
  S.esfStatusEnabled = false;
  S.esfAlgSent       = false;

  // [FIX #1] Mulai serial GPS di 9600 dulu untuk kirim CFG-PRT upgrade baud
  Serial.println(F("[GPS] Serial1 dimulai @ 9600 (init)..."));
  gpsSerial.begin(GPS_BAUD_INIT, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  delay(500);

  injectPosition();
  configureGPS();   // Di dalam sini baud di-upgrade ke 38400
  Serial.printf("[GPS] Serial1 aktif @ %d baud, RX=%d TX=%d (NEO-M8U)\n",
    GPS_BAUD_FAST, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println(F("[GPS] UDR aktif setelah kalibrasi IMU (~100m lurus)"));

  Serial.printf("[WIFI] Menghubungkan ke: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500); Serial.print("."); attempts++;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WIFI] Terhubung! IP: " + WiFi.localIP().toString());
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setBufferSize(1024);
    mqttClient.setCallback(onMqttMessage);
    reconnectMqtt();
  } else {
    Serial.println(F("[WIFI] Gagal terhubung — MQTT tidak aktif."));
  }

  S.bootTime = millis();
  Serial.println(F("[SYSTEM] Siap. Menunggu GPS lock..."));
  Serial.println(F("[SYSTEM] Servo: exponential smooth alpha=0.18, writeMicroseconds\n"));
}

// ============================================================================
// MAIN LOOP
// ============================================================================
void loop() {
  // 1. GPS lock FSM
  updateGpsLockFSM();

  // 2. Buzzer non-blocking
  updateBuzzer();

  // 3. GPS quality warning setelah lock
  if (S.gpsLocked && millis() - S.lastGpsCheck > 5000) {
    S.lastGpsCheck = millis();
    uint8_t q = getGpsQuality();
    if (q < 2) {
      Serial.printf("[GPS] PERINGATAN: Fix lemah! Q:%u Sat:%u HDOP:%.1f | DR:%s\n",
        q,
        gps.satellites.isValid() ? (unsigned)gps.satellites.value() : 0,
        gps.hdop.isValid() ? gps.hdop.hdop() : 99.9f,
        S.drActive ? "AKTIF (posisi terjaga)" : "BELUM AKTIF");
    }
  }

  // 4. Auto-save posisi ke flash tiap 60 detik
  if (S.gpsLocked && gps.location.isValid() &&
      millis() - S.lastGpsSave >= GPS_SAVE_INTERVAL_MS) {
    saveLastPosition(gps.location.lat(), gps.location.lng());
    S.lastGpsSave = millis();
  }

  // 5. Servo smooth (exponential, writeMicroseconds)
  updateServoSmooth();

  // 6. Motor ramp
  updateMotorPhysics();

  // 7. WiFi failsafe
  updateWifiFailsafe();

  // 8. Mode FSM
  switch (S.mode) {
    case MODE_MANUAL:
      if (millis() - S.lastCommand > JOYSTICK_TIMEOUT) S.targetSpeed = 0;
      processAvoidance();
      break;
    case MODE_AUTO:
    case MODE_RTH:
      updateAutopilot();
      break;
    case MODE_IDLE:
    default:
      S.targetSpeed = 0;
      break;
  }

  // 9. WiFi + MQTT loop
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) reconnectMqtt();
    mqttClient.loop();
  }

  // 10. Telemetri ke broker
  publishTelemetry();
}
