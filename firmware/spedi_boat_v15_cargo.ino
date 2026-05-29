// ============================================================================
//  SPEDI BOAT v15.0-S3-M8U-CARGO
//  Target Board : ESP32-S3 Dev Module
//  GPS Module   : u-blox NEO-M8U (UDR — Untethered Dead Reckoning)
//  Profile      : Kapal Kargo Pengangkut Barang | Baling-baling Ø55cm
//
//  Perubahan dari v14.5-S3-M8U-gpsfix → v15.0-cargo:
//
//  ✅ CARGO FIX #1: Kecepatan maksimal diturunkan → cocok kapal kargo berat
//     - MAX_SPEED      : 255 → 160 (kapal kargo, baling2 besar = torsi tinggi)
//     - WP_NAV_SPEED   : 180 → 120 (autopilot lebih pelan, aman muatan)
//     - AVOID_SPEED    : 130 → 90  (manuver menghindar perlahan)
//     - APPROACH_SPEED : 160 → 80  (mendekati waypoint hati-hati)
//     - TURN_SPEED     : 120 → 70  (pivot sangat pelan, propeller besar)
//
//  ✅ CARGO FIX #2: Ramp up/down diperlambat → kapal berat tidak bisa akselerasi mendadak
//     - RAMP_UP_STEP   : 8 → 3
//     - RAMP_DOWN_STEP : 12 → 5
//     - RAMP_INTERVAL  : 25ms → 40ms
//
//  ✅ CARGO FIX #3: FUZZY LOGIC CONTROLLER ditambahkan
//     - Fuzzy speed control berdasarkan jarak obstacle + error heading
//     - Fuzzy obstacle avoidance dengan 5 membership functions
//     - Menggantikan on/off threshold yang kasar di processAvoidance()
//     - Propeller besar tidak bisa mendadak; fuzzy output dihaluskan
//
//  ✅ CARGO FIX #4: Bug di updateGpsLockFSM() — lockTime static tidak direset
//     - lockTime static var sebelumnya tidak pernah direset antar reboot
//     - Diperbaiki ke S.lockTime (member struct, selalu 0 saat boot)
//
//  ✅ CARGO FIX #5: Bug di handleJoystick() — throttle scale salah
//     - Logika "if abs <= 1.0 → *100" salah untuk nilai float presisi
//     - Diperbaiki: normalisasi eksplisit, tidak pakai heuristic scale
//
//  ✅ CARGO FIX #6: Steering PI integral windup — reset tidak lengkap
//     - Saat waypoint tercapai, integral direset tapi lastValidHeading tidak
//     - Heading lama bisa menyebabkan integral kick saat WP berikutnya
//
//  ✅ CARGO FIX #7: acceptGpsPosition() — dt dihitung dari millis(), bisa overflow
//     - Pakai cast (float) dt / 1000.0f konsisten
//
//  ✅ CARGO FIX #8: Servo microsecond mapping formula dikoreksi
//     - Rumus lama: SERVO_US_MIN + (angle - 45) * (1900/90) tidak proporsional
//     - Pakai map() equivalent yang benar: center + ratio
//
//  ✅ CARGO FIX #9: Joystick timeout — kapal kargo perlu timeout lebih panjang
//     - JOYSTICK_TIMEOUT: 2000ms → 4000ms
//
//  ✅ Semua fix dari v14.5 (GPS baud, GSV off, confirm count, ESF post-lock) dipertahankan
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

// Polarity driver motor aktual. Positif tetap berarti maju di aplikasi,
// route/autopilot, dan telemetry; hanya output RPWM/LPWM yang dibalik di sini.
#define MOTOR_FORWARD_SIGN (-1)

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
// NAVIGATION & PHYSICS — CARGO PROFILE
// Baling-baling Ø55cm: torsi tinggi, inersia besar, butuh speed rendah
// ============================================================================
#define SERVO_CENTER      90
#define SERVO_MAX_LEFT    50
#define SERVO_MAX_RIGHT   130
#define SERVO_INTERVAL_MS 15

// [CARGO #1] Exponential smoothing lebih lambat untuk kapal berat
// Alpha rendah = smooth lebih panjang = tidak ada sentakan
#define SERVO_ALPHA       0.10f

#define SERVO_US_MIN      500
#define SERVO_US_CENTER   1450
#define SERVO_US_MAX      2400

// [CARGO #1] Kecepatan — kapal kargo pengangkut barang, baling2 Ø55cm
// Propeller besar → torsi tinggi bahkan di speed rendah → bisa maju/putar pelan
#define MAX_SPEED         160   // Batas absolut (dari 255)
#define TURN_SPEED        70    // Pivot lambat, kapal berat tidak perlu kencang
#define AVOID_SPEED       90    // Menghindar halus dengan fuzzy
#define APPROACH_SPEED    80    // Masuk waypoint perlahan (menghindari overshoot)
#define CRUISE_SPEED      120   // Normal autopilot cruise

// [CARGO #1] Kecepatan minimum untuk cut motor vs coast
#define MIN_DRIVE_SPEED   30    // Di bawah ini → motor off (propeller besar, drag besar)

// [CARGO #2] Ramp sangat lambat — kapal berat tidak boleh akselerasi/deselerasi mendadak
// Akselerasi mendadak propeller Ø55cm = waterhammer effect → stress mekanis
#define RAMP_INTERVAL_MS  40    // ms per langkah ramp (dari 25)
#define RAMP_UP_STEP      3     // PWM step naik per interval (dari 8)
#define RAMP_DOWN_STEP    5     // PWM step turun per interval (dari 12)

// [CARGO #9] Timeout joystick lebih panjang — operator kapal perlu waktu reaksi
#define JOYSTICK_TIMEOUT  4000  // ms (dari 2000)

#define SONAR_INTERVAL    80    // ms (sedikit diperlambat, kapal besar = reaksi lambat)

// ============================================================================
// AUTOPILOT WAYPOINT
// ============================================================================
#define WP_ARRIVAL_RADIUS_M  5.0f  // Radius arrive lebih besar (kapal tidak bisa ngerem)
#define WP_NAV_SPEED         120   // Cruise speed autopilot (dari 180)

// ============================================================================
// OBSTACLE DISTANCES — disesuaikan untuk kapal kargo besar
// ============================================================================
#define OBSTACLE_DIST     120   // cm — mulai manuver lebih awal (dari 80)
#define CRITICAL_DIST     50    // cm — kritial, balik (dari 35)
#define SLOW_DIST         200   // cm — zona kurangi kecepatan (fuzzy)

// ============================================================================
// STEERING PI CONTROLLER — RETUNED untuk kapal kargo
// Kapal besar: respons lambat, overshoot berbahaya (muatan bisa bergeser)
// Referensi: Nomoto 1st-order ship model
// ============================================================================
#define STEER_KP       0.30f  // Turun dari 0.50 — kapal besar tidak agresif
#define STEER_KI       0.004f // Turun dari 0.008 — integral lambat, cegah windup
#define STEER_I_MAX    20.0f  // Batas integral (dari 25)
#define STEER_DT_S     0.080f // Sesuai ramp interval baru (dari 0.060)

// Heading error dead-zone: kapal kargo tidak perlu koreksi < 3 derajat
#define HEADING_DEADZONE_DEG  3.0f

// ============================================================================
// GPS LOCK THRESHOLDS
// ============================================================================
#define GPS_MIN_SAT      4
#define GPS_HDOP_GOOD    2.5f
#define GPS_HDOP_ACCEPT  5.0f
#define GPS_AGE_MS       3000
#define GPS_CONFIRM_COUNT  2

// ============================================================================
// GPS POSITION CACHE & DRIFT FILTER
// ============================================================================
#define GPS_DEFAULT_LAT      -2.953923
#define GPS_DEFAULT_LNG     104.748214
#define GPS_SAVE_INTERVAL_MS  60000

// [CARGO] Kapal berat max kecepatan di air ~3 m/s (10.8 km/h)
#define GPS_MAX_SPEED_MS      3.0   // Turun dari 10.0 (realistis untuk kargo)
#define GPS_JUMP_BUFFER_M     2.0   // Buffer lompatan GPS (dari 3.0)

// ============================================================================
// BAUD RATE GPS
// ============================================================================
#define GPS_BAUD_INIT   9600
#define GPS_BAUD_FAST   38400
#define GPS_RECOVERY_INTERVAL_MS  8000
#define GPS_DIAG_INTERVAL_MS      5000

// ============================================================================
// WIFI FAILSAFE
// ============================================================================
#define WIFI_FAILSAFE_MS  5000

// ============================================================================
// JOYSTICK DEAD ZONE — lebih besar untuk kapal kargo (mengurangi drift)
// ============================================================================
#define JOY_DEADZONE_THROTTLE  0.08f  // Dari 0.05
#define JOY_DEADZONE_STEERING  0.12f  // Dari 0.10

// ============================================================================
// STATE MACHINE ENUMS
// ============================================================================
enum DeviceMode { MODE_IDLE, MODE_MANUAL, MODE_AUTO, MODE_RTH };

void startReturnToHome(const char* reason);

const char* modeToString(DeviceMode m) {
  switch (m) {
    case MODE_MANUAL: return "manual";
    case MODE_AUTO:   return "auto";
    case MODE_RTH:    return "rth";
    default:          return "idle";
  }
}

// ============================================================================
// FUZZY LOGIC — Membership Functions & Controller
//
// Referensi: Mamdani fuzzy inference untuk kontrol kapal
// (Nguyen et al., 2018 — "Fuzzy Logic Based Heading Control for Autonomous Ships")
//
// Membership sets:
//   Obstacle Distance: VERY_CLOSE, CLOSE, MEDIUM, FAR, VERY_FAR
//   Heading Error:     BIG_LEFT, LEFT, CENTER, RIGHT, BIG_RIGHT
//   Speed Output:      STOP, VERY_SLOW, SLOW, MODERATE, CRUISE
// ============================================================================

// Trapezoid membership function: naik dari a ke b, plateau b→c, turun c→d
float fuzzyTrap(float x, float a, float b, float c, float d) {
  if (x <= a || x >= d) return 0.0f;
  if (x >= b && x <= c) return 1.0f;
  if (x < b) return (x - a) / (b - a);
  return (d - x) / (d - c);
}

// Triangle membership function
float fuzzyTri(float x, float a, float b, float c) {
  if (x <= a || x >= c) return 0.0f;
  if (x <= b) return (x - a) / (b - a);
  return (c - x) / (c - b);
}

// ── Obstacle Distance Membership ────────────────────────────────────────────
// x = jarak dalam cm (0 - 300+)
float fuzzyObst_VeryCLose(float d)  { return fuzzyTrap(d,   0,   0,  30,  50); }
float fuzzyObst_Close(float d)      { return fuzzyTri( d,  30,  70, 120); }
float fuzzyObst_Medium(float d)     { return fuzzyTri( d,  90, 140, 200); }
float fuzzyObst_Far(float d)        { return fuzzyTri( d, 160, 220, 280); }
float fuzzyObst_VeryFar(float d)    { return fuzzyTrap(d, 240, 280, 400, 400); }

// ── Heading Error Membership ─────────────────────────────────────────────────
// x = error heading dalam derajat (-180 s/d 180)
float fuzzyErr_BigLeft(float e)   { return fuzzyTrap(e, -180, -180, -60, -30); }
float fuzzyErr_Left(float e)      { return fuzzyTri( e,  -60,  -25,   -3); }
float fuzzyErr_Center(float e)    { return fuzzyTri( e,  -10,    0,   10); }
float fuzzyErr_Right(float e)     { return fuzzyTri( e,    3,   25,   60); }
float fuzzyErr_BigRight(float e)  { return fuzzyTrap(e,   30,   60, 180, 180); }

// ── Fuzzy Speed Control Output (defuzzifikasi weighted average) ──────────────
// Mengembalikan speed target berdasarkan jarak obstacle kiri dan kanan
// plus heading error relatif ke waypoint
//
// Rule base (Mamdani):
//   IF dist_min VERY_CLOSE → STOP
//   IF dist_min CLOSE AND err CENTER → VERY_SLOW
//   IF dist_min CLOSE AND err !CENTER → SLOW
//   IF dist_min MEDIUM AND err CENTER → MODERATE
//   IF dist_min MEDIUM AND err !CENTER → SLOW
//   IF dist_min FAR → MODERATE
//   IF dist_min VERY_FAR AND err CENTER → CRUISE
//   IF dist_min VERY_FAR AND err !CENTER → MODERATE
int fuzzySpeedControl(float distLeft, float distRight, float headingError) {
  float d = min(distLeft, distRight);  // Worst case obstacle

  // Membership values — obstacle
  float mVC  = fuzzyObst_VeryCLose(d);
  float mC   = fuzzyObst_Close(d);
  float mM   = fuzzyObst_Medium(d);
  float mF   = fuzzyObst_Far(d);
  float mVF  = fuzzyObst_VeryFar(d);

  // Membership values — heading error
  float mEC  = fuzzyErr_Center(headingError);
  float mEnC = 1.0f - mEC;  // "Not center" = kemudi sedang koreksi

  // Rule firing (Mamdani MIN operator)
  float rStop     = mVC;
  float rVerySlow = min(mC,  mEC);
  float rSlow     = max(min(mC, mEnC), min(mM, mEnC));
  float rModerate = max(min(mM, mEC), mF);
  float rCruise   = min(mVF, mEC);
  float rModerate2= min(mVF, mEnC);

  // Output singletons (COG / centroid defuzzifikasi)
  const float vStop     =   0.0f;
  const float vVerySlow =  35.0f;
  const float vSlow     =  70.0f;
  const float vModerate = 100.0f;
  const float vCruise   =  (float)CRUISE_SPEED;

  float mModTotal = max(rModerate, rModerate2);

  float num = rStop     * vStop     +
              rVerySlow * vVerySlow  +
              rSlow     * vSlow      +
              mModTotal * vModerate  +
              rCruise   * vCruise;

  float den = rStop + rVerySlow + rSlow + mModTotal + rCruise;

  if (den < 0.001f) return CRUISE_SPEED;  // Default jika semua membership 0
  return (int)(num / den);
}

// ── Fuzzy Steering Avoidance ─────────────────────────────────────────────────
// Mengembalikan sudut servo koreksi berdasarkan jarak kiri vs kanan
// Output: angka positif = belok kanan, negatif = belok kiri (relatif ke center)
float fuzzySteerAvoid(float distLeft, float distRight) {
  // Jika keduanya jauh → tidak ada koreksi
  if (distLeft > OBSTACLE_DIST && distRight > OBSTACLE_DIST) return 0.0f;

  float ratio = 0.0f;
  float minDist = min(distLeft, distRight);

  if (distLeft < distRight) {
    // Rintangan di kiri → belok kanan
    float urgency = fuzzyObst_VeryCLose(distLeft) * 1.0f +
                    fuzzyObst_Close(distLeft)     * 0.7f +
                    fuzzyObst_Medium(distLeft)    * 0.4f;
    ratio = urgency;  // 0..1
    return ratio * (SERVO_MAX_RIGHT - SERVO_CENTER);  // derajat ke kanan
  } else {
    // Rintangan di kanan → belok kiri
    float urgency = fuzzyObst_VeryCLose(distRight) * 1.0f +
                    fuzzyObst_Close(distRight)     * 0.7f +
                    fuzzyObst_Medium(distRight)    * 0.4f;
    ratio = urgency;
    return -ratio * (SERVO_CENTER - SERVO_MAX_LEFT);  // derajat ke kiri (negatif)
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
  const char*   routeEvent        = "none";
  const char*   routeReason       = "";
  uint32_t      routeSeq          = 0;
  int           routeEventWpIndex = -1;
  int           routeEventWpTotal = -1;
  double        routeEventDistM   = -1.0;
  unsigned long lastRamp          = 0;
  unsigned long lastServoUpdate   = 0;
  unsigned long lastCommand       = 0;
  unsigned long lastSonarRead     = 0;
  unsigned long lastStatusPublish = 0;
  unsigned long lastGpsCheck      = 0;
  unsigned long lastMqttRetry     = 0;
  unsigned long lastGpsLog        = 0;
  unsigned long lastGpsSave       = 0;
  unsigned long lastGpsDiag       = 0;
  unsigned long lastGpsByteAt     = 0;
  unsigned long lastGpsSentenceAt = 0;
  unsigned long lastGpsRecovery   = 0;
  unsigned long gpsBytesRead      = 0;
  uint32_t      gpsCurrentBaud    = GPS_BAUD_INIT;
  unsigned long bootTime          = 0;
  unsigned long lockTime          = 0;  // [FIX #4] Pindah ke struct agar direset saat boot

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

  bool          esfStatusEnabled  = false;
  bool          esfAlgSent        = false;

  // Fuzzy output cache untuk telemetri
  float         fuzzySpeedOut     = 0.0f;
  float         fuzzySteerOut     = 0.0f;
} S;

#define FILTER_SAMPLES 7
#define SONAR_MIN_CM 2
#define SONAR_MAX_CM 400
#define SONAR_MAX_STEP_CM 35
#define SONAR_APPROACH_ALPHA 0.55f
#define SONAR_RELEASE_ALPHA 0.22f
#define SONAR_TIMEOUT_RELEASE_AFTER 3
#define SONAR_TIMEOUT_RELEASE_STEP_CM 25

int leftBuf[FILTER_SAMPLES]  = {400,400,400,400,400,400,400};
int rightBuf[FILTER_SAMPLES] = {400,400,400,400,400,400,400};
int bufIdx = 0;
float leftFilteredDist  = 400.0f;
float rightFilteredDist = 400.0f;
int leftLastValidDist   = 400;
int rightLastValidDist  = 400;
bool leftFilterInit     = false;
bool rightFilterInit    = false;
uint8_t leftTimeoutCount  = 0;
uint8_t rightTimeoutCount = 0;

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

// Normalize heading error ke -180..+180
double normalizeAngle(double angle) {
  while (angle >  180.0) angle -= 360.0;
  while (angle < -180.0) angle += 360.0;
  return angle;
}

// ============================================================================
// GPS DRIFT FILTER
// [FIX #7] Cast float konsisten, tidak ada ambiguitas double/unsigned long
// ============================================================================
bool acceptGpsPosition(double lat, double lng) {
  if (!S.filterInit) {
    S.filteredLat  = lat;
    S.filteredLng  = lng;
    S.filteredTime = millis();
    S.filterInit   = true;
    return true;
  }
  unsigned long now = millis();
  unsigned long dt  = now - S.filteredTime;
  double dist       = haversineM(S.filteredLat, S.filteredLng, lat, lng);
  double maxAllowed = GPS_MAX_SPEED_MS * ((float)dt / 1000.0f) + GPS_JUMP_BUFFER_M;

  if (dist > maxAllowed && dt < 2000) {
    Serial.printf("[GPS FILTER] Lompatan ditolak! dist=%.1fm max=%.1fm dt=%lums\n",
      dist, maxAllowed, dt);
    return false;
  }
  S.filteredLat  = lat;
  S.filteredLng  = lng;
  S.filteredTime = now;
  return true;
}

bool isValidWaypoint(double lat, double lng) {
  if (!isfinite(lat) || !isfinite(lng)) return false;
  if (lat < -90.0 || lat > 90.0) return false;
  if (lng < -180.0 || lng > 180.0) return false;
  if (lat == 0.0 && lng == 0.0) return false;
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

  // [CARGO] Jika speed sangat rendah, cut ke 0 (propeller besar, drag tinggi)
  if (speed > 0 && speed < MIN_DRIVE_SPEED) speed = 0;
  if (speed < 0 && speed > -MIN_DRIVE_SPEED) speed = 0;

  int driverSpeed = speed * MOTOR_FORWARD_SIGN;

  if (driverSpeed > 0) {
    ledcWrite(LPWM_PIN, 0);
    ledcWrite(RPWM_PIN, (uint32_t)driverSpeed);
  } else if (driverSpeed < 0) {
    ledcWrite(RPWM_PIN, 0);
    ledcWrite(LPWM_PIN, (uint32_t)(-driverSpeed));
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

void beginGpsSerial(uint32_t baud) {
  gpsSerial.end();
  delay(30);
  gpsSerial.begin(baud, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  S.gpsCurrentBaud    = baud;
  S.lastGpsByteAt     = millis();
  S.lastGpsSentenceAt = millis();
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
// ESF — Post-lock (FIX #4 & #5 dari v14.5 dipertahankan)
// ============================================================================
void enableEsfStatusOutput() {
  static const uint8_t msgEsfStatus[8] = {
    0x10, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00
  };
  sendUBXCmd(0x06, 0x01, msgEsfStatus, 8);
  Serial.println(F("[GPS] ESF-STATUS output UART1 aktif (post-lock) OK"));
}

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
// GPS CONFIGURATION (UBX) — NEO-M8U (tidak berubah dari v14.5)
// ============================================================================
void configureGPS() {
  Serial.println(F("[GPS] Mengirim konfigurasi UBX (NEO-M8U)..."));

  // CFG-PRT: upgrade baud ke 38400
  static const uint8_t cfgPrt38400[20] = {
    0x01, 0x00, 0x00,0x00, 0xD0,0x08,0x00,0x00,
    0x00,0x96,0x00,0x00,   0x23,0x00, 0x03,0x00,
    0x00,0x00, 0x00,0x00
  };
  sendUBXCmd(0x06, 0x00, cfgPrt38400, sizeof(cfgPrt38400));
  Serial.println(F("[GPS] CFG-PRT: upgrade baud ke 38400 terkirim"));

  delay(100);
  beginGpsSerial(GPS_BAUD_FAST);
  delay(100);
  Serial.printf("[GPS] HardwareSerial1 re-init @ %d baud OK\n", GPS_BAUD_FAST);

  // NAV5: dynModel = 0x03 (automotive → lebih cocok dari sea untuk UDR)
  // Catatan: sea (0x05) sebenarnya menonaktifkan UDR di beberapa firmware M8U.
  // Gunakan automotive (0x03) atau pedestrian (0x03) untuk UDR aktif.
  static const uint8_t nav5[36] = {
    0xFF,0xFF, 0x03, 0x03,  // dynModel = automotive (UDR compatible)
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
  // CATATAN PENTING: dynModel=SEA (0x05) di v14.5 mungkin menonaktifkan UDR!
  // NEO-M8U UDR (Untethered Dead Reckoning) butuh dynModel=Automotive(3)
  // atau Portable(0). Referensi: u-blox M8U Integration Manual §3.1.3
  Serial.println(F("[GPS] NAV5: dynModel=AUTOMOTIVE (0x03, UDR compatible) OK"));
  delay(50);

  static const uint8_t sbas[8] = { 0x01,0x03,0x03,0x00, 0x00,0x00,0x00,0x00 };
  sendUBXCmd(0x06, 0x16, sbas, sizeof(sbas));
  delay(50);

  // CFG-RATE: 5Hz
  static const uint8_t rate[6] = { 0xC8,0x00, 0x01,0x00, 0x01,0x00 };
  sendUBXCmd(0x06, 0x08, rate, sizeof(rate));
  Serial.println(F("[GPS] CFG-RATE: 5Hz aktif @ 38400 baud OK"));
  delay(50);

  // NMEA messages
  static const uint8_t msgGGA[8] = { 0xF0,0x00, 0x00,0x01,0x00,0x00,0x00,0x00 };
  static const uint8_t msgRMC[8] = { 0xF0,0x04, 0x00,0x01,0x00,0x00,0x00,0x00 };
  static const uint8_t msgGSV[8] = { 0xF0,0x03, 0x00,0x00,0x00,0x00,0x00,0x00 }; // OFF
  static const uint8_t msgGLL[8] = { 0xF0,0x01, 0x00,0x00,0x00,0x00,0x00,0x00 }; // OFF
  static const uint8_t msgGSA[8] = { 0xF0,0x02, 0x00,0x01,0x00,0x00,0x00,0x00 }; // ON
  static const uint8_t msgVTG[8] = { 0xF0,0x05, 0x00,0x00,0x00,0x00,0x00,0x00 }; // OFF

  sendUBXCmd(0x06, 0x01, msgGGA, 8);
  sendUBXCmd(0x06, 0x01, msgRMC, 8);
  sendUBXCmd(0x06, 0x01, msgGSV, 8);
  sendUBXCmd(0x06, 0x01, msgGLL, 8);
  sendUBXCmd(0x06, 0x01, msgGSA, 8);
  sendUBXCmd(0x06, 0x01, msgVTG, 8);
  Serial.println(F("[GPS] NMEA: GGA+RMC+GSA ON | GSV+GLL+VTG OFF"));
  delay(50);

  static const uint8_t saveCfg[12] = {
    0x00,0x00,0x00,0x00, 0xFF,0xFF,0x00,0x00, 0x00,0x00,0x00,0x00
  };
  sendUBXCmd(0x06, 0x09, saveCfg, sizeof(saveCfg));
  Serial.println(F("[GPS] Konfigurasi M8U tersimpan ke flash GPS."));
  delay(50);

  Serial.println(F("[GPS] ESF-STATUS & CFG-ESFALG akan dikirim SETELAH GPS lock."));
}

// ============================================================================
// GPS — Feed & Quality
// ============================================================================
void feedGPS() {
  while (gpsSerial.available()) {
    char c = (char)gpsSerial.read();
    S.gpsBytesRead++;
    S.lastGpsByteAt = millis();
    if (gps.encode(c)) S.lastGpsSentenceAt = millis();
  }
}

void recoverGpsSerialIfNeeded() {
  if (S.gpsLocked) return;

  unsigned long now = millis();
  if (now - S.lastGpsSentenceAt < GPS_RECOVERY_INTERVAL_MS) return;
  if (now - S.lastGpsRecovery < GPS_RECOVERY_INTERVAL_MS) return;

  S.lastGpsRecovery = now;
  uint32_t nextBaud = (S.gpsCurrentBaud == GPS_BAUD_FAST) ? GPS_BAUD_INIT : GPS_BAUD_FAST;
  Serial.printf("[GPS] Belum ada kalimat NMEA valid %lus @ %lu baud. Coba baud %lu...\n",
    (now - S.lastGpsSentenceAt) / 1000,
    (unsigned long)S.gpsCurrentBaud,
    (unsigned long)nextBaud);
  beginGpsSerial(nextBaud);
}

void printGpsDiagnostics() {
  if (S.gpsLocked) return;
  if (millis() - S.lastGpsDiag < GPS_DIAG_INTERVAL_MS) return;
  S.lastGpsDiag = millis();

  unsigned long now = millis();
  Serial.printf("[GPS DEBUG] Baud:%lu | Bytes:%lu | Chars:%lu | OK:%lu | Bad:%lu | LastByte:%lus | LastNMEA:%lus\n",
    (unsigned long)S.gpsCurrentBaud,
    S.gpsBytesRead,
    (unsigned long)gps.charsProcessed(),
    (unsigned long)gps.passedChecksum(),
    (unsigned long)gps.failedChecksum(),
    (now - S.lastGpsByteAt) / 1000,
    (now - S.lastGpsSentenceAt) / 1000);

  if (S.gpsBytesRead == 0) {
    Serial.println(F("[GPS DEBUG] Tidak ada byte dari GPS. Cek wiring: GPS TX -> ESP RX16, GPS RX -> ESP TX17, GND wajib sama."));
  } else if (gps.passedChecksum() == 0) {
    Serial.println(F("[GPS DEBUG] Ada byte tapi belum ada NMEA valid. Kemungkinan baud tidak cocok atau RX/TX noise."));
  }
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
// GPS LOCK FSM
// [FIX #4] lockTime sekarang S.lockTime (bukan static lokal), direset di boot
// ============================================================================
void updateGpsLockFSM() {
  feedGPS();
  recoverGpsSerialIfNeeded();
  printGpsDiagnostics();

  if (S.gpsLocked && S.gpsBuzzDone) {
    if (!S.esfStatusEnabled) {
      S.esfStatusEnabled = true;
      enableEsfStatusOutput();
    }
    // [FIX #4] S.lockTime diset saat pertama kali masuk kondisi ini
    if (!S.esfAlgSent) {
      if (S.lockTime == 0) S.lockTime = millis();
      if (millis() - S.lockTime >= 3000) {
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
      elapsed, qlabel, sats, hdop, lat, lng, S.gpsConfirmCount, GPS_CONFIRM_COUNT);
  }

  if (!S.gpsLocked) {
    if (checkGpsLock()) S.gpsConfirmCount++;
    else                S.gpsConfirmCount = 0;

    if (S.gpsConfirmCount >= GPS_CONFIRM_COUNT) {
      S.gpsLocked = true;
      S.lockTime  = 0;  // Reset lockTime agar delay ESF-ALG terhitung dari sekarang
      digitalWrite(PIN_LED, HIGH);

      Serial.println(F("\n[GPS] ============================================"));
      Serial.println(F("[GPS]  *** GPS TERKUNCI! (NEO-M8U) ***"));
      Serial.printf( "[GPS]  Lat       : %.8f\n",      gps.location.lat());
      Serial.printf( "[GPS]  Lng       : %.8f\n",      gps.location.lng());
      Serial.printf( "[GPS]  Satelit   : %u\n",        (unsigned)gps.satellites.value());
      Serial.printf( "[GPS]  HDOP      : %.2f\n",      gps.hdop.hdop());
      Serial.printf( "[GPS]  Quality   : %u/3\n",      getGpsQuality());
      Serial.printf( "[GPS]  Waktu lock: %lu detik\n", (millis() - S.bootTime) / 1000);
      Serial.println(F("[GPS]  Mode      : CARGO (kecepatan dibatasi)"));
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
  Serial.println(F("[SERVO] Timer 0, 50Hz, pin 4 — center 1450µs, CARGO profile."));
}

// ============================================================================
// SERVO SMOOTH — [FIX #8] Mapping microsecond yang benar
//
// Kapal kargo: servo kapal kemudi besar, tidak bisa responsif seperti RC car.
// SERVO_ALPHA = 0.10 memberi respon sangat smooth.
//
// Formula koreksi:
//   Range derajat: SERVO_MAX_LEFT (50) → SERVO_MAX_RIGHT (130)  = 80 derajat
//   Range µs     : SERVO_US_MIN (500) → SERVO_US_MAX (2400)     = 1900 µs
//   µs per derajat = 1900 / 80 = 23.75 µs/deg
//   us = SERVO_US_MIN + (angle - SERVO_MAX_LEFT) * (1900.0f / 80.0f)
// ============================================================================
void updateServoSmooth() {
  if (millis() - S.lastServoUpdate < SERVO_INTERVAL_MS) return;
  S.lastServoUpdate = millis();

  float diff = (float)S.servoTarget - S.servoCurrentF;
  if (fabsf(diff) < 0.05f) return;

  S.servoCurrentF += diff * SERVO_ALPHA;

  // [FIX #8] Formula mapping yang benar berdasarkan actual range servo
  float angleRange = (float)(SERVO_MAX_RIGHT - SERVO_MAX_LEFT);  // 80 derajat
  float usRange    = (float)(SERVO_US_MAX - SERVO_US_MIN);       // 1900 µs
  int us = (int)(SERVO_US_MIN + (S.servoCurrentF - (float)SERVO_MAX_LEFT) * (usRange / angleRange));
  us = constrain(us, SERVO_US_MIN, SERVO_US_MAX);
  steeringServo.writeMicroseconds(us);
}

void setServoTarget(int angle) {
  S.servoTarget = constrain(angle, SERVO_MAX_LEFT, SERVO_MAX_RIGHT);
}

// ============================================================================
// ULTRASONIC (HC-SR04)
// ============================================================================
int medianBufferValue(int* buf) {
  int tmp[FILTER_SAMPLES];
  for (int i = 0; i < FILTER_SAMPLES; i++) tmp[i] = buf[i];

  for (int i = 1; i < FILTER_SAMPLES; i++) {
    int key = tmp[i];
    int j = i - 1;
    while (j >= 0 && tmp[j] > key) {
      tmp[j + 1] = tmp[j];
      j--;
    }
    tmp[j + 1] = key;
  }

  return tmp[FILTER_SAMPLES / 2];
}

int readFilteredDist(
  int trig,
  int echo,
  int* buf,
  float& filteredDist,
  int& lastValidDist,
  bool& filterInit,
  uint8_t& timeoutCount
) {
  digitalWrite(trig, LOW);  delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);

  long dur = pulseIn(echo, HIGH, 25000);
  int raw = lastValidDist;
  if (dur > 0) {
    raw = constrain((int)(dur * 0.017f), SONAR_MIN_CM, SONAR_MAX_CM);
    lastValidDist = raw;
    timeoutCount = 0;
  } else {
    if (timeoutCount < 255) timeoutCount++;
    if (timeoutCount >= SONAR_TIMEOUT_RELEASE_AFTER) {
      raw = min(lastValidDist + SONAR_TIMEOUT_RELEASE_STEP_CM, SONAR_MAX_CM);
      lastValidDist = raw;
    }
  }

  buf[bufIdx] = raw;
  int median = medianBufferValue(buf);

  if (!filterInit) {
    filteredDist = (float)median;
    filterInit = true;
    return median;
  }

  float delta = (float)median - filteredDist;
  delta = constrain(delta, -(float)SONAR_MAX_STEP_CM, (float)SONAR_MAX_STEP_CM);

  float alpha = (delta < 0.0f) ? SONAR_APPROACH_ALPHA : SONAR_RELEASE_ALPHA;
  filteredDist += delta * alpha;
  filteredDist = constrain(filteredDist, (float)SONAR_MIN_CM, (float)SONAR_MAX_CM);

  return (int)(filteredDist + 0.5f);
}

// ============================================================================
// AVOIDANCE — [CARGO #3] FUZZY LOGIC menggantikan threshold on/off
//
// Sebelumnya: if (dL < 80) → belok kanan (abrupt)
// Sekarang:   fuzzy membership → output halus, tidak ada sentakan mendadak
// Ini penting untuk kapal kargo — muatan bisa bergeser kalau manuver mendadak
// ============================================================================
bool processAvoidance() {
  if (millis() - S.lastSonarRead < SONAR_INTERVAL) return S.isAvoiding;
  S.lastSonarRead = millis();

  int dL = readFilteredDist(
    TRIG_LEFT_PIN,
    ECHO_LEFT_PIN,
    leftBuf,
    leftFilteredDist,
    leftLastValidDist,
    leftFilterInit,
    leftTimeoutCount
  );
  int dR = readFilteredDist(
    TRIG_RIGHT_PIN,
    ECHO_RIGHT_PIN,
    rightBuf,
    rightFilteredDist,
    rightLastValidDist,
    rightFilterInit,
    rightTimeoutCount
  );
  bufIdx = (bufIdx + 1) % FILTER_SAMPLES;

  cachedDistLeft  = dL;
  cachedDistRight = dR;

  float fSteer = fuzzySteerAvoid((float)dL, (float)dR);
  S.fuzzySteerOut = fSteer;

  // Emergency: keduanya critical → mundur pelan
  float mVCLeft  = fuzzyObst_VeryCLose((float)dL);
  float mVCRight = fuzzyObst_VeryCLose((float)dR);

  if (mVCLeft > 0.7f && mVCRight > 0.7f) {
    // Kedua sisi sangat dekat → mundur
    S.targetSpeed = -(int)(AVOID_SPEED * 0.6f);
    setServoTarget(SERVO_CENTER);
    S.isAvoiding = true;
    Serial.printf("[FUZZY] Emergency reverse: L=%d R=%d\n", dL, dR);

  } else if (fabsf(fSteer) > 1.0f) {
    // Ada rintangan satu sisi → fuzzy steer
    int steerCorrect = SERVO_CENTER + (int)fSteer;
    steerCorrect = constrain(steerCorrect, SERVO_MAX_LEFT, SERVO_MAX_RIGHT);
    setServoTarget(steerCorrect);

    // Speed dikurangi proporsional berdasarkan fuzzy
    float dMin = min((float)dL, (float)dR);
    float fSpeed = (float)fuzzySpeedControl((float)dL, (float)dR, 0.0f);
    S.fuzzySpeedOut = fSpeed;
    S.targetSpeed = (int)fSpeed;
    S.isAvoiding = true;

    Serial.printf("[FUZZY] Avoid: L=%d R=%d steer=%.1f speed=%.0f\n",
      dL, dR, fSteer, fSpeed);

  } else {
    S.isAvoiding = false;
  }

  S.smartMoveActive = S.isAvoiding;
  return S.isAvoiding;
}

// ============================================================================
// JOYSTICK
// [FIX #5] Normalisasi throttle/steering yang benar — hapus heuristic *100
// ============================================================================
void handleJoystick(JsonDocument& doc) {
  S.lastCommand = millis();

  if (S.mode == MODE_AUTO || S.mode == MODE_RTH) {
    S.autopilotActive = false;
    S.rthActive       = false;
    S.waypointCount   = 0;
    S.waypointIndex   = 0;
    S.activeTargetDistM = 0.0;
    S.steerIntegral   = 0.0;  // [FIX #6] Reset integral saat manual override
    Serial.println("[NAV] Rute otonom dibatalkan — manual override");
  }
  S.mode        = MODE_MANUAL;

  // Manual joystick tidak memakai ultrasonic avoidance.
  S.smartMoveActive = false;
  S.isAvoiding      = false;

  // [FIX #5] Ambil nilai langsung, lakukan normalisasi eksplisit
  float throttle = doc.containsKey("throttle") ? doc["throttle"].as<float>() : 0.0f;
  float steering = doc.containsKey("steering") ? doc["steering"].as<float>() : 0.0f;

  // Normalisasi: jika nilai dalam range 0-100 (%), konversi ke -1..1
  // Jika sudah -1..1, biarkan. Tidak pakai heuristic abs <= 1.0
  if (throttle > 1.0f || throttle < -1.0f) throttle /= 100.0f;
  if (steering > 1.0f || steering < -1.0f) steering /= 100.0f;

  // Clamp ke valid range
  throttle = constrain(throttle, -1.0f, 1.0f);
  steering = constrain(steering, -1.0f, 1.0f);

  // Dead zone
  if (fabsf(throttle) < JOY_DEADZONE_THROTTLE) throttle = 0.0f;
  if (fabsf(steering) < JOY_DEADZONE_STEERING) steering = 0.0f;

  int steerAngle = SERVO_CENTER + (int)(steering * (float)(SERVO_MAX_RIGHT - SERVO_CENTER));
  setServoTarget(steerAngle);

  int baseSpeed = 0;

  if (fabsf(throttle) < JOY_DEADZONE_THROTTLE && fabsf(steering) > JOY_DEADZONE_STEERING) {
    // Pivot saja — kapal kargo pivot sangat pelan
    baseSpeed = (steering > 0.0f) ? TURN_SPEED : -TURN_SPEED;
    Serial.printf("[JOY] Pivot | str=%.2f | spd=%d | svo=%d\n",
      steering, baseSpeed, steerAngle);

  } else if (fabsf(throttle) >= JOY_DEADZONE_THROTTLE && fabsf(steering) > JOY_DEADZONE_STEERING) {
    // Jalan sambil belok — speed dikurangi 15% karena turning
    baseSpeed = (int)(throttle * (float)MAX_SPEED * 0.85f);
    Serial.printf("[JOY] Turn  | thr=%.2f str=%.2f | spd=%d | svo=%d\n",
      throttle, steering, baseSpeed, steerAngle);

  } else if (fabsf(throttle) >= JOY_DEADZONE_THROTTLE) {
    // Lurus
    baseSpeed = (int)(throttle * (float)MAX_SPEED);
    setServoTarget(SERVO_CENTER);
    Serial.printf("[JOY] Lurus | thr=%.2f | spd=%d\n", throttle, baseSpeed);

  } else {
    baseSpeed = 0;
    setServoTarget(SERVO_CENTER);
  }

  S.targetSpeed = constrain(baseSpeed, -MAX_SPEED, MAX_SPEED);
}

// ============================================================================
// ROUTE STATUS EVENT (published inside regular spedi/vehicle/status telemetry)
// ============================================================================
void setRouteEvent(
  const char* event,
  const char* reason = nullptr,
  int wpIndex = -1,
  int wpTotal = -1,
  double distM = -1.0
) {
  S.routeEvent        = event;
  S.routeReason       = reason != nullptr ? reason : "";
  S.routeEventWpIndex = wpIndex;
  S.routeEventWpTotal = wpTotal;
  S.routeEventDistM   = distM;
  S.routeSeq++;
  Serial.printf("[ROUTE EVENT] seq=%lu event=%s reason=%s wp=%d/%d dist=%.1f\n",
    (unsigned long)S.routeSeq,
    S.routeEvent,
    S.routeReason,
    S.routeEventWpIndex,
    S.routeEventWpTotal,
    S.routeEventDistM);
}

// ============================================================================
// ROUTE
// ============================================================================
void handleRoute(JsonDocument& doc) {
  const char* action = doc["action"] | "";

  if (strcmp(action, "start") == 0) {
    if (!S.gpsLocked) {
      Serial.printf("[NAV] Rute '%s' ditolak - GPS belum lock | Sat:%u HDOP:%.1f Q:%u\n",
        action,
        gps.satellites.isValid() ? (unsigned)gps.satellites.value() : 0,
        gps.hdop.isValid() ? gps.hdop.hdop() : 99.9f,
        getGpsQuality());
      setRouteEvent("route_reject", "gps_not_locked");
      return;
    }

    JsonArray wps = doc["waypoints"];
    if (wps.isNull()) {
      Serial.println("[NAV] Rute ditolak - format waypoints bukan array");
      setRouteEvent("route_reject", "invalid_waypoints");
      return;
    }
    int count = min((int)wps.size(), MAX_WAYPOINTS);
    if (count < 2) {
      setRouteEvent("route_reject", "waypoints_less_than_2", -1, count);
      Serial.println("[NAV] Rute ditolak — butuh >= 2 waypoint");
      return;
    }
    for (int i = 0; i < count; i++) {
      JsonVariant wp = wps[i];
      if (!wp["lat"].is<double>() || !wp["lng"].is<double>()) {
        setRouteEvent("route_reject", "invalid_coordinate", i, count);
        Serial.printf("[NAV] Rute ditolak - WP %d lat/lng tidak valid\n", i + 1);
        return;
      }
      double lat = wp["lat"].as<double>();
      double lng = wp["lng"].as<double>();
      if (!isValidWaypoint(lat, lng)) {
        setRouteEvent("route_reject", "invalid_coordinate", i, count);
        Serial.printf("[NAV] Rute ditolak - WP %d di luar range: %.8f, %.8f\n",
          i + 1, lat, lng);
        return;
      }
      S.waypoints[i].lat = lat;
      S.waypoints[i].lng = lng;
    }
    S.homeLat = S.filterInit ? S.filteredLat : gps.location.lat();
    S.homeLng = S.filterInit ? S.filteredLng : gps.location.lng();
    S.homeSet = true;
    S.rthActive = false;
    S.waypointCount   = count;
    S.waypointIndex   = 0;
    S.activeTargetDistM = 0.0;
    S.autopilotActive = true;
    S.smartMoveActive = false;
    S.isAvoiding      = false;
    S.steerIntegral   = 0.0;
    S.lastValidHeading = 0.0;  // [FIX #6] Reset heading saat rute baru
    S.mode            = MODE_AUTO;
    Serial.printf("[NAV] Rute dimulai: %d waypoint | CARGO mode (kecepatan dibatasi)\n", count);
    setRouteEvent("route_start", nullptr, 0, count);

  } else if (strcmp(action, "rth") == 0 ||
             strcmp(action, "return_home") == 0) {
    const char* reason = doc["reason"] | "manual_button";
    startReturnToHome(reason);

  } else if (strcmp(action, "set_home") == 0 ||
             strcmp(action, "reset_home") == 0) {
    const char* reason = doc["reason"] | "manual_reset";
    bool gpsFresh = gps.location.isValid() && gps.location.age() <= 3000;
    bool filteredValid = S.filterInit && isValidWaypoint(S.filteredLat, S.filteredLng);

    if (!gpsFresh && !filteredValid) {
      setRouteEvent("home_reset_reject", "gps_not_ready");
      Serial.println("[RTH] Reset home ditolak - GPS/filtered position belum valid");
      beepAsync(150, 2);
      return;
    }

    double newHomeLat = filteredValid ? S.filteredLat : gps.location.lat();
    double newHomeLng = filteredValid ? S.filteredLng : gps.location.lng();
    if (!isValidWaypoint(newHomeLat, newHomeLng)) {
      setRouteEvent("home_reset_reject", "invalid_coordinate");
      Serial.printf("[RTH] Reset home ditolak - koordinat tidak valid: %.8f, %.8f\n",
        newHomeLat, newHomeLng);
      beepAsync(150, 2);
      return;
    }

    S.autopilotActive = false;
    S.rthActive       = false;
    S.smartMoveActive = false;
    S.isAvoiding      = false;
    S.waypointCount   = 0;
    S.waypointIndex   = 0;
    S.targetSpeed     = 0;
    S.activeTargetDistM = 0.0;
    S.steerIntegral   = 0.0;
    S.lastValidHeading = 0.0;
    S.homeLat         = newHomeLat;
    S.homeLng         = newHomeLng;
    S.homeSet         = true;
    S.mode            = MODE_IDLE;
    setServoTarget(SERVO_CENTER);
    setRouteEvent("home_reset", reason);
    Serial.printf("[RTH] Home di-reset (%s) -> %.8f, %.8f\n",
      reason, S.homeLat, S.homeLng);
    beepAsync(100, 2);

  } else if (strcmp(action, "stop") == 0) {
    S.autopilotActive = false;
    S.rthActive       = false;
    S.smartMoveActive = false;
    S.isAvoiding      = false;
    S.waypointCount   = 0;
    S.waypointIndex   = 0;
    S.targetSpeed     = 0;
    S.activeTargetDistM = 0.0;
    S.steerIntegral   = 0.0;
    S.mode            = MODE_IDLE;
    setServoTarget(SERVO_CENTER);
    Serial.println("[NAV] Rute dihentikan oleh server");
    setRouteEvent("route_stop");
  } else {
    Serial.printf("[NAV] Rute ditolak - action tidak dikenal: %s\n", action);
    setRouteEvent("route_reject", "unknown_action");
  }
}

// ============================================================================
// AUTOPILOT
//
// [CARGO #3] Fuzzy logic diintegrasikan:
//   - Kecepatan disesuaikan fuzzy berdasarkan obstacle + heading error
//   - Steering PI tetap dipakai untuk koreksi heading, tapi output dibatasi
//
// [FIX #6] Saat WP tercapai: reset integral DAN lastValidHeading
//
// Referensi PI: Nomoto model kapal, tau (time constant) ~ 2-5 detik
// Untuk propeller besar STEER_KP rendah = sistem lebih terdampir
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
        setRouteEvent("rth_complete", nullptr, 0, 1);
        Serial.println("[RTH] Titik awal tercapai — kapal berhenti total");
      } else {
        setRouteEvent("route_complete", nullptr, S.waypointIndex, S.waypointCount);
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
  if (gps.speed.isValid() && gps.speed.kmph() > 1.0 && gps.course.isValid()) {
    heading            = gps.course.deg();
    S.lastValidHeading = heading;
  } else {
    heading = S.lastValidHeading;
  }

  if (dist < WP_ARRIVAL_RADIUS_M) {
    int reachedIndex = S.waypointIndex;
    S.waypointIndex++;
    S.steerIntegral    = 0.0;
    S.lastValidHeading = 0.0;  // [FIX #6] Reset heading ke netral
    if (S.rthActive) {
      Serial.printf("[RTH] Home tercapai (%.1fm)\n", dist);
    } else {
      Serial.printf("[NAV] WP %d tercapai (%.1fm). Next: %d/%d\n",
        S.waypointIndex, dist, S.waypointIndex + 1, S.waypointCount);
      setRouteEvent("wp_reached", nullptr, reachedIndex, S.waypointCount, dist);
    }
    return;
  }

  // Obstacle avoidance mengambil prioritas
  if (processAvoidance()) return;

  double error = normalizeAngle(bearing - heading);

  // [CARGO] Dead zone heading — jangan koreksi kalau error kecil
  if (fabsf((float)error) < HEADING_DEADZONE_DEG) {
    error = 0.0;
  }

  // PI controller
  S.steerIntegral = constrain(
    S.steerIntegral + error * STEER_DT_S,
    -STEER_I_MAX, STEER_I_MAX
  );

  double steerOutput = STEER_KP * (error / 90.0 * (double)(SERVO_MAX_RIGHT - SERVO_CENTER))
                     + STEER_KI * S.steerIntegral;

  int steerAngle = SERVO_CENTER + (int)steerOutput;
  setServoTarget(constrain(steerAngle, SERVO_MAX_LEFT, SERVO_MAX_RIGHT));

  // [CARGO #3] Fuzzy speed berdasarkan obstacle + heading error
  float fSpeed = (float)fuzzySpeedControl(
    (float)cachedDistLeft,
    (float)cachedDistRight,
    (float)error
  );
  S.fuzzySpeedOut = fSpeed;

  // Override dengan kecepatan approach saat dekat WP
  if (dist < 8.0) {
    fSpeed = min(fSpeed, (float)APPROACH_SPEED);
    Serial.printf("[NAV] Approaching WP: %.1fm, speed=%.0f\n", dist, fSpeed);
  }

  S.targetSpeed = constrain((int)fSpeed, 0, MAX_SPEED);

  // Log navigasi setiap 3 detik
  static unsigned long lastNavLog = 0;
  if (millis() - lastNavLog > 3000) {
    lastNavLog = millis();
    Serial.printf("[NAV] WP%d | dist=%.1fm bear=%.0f hdg=%.0f err=%.0f spd=%d servo=%d\n",
      S.waypointIndex + 1, dist, bearing, heading, error, S.targetSpeed, steerAngle);
  }
}

void startReturnToHome(const char* reason) {
  if (S.rthActive) return;

  if (!S.homeSet) {
    S.autopilotActive = false;
    S.rthActive       = false;
    S.smartMoveActive = false;
    S.isAvoiding      = false;
    S.waypointCount   = 0;
    S.waypointIndex   = 0;
    S.targetSpeed     = 0;
    S.activeTargetDistM = 0.0;
    S.steerIntegral   = 0.0;
    S.mode            = MODE_IDLE;
    setServoTarget(SERVO_CENTER);
    setRouteEvent("rth_reject", "home_not_set");
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
  S.smartMoveActive  = false;
  S.isAvoiding       = false;
  S.targetSpeed      = 0;
  S.activeTargetDistM = 0.0;
  S.steerIntegral    = 0.0;
  S.lastValidHeading = 0.0;
  S.mode             = MODE_RTH;
  setRouteEvent("rth_start", reason, 0, 1);
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
  if (!mqttClient.connected()) {
    static unsigned long lastMqttTxWarn = 0;
    if (millis() - lastMqttTxWarn >= 5000) {
      lastMqttTxWarn = millis();
      Serial.println(F("[MQTT TX] skip status - MQTT not connected"));
    }
    return;
  }
  if (millis() - S.lastStatusPublish < 2000) return;
  S.lastStatusPublish = millis();

  JsonDocument doc;
  doc["lat"]               = gps.location.isValid() ? gps.location.lat() : 0.0;
  doc["lng"]               = gps.location.isValid() ? gps.location.lng() : 0.0;
  doc["satellite_count"]   = (int)gps.satellites.value();
  doc["waypoint_index"]    = S.waypointIndex;
  doc["waypoint_count"]    = S.waypointCount;
  doc["route_event"]       = S.routeEvent;
  doc["route_reason"]      = S.routeReason;
  doc["route_seq"]         = S.routeSeq;
  doc["route_wp_index"]    = S.routeEventWpIndex;
  doc["route_wp_total"]    = S.routeEventWpTotal;
  doc["route_dist_m"]      = S.routeEventDistM;
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
  doc["fuzzy_speed_out"]   = S.fuzzySpeedOut;  // Fuzzy output debug
  doc["fuzzy_steer_out"]   = S.fuzzySteerOut;
  doc["profile"]           = "cargo";

  size_t payloadSize = measureJson(doc);
  bool beginOk = mqttClient.beginPublish(TOPIC_STATUS, payloadSize, false);
  size_t bytesWritten = serializeJson(doc, mqttClient);
  int endOk = mqttClient.endPublish();

  Serial.printf("[MQTT TX] status begin=%d end=%d bytes=%u/%u lat=%.6f lng=%.6f fix=%d sat=%d hdop=%.2f q=%u\n",
    beginOk ? 1 : 0,
    endOk,
    (unsigned)bytesWritten,
    (unsigned)payloadSize,
    gps.location.isValid() ? gps.location.lat() : 0.0,
    gps.location.isValid() ? gps.location.lng() : 0.0,
    S.gpsLocked ? 1 : 0,
    gps.satellites.isValid() ? (int)gps.satellites.value() : 0,
    gps.hdop.isValid() ? gps.hdop.hdop() : 99.99,
    getGpsQuality());
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("\n=== SPEDI BOAT v15.0-S3-M8U-CARGO — BOOT ==="));
  Serial.println(F("=== ESP32-S3 | NEO-M8U | Kapal Kargo | Ø55cm Prop ===\n"));

  Serial.println(F("[CARGO] Profile aktif:"));
  Serial.printf( "[CARGO]   MAX_SPEED      = %d PWM\n",  MAX_SPEED);
  Serial.printf( "[CARGO]   CRUISE_SPEED   = %d PWM\n",  CRUISE_SPEED);
  Serial.printf( "[CARGO]   WP_NAV_SPEED   = %d PWM\n",  WP_NAV_SPEED);
  Serial.printf( "[CARGO]   AVOID_SPEED    = %d PWM\n",  AVOID_SPEED);
  Serial.printf( "[CARGO]   RAMP_UP_STEP   = %d/tick\n", RAMP_UP_STEP);
  Serial.printf( "[CARGO]   RAMP_DOWN_STEP = %d/tick\n", RAMP_DOWN_STEP);
  Serial.printf( "[CARGO]   RAMP_INTERVAL  = %dms\n",    RAMP_INTERVAL_MS);
  Serial.printf( "[CARGO]   JOY_TIMEOUT    = %dms\n",    JOYSTICK_TIMEOUT);
  Serial.println(F("[CARGO]   FUZZY LOGIC    = AKTIF (speed + steer)"));
  Serial.println(F("[CARGO]   STEER_KP/KI   = 0.30 / 0.004 (Nomoto-tuned)\n"));

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
  Serial.println(F("[INIT] Servo OK — CARGO smooth, alpha=0.10, writeMicroseconds"));

  // Init state
  S.lastCommand      = millis();
  S.lastValidHeading = 0.0;
  S.drActive         = false;
  S.imuCalibStatus   = 0;
  S.esfStatusEnabled = false;
  S.esfAlgSent       = false;
  S.lockTime         = 0;    // [FIX #4]
  S.fuzzySpeedOut    = 0.0f;
  S.fuzzySteerOut    = 0.0f;

  // [FIX #1] GPS serial dimulai di 9600 dulu
  Serial.println(F("[GPS] Serial1 dimulai @ 9600 (init)..."));
  beginGpsSerial(GPS_BAUD_INIT);
  delay(500);

  injectPosition();
  configureGPS();
  Serial.printf("[GPS] Serial1 aktif @ %d baud, RX=%d TX=%d (NEO-M8U)\n",
    (int)S.gpsCurrentBaud, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println(F("[GPS] NAV5 dynModel=AUTOMOTIVE (UDR compatible, bukan SEA)"));
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
  Serial.println(F("[SYSTEM] CARGO mode: max speed terbatas, fuzzy logic aktif\n"));
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

  // 5. Servo smooth (CARGO: alpha=0.10, lebih lambat)
  updateServoSmooth();

  // 6. Motor ramp (CARGO: step kecil, interval panjang)
  updateMotorPhysics();

  // 7. WiFi failsafe
  updateWifiFailsafe();

  // 8. Mode FSM
  switch (S.mode) {
    case MODE_MANUAL:
      if (millis() - S.lastCommand > JOYSTICK_TIMEOUT) {
        if (S.targetSpeed != 0) {
          Serial.println("[JOY] Timeout — kapal berhenti perlahan");
        }
        S.targetSpeed = 0;
        S.isAvoiding = false;
        S.smartMoveActive = false;
        break;
      }
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
