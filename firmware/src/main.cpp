// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lileetung

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_netif.h>
#include <esp_netif_net_stack.h>
#include <esp_netif_sta_list.h>
#include <esp_wifi.h>
#include <lwip/etharp.h>
#include <lwip/tcpip.h>
#include <WiFiUdp.h>
#include <VL53L0X.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <esp_system.h>
#include <driver/temp_sensor.h>

#include "blackbox.h"
#include "camera_stream.h"
#include "net_send.h"
#include "dhcp_unicast.h"
#include "control_math.h"
#include "soft_i2c.h"
#include "auto_land.h"
#include "vel_mode.h"
#include "trim_math.h"

static const int MOT_PINS[] = {7, 8, 2, 4};
static const int PIN_BAT_ADC = 1;
static const int PIN_LED = 21;
static const int PIN_SCL = 5, PIN_SDA = 6;
static const int PIN_TOF_INT = 9, PIN_TOF_XSHUT = 44;
static const uint8_t MPU_ADDR = 0x68;

static const uint32_t PWM_HZ = 32000;
static const int PWM_BITS = 10, PWM_MAX = (1 << PWM_BITS) - 1;

static const char *SSID_BASE = "SPARROW";
static const char *WIFI_PASSWORD = "12345678";
static const int WIFI_CHANNEL = 6;
static const uint16_t APP_PORT = 2390, TELEM_PORT = 2391;
static const uint8_t CRTP_HEADER_COMMANDER = 0x30;
static const uint8_t CRTP_HEADER_GENERIC = 0x70, GENERIC_ALT_HOLD = 4;
static const uint32_t TAKEOFF_ASK_MS = 1000;

static const char *FIRMWARE_VERSION = "1.0.0";
static const uint8_t FIRMWARE_BUILD = 17;
static const uint32_t PERIOD_US = 2000;
static const float MAX_LOOP_DT_S = 0.05f;
static const float ACC_LSB = 4096.0f;
static const float GYRO_LSB = 16.4f;
struct UnitCal {
  float accBias[3], accScale[3];
  float vbatCal;
  float tofGroundMm;
};
static const UnitCal UNIT_CAL_DEFAULT = {
  {0.030000f, -0.004700f, 0.135800f}, {1.000000f, 1.004722f, 0.996115f}, 0.999665f, 46.42f};
static const float ATT_TAU_S = 0.5f;
static const float ATT_TAU_FLIGHT_S = 3.0f;
static const float DTERM_ALPHA = 0.33f;
static const int TOF_MAX_MM = 2000;
static const uint8_t TOF_ADDR = 0x29;
static const uint8_t TOF_REG_SYSTEM_INTERRUPT_CLEAR = 0x0B, TOF_REG_RESULT_RANGE_MM = 0x14 + 10;
static const int TOF_AIRBORNE_MM = 25, TOF_RP_I_ENABLE_MM = 30, TOF_GROUNDED_MM = 10;
static const uint8_t TOF_CONFIRM_SAMPLES = 3;
static const uint32_t CONTROL_FADE_MS = 300, YAW_I_RAMP_MS = 250, RP_I_RAMP_MS = 600;

static const uint32_t LINK_TIMEOUT_MS = 500;
static const uint32_t ARM_ZERO_MS = 300, UNKILL_ZERO_MS = 1000;
static const uint32_t I2C_HZ = 400000;
static const float LAND_RAMP_S = 1.5f;
static const float VBAT_MOTORS = 3.3f, VBAT_LOW = 3.3f;
static const float VBAT_ALPHA = 0.01f;
static const float RATE_SP_MAX = 250;
static const float APP_ANGLE_FULL = 15;
static const float APP_YAW_FULL = 100;
static const float AXIS_OUT_MAX = 30;
static const float I_MAX = 15;
static const float I_MIN_THRUST = 20;
static const uint32_t TELEM_MS = 50, HELLO_TIMEOUT_MS = 3000;
static const int MAX_TEL_PEERS = 3;
static const float CHIRP_DUTY = 15;
static const uint32_t CHIRP_MS = 150, CHIRP_GAP_MS = 100;

static const float TRIM_DUTY = 45;
static const uint32_t TRIM_WARN_MS = 3000, TRIM_BASE_MS = 150, TRIM_RAMP_MS = 200;
static const uint32_t TRIM_HOLD_MS = 800, TRIM_GAP_MS = 400, TRIM_ALERT_MS = 5000;
static const int TRIM_BURST = 4;
static const float TRIM_MIN = 0.7f, TRIM_MAX = 1.3f, TRIM_MIN_LOAD_V = 0.01f;
static const float TRIM_GAIN = 0.6f;

static const float LAND_TILT_CUT_DEG = 30;
static const float CRASH_TILT_DEG = 60;
static const uint32_t CRASH_TILT_MS = 200;
static const float TAKEOFF_TILT_MAX_DEG = 30;

static const float HOVER_REF_V = 3.40f, BATTERY_SAG_V = 1.0f;
static const float HOVER_PRIOR_MIN = 40, HOVER_PRIOR_MAX = 70;
static const float VBAT_REST_LOW = 3.80f;

static const float AUTO_LEVEL_MAX_DEG = 5, AUTO_LEVEL_STILL_DPS = 3;
static const uint32_t AUTO_LEVEL_STILL_MS = 2000;
static const int AUTO_LEVEL_MAX_HEIGHT_MM = 15;

static const float ACCEL_OK_MIN_G = 0.8f, ACCEL_OK_MAX_G = 1.2f;
static const uint32_t ACCEL_BAD_REPAIR_MS = 1000;
static const float ARM_ATT_MISMATCH_DEG = 10;
static const float NOLOAD_DEFAULT_V[4] = {0.015f, 0.016f, 0.047f, 0.022f};
static const int BOOT_LOG_LEN = 8;
static const uint32_t USB_DETECT_MS = 1500;

UnitCal unitCal = UNIT_CAL_DEFAULT;
const char *unitCalSource = "default (first airframe)";
char apSsid[32] = "?";
float motorTrim[4] = {1, 1, 1, 1};
float attOffset[2] = {0, 0};

static const uint8_t MPU_DLPF = 3;

float hoverRefPct = 55;
float holdMinMm = 300, holdMaxMm = 800;
float takeoffMm = 500;
static const float HOLD_ENGAGE_BELOW_MM = 100;
static const float ALT_TAU_S = 0.5f;
static const float G_MM_S2 = 9806.65f;
float camRes = 1;

static const float ANGLE_KP = 5;
static const float RATE_KP[3] = {0.18f, 0.15f, 0.30f};
static const float RATE_KI[3] = {0.35f, 0.35f, 0.40f};
static const float RATE_KD[3] = {0.0018f, 0.0014f, 0};
float angleCmdMax = 10.0f;
float angleCmdExpo = 0.70f;
float yawCmdMax = 50.0f;
float yawCmdExpo = 0.60f;

enum : uint8_t { P_INT = 1, P_SAVED = 2, P_TRIM = 4 };
static const char *applyCamera();
static const char *applyHoldMin();
static const char *applyHoldMax();

static const struct {
  const char *name; float *v, lo, hi; uint8_t flags; const char *(*apply)();
} PARAMS[] = {
  {"angle_cmd_max", &angleCmdMax, 0, 30, P_SAVED},
  {"angle_cmd_expo", &angleCmdExpo, 0, 1, P_SAVED},
  {"yaw_cmd_max", &yawCmdMax, 0, RATE_SP_MAX, P_SAVED},
  {"yaw_cmd_expo", &yawCmdExpo, 0, 1, P_SAVED},
  {"takeoff_mm", &takeoffMm, 200, 1100, P_SAVED},
  {"hold_min", &holdMinMm, 200, 1100, P_SAVED, applyHoldMin},
  {"hold_max", &holdMaxMm, 200, 1100, P_SAVED, applyHoldMax},
  {"hover_ref", &hoverRefPct, HOVER_PRIOR_MIN, HOVER_PRIOR_MAX, P_SAVED},
  {"cam_res", &camRes, sparrow::CAMERA_RES_MIN, sparrow::CAMERA_RES_MAX, P_INT | P_SAVED, applyCamera},
  {"roll_off", &attOffset[0], -5, 5, P_TRIM}, {"pitch_off", &attOffset[1], -5, 5, P_TRIM},
};
static const size_t PARAM_COUNT = sizeof(PARAMS) / sizeof(PARAMS[0]);
float paramDefaults[PARAM_COUNT];

struct __attribute__((packed)) CommanderPacket {
  uint8_t header;
  float roll, pitch, yaw;
  uint16_t thrust;
};

struct __attribute__((packed)) AltHoldPacket {
  uint8_t header, type;
  float roll, pitch, yaw;
  float climbMS;
};

struct BootRec {
  uint32_t boot;
  uint8_t reason;
  uint8_t usb;
  uint16_t vbatMv;
  uint32_t apMs, staMs, appMs;
};

enum State { IDLE, FLYING, LANDING, KILLED };
static const char *STATE_NAMES[] = {"IDLE", "FLYING", "LANDING", "KILLED"};

VL53L0X tof;
Preferences prefs;
WiFiUDP appUdp, telUdp;
bool tofOk = false, motorsEnabled = false, onUsb = false;
bool airborne = false, rpIntegratorReady = false;
uint8_t tofHighSamples = 0, tofRpIHighSamples = 0, tofLowSamples = 0;
uint32_t airborneSinceMs = 0, rpIntegratorSinceMs = 0;

float gyroBias[3] = {0, 0, 0};
float trim[2] = {0, 0};
float storedTrim[2] = {0, 0};
const char *levelStatus = "stored";
double levelSum[3] = {0, 0, 0};
uint32_t levelSamples = 0, levelStillSinceMs = 0;
float lastAcc[3] = {0, 0, -1};
uint32_t accelBadSinceMs = 0, mpuRepairs = 0;
float accUpSum = 0;
uint16_t accUpCount = 0;
bool accFaultReported = false;
float align[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
float att[3] = {0, 0, 0};
float rate[3] = {0, 0, 0};
float iTerm[3] = {0, 0, 0}, dFilt[3] = {0, 0, 0}, prevRate[3] = {0, 0, 0};
float processedSp[3] = {0, 0, 0}, rateSetpoint[3] = {0, 0, 0};
float pidP[3] = {0, 0, 0}, pidD[3] = {0, 0, 0}, pidOut[3] = {0, 0, 0};
float iScale[3] = {0, 0, 0};
float motor[4] = {0, 0, 0, 0};
sparrow::MixerResult mixState = {};
sparrow::FlightBlackbox blackbox;
sparrow::SoftI2C i2c;
sparrow::AutoLand lander;
sparrow::VelMode vel;
float noloadDropV[4] = {0, 0, 0, 0};
float lastLoadedDropV[4] = {0, 0, 0, 0};
float lastRawTrim[4] = {1, 1, 1, 1};
bool motorTrimStored = false;
const char *trimStatus = "not stored (the next battery boot measures it)";
uint32_t trimAlertUntilMs = 0;
enum TrimJob : uint8_t { TRIM_JOB_NONE, TRIM_JOB_MEASURE, TRIM_JOB_NOLOAD };
enum TrimPhase : uint8_t { TRIM_WARN, TRIM_BASE, TRIM_SPIN, TRIM_GAP };
struct TrimRun {
  TrimJob job;
  TrimPhase phase;
  bool fromCommand;
  int motor;
  uint32_t phaseMs;
  float sum, before;
  int n;
  float drops[4];
};
TrimRun trimRun = {};
static bool trimRunning() { return trimRun.job != TRIM_JOB_NONE; }
static bool trimWarnLed(uint32_t sinceMs) { return (sinceMs / 250) % 2 == 0; }
float baseThrust = 0, landFrom = 0, vbat = 0;
float motorDuty = 0;
bool hoverShiftReported = false;
float holdSurfaceReportedMm = 0;
float holdTargetReportedMm = 0;
int tofMm = -1, heightMm = -1;

CommanderPacket sp = {};
uint32_t lastRxMs = 0, rxCount = 0, zeroSinceMs = 0;
bool linkUp = false, thrustZero = false, armReady = false;
bool velMode = false;
bool flightVel = false;
float spClimbMmS = 0;
uint32_t takeoffAskedMs = 0;
bool landAsked = false;
bool bleReady = false, beaconOn = false;
uint32_t beaconDataMs = 0;
uint32_t lastArpGreetMs = 0, arpGreetings = 0;

State state = IDLE;
uint32_t crashSinceMs = 0;
uint32_t landStartMs = 0, lastTelemMs = 0, lastLoopUs = 0, loopUsMax = 0;
static const uint32_t IMU_ANSWER_MS = 100;
uint32_t imuAnsweredMs = 0;
uint32_t imuMissTotal = 0, imuMissStreak = 0, lastBlackboxMs = 0;
sparrow::BlackboxTrigger landingTrigger = sparrow::BB_NONE;
struct TelPeer {
  IPAddress ip;
  uint32_t ms;
  bool active;
};
TelPeer telPeers[MAX_TEL_PEERS];
uint32_t cmdRxCount = 0, peerExpiries = 0;
char telLine[640];
uint8_t telPending = 0;
String serialLine;
BootRec bootLog[BOOT_LOG_LEN];
int bootIdx = 0;

enum LoopSection { LS_IMU, LS_APP, LS_CMD, LS_STATE, LS_CONTROL, LS_TOF, LS_ADC, LS_TELEM, LS_OTHER, LS_COUNT };
static const char *LOOP_SECTION_NAMES[LS_COUNT] = {"imu", "app", "cmd", "state", "control",
                                                   "tof", "adc", "telem", "other"};
static const uint32_t SLOW_LOOP_US = 5000, SLOW_UDP_US = 2000;
struct LoopProfile {
  uint32_t loops, slowLoops, worstUs;
  uint32_t worst[LS_COUNT];
  uint32_t sectionMax[LS_COUNT];
  uint32_t udpSends, udpSlow, udpMaxUs;
};
LoopProfile loopProfile = {};
bool loopProfileReported = true;

static void reply(const char *fmt, ...);
static void startTrimRun(TrimJob job, bool warn, bool fromCommand);
static void stepTrimRun(uint32_t now);
static void abortTrimRun(const char *why);
static void saveMotorTrim();
static void chirpMotors();
static void reportTrim();
static void pollTof();
static float vbatMean(uint32_t ms);
static float vbatNoLoad();
static float hoverPrior();

static bool i2cRead(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n) {
  return i2c.read(addr, reg, buf, n);
}

static bool i2cWrite(uint8_t addr, uint8_t reg, uint8_t val) {
  return i2c.write(addr, reg, val);
}

static void configureMpu() {
  i2cWrite(MPU_ADDR, 0x6B, 0x01);
  delay(10);
  i2cWrite(MPU_ADDR, 0x1A, MPU_DLPF);
  i2cWrite(MPU_ADDR, 0x1B, 0x18);
  i2cWrite(MPU_ADDR, 0x1C, 0x10);
  i2cWrite(MPU_ADDR, 0x19, 0x00);
}

static const char *applyCamera() {
  sparrow::cameraConfigure((int)camRes);
  return "";
}
static const char *applyHoldMin() {
  if (holdMinMm <= holdMaxMm) return "";
  reply("# hold_min refused: above hold_max (%.0f)", holdMaxMm);
  return nullptr;
}
static const char *applyHoldMax() {
  if (holdMaxMm >= holdMinMm) return "";
  reply("# hold_max refused: below hold_min (%.0f)", holdMinMm);
  return nullptr;
}

static void loadSavedParams() {
  for (auto &p : PARAMS) {
    if (!(p.flags & P_SAVED)) continue;
    float v = prefs.getFloat(p.name, *p.v);
    if (p.flags & P_INT) v = roundf(v);
    *p.v = constrain(v, p.lo, p.hi);
  }
}

static bool readImu(float acc[3], float gyro[3]) {
  uint8_t b[14];
  if (!i2cRead(MPU_ADDR, 0x3B, b, 14)) return false;
  imuAnsweredMs = millis();
  auto s = [&](int i) { return (int16_t)((b[i] << 8) | b[i + 1]); };
  float ax = (s(0) / ACC_LSB - unitCal.accBias[0]) * unitCal.accScale[0];
  float ay = (s(2) / ACC_LSB - unitCal.accBias[1]) * unitCal.accScale[1];
  float az = (s(4) / ACC_LSB - unitCal.accBias[2]) * unitCal.accScale[2];
  float gx = s(8) / GYRO_LSB, gy = s(10) / GYRO_LSB, gz = s(12) / GYRO_LSB;
  float a[3] = {ax, -ay, -az}, g[3] = {gx, -gy, -gz};
  for (int i = 0; i < 3; i++) {
    acc[i] = align[i][0] * a[0] + align[i][1] * a[1] + align[i][2] * a[2];
    gyro[i] = align[i][0] * g[0] + align[i][1] * g[1] + align[i][2] * g[2];
  }
  return true;
}

static void buildAlign() {
  float r = trim[0] * DEG_TO_RAD, p = trim[1] * DEG_TO_RAD;
  float cr = cosf(r), sr = sinf(r), cp = cosf(p), sp = sinf(p);
  float m[3][3] = {{cp, sp * sr, sp * cr}, {0, cr, -sr}, {-sp, cp * sr, cp * cr}};
  memcpy(align, m, sizeof(align));
}

static void accelAngles(const float a[3], float &r, float &p) {
  r = atan2f(-a[1], -a[2]) * RAD_TO_DEG;
  p = atan2f(a[0], sqrtf(a[1] * a[1] + a[2] * a[2])) * RAD_TO_DEG;
}

static float verticalAccelMmS2(const float a[3]) {
  float sr = sinf(att[0] * DEG_TO_RAD), cr = cosf(att[0] * DEG_TO_RAD);
  float sp = sinf(att[1] * DEG_TO_RAD), cp = cosf(att[1] * DEG_TO_RAD);
  float down = -sp * a[0] + sr * cp * a[1] + cr * cp * a[2];
  return -(down + 1.0f) * G_MM_S2;
}

static bool levelTrim() {
  trim[0] = trim[1] = 0;
  buildAlign();
  double sum[3] = {0, 0, 0};
  int n = 0;
  float acc[3], gyro[3];
  for (int i = 0; i < 250; i++) {
    if (readImu(acc, gyro)) {
      for (int k = 0; k < 3; k++) sum[k] += acc[k];
      n++;
    }
    delay(2);
  }
  if (n < 200) return false;
  float a[3] = {(float)(sum[0] / n), (float)(sum[1] / n), (float)(sum[2] / n)};
  accelAngles(a, trim[0], trim[1]);
  buildAlign();
  prefs.putFloat("trim_r", trim[0]);
  prefs.putFloat("trim_p", trim[1]);
  storedTrim[0] = trim[0];
  storedTrim[1] = trim[1];
  levelStatus = "stored";
  return true;
}

static bool applyAutoLevel(const float a[3], const char *source) {
  float dr, dp;
  accelAngles(a, dr, dp);
  float r = trim[0] + dr, p = trim[1] + dp;
  if (fabsf(r - storedTrim[0]) > AUTO_LEVEL_MAX_DEG || fabsf(p - storedTrim[1]) > AUTO_LEVEL_MAX_DEG) {
    levelStatus = "rejected: frame not flat";
    return false;
  }
  trim[0] = r;
  trim[1] = p;
  buildAlign();
  att[0] = att[1] = 0;
  levelStatus = source;
  return true;
}

static bool restingOnGround() {
  return heightMm < 0 ? !tofOk : heightMm <= AUTO_LEVEL_MAX_HEIGHT_MM;
}

static void bootAutoLevel() {
  for (int i = 0; i < 10 && heightMm < 0; i++) {
    pollTof();
    delay(20);
  }
  if (!restingOnGround()) {
    levelStatus = "skipped: not on the ground";
    return;
  }
  double sum[3] = {0, 0, 0};
  int n = 0;
  float acc[3], gyro[3];
  for (int i = 0; i < 250; i++) {
    if (readImu(acc, gyro)) {
      for (int k = 0; k < 3; k++) sum[k] += acc[k];
      n++;
    }
    delay(2);
  }
  if (n < 200) return;
  float a[3] = {(float)(sum[0] / n), (float)(sum[1] / n), (float)(sum[2] / n)};
  applyAutoLevel(a, "auto at boot");
}

static void checkAccelHealth(uint32_t now, const float acc[3]) {
  float g = sqrtf(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);
  bool quiet = fabsf(rate[0]) < 5 && fabsf(rate[1]) < 5 && fabsf(rate[2]) < 5;
  if (!quiet || (g >= ACCEL_OK_MIN_G && g <= ACCEL_OK_MAX_G)) {
    accelBadSinceMs = 0;
    return;
  }
  if (accelBadSinceMs == 0) accelBadSinceMs = now;
  if (now - accelBadSinceMs < ACCEL_BAD_REPAIR_MS) return;
  reply("# IMU accel abnormal at rest: acc %.3f %.3f %.3f |a| %.3f g; rewriting MPU config (%lu)",
        acc[0], acc[1], acc[2], g, (unsigned long)(mpuRepairs + 1));
  configureMpu();
  mpuRepairs++;
  accelBadSinceMs = 0;
}

static bool imuAnswering() { return imuAnsweredMs && millis() - imuAnsweredMs < IMU_ANSWER_MS; }

static bool attitudeTrusted(char *why, size_t n) {
  if (!imuAnswering()) {
    snprintf(why, n, "no IMU");
    return false;
  }
  float g = sqrtf(lastAcc[0] * lastAcc[0] + lastAcc[1] * lastAcc[1] + lastAcc[2] * lastAcc[2]);
  if (g < ACCEL_OK_MIN_G || g > ACCEL_OK_MAX_G) {
    snprintf(why, n, "accel |a| %.2f g", g);
    return false;
  }
  float ar, ap;
  accelAngles(lastAcc, ar, ap);
  if (max(fabsf(ar), fabsf(ap)) > TAKEOFF_TILT_MAX_DEG) {
    snprintf(why, n, "frame tilted roll %.0f pitch %.0f deg", ar, ap);
    return false;
  }
  float dr = fabsf(att[0] - ar), dp = fabsf(att[1] - ap);
  if (dr > 180) dr = 360 - dr;
  if (dr > ARM_ATT_MISMATCH_DEG || dp > ARM_ATT_MISMATCH_DEG) {
    snprintf(why, n, "attitude %.0f/%.0f vs accel %.0f/%.0f deg", att[0], att[1], ar, ap);
    return false;
  }
  return true;
}

static void groundLevelSample(uint32_t now, const float acc[3]) {
  float g = sqrtf(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);
  bool still = restingOnGround() && g > 0.9f && g < 1.1f;
  for (int k = 0; k < 3; k++) still = still && fabsf(rate[k]) < AUTO_LEVEL_STILL_DPS;
  if (!still) {
    levelStillSinceMs = 0;
    return;
  }
  if (levelStillSinceMs == 0) {
    levelStillSinceMs = now;
    levelSamples = 0;
    for (double &s : levelSum) s = 0;
  }
  for (int k = 0; k < 3; k++) levelSum[k] += acc[k];
  levelSamples++;
  if (now - levelStillSinceMs >= AUTO_LEVEL_STILL_MS && levelSamples > 100) {
    float a[3] = {(float)(levelSum[0] / levelSamples), (float)(levelSum[1] / levelSamples),
                  (float)(levelSum[2] / levelSamples)};
    applyAutoLevel(a, "auto on the ground");
    levelStillSinceMs = 0;
  }
}

static void calibrateGyro() {
  for (int attempt = 0; attempt < 10; attempt++) {
    double sum[3] = {0, 0, 0};
    float lo[3] = {1e9, 1e9, 1e9}, hi[3] = {-1e9, -1e9, -1e9};
    float acc[3], gyro[3];
    int n = 0;
    for (int i = 0; i < 500; i++) {
      if (readImu(acc, gyro)) {
        for (int k = 0; k < 3; k++) {
          sum[k] += gyro[k];
          lo[k] = min(lo[k], gyro[k]);
          hi[k] = max(hi[k], gyro[k]);
        }
        n++;
      }
      delay(2);
    }
    bool still = n > 400;
    for (int k = 0; k < 3; k++) still = still && hi[k] - lo[k] < 8;
    if (still) {
      for (int k = 0; k < 3; k++) gyroBias[k] = sum[k] / n;
      if (readImu(acc, gyro)) accelAngles(acc, att[0], att[1]);
      att[2] = 0;
      if (onUsb) Serial.printf("# gyro bias (FRD) = %.2f %.2f %.2f dps\n", gyroBias[0], gyroBias[1], gyroBias[2]);
      return;
    }
    if (onUsb) Serial.println("# board moving, retrying gyro calibration");
  }
}

static float readVbat() {
  uint32_t mv = 0;
  for (int i = 0; i < 16; i++) mv += analogReadMilliVolts(PIN_BAT_ADC);
  return mv / 16 * 2 / 1000.0f * unitCal.vbatCal;
}

static float vbatSampleV() { return analogReadMilliVolts(PIN_BAT_ADC) * 2 / 1000.0f * unitCal.vbatCal; }

static bool unitCalPlausible(const UnitCal &c) {
  for (int k = 0; k < 3; k++)
    if (!(fabsf(c.accBias[k]) < 0.3f && c.accScale[k] > 0.9f && c.accScale[k] < 1.1f)) return false;
  return c.vbatCal > 0.9f && c.vbatCal < 1.1f && c.tofGroundMm > 0 && c.tofGroundMm < 150;
}

static void loadUnitCal() {
  UnitCal c;
  if (prefs.getBytes("unitcal", &c, sizeof(c)) == sizeof(c) && unitCalPlausible(c)) {
    unitCal = c;
    unitCalSource = "flash";
  }
}

static bool saveUnitCal(const UnitCal &c) {
  if (!unitCalPlausible(c)) return false;
  unitCal = c;
  prefs.putBytes("unitcal", &unitCal, sizeof(unitCal));
  unitCalSource = "flash";
  return true;
}

static void reportUnitCal() {
  reply("# unitcal %s on %s: acc bias %.4f %.4f %.4f scale %.4f %.4f %.4f", unitCalSource, apSsid,
        unitCal.accBias[0], unitCal.accBias[1], unitCal.accBias[2], unitCal.accScale[0],
        unitCal.accScale[1], unitCal.accScale[2]);
  reply("# unitcal vbat_cal %.6f tof_ground %.2f mm", unitCal.vbatCal, unitCal.tofGroundMm);
}

static bool rawAccelMean(float mean[3], float &spread) {
  double sum[3] = {0, 0, 0}, sq[3] = {0, 0, 0};
  int n = 0;
  for (int i = 0; i < 250; i++, delay(2)) {
    uint8_t b[6];
    if (!i2cRead(MPU_ADDR, 0x3B, b, 6)) continue;
    for (int k = 0; k < 3; k++) {
      float v = (int16_t)((b[2 * k] << 8) | b[2 * k + 1]) / ACC_LSB;
      sum[k] += v;
      sq[k] += v * v;
    }
    n++;
  }
  if (n < 200) return false;
  spread = 0;
  for (int k = 0; k < 3; k++) {
    mean[k] = sum[k] / n;
    spread = max(spread, sqrtf(max(0.0, sq[k] / n - mean[k] * mean[k])));
  }
  return true;
}

static bool measureTofGround(float &meanMm, float &sdMm) {
  double sum = 0, sq = 0;
  int n = 0;
  float c = cosf(att[0] * DEG_TO_RAD) * cosf(att[1] * DEG_TO_RAD);
  for (uint32_t t0 = millis(); n < 48 && millis() - t0 < 5000; delay(2)) {
    if (digitalRead(PIN_TOF_INT) != LOW) continue;
    pollTof();
    if (tofMm <= 0) continue;
    sum += tofMm * c;
    sq += (double)tofMm * c * tofMm * c;
    n++;
  }
  if (n < 40) return false;
  meanMm = sum / n;
  sdMm = sqrtf(max(0.0, sq / n - (double)meanMm * meanMm));
  return true;
}

static void handleUnitCal(const String &line) {
  if (line == "unitcal") { reportUnitCal(); return; }
  if (state != IDLE && state != KILLED) { reply("# unitcal refused: not on the ground"); return; }
  UnitCal c = unitCal;
  if (line == "unitcal raw") {
    float m[3], spread;
    if (!rawAccelMean(m, spread)) { reply("# unitcal raw failed: imu read"); return; }
    reply("# unitcal raw %.4f %.4f %.4f g spread %.4f", m[0], m[1], m[2], spread);
    return;
  }
  if (line.startsWith("unitcal acc ")) {
    if (sscanf(line.c_str() + 12, "%f %f %f %f %f %f", &c.accBias[0], &c.accBias[1], &c.accBias[2],
               &c.accScale[0], &c.accScale[1], &c.accScale[2]) != 6) {
      reply("# usage: unitcal acc <bias x y z> <scale x y z>");
      return;
    }
  } else if (line.startsWith("unitcal vbat ")) {
    float meter = line.substring(13).toFloat(), measured = vbatMean(1000);
    if (measured < 3.0f || meter < 3.0f) { reply("# unitcal vbat refused: battery not connected"); return; }
    c.vbatCal = unitCal.vbatCal * meter / measured;
  } else if (line == "unitcal tof") {
    float sd;
    if (!tofOk || !measureTofGround(c.tofGroundMm, sd)) { reply("# unitcal tof failed: too few ranges"); return; }
    reply("# unitcal tof: mean %.2f mm, sd %.2f mm", c.tofGroundMm, sd);
    if (sd > 4) { reply("# unitcal tof refused: readings too noisy"); return; }
  } else if (line == "unitcal reset") {
    prefs.remove("unitcal");
    unitCal = UNIT_CAL_DEFAULT;
    unitCalSource = "default (first airframe)";
    reportUnitCal();
    return;
  } else {
    reply("# unitcal commands: unitcal | raw | acc <bx by bz sx sy sz> | vbat <meter V> | tof | reset");
    return;
  }
  if (!saveUnitCal(c)) { reply("# unitcal refused: value out of range"); return; }
  if (line.startsWith("unitcal vbat ")) vbat = readVbat();
  reportUnitCal();
}

static void resetAirborneDetection() {
  airborne = rpIntegratorReady = false;
  airborneSinceMs = rpIntegratorSinceMs = 0;
  tofHighSamples = tofRpIHighSamples = tofLowSamples = 0;
}

static float rampFrom(uint32_t sinceMs, uint32_t durationMs) {
  if (sinceMs == 0 || durationMs == 0) return 0.0f;
  return sparrow::clampf((millis() - sinceMs) / (float)durationMs, 0.0f, 1.0f);
}

static float takeoffRamp(uint32_t durationMs) {
  return airborne ? rampFrom(airborneSinceMs, durationMs) : 0.0f;
}

static void updateAirborneFromTof() {
  bool active = state == FLYING || state == LANDING;
  if (!active) {
    resetAirborneDetection();
    return;
  }
  if (heightMm < 0) {
    tofHighSamples = tofRpIHighSamples = tofLowSamples = 0;
    return;
  }

  if (!airborne) {
    rpIntegratorReady = false;
    rpIntegratorSinceMs = 0;
    tofRpIHighSamples = 0;
    tofLowSamples = 0;
    if (baseThrust >= I_MIN_THRUST && heightMm >= TOF_AIRBORNE_MM) {
      if (tofHighSamples < TOF_CONFIRM_SAMPLES) tofHighSamples++;
      if (tofHighSamples >= TOF_CONFIRM_SAMPLES) {
        airborne = true;
        airborneSinceMs = millis();
        tofHighSamples = 0;
        for (float &i : iTerm) i = 0;
        reply("# AIRBORNE: height %d mm, controls/yaw-I ramp started", heightMm);
      }
    } else {
      tofHighSamples = 0;
    }
    return;
  }

  tofHighSamples = 0;
  if (heightMm <= TOF_GROUNDED_MM) {
    if (tofLowSamples < TOF_CONFIRM_SAMPLES) tofLowSamples++;
    if (tofLowSamples >= TOF_CONFIRM_SAMPLES) {
      airborne = false;
      rpIntegratorReady = false;
      airborneSinceMs = 0;
      rpIntegratorSinceMs = 0;
      tofLowSamples = 0;
      tofRpIHighSamples = 0;
      for (float &i : iTerm) i = 0;
      reply("# GROUNDED: height %d mm, I reset", heightMm);
    }
  } else {
    tofLowSamples = 0;
  }

  if (!airborne || rpIntegratorReady) {
    tofRpIHighSamples = 0;
    return;
  }
  if (baseThrust >= I_MIN_THRUST && heightMm >= TOF_RP_I_ENABLE_MM) {
    if (tofRpIHighSamples < TOF_CONFIRM_SAMPLES) tofRpIHighSamples++;
    if (tofRpIHighSamples >= TOF_CONFIRM_SAMPLES) {
      rpIntegratorReady = true;
      rpIntegratorSinceMs = millis();
      tofRpIHighSamples = 0;
      reply("# ROLL/PITCH I RAMP STARTED: height %d mm", heightMm);
    }
  } else {
    tofRpIHighSamples = 0;
  }
}

static void pollTof() {
  if (!tofOk || digitalRead(PIN_TOF_INT) != LOW) return;
  uint8_t b[2];
  bool ok = i2cRead(TOF_ADDR, TOF_REG_RESULT_RANGE_MM, b, 2);
  i2cWrite(TOF_ADDR, TOF_REG_SYSTEM_INTERRUPT_CLEAR, 0x01);
  int mm = ok ? (b[0] << 8) | b[1] : -1;
  tofMm = (!ok || mm <= 0 || mm >= TOF_MAX_MM) ? -1 : mm;
  float c = cosf(att[0] * DEG_TO_RAD) * cosf(att[1] * DEG_TO_RAD);
  if (tofMm > 0 && c > 0.5f) {
    int h = (int)lroundf(tofMm * c - unitCal.tofGroundMm);
    heightMm = h > 0 ? h : 0;
    lander.observe(millis(), heightMm);
  } else {
    heightMm = -1;
  }
  updateAirborneFromTof();
}

static void updateAttitude(const float acc[3], float dt) {
  float sr = sinf(att[0] * DEG_TO_RAD), cr = cosf(att[0] * DEG_TO_RAD);
  float cp = cosf(att[1] * DEG_TO_RAD), tp = tanf(att[1] * DEG_TO_RAD);
  if (fabsf(cp) < 0.05f) cp = copysignf(0.05f, cp);
  float p = rate[0], q = rate[1], r = rate[2];
  att[0] += (p + (q * sr + r * cr) * tp) * dt;
  att[1] += (q * cr - r * sr) * dt;
  att[2] += (q * sr + r * cr) / cp * dt;
  if (att[2] > 180) att[2] -= 360;
  if (att[2] < -180) att[2] += 360;

  float an = sqrtf(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);
  if (an > 0.75f && an < 1.25f) {
    float ar, ap;
    accelAngles(acc, ar, ap);
    float tau = (state == FLYING || state == LANDING) ? ATT_TAU_FLIGHT_S : ATT_TAU_S;
    float k = dt / (tau + dt);
    att[0] += k * (ar - att[0]);
    att[1] += k * (ap - att[1]);
  }
}

static uint8_t checksum(const uint8_t *buf, size_t n) {
  uint8_t s = 0;
  for (size_t i = 0; i < n; i++) s += buf[i];
  return s;
}

static void pollApp() {
  while (appUdp.parsePacket() > 0) {
    uint8_t buf[64];
    int n = appUdp.read(buf, sizeof(buf));
    appUdp.flush();
    if (n < 2 || checksum(buf, n - 1) != buf[n - 1]) continue;
    if (buf[0] == CRTP_HEADER_COMMANDER && n - 1 == sizeof(CommanderPacket)) {
      CommanderPacket p;
      memcpy(&p, buf, sizeof(p));
      if (!isfinite(p.roll) || !isfinite(p.pitch) || !isfinite(p.yaw)) continue;
      sp = p;
      velMode = false;
    } else if (buf[0] == CRTP_HEADER_GENERIC && n - 1 == sizeof(AltHoldPacket)
               && buf[1] == GENERIC_ALT_HOLD) {
      AltHoldPacket p;
      memcpy(&p, buf, sizeof(p));
      if (!isfinite(p.roll) || !isfinite(p.pitch) || !isfinite(p.yaw) || !isfinite(p.climbMS)) continue;
      sp = {CRTP_HEADER_COMMANDER, p.roll, p.pitch, p.yaw, 0};
      float most = vel.config().maxMmS;
      spClimbMmS = sparrow::clampf(p.climbMS * 1000.0f, -most, most);
      velMode = true;
    } else {
      continue;
    }
    lastRxMs = millis();
    rxCount++;
  }
}

static void sendUdp(const IPAddress &ip, const char *buf, bool telemetry = false) {
  uint32_t t = micros();
  if (telemetry) sparrow::netSendLatest((uint32_t)ip, buf);
  else sparrow::netSend((uint32_t)ip, buf);
  uint32_t us = micros() - t;
  if (state != FLYING && state != LANDING) return;
  loopProfile.udpSends++;
  if (us > SLOW_UDP_US) loopProfile.udpSlow++;
  loopProfile.udpMaxUs = max(loopProfile.udpMaxUs, us);
}

static void reply(const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (onUsb) Serial.println(buf);
  for (auto &p : telPeers)
    if (p.active) sendUdp(p.ip, buf);
}

static void recordLoop(const uint32_t ts[LS_COUNT + 1]) {
  if (state != FLYING && state != LANDING) return;
  LoopProfile &p = loopProfile;
  uint32_t total = ts[LS_COUNT] - ts[0];
  p.loops++;
  if (total > SLOW_LOOP_US) p.slowLoops++;
  for (int k = 0; k < LS_COUNT; k++) p.sectionMax[k] = max(p.sectionMax[k], ts[k + 1] - ts[k]);
  if (total > p.worstUs) {
    p.worstUs = total;
    for (int k = 0; k < LS_COUNT; k++) p.worst[k] = ts[k + 1] - ts[k];
  }
}

static void reportLoopProfile(const char *what) {
  const LoopProfile &p = loopProfile;
  char worst[200], maxes[200];
  int a = 0, b = 0;
  for (int k = 0; k < LS_COUNT; k++) {
    a += snprintf(worst + a, sizeof(worst) - a, " %s %lu", LOOP_SECTION_NAMES[k], (unsigned long)p.worst[k]);
    b += snprintf(maxes + b, sizeof(maxes) - b, " %s %lu", LOOP_SECTION_NAMES[k], (unsigned long)p.sectionMax[k]);
    if (a >= (int)sizeof(worst) || b >= (int)sizeof(maxes)) break;
  }
  reply("# loop (%s): %lu of %lu loops over %lu us, slowest %lu us =%s", what,
        (unsigned long)p.slowLoops, (unsigned long)p.loops, (unsigned long)SLOW_LOOP_US,
        (unsigned long)p.worstUs, worst);
  reply("# loop section max us:%s; udp send max %lu us, %lu of %lu over %lu us", maxes,
        (unsigned long)p.udpMaxUs, (unsigned long)p.udpSlow, (unsigned long)p.udpSends,
        (unsigned long)SLOW_UDP_US);
}

static int16_t blackboxSigned(float value, float scale) {
  long q = lroundf(value * scale);
  return (int16_t)constrain(q, -32768L, 32767L);
}

static uint16_t blackboxUnsigned(float value, float scale) {
  long q = lroundf(value * scale);
  return (uint16_t)constrain(q, 0L, 65535L);
}

static void captureBlackbox(uint32_t now, sparrow::BlackboxTrigger event = sparrow::BB_NONE) {
  sparrow::BlackboxSample sample = {};
  sample.ms = now;
  for (int k = 0; k < 3; k++) {
    sample.attitudeCd[k] = blackboxSigned(att[k], 100.0f);
    sample.rateDps10[k] = blackboxSigned(rate[k], 10.0f);
    float raw = k == 0 ? sp.roll : (k == 1 ? sp.pitch : sp.yaw);
    sample.rawSetpoint10[k] = blackboxSigned(raw, 10.0f);
    sample.setpoint10[k] = blackboxSigned(processedSp[k], 10.0f);
    sample.pidP100[k] = blackboxSigned(pidP[k], 100.0f);
    sample.pidI100[k] = blackboxSigned(iTerm[k], 100.0f);
    sample.pidD100[k] = blackboxSigned(pidD[k], 100.0f);
    sample.pidOut100[k] = blackboxSigned(pidOut[k], 100.0f);
  }
  if (accUpCount > 0) sample.accUpMmS2 = blackboxSigned(accUpSum / accUpCount, 1.0f);
  accUpSum = 0;
  accUpCount = 0;
  sample.manual100 = blackboxUnsigned(linkUp ? sp.thrust * 100.0f / 65535 : 0, 100.0f);
  sample.base100 = blackboxUnsigned(baseThrust, 100.0f);
  for (int k = 0; k < 4; k++) sample.motor100[k] = blackboxUnsigned(motor[k], 100.0f);
  sample.vbatMv = blackboxUnsigned(vbat, 1000.0f);
  sample.tofMm = (int16_t)constrain(tofMm, -1, 32767);
  sample.heightMm = (int16_t)constrain(heightMm, -1, 32767);
  sample.altitudeFilteredMm = blackboxSigned(lander.heldHeightMm(), 1.0f);
  sample.verticalSpeedMmS = blackboxSigned(lander.verticalSpeedMmS(), 1.0f);
  sample.altitudeLimit100 = blackboxUnsigned(lander.hoverEstimate(), 100.0f);
  uint32_t linkAge = rxCount ? now - lastRxMs : 0;
  sample.linkAgeMs = (uint16_t)min(linkAge, (uint32_t)65535);
  sample.loopUs = (uint16_t)min(loopUsMax, (uint32_t)65535);
  sample.state = (uint8_t)state;
  sample.flags = (airborne ? 1u : 0u) | (rpIntegratorReady ? 2u : 0u)
               | (linkUp ? 4u : 0u) | (motorsEnabled ? 8u : 0u) | (lander.holding() ? 16u : 0u);
  sample.saturationMask = mixState.satMask;
  sample.event = (uint8_t)event;
  sample.imuMissTotal = (uint16_t)min(imuMissTotal, (uint32_t)65535);
  sample.flags |= (uint8_t)(min(imuMissStreak, (uint32_t)7) << 5);
  blackbox.capture(sample);
}

static void sampleBlackbox(uint32_t now) {
  if (now - lastBlackboxMs < sparrow::BLACKBOX_SAMPLE_PERIOD_MS) return;
  lastBlackboxMs = now;
  captureBlackbox(now);
}

static void kill(const char *why, sparrow::BlackboxTrigger trigger) {
  zeroSinceMs = millis();
  takeoffAskedMs = 0;
  captureBlackbox(millis(), trigger);
  blackbox.freeze(trigger);
  if (state != KILLED) reply("# KILLED: %s", why);
  abortTrimRun("kill");
  state = KILLED;
  baseThrust = 0;
  landAsked = false;
  vel.reset();
  resetAirborneDetection();
}

static float chipTemperature(temp_sensor_dac_offset_t range) {
  temp_sensor_config_t c = TSENS_CONFIG_DEFAULT();
  c.dac_offset = range;
  float t = NAN;
  temp_sensor_set_config(c);
  temp_sensor_start();
  temp_sensor_read_celsius(&t);
  temp_sensor_stop();
  return t;
}

static void replyHardware() {
  reply("# hardware imu %s, tof %s, camera %s, blackbox %s", imuAnsweredMs ? "ok" : "missing",
        tofOk ? "ok" : "missing", sparrow::cameraHardware(), blackbox.fsOk() ? "ok" : "failed");
}

static void handleCommand(String line) {
  line.trim();
  if (line.isEmpty() || line == "hello") return;
  if (line == "kill") { kill("command", sparrow::BB_KILL_COMMAND); return; }
  if (trimRunning() && line != "get" && line != "hw" && line != "params" && line != "trim"
      && line != "imu") {
    reply("# %s refused: motor trim measurement running (kill stops it)", line.c_str());
    return;
  }
  if (line == "takeoff") {
    if (state != IDLE) { reply("# takeoff ignored: %s", STATE_NAMES[state]); return; }
    if (!linkUp || !velMode) { reply("# take-off refused: the App is not in velocity mode"); return; }
    takeoffAskedMs = millis();
    return;
  }
  if (line == "land") {
    if (state == FLYING || state == LANDING) landAsked = true;
    else reply("# land ignored: %s", STATE_NAMES[state]);
    return;
  }
  if (line == "hw") {
    replyHardware();
    return;
  }
  if (line == "get") {
    reply("# firmware %s, built %s %s", FIRMWARE_VERSION, __DATE__, __TIME__);
    replyHardware();
    char s[208] = "#";
    size_t len = 1;
    for (auto &p : PARAMS) {
      char item[40];
      size_t n = snprintf(item, sizeof(item), " %s=%.4f", p.name, *p.v);
      if (len + n > 200) {
        reply("%s", s);
        len = 1;
      }
      memcpy(s + len, item, n + 1);
      len += n;
    }
    reply("%s", s);
    if (state == FLYING || state == LANDING) {
      reportUnitCal();
      return;
    }
    reply("# takeoff airborne=%d rp_i_ready=%d control=%.2f yaw_i=%.2f rp_i=%.2f tof=%d height=%d mm thresholds=%d/%d/%d mm",
          airborne ? 1 : 0, rpIntegratorReady ? 1 : 0,
          takeoffRamp(CONTROL_FADE_MS), takeoffRamp(YAW_I_RAMP_MS),
          rampFrom(rpIntegratorSinceMs, RP_I_RAMP_MS), tofMm, heightMm,
          TOF_AIRBORNE_MM, TOF_RP_I_ENABLE_MM, TOF_GROUNDED_MM);
    reply("# hold %s: target %.0f mm above the surface (stick 50-80%% -> hold_min %.0f..hold_max %.0f mm), held height %.0f mm, surface %+.0f mm (followed %+.0f mm), filtered %.0f mm, speed %.0f mm/s",
          lander.holding() ? "ON" : "armed",
          lander.holding() ? lander.holdTargetMm()
                           : sparrow::holdTargetForStick(linkUp ? sp.thrust * 100.0f / 65535 : 0,
                                                         holdMinMm, holdMaxMm),
          holdMinMm, holdMaxMm, lander.heldHeightMm(),
          lander.surfaceMm(), lander.holdSurfaceMm(), lander.filteredHeightMm(),
          lander.verticalSpeedMmS());
    reply("# Bluetooth beacon %s", beaconOn ? "on" : "off");
    char stations[80] = "";
    wifi_sta_list_t list = {};
    esp_wifi_ap_get_sta_list(&list);
    for (int i = 0, at = 0; i < list.num && at < (int)sizeof(stations); i++)
      at += snprintf(stations + at, sizeof(stations) - at, " %d", list.sta[i].rssi);
    reply("# WiFi channel %d, %d stations%s%s, %lu ARP greetings", WIFI_CHANNEL, list.num,
          stations[0] ? " at dBm" : "", stations, (unsigned long)arpGreetings);
    char net[200];
    sparrow::netSendStatus(net, sizeof(net));
    reply("# %s", net);
    reply("# received: %lu stick packets, %lu command datagrams; %lu telemetry peers dropped for silence",
          (unsigned long)rxCount, (unsigned long)cmdRxCount, (unsigned long)peerExpiries);
    char camera[240], video[240];
    sparrow::cameraStatus(camera, video, sizeof(camera));
    reply("# %s", camera);
    char bbStatus[200];
    blackbox.status(bbStatus, sizeof(bbStatus));
    reply("# %s", bbStatus);
    if (video[0]) reply("# %s", video);
    reply("# chip %.1f C (-10..80 C range), %.1f C (20..100), %.1f C (50..125)",
          chipTemperature(TSENS_DAC_L2), chipTemperature(TSENS_DAC_L1), chipTemperature(TSENS_DAC_L0));
    reply("# app %s mode: %s, takeoff_mm %.0f, vertical speed %.0f mm/s",
          velMode ? "velocity" : "thrust", velMode ? "0x70 altHold packets" : "0x30 commander packets",
          takeoffMm, spClimbMmS);
    reply("# hover prior %.1f%% at vbat %.2f V (motor duty %.0f%%, no load %.2f V), estimate %.1f%% (%s)",
          hoverPrior(), vbat, motorDuty, vbatNoLoad(), lander.hoverEstimate(),
          lander.hoverTracked() ? "tracked" : "prior");
    const char *mode = lander.inertial() ? "accel-aided"
                       : lander.accFault() ? "ToF only (accel fault)" : "ToF only (no accel yet)";
    reply("# height estimate %s: tau %.2f s, accel bias %.0f mm/s2 (%.3f g), surface steps %lu, clipped ToF %lu, accel rejects %lu, ToF-only speed %.0f mm/s",
          mode, lander.accTauS(), lander.accBiasMmS2(), lander.accBiasMmS2() / G_MM_S2,
          (unsigned long)lander.surfaceSteps(), (unsigned long)lander.clippedSamples(),
          (unsigned long)lander.accRejects(), lander.tofSpeedMmS());
    reply("# soft i2c errors=%lu recoveries=%lu imu_misses=%lu mpu_repairs=%lu",
          (unsigned long)i2c.errors(), (unsigned long)i2c.recoveries(),
          (unsigned long)imuMissTotal, (unsigned long)mpuRepairs);
    reportLoopProfile("last flight");
    reportUnitCal();
    return;
  }
  if (line == "params") {
    for (size_t i = 0; i < PARAM_COUNT; i++) {
      const auto &p = PARAMS[i];
      const char *store = (p.flags & P_SAVED) ? "saved" : (p.flags & P_TRIM) ? "trim-save" : "ram";
      reply("# param %s lo=%g hi=%g default=%g store=%s int=%d", p.name, p.lo, p.hi,
            paramDefaults[i], store, (p.flags & P_INT) ? 1 : 0);
    }
    return;
  }
  if (line.startsWith("unitcal")) { handleUnitCal(line); return; }
  if (line.startsWith("camtest")) {
    long secs = line.length() > 8 ? line.substring(8).toInt() : 30;
    secs = constrain(secs, 1, 600);
    if (sparrow::cameraSelfTest(secs))
      reply("# camtest: a viewer on 127.0.0.1 for %ld s (see \"get\")", secs);
    else reply("# camtest refused: camera not running, or a camtest is still on");
    return;
  }
  if (line == "level") {
    if (state != IDLE && state != KILLED) { reply("# level refused: not on the ground"); return; }
    if (!levelTrim()) { reply("# level failed: imu read"); return; }
    calibrateGyro();
    reply("# level trim saved: roll %.2f pitch %.2f deg", trim[0], trim[1]);
    return;
  }
  if (line == "cal") {
    if (state != IDLE && state != KILLED) { reply("# cal refused: not on the ground"); return; }
    reply("# calibrating gyro, keep still");
    calibrateGyro();
    reply("# gyro bias %.2f %.2f %.2f dps", gyroBias[0], gyroBias[1], gyroBias[2]);
    return;
  }
  if (line.startsWith("bb")) {
    if (!onUsb || (state != IDLE && state != KILLED)) {
      reply("# blackbox refused: USB dry-run and motors-off state required");
      return;
    }
    if (blackbox.saving()) {
      reply("# blackbox refused: still saving the last flight, try again in a second");
      return;
    }
    if (line == "bb list" || line == "bb info") {
      blackbox.list(Serial);
      return;
    }
    if (line == "bb dump" || line.startsWith("bb dump ")) {
      uint32_t sequence = line.length() > 8 ? (uint32_t)line.substring(8).toInt() : 0;
      if (!blackbox.dump(Serial, sequence)) reply("# no valid blackbox log");
      return;
    }
    if (line == "bb erase") {
      reply("# blackbox erase %s", blackbox.erase() ? "ok" : "FAILED");
      return;
    }
    reply("# blackbox commands: bb list | bb dump [seq] | bb erase");
    return;
  }
  if (line == "trim") { reportTrim(); return; }
  if (line == "imu") {
    float acc[3], gyro[3], ar = 0, ap = 0;
    bool ok = readImu(acc, gyro);
    float an = ok ? sqrtf(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]) : 0;
    if (ok) accelAngles(acc, ar, ap);
    reply("# imu %s acc %.3f %.3f %.3f |a| %.3f -> roll %.1f pitch %.1f | att %.1f %.1f %.1f | rate %.1f %.1f %.1f | up %.3f g",
          ok ? "ok" : "READ FAILED", acc[0], acc[1], acc[2], an, ar, ap, att[0], att[1], att[2],
          rate[0], rate[1], rate[2], ok ? verticalAccelMmS2(acc) / G_MM_S2 : 0.0f);
    return;
  }
  if (line == "trim measure") {
    if (!motorsEnabled) {
      reply("# trim measure refused: motors off (USB dry run); boot on the battery without USB");
      return;
    }
    if (state != IDLE && state != KILLED) {
      reply("# trim measure refused: not on the ground (%s)", STATE_NAMES[state]);
      return;
    }
    reply("# trim measure: props on, on the ground, hands off; LED blinks 3 s, then each motor spins alone at %.0f%% (about 9 s)",
          TRIM_DUTY);
    startTrimRun(TRIM_JOB_MEASURE, true, true);
    return;
  }
  if (line == "trim noload") {
    if (!motorsEnabled || state != IDLE) {
      reply("# trim noload refused: needs a battery boot, motors idle and props off");
      return;
    }
    reply("# trim noload: props must be OFF; measuring for about 6 s");
    startTrimRun(TRIM_JOB_NOLOAD, false, true);
    return;
  }
  if (line == "trim save") { saveMotorTrim(); reportTrim(); return; }
  if (line == "trim reset") {
    for (float &k : motorTrim) k = 1;
    for (float &k : lastRawTrim) k = 1;
    motorTrimStored = false;
    prefs.remove("mtrim");
    trimStatus = "reset (the next battery boot measures it)";
    reportTrim();
    return;
  }
  if (line.startsWith("set ")) {
    int sp1 = line.indexOf(' ', 4);
    if (sp1 < 0) { reply("# usage: set <name> <value>"); return; }
    String name = line.substring(4, sp1);
    float v = line.substring(sp1 + 1).toFloat();
    for (auto &p : PARAMS) {
      if (name != p.name) continue;
      if (!isfinite(v) || v < p.lo || v > p.hi) {
        reply("# %s refused: %.4f outside %.4f..%.4f", p.name, v, p.lo, p.hi);
        return;
      }
      if ((p.flags & P_SAVED) && state != IDLE && state != KILLED) {
        reply("# %s refused: not on the ground", p.name);
        return;
      }
      const float old = *p.v;
      *p.v = (p.flags & P_INT) ? roundf(v) : v;
      const char *note = p.apply ? p.apply() : "";
      if (!note) { *p.v = old; return; }
      if (p.flags & P_SAVED) prefs.putFloat(p.name, *p.v);
      reply((p.flags & P_INT) ? "# %s = %.0f (%s)%s" : "# %s = %.4f (%s)%s", p.name, *p.v,
            (p.flags & P_SAVED) ? "saved" : "RAM only", note);
      return;
    }
    reply("# unknown parameter: %s", name.c_str());
    return;
  }
  reply("# commands: kill | get | hw | params | set <name> <value> | takeoff | land | cal | level | imu | trim [measure|noload|save|reset] | unitcal [raw|acc|vbat|tof|reset] | camtest [s] | bb list | bb dump [seq] | bb erase");
}

static void notePeer(const IPAddress &ip, uint32_t now) {
  TelPeer *slot = nullptr;
  for (auto &p : telPeers)
    if (p.active && p.ip == ip) slot = &p;
  for (auto &p : telPeers)
    if (!slot && !p.active) slot = &p;
  if (!slot) {
    slot = &telPeers[0];
    for (auto &p : telPeers)
      if (p.ms < slot->ms) slot = &p;
  }
  slot->ip = ip;
  slot->ms = now;
  slot->active = true;
}

static void pollCommands() {
  bool answeredGet = false;
  while (telUdp.parsePacket() > 0) {
    char buf[128];
    int n = telUdp.read(buf, sizeof(buf) - 1);
    telUdp.flush();
    if (n <= 0) continue;
    buf[n] = 0;
    cmdRxCount++;
    notePeer(telUdp.remoteIP(), millis());
    String line(buf);
    line.trim();
    if (line == "get") {
      if (answeredGet) continue;
      answeredGet = true;
    }
    handleCommand(line);
  }
  for (auto &p : telPeers)
    if (p.active && millis() - p.ms > HELLO_TIMEOUT_MS) {
      p.active = false;
      peerExpiries++;
    }

  if (!onUsb) return;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') { handleCommand(serialLine); serialLine = ""; }
    else if (serialLine.length() < 120) serialLine += c;
  }
}

static void resetPid() {
  for (int k = 0; k < 3; k++) {
    iTerm[k] = dFilt[k] = processedSp[k] = rateSetpoint[k] = 0;
    pidP[k] = pidD[k] = pidOut[k] = iScale[k] = 0;
    prevRate[k] = rate[k];
  }
  mixState = {};
}

static float vbatNoLoad() { return vbat + BATTERY_SAG_V * motorDuty / 100; }

static float hoverPrior() {
  if (!motorsEnabled && vbat < 3.0f) return 0;
  float h = sparrow::hoverFromBattery(vbat, motorDuty, hoverRefPct, HOVER_REF_V, BATTERY_SAG_V);
  return sparrow::clampf(h, HOVER_PRIOR_MIN, HOVER_PRIOR_MAX);
}

static void stopFlight(uint32_t now, sparrow::BlackboxTrigger why, const char *text) {
  lander.stop();
  baseThrust = 0;
  landAsked = false;
  vel.reset();
  captureBlackbox(now, why);
  blackbox.freeze(why);
  state = IDLE;
  reply("# IDLE (%s)", text);
}

static void reportHoldSurface() {
  float followed = lander.holdSurfaceMm();
  if (fabsf(followed - lander.surfaceMm()) < 5 && fabsf(followed - holdSurfaceReportedMm) >= 100) {
    holdSurfaceReportedMm = followed;
    reply("# HOLD follows a new surface %+.0f mm from where it engaged, %.0f mm above it",
          followed, lander.filteredHeightMm());
  }
}

static void flyVelocity(uint32_t now, float dt) {
  if (!lander.holding()) {
    if (vel.takeoffFailed(now, airborne)) {
      stopFlight(now, sparrow::BB_LAND_THROTTLE, "take-off failed: not airborne");
      return;
    }
    baseThrust = vel.takeoffThrust(now, hoverPrior(), airborne);
    if (!airborne) return;
    lander.startHold(now, baseThrust, takeoffMm);
    vel.engaged();
    holdSurfaceReportedMm = 0;
    holdTargetReportedMm = takeoffMm;
    reply("# HOLD %.0f mm (take-off): engaged at %.0f mm, %.0f mm/s (%s), climb-out %.0f%%, hover %.0f%% (%s)",
          takeoffMm, lander.heldHeightMm(), lander.verticalSpeedMmS(),
          lander.inertial() ? "accel-aided" : "ToF only", baseThrust, lander.hoverUsed(),
          lander.hoverTracked() ? "tracked" : "prior");
  }
  vel.steer(lander, spClimbMmS, holdMinMm, holdMaxMm);
  if (!landAsked && vel.pulledDown(now, lander, spClimbMmS, holdMinMm)) {
    landAsked = true;
    reply("# land: stick held down at hold_min");
  }
  sparrow::AutoLandEnd end;
  baseThrust = lander.update(now, dt, end);
  if (end == sparrow::LAND_TOF_RAMP) {
    stopFlight(now, sparrow::BB_LAND_TOF, "hold ramp, no ToF height for 5 s");
    return;
  }
  float target = lander.holdTargetMm();
  if (!lander.holdSpeedOn() && fabsf(target - holdTargetReportedMm) >= 100) {
    holdTargetReportedMm = target;
    reply("# HOLD target %.0f mm (velocity mode), %.0f mm above the surface now", target,
          lander.filteredHeightMm());
  }
  reportHoldSurface();
}

static void updateState(uint32_t now, float dt) {
  bool up = rxCount > 0 && now - lastRxMs < LINK_TIMEOUT_MS;
  if (up != linkUp) reply("# link %s (stations %d)", up ? "up" : "lost", WiFi.softAPgetStationNum());
  linkUp = up;
  bool velNow = linkUp && velMode;
  bool zeroNow = linkUp && (velNow ? vel.centred(spClimbMmS) : sp.thrust == 0);
  if (zeroNow && !thrustZero) zeroSinceMs = now;
  thrustZero = zeroNow;
  uint32_t zeroMs = zeroNow ? now - zeroSinceMs : 0;
  bool active = state == FLYING || state == LANDING;
  if (state == LANDING && flightVel && velNow && airborne && vel.cancelsLanding(spClimbMmS))
    landAsked = false;
  bool switched = active && linkUp && velMode != flightVel;
  bool wantDown = !linkUp || landAsked || switched || (!velNow && sp.thrust == 0);
  float tilt = max(fabsf(att[0]), fabsf(att[1]));
  lander.setHoverPrior(hoverPrior());
  if (lander.accFault() && !accFaultReported) {
    accFaultReported = true;
    reply("# ALT: accelerometer and ToF disagree (surface offset keeps drifting one way), ToF-only height until the next take-off");
  }
  if (lander.hoverShift() < -1 && !hoverShiftReported) {
    hoverShiftReported = true;
    reply("# HOVER estimate too high (at the thrust floor and still climbing): lowering it from %.0f%%",
          lander.hoverUsed() - lander.hoverShift());
  }

  if (!active || tilt <= CRASH_TILT_DEG) {
    crashSinceMs = 0;
  } else if (crashSinceMs == 0) {
    crashSinceMs = now;
  } else if (now - crashSinceMs >= CRASH_TILT_MS) {
    crashSinceMs = 0;
    char why[48];
    snprintf(why, sizeof(why), "crash: tilted %.0f deg", tilt);
    stopFlight(now, sparrow::BB_KILL_TILT, why);
    return;
  }

  float appThrust = linkUp ? sp.thrust * 100.0f / 65535 : 0;
  bool takeoffAsked = takeoffAskedMs != 0 && now - takeoffAskedMs < TAKEOFF_ASK_MS;
  if (!takeoffAsked) takeoffAskedMs = 0;
  switch (state) {
    case IDLE: {
      baseThrust = 0;
      resetAirborneDetection();
      if (trimRunning()) {
        armReady = false;
        takeoffAskedMs = 0;
        break;
      }
      bool go = false;

      if (!linkUp) {
        armReady = false;
      } else if (zeroNow) {
        armReady = zeroMs >= ARM_ZERO_MS;
        go = velNow && armReady && takeoffAsked;
      } else if (armReady && !velNow) {
        go = true;
      }
      if (velNow && takeoffAsked && !zeroNow) {
        takeoffAskedMs = 0;
        reply("# take-off refused: centre the left stick (vertical speed %.0f mm/s)", spClimbMmS);
      }
      if (go) {
        armReady = false;
        takeoffAskedMs = 0;
        char why[64];
        if (!attitudeTrusted(why, sizeof(why))) {
          reply("# take-off refused: %s (release the stick and try again)", why);
          trimAlertUntilMs = millis() + 2000;
          break;
        }
        if (velNow && !tofOk) {
          reply("# take-off refused: velocity mode needs the ToF");
          break;
        }
        resetPid();
        state = FLYING;
        flightVel = velNow;
        landAsked = false;
        landingTrigger = sparrow::BB_NONE;
        lander.forgetHover();
        lander.rearm();
        accFaultReported = false;
        hoverShiftReported = false;
        loopProfile = {};
        loopProfileReported = false;
        blackbox.startFlight(motorsEnabled);
        if (flightVel) {
          vel.startTakeoff(now);
          reply("# TAKEOFF to %.0f mm (velocity mode)", takeoffMm);
        }
        reply("# FLYING (battery %.2f V at rest, hover prior %.0f%%)", vbat, hoverPrior());
        if (motorsEnabled && vbat < VBAT_REST_LOW)
          reply("# battery low: %.2f V at rest, about %.1f V under hover load; land soon", vbat,
                vbat - BATTERY_SAG_V * hoverPrior() / 100);
      }
      break;
    }
    case FLYING:
      if (!wantDown) {
        if (flightVel) {
          flyVelocity(now, dt);
          if (state != FLYING) break;
        } else {
          float stickMm = sparrow::holdTargetForStick(appThrust, holdMinMm, holdMaxMm);
          if (airborne && !lander.holding() && lander.holdDue(now, stickMm - HOLD_ENGAGE_BELOW_MM)) {
            lander.startHold(now, appThrust, stickMm);
            holdSurfaceReportedMm = 0;
            holdTargetReportedMm = stickMm;
            reply("# HOLD %.0f mm (stick %.0f%%): engaged at %.0f mm, %.0f mm/s (%s), hover %.0f%% (%s)",
                  stickMm, appThrust, lander.heldHeightMm(), lander.verticalSpeedMmS(),
                  lander.inertial() ? "accel-aided" : "ToF only", lander.hoverUsed(),
                  lander.hoverTracked() ? "tracked" : "prior");
          }
          if (lander.holding()) {
            lander.setHoldTarget(stickMm, dt);
            sparrow::AutoLandEnd end;
            baseThrust = lander.update(now, dt, end);
            if (end == sparrow::LAND_TOF_RAMP) {
              stopFlight(now, sparrow::BB_LAND_TOF, "hold ramp, no ToF height for 5 s");
              break;
            }
            float target = lander.holdTargetMm();
            if (fabsf(target - stickMm) < 5 && fabsf(target - holdTargetReportedMm) >= 100) {
              holdTargetReportedMm = target;
              reply("# HOLD target %.0f mm (stick %.0f%%), %.0f mm above the surface now", target,
                    appThrust, lander.filteredHeightMm());
            }
            reportHoldSurface();
          } else {
            baseThrust = appThrust;
          }
        }
        if (airborne) lander.trackHover(baseThrust, dt);
      } else {
        landingTrigger = linkUp ? sparrow::BB_LAND_THROTTLE : sparrow::BB_LAND_LINK;
        const char *why = !linkUp ? "link lost" : landAsked ? "land command"
                          : switched ? "App mode switched in flight" : "thrust 0";
        if (tilt > LAND_TILT_CUT_DEG) {
          stopFlight(now, sparrow::BB_KILL_TILT, "released while tilted");
        } else if (!airborne) {
          stopFlight(now, landingTrigger, why);
        } else {
          lander.start(now, baseThrust);
          state = LANDING;
          reply("# LANDING auto (%s), from %.0f%%, hover %.0f%% (%s, prior %.0f%%, lowered %.0f%% in flight), height %.0f mm",
                why, baseThrust, lander.hoverUsed(),
                lander.hoverTracked() ? "tracked" : "prior", hoverPrior(), 0.0f - lander.hoverShift(),
                lander.filteredHeightMm());
        }
      }
      break;
    case LANDING:
      if (!wantDown && flightVel) {
        float hold = sparrow::clampf(lander.filteredHeightMm(), holdMinMm, holdMaxMm);
        lander.startHold(now, baseThrust, hold);
        vel.engaged();
        holdSurfaceReportedMm = 0;
        holdTargetReportedMm = hold;
        state = FLYING;
        landingTrigger = sparrow::BB_NONE;
        reply("# FLYING (landing stopped, holding %.0f mm)", hold);
      } else if (!wantDown) {
        lander.stop();
        state = FLYING;
        baseThrust = appThrust;
        landingTrigger = sparrow::BB_NONE;
        reply("# FLYING (landing cancelled)");
      } else if (tilt > LAND_TILT_CUT_DEG) {
        stopFlight(now, sparrow::BB_KILL_TILT, "tilted during landing");
      } else {
        sparrow::AutoLandEnd end;
        baseThrust = lander.update(now, dt, end);
        if (end == sparrow::LAND_TOUCHDOWN) stopFlight(now, landingTrigger, "landed");
        else if (end == sparrow::LAND_STALLED)
          stopFlight(now, landingTrigger, "landed: height stopped coming down");
        else if (end == sparrow::LAND_TIMEOUT) stopFlight(now, landingTrigger, "landing timeout");
        else if (end == sparrow::LAND_TOF_RAMP)
          stopFlight(now, sparrow::BB_LAND_TOF, "landing ramp, no ToF height for 5 s");
      }
      break;
    case KILLED:
      baseThrust = 0;
      if (zeroMs >= UNKILL_ZERO_MS) {
        armReady = false;
        state = IDLE;
        reply("# IDLE (kill cleared)");
      }
      break;
  }
}

static void runControl(float dt) {
  bool active = state == FLYING || state == LANDING;
  if (!active) {
    for (int i = 0; i < 4; i++) motor[i] = 0;
    resetPid();
    return;
  }

  bool takeoffAssist = state == FLYING && tofOk && !airborne && heightMm >= 0;
  bool landingSteer = state == LANDING && linkUp && airborne;
  bool fromApp = (state == FLYING && !takeoffAssist) || landingSteer;
  float controlScale = airborne ? takeoffRamp(CONTROL_FADE_MS) : (fromApp ? 1.0f : 0.0f);
  float angleMax = sparrow::clampf(angleCmdMax, 0.0f, 30.0f);
  float yawMax = sparrow::clampf(yawCmdMax, 0.0f, RATE_SP_MAX);
  float angleExpo = sparrow::clampf(angleCmdExpo, 0.0f, 1.0f);
  float yawExpo = sparrow::clampf(yawCmdExpo, 0.0f, 1.0f);
  if (fromApp) {
    processedSp[0] = controlScale * angleMax
                     * sparrow::expo(sp.roll, APP_ANGLE_FULL, angleExpo) / APP_ANGLE_FULL;
    processedSp[1] = controlScale * angleMax
                     * sparrow::expo(sp.pitch, APP_ANGLE_FULL, angleExpo) / APP_ANGLE_FULL;
    processedSp[2] = controlScale * yawMax
                     * sparrow::expo(sp.yaw, APP_YAW_FULL, yawExpo) / APP_YAW_FULL;
  } else {
    for (float &v : processedSp) v = 0;
  }

  for (int k = 0; k < 2; k++)
    rateSetpoint[k] = constrain(ANGLE_KP * (processedSp[k] + attOffset[k] - att[k]),
                                -RATE_SP_MAX, RATE_SP_MAX);
  rateSetpoint[2] = constrain(processedSp[2], -RATE_SP_MAX, RATE_SP_MAX);

  bool enoughThrust = baseThrust >= I_MIN_THRUST;
  for (int k = 0; k < 3; k++) {
    float err = rateSetpoint[k] - rate[k];
    bool integratorReady = k < 2 ? rpIntegratorReady : airborne;
    iScale[k] = k < 2 ? rampFrom(rpIntegratorSinceMs, RP_I_RAMP_MS)
                      : takeoffRamp(YAW_I_RAMP_MS);
    if (!integratorReady) iTerm[k] = 0;
    bool blocked = sparrow::saturationBlocks(mixState.satMask, k, err);
    if (enoughThrust && integratorReady && !blocked)
      iTerm[k] = constrain(iTerm[k] + RATE_KI[k] * err * dt * iScale[k], -I_MAX, I_MAX);
    float d = -(rate[k] - prevRate[k]) / dt;
    prevRate[k] = rate[k];
    dFilt[k] += DTERM_ALPHA * (d - dFilt[k]);
    pidP[k] = RATE_KP[k] * err;
    pidD[k] = RATE_KD[k] * dFilt[k];
    pidOut[k] = constrain(pidP[k] + iTerm[k] + pidD[k], -AXIS_OUT_MAX, AXIS_OUT_MAX);
  }

  mixState = sparrow::mixX(baseThrust, pidOut[0], pidOut[1], pidOut[2]);
  for (int i = 0; i < 4; i++) motor[i] = constrain(mixState.motor[i] * motorTrim[i], 0.0f, 100.0f);
  for (int k = 0; k < 3; k++) {
    if (pidOut[k] >= AXIS_OUT_MAX - 0.001f) mixState.satMask |= 1u << k;
    if (pidOut[k] <= -AXIS_OUT_MAX + 0.001f) mixState.satMask |= 1u << (k + 3);
  }
}

static void writeMotors() {
  for (int i = 0; i < 4; i++)
    ledcWrite(i, motorsEnabled ? (int)(motor[i] / 100 * PWM_MAX + 0.5f) : 0);
}

static void sendTelemetry(uint32_t now) {
  uint32_t age = rxCount ? now - lastRxMs : 0;
  snprintf(telLine, sizeof(telLine),
           "T,%lu,%s,%d,"
           "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,"
           "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,"
           "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,"
           "%.2f,%.2f,%.2f,%u,%.3f,%.3f,%.2f,%u,%.2f,"
           "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.3f,%d,%d,%lu,%lu,%d",
           (unsigned long)now, STATE_NAMES[state], airborne ? 1 : 0,
           att[0], att[1], att[2], rate[0], rate[1], rate[2],
           sp.roll, sp.pitch, sp.yaw, processedSp[0], processedSp[1], processedSp[2],
           rateSetpoint[0], rateSetpoint[1], rateSetpoint[2],
           pidP[0], pidP[1], pidP[2], iTerm[0], iTerm[1], iTerm[2],
           pidD[0], pidD[1], pidD[2], pidOut[0], pidOut[1], pidOut[2],
           iScale[0], iScale[1], iScale[2], (unsigned int)mixState.satMask,
           mixState.rpScale, mixState.yawScale, mixState.collective,
           (unsigned int)sp.thrust, baseThrust,
           mixState.raw[0], mixState.raw[1], mixState.raw[2], mixState.raw[3],
           motor[0], motor[1], motor[2], motor[3], vbat, tofMm, heightMm,
           (unsigned long)age, (unsigned long)loopUsMax, motorsEnabled ? 1 : 0);
  loopUsMax = 0;
  if (onUsb) Serial.println(telLine);
  telPending = 0;
  for (int k = 0; k < MAX_TEL_PEERS; k++)
    if (telPeers[k].active) telPending |= 1u << k;
}

static void sendPendingTelemetry() {
  for (int k = 0; k < MAX_TEL_PEERS && telPending; k++) {
    if (!(telPending & (1u << k))) continue;
    telPending &= ~(1u << k);
    if (!telPeers[k].active) continue;
    sendUdp(telPeers[k].ip, telLine, true);
    return;
  }
}

static const uint16_t BEACON_COMPANY = 0xFFFF;
static const uint8_t BEACON_LAYOUT = 1;
static const uint32_t BEACON_REFRESH_MS = 5000;
static const uint16_t BEACON_INTERVAL = 320;

static void bleInit() {
  NimBLEDevice::init(apSsid);
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData ad;
  ad.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  ad.setName(apSsid);
  adv->setAdvertisementData(ad);
  adv->setAdvertisementType(BLE_GAP_CONN_MODE_NON);
  adv->setMinInterval(BEACON_INTERVAL);
  adv->setMaxInterval(BEACON_INTERVAL + 80);
  bleReady = true;
}

static void setBeaconData() {
  uint16_t mv = (uint16_t)constrain(vbat * 1000.0f, 0.0f, 65535.0f);
  const uint8_t data[] = {BEACON_COMPANY & 0xFF, BEACON_COMPANY >> 8, 'S', 'P', BEACON_LAYOUT,
                          FIRMWARE_BUILD, (uint8_t)(mv & 0xFF),
                          (uint8_t)(mv >> 8), (uint8_t)state};
  NimBLEAdvertisementData scan;
  scan.setManufacturerData(std::string((const char *)data, sizeof(data)));
  NimBLEDevice::getAdvertising()->setScanResponseData(scan);
}

static void updateBeacon(uint32_t now) {
  if (!bleReady) return;
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  bool want = !linkUp && (state == IDLE || state == KILLED);
  if (!want) {
    if (beaconOn) adv->stop();
    beaconOn = false;
    return;
  }
  if (beaconOn && now - beaconDataMs < BEACON_REFRESH_MS) return;
  if (beaconOn) adv->stop();
  setBeaconData();
  beaconOn = adv->start();
  beaconDataMs = now;
}

// ---------------------------------------------------------------- ARP greeting
// iOS stops sending to an address that left 5 ARP requests unanswered, for 20 s. On 2 Nov 2026 a
// phone that had just joined went off scanning other channels, missed the drone's answers, and
// sent nothing for 20 s (the app read "no answer"). So while no app is linked, the drone asks
// every phone on the AP for its MAC once a second: an ARP request naming the phone ends that wait
// on the phone, and teaches it the drone's MAC before the app sends anything.
static const uint32_t ARP_GREET_MS = 1000;

struct ArpGreeting {
  struct netif *netif;
  int count;
  ip4_addr_t ip[ESP_WIFI_MAX_CONN_NUM];
};

// On the TCP/IP task, where lwIP may be called.
static void sendArpGreeting(void *ctx) {
  ArpGreeting *g = (ArpGreeting *)ctx;
  for (int i = 0; i < g->count; i++) etharp_request(g->netif, &g->ip[i]);
  delete g;
}

static void greetStations(uint32_t now) {
  if (linkUp || now - lastArpGreetMs < ARP_GREET_MS) return;
  lastArpGreetMs = now;
  wifi_sta_list_t wifiList = {};
  esp_netif_sta_list_t ipList = {};
  if (esp_wifi_ap_get_sta_list(&wifiList) != ESP_OK || wifiList.num == 0) return;
  if (esp_netif_get_sta_list(&wifiList, &ipList) != ESP_OK) return;
  esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (!ap) return;
  ArpGreeting *g = new ArpGreeting();
  g->netif = (struct netif *)esp_netif_get_netif_impl(ap);
  for (int i = 0; i < ipList.num; i++)
    if (ipList.sta[i].ip.addr) g->ip[g->count++].addr = ipList.sta[i].ip.addr;
  if (g->netif && g->count && tcpip_callback(sendArpGreeting, g) == ERR_OK) {
    arpGreetings += g->count;
    return;
  }
  delete g;
}

static void updateLed(uint32_t now) {
  bool on;
  bool lowBat = motorsEnabled && vbat < VBAT_LOW;
  if (trimRunning() && trimRun.phase == TRIM_WARN) on = trimWarnLed(now - trimRun.phaseMs);
  else if (state == KILLED || lowBat || now < trimAlertUntilMs) on = (now / 100) % 2;
  else if (linkUp || state == FLYING || state == LANDING) on = true;
  else on = (now / 500) % 2;
  digitalWrite(PIN_LED, on ? LOW : HIGH);
}

static void saveBootLog() { prefs.putBytes("bootlog", bootLog, sizeof(bootLog)); }

static void startBootLog(uint32_t apMs) {
  if (prefs.getBytes("bootlog", bootLog, sizeof(bootLog)) != sizeof(bootLog))
    memset(bootLog, 0, sizeof(bootLog));
  uint32_t last = 0;
  for (int i = 0; i < BOOT_LOG_LEN; i++)
    if (bootLog[i].boot > last) { last = bootLog[i].boot; bootIdx = (i + 1) % BOOT_LOG_LEN; }
  BootRec &r = bootLog[bootIdx];
  r = {last + 1, (uint8_t)esp_reset_reason(), (uint8_t)onUsb, (uint16_t)(vbat * 1000), apMs, 0, 0};
  saveBootLog();
}

static void updateBootLog(uint32_t now) {
  BootRec &r = bootLog[bootIdx];
  if (!r.staMs && WiFi.softAPgetStationNum() > 0) { r.staMs = now; saveBootLog(); }
  if (!r.appMs && rxCount > 0) { r.appMs = now; saveBootLog(); }
}

static void printBootLog() {
  static const char *REASONS[] = {"unknown", "power-on", "ext", "software", "panic", "int-wdt",
                                  "task-wdt", "wdt", "deep-sleep", "brownout", "sdio"};
  Serial.println("# boot log (ms after boot): boot reason power vbat ap_up phone_joined first_setpoint");
  for (int k = 1; k <= BOOT_LOG_LEN; k++) {
    const BootRec &r = bootLog[(bootIdx + k) % BOOT_LOG_LEN];
    if (!r.boot) continue;
    Serial.printf("#   %4lu %-9s %-7s %.2f V %6lu %6lu %6lu\n", (unsigned long)r.boot,
                  r.reason <= 10 ? REASONS[r.reason] : "?", r.usb ? "usb" : "battery",
                  r.vbatMv / 1000.0f, (unsigned long)r.apMs, (unsigned long)r.staMs,
                  (unsigned long)r.appMs);
  }
}

static void checkUsbHost() {
  if (!motorsEnabled || !HWCDC::isPlugged()) return;
  motorsEnabled = false;
  onUsb = true;
  for (int i = 0; i < 4; i++) { motor[i] = 0; ledcWrite(i, 0); }
  Serial.begin(115200);
  Serial.setTxTimeoutMs(20);
  bootLog[bootIdx].usb = 1;
  saveBootLog();
  reply("# USB host detected: dry run, motors stay off");
}

static float vbatMean(uint32_t ms) {
  float sum = 0;
  int n = 0;
  for (uint32_t t0 = millis(); millis() - t0 < ms; n++) {
    sum += vbatSampleV();
    delayMicroseconds(500);
  }
  return n ? sum / n : 0;
}

static void trimPhase(TrimPhase phase, uint32_t now) {
  trimRun.phase = phase;
  trimRun.phaseMs = now;
  trimRun.sum = 0;
  trimRun.n = 0;
}

static void trimSample() {
  for (int i = 0; i < TRIM_BURST; i++) trimRun.sum += vbatSampleV();
  trimRun.n += TRIM_BURST;
}

static void startTrimRun(TrimJob job, bool warn, bool fromCommand) {
  trimRun = {};
  trimRun.job = job;
  trimRun.fromCommand = fromCommand;
  trimPhase(warn ? TRIM_WARN : TRIM_BASE, millis());
}

static void abortTrimRun(const char *why) {
  if (!trimRunning()) return;
  const char *name = trimRun.job == TRIM_JOB_NOLOAD ? "noload" : "measure";
  trimRun.job = TRIM_JOB_NONE;
  for (int i = 0; i < 4; i++) {
    motor[i] = 0;
    ledcWrite(i, 0);
  }
  reply("# trim %s stopped (%s), nothing stored", name, why);
}

static const char *trimRejection(const float drops[4], char *why, size_t n) {
  for (int i = 0; i < 4; i++) {
    float load = drops[i] - noloadDropV[i];
    if (!(load >= TRIM_MIN_LOAD_V)) {
      snprintf(why, n, "m%d prop load %.3f V, below %.3f V (prop missing, motor not spinning?)",
               i + 1, load, TRIM_MIN_LOAD_V);
      return "rejected (prop load too low)";
    }
  }
  float k[4] = {1, 1, 1, 1};
  sparrow::computeMotorTrim(drops, noloadDropV, 0, INFINITY, 0, k);
  snprintf(why, n, "out of range");
  for (int i = 0; i < 4; i++)
    if (k[i] < TRIM_MIN || k[i] > TRIM_MAX) {
      snprintf(why, n, "m%d multiplier %.2f outside %.2f..%.2f", i + 1, k[i], TRIM_MIN, TRIM_MAX);
      break;
    }
  return "rejected (out of range)";
}

static void finishTrimRun() {
  const TrimRun run = trimRun;
  trimRun.job = TRIM_JOB_NONE;
  if (run.job == TRIM_JOB_NOLOAD) {
    memcpy(noloadDropV, run.drops, sizeof(noloadDropV));
    prefs.putBytes("noload", noloadDropV, sizeof(noloadDropV));
    reportTrim();
    return;
  }
  memcpy(lastLoadedDropV, run.drops, sizeof(lastLoadedDropV));
  float k[4];
  if (sparrow::computeMotorTrim(run.drops, noloadDropV, TRIM_MIN, TRIM_MAX, TRIM_MIN_LOAD_V, k)) {
    memcpy(lastRawTrim, k, sizeof(k));
    sparrow::scaleTrim(k, TRIM_GAIN, motorTrim);
    motorTrimStored = true;
    prefs.putBytes("mtrim", motorTrim, sizeof(motorTrim));
    trimStatus = "measured";
    if (run.fromCommand) reportTrim();
    return;
  }
  char why[96];
  trimStatus = trimRejection(run.drops, why, sizeof(why));
  trimAlertUntilMs = millis() + TRIM_ALERT_MS;
  if (!run.fromCommand) return;
  reply("# trim measure rejected: %s; %s", why,
        motorTrimStored ? "kept the stored trim" : "still no trim stored");
  reportTrim();
}

static void stepTrimRun(uint32_t now) {
  if (!trimRunning()) return;
  if (!motorsEnabled) {
    abortTrimRun("USB host");
    return;
  }
  uint32_t t = now - trimRun.phaseMs;
  int m = trimRun.motor;
  switch (trimRun.phase) {
    case TRIM_WARN:
      if (t >= TRIM_WARN_MS) trimPhase(TRIM_BASE, now);
      break;
    case TRIM_BASE:
      if (t < TRIM_BASE_MS) {
        trimSample();
        break;
      }
      trimRun.before = trimRun.n ? trimRun.sum / trimRun.n : 0;
      trimPhase(TRIM_SPIN, now);
      break;
    case TRIM_SPIN:
      if (t < TRIM_RAMP_MS + TRIM_HOLD_MS) {
        motor[m] = t < TRIM_RAMP_MS ? TRIM_DUTY * t / TRIM_RAMP_MS : TRIM_DUTY;
        if (t >= TRIM_RAMP_MS + TRIM_HOLD_MS * 4 / 10) trimSample();
        break;
      }
      motor[m] = 0;
      trimRun.drops[m] = trimRun.n ? trimRun.before - trimRun.sum / trimRun.n : 0;
      trimPhase(TRIM_GAP, now);
      break;
    case TRIM_GAP:
      if (t < TRIM_GAP_MS) break;
      if (++trimRun.motor < 4) trimPhase(TRIM_BASE, now);
      else finishTrimRun();
      break;
  }
}

static void saveMotorTrim() {
  if (motorTrimStored) prefs.putBytes("mtrim", motorTrim, sizeof(motorTrim));
  prefs.putBytes("attoff", attOffset, sizeof(attOffset));
}

static void loadMotorTrim() {
  motorTrimStored = prefs.getBytes("mtrim", motorTrim, sizeof(motorTrim)) == sizeof(motorTrim);
  if (!motorTrimStored)
    for (float &k : motorTrim) k = 1;
  trimStatus = motorTrimStored ? "stored" : "not stored (the next battery boot measures it)";
  for (int i = 0; i < 4; i++) lastRawTrim[i] = 1 + (motorTrim[i] - 1) / TRIM_GAIN;
  if (prefs.getBytes("noload", noloadDropV, sizeof(noloadDropV)) != sizeof(noloadDropV))
    memcpy(noloadDropV, NOLOAD_DEFAULT_V, sizeof(noloadDropV));
  if (prefs.getBytes("attoff", attOffset, sizeof(attOffset)) != sizeof(attOffset))
    attOffset[0] = attOffset[1] = 0;
}

static void bootMotorTrim() {
  for (uint32_t t0 = millis(); millis() - t0 < TRIM_WARN_MS; delay(10))
    digitalWrite(PIN_LED, trimWarnLed(millis() - t0) ? LOW : HIGH);
  digitalWrite(PIN_LED, LOW);
  if (motorTrimStored) {
    chirpMotors();
    return;
  }
  startTrimRun(TRIM_JOB_MEASURE, false, false);
  for (uint32_t stepUs = micros(); trimRunning(); stepUs += PERIOD_US) {
    stepTrimRun(millis());
    writeMotors();
    while (micros() - stepUs < PERIOD_US) {}
  }
}

static void reportTrim() {
  reply("# trim %s: m1..m4 x %.3f %.3f %.3f %.3f", trimStatus, motorTrim[0], motorTrim[1],
        motorTrim[2], motorTrim[3]);
  reply("# trim raw %.3f %.3f %.3f %.3f, applied at gain %.2f; attitude offset roll %.2f pitch %.2f deg",
        lastRawTrim[0], lastRawTrim[1], lastRawTrim[2], lastRawTrim[3], TRIM_GAIN, attOffset[0],
        attOffset[1]);
  reply("# level %s: roll %.2f pitch %.2f deg (stored by \"level\": %.2f %.2f)", levelStatus,
        trim[0], trim[1], storedTrim[0], storedTrim[1]);
  reply("# trim drops at %.0f%%: loaded %.3f %.3f %.3f %.3f V, no-load %.3f %.3f %.3f %.3f V",
        TRIM_DUTY, lastLoadedDropV[0], lastLoadedDropV[1], lastLoadedDropV[2],
        lastLoadedDropV[3], noloadDropV[0], noloadDropV[1], noloadDropV[2], noloadDropV[3]);
}

static void chirpMotors() {
  int duty = (int)(CHIRP_DUTY / 100 * PWM_MAX + 0.5f);
  for (int i = 0; i < 4; i++) {
    ledcWrite(i, duty);
    delay(CHIRP_MS);
    ledcWrite(i, 0);
    delay(CHIRP_GAP_MS);
  }
}

static void localOnlyDhcp() {
  esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (!ap) return;
  uint8_t none = 0;
  esp_netif_dhcps_stop(ap);
  esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_ROUTER_SOLICITATION_ADDRESS, &none, sizeof(none));
  esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &none, sizeof(none));
  esp_netif_dhcps_start(ap);
}

void setup() {
  for (int p : MOT_PINS) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  for (int i = 0; i < 4; i++) {
    ledcSetup(i, PWM_HZ, PWM_BITS);
    ledcWrite(i, 0);
    ledcAttachPin(MOT_PINS[i], i);
  }
  for (size_t i = 0; i < PARAM_COUNT; i++) paramDefaults[i] = *PARAMS[i].v;
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  pinMode(PIN_TOF_INT, INPUT);
  pinMode(PIN_TOF_XSHUT, OUTPUT);
  digitalWrite(PIN_TOF_XSHUT, LOW);
  delay(10);
  pinMode(PIN_TOF_XSHUT, INPUT);

  delay(100);
  prefs.begin("flight");
  loadUnitCal();
  // The battery's highest reading while the USB host is looked for (1.5 s without one): a
  // battery plugged in with SW1 on can bounce on its connector, and a single reading in that
  // dip read 2.82 V on a 4.02 V battery, which kept the motors off until the next boot.
  vbat = readVbat();
  bool usbHost = false;
  for (uint32_t t0 = millis(); !usbHost && millis() - t0 < USB_DETECT_MS; delay(10)) {
    usbHost = HWCDC::isPlugged();
    vbat = max(vbat, readVbat());
  }
  motorsEnabled = vbat >= VBAT_MOTORS && !usbHost;
  onUsb = !motorsEnabled;
  if (onUsb) {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(20);
    delay(300);
    Serial.printf("# sparrow firmware %s, vbat %.2f V, USB host %s: dry run, motors stay off\n",
                  FIRMWARE_VERSION, vbat, usbHost ? "yes" : "no");
  }

  trim[0] = prefs.getFloat("trim_r", 0);
  trim[1] = prefs.getFloat("trim_p", 0);
  storedTrim[0] = trim[0];
  storedTrim[1] = trim[1];
  buildAlign();
  loadMotorTrim();
  loadSavedParams();
  blackbox.begin();

  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  tof.setTimeout(200);
  tofOk = tof.init();
  for (int attempt = 0; !tofOk && attempt < 3; attempt++) {
    pinMode(PIN_TOF_XSHUT, OUTPUT);
    digitalWrite(PIN_TOF_XSHUT, LOW);
    delay(10);
    pinMode(PIN_TOF_XSHUT, INPUT);
    delay(50);
    tofOk = tof.init();
    if (onUsb) Serial.printf("# VL53L0X init retry %d: %s\n", attempt + 1, tofOk ? "ok" : "failed");
  }
  if (tofOk) {
    tof.startContinuous(50);
  }
  Wire.end();
  i2c.begin(PIN_SDA, PIN_SCL, I2C_HZ);
  configureMpu();
  lander.setAccelTau(ALT_TAU_S);

  WiFi.mode(WIFI_AP);
  uint8_t mac[6];
  WiFi.softAPmacAddress(mac);
  snprintf(apSsid, sizeof(apSsid), "%s_%02X%02X%02X%02X%02X%02X", SSID_BASE,
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  IPAddress ip(192, 168, 43, 42), mask(255, 255, 255, 0);
  WiFi.softAPConfig(ip, ip, mask);
  bool apOk = WiFi.softAP(apSsid, WIFI_PASSWORD, WIFI_CHANNEL, 0, 3);
  localOnlyDhcp();
  sparrow::dhcpUnicastBegin();
  appUdp.begin(APP_PORT);
  telUdp.begin(TELEM_PORT);
  sparrow::netSendBegin(TELEM_PORT);
  startBootLog(millis());
  bleInit();

  checkUsbHost();
  if (motorsEnabled) bootMotorTrim();
  calibrateGyro();
  bootAutoLevel();
  sparrow::cameraBegin((int)camRes);
  if (onUsb) {
    printBootLog();
    Serial.printf("# WiFi channel %d\n", WIFI_CHANNEL);
    Serial.printf("# AP %s %s, VL53L0X %s, blackbox %s, level trim roll %.2f pitch %.2f\n",
                  apSsid, apOk ? "up" : "FAILED", tofOk ? "ok" : "FAILED",
                  blackbox.fsOk() ? "ok" : "FAILED", trim[0], trim[1]);
    char bbStatus[200];
    blackbox.status(bbStatus, sizeof(bbStatus));
    Serial.printf("# %s\n", bbStatus);
    reportUnitCal();
    reportTrim();
    Serial.println("# sat_mask bits: roll+/pitch+/yaw+/roll-/pitch-/yaw- = bits 0..5");
    Serial.println("# T,ms,state,airborne,roll,pitch,yaw,rate_r,rate_p,rate_y,sp_roll_raw,sp_pitch_raw,sp_yaw_raw,sp_roll,sp_pitch,sp_yaw,rate_sp_r,rate_sp_p,rate_sp_y,p_r,p_p,p_y,i_r,i_p,i_y,d_r,d_p,d_y,out_r,out_p,out_y,i_scale_r,i_scale_p,i_scale_y,sat_mask,rp_scale,yaw_scale,collective,thrust,base,raw_m1,raw_m2,raw_m3,raw_m4,m1,m2,m3,m4,vbat,tof,height,link_age,loop_us_max,motors");
  }
  digitalWrite(PIN_LED, HIGH);
  lastLoopUs = micros();
}

void loop() {
  uint32_t nowUs = micros();
  if (nowUs - lastLoopUs < PERIOD_US) return;
  float dt = (nowUs - lastLoopUs) / 1e6f;
  lastLoopUs = nowUs;
  if (dt > MAX_LOOP_DT_S) dt = PERIOD_US / 1e6f;

  static float imuDt = 0;
  imuDt += dt;
  float acc[3], gyro[3];
  if (readImu(acc, gyro)) {
    for (int k = 0; k < 3; k++) rate[k] = gyro[k] - gyroBias[k];
    updateAttitude(acc, min(imuDt, MAX_LOOP_DT_S));
    float accUp = verticalAccelMmS2(acc);
    lander.setGrounded(!airborne);
    if (fmaxf(fabsf(att[0]), fabsf(att[1])) <= CRASH_TILT_DEG)
      lander.predict(millis(), min(imuDt, MAX_LOOP_DT_S), accUp);
    accUpSum += accUp;
    accUpCount++;
    memcpy(lastAcc, acc, sizeof(lastAcc));
    if (state == IDLE && !trimRunning()) {
      groundLevelSample(millis(), acc);
      checkAccelHealth(millis(), acc);
    } else {
      levelStillSinceMs = 0;
    }
    imuDt = 0;
    imuMissStreak = 0;
  } else {
    imuMissTotal++;
    imuMissStreak++;
  }

  uint32_t ts[LS_COUNT + 1];
  ts[LS_IMU] = nowUs;
  ts[LS_APP] = micros();
  pollApp();
  ts[LS_CMD] = micros();
  pollCommands();
  uint32_t now = millis();
  ts[LS_STATE] = micros();
  updateState(now, dt);
  ts[LS_CONTROL] = micros();
  runControl(dt);
  stepTrimRun(now);
  writeMotors();

  ts[LS_TOF] = micros();
  pollTof();
  ts[LS_ADC] = micros();
  vbat += VBAT_ALPHA * (vbatSampleV() - vbat);
  float driven = motorsEnabled ? (motor[0] + motor[1] + motor[2] + motor[3]) / 4 : 0.0f;
  motorDuty += VBAT_ALPHA * (driven - motorDuty);
  ts[LS_TELEM] = micros();
  if (now - lastTelemMs >= TELEM_MS) {
    lastTelemMs = now;
    sendTelemetry(now);
  }
  sendPendingTelemetry();
  ts[LS_OTHER] = micros();
  if (state == IDLE) {
    updateBootLog(now);
    checkUsbHost();
  }
  bool flushTried = false;
  if (blackbox.pending() && state != FLYING && state != LANDING) {
    // Queued for the blackbox's flash task (see blackbox.h): written here, the flash held this
    // loop and the WiFi for over a second after every landing.
    if (!blackbox.startSave(bootLog[bootIdx].boot)) reply("# BLACKBOX save FAILED (not queued)");
    flushTried = true;
  }
  int saveResult = blackbox.saveResult();
  if (saveResult) reply("# BLACKBOX %s", saveResult > 0 ? "saved" : "save FAILED");
  // Each flash erase or write holds both cores: only on the ground with the motors off.
  blackbox.setFlashWork((state == IDLE || state == KILLED) && !trimRunning());
  sampleBlackbox(now);
  updateLed(now);
  updateBeacon(now);
  greetStations(now);
  ts[LS_COUNT] = micros();
  recordLoop(ts);
  loopUsMax = max(loopUsMax, ts[LS_COUNT] - nowUs);
  if (!loopProfileReported && state != FLYING && state != LANDING
      && (flushTried || !blackbox.pending())) {
    loopProfileReported = true;
    reportLoopProfile("last flight");
  }
}
