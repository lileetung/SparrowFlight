// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lileetung

#include "camera_stream.h"

#include <esp_camera.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>

namespace sparrow {
namespace {

const framesize_t SIZES[] = {FRAMESIZE_QVGA, FRAMESIZE_HVGA, FRAMESIZE_VGA};
const char *const SIZE_NAMES[] = {"320x240", "480x320", "640x480"};
const UBaseType_t TASK_PRIORITY = 2;
const uint32_t TASK_STACK = 6144;
const uint32_t STALL_MS = 150;

enum Status { STARTING, RUNNING, ABSENT };

volatile int wantRes = 1;
volatile Status status = STARTING;
volatile bool awake = false;
volatile esp_err_t initErr = ESP_OK;
const char *volatile sensorName = "";
volatile int curRes = 0;
volatile uint32_t frames = 0, misses = 0, lastBytes = 0, wakes = 0;
volatile uint32_t stalls = 0;
volatile uint32_t sendMaxMs = 0, gapMaxMs = 0;
volatile float fps = 0;
volatile uint32_t viewerIp = 0;
volatile uint32_t viewers = 0, sendErrors = 0;
volatile uint32_t appShown = 0, appLost = 0, appGapMaxMs = 0;

bool startCamera(int res) {
  camera_config_t c = {};
  c.pin_pwdn = -1;
  c.pin_reset = -1;
  c.pin_xclk = 10;
  c.pin_sccb_sda = 40;
  c.pin_sccb_scl = 39;
  c.pin_d7 = 48;
  c.pin_d6 = 11;
  c.pin_d5 = 12;
  c.pin_d4 = 14;
  c.pin_d3 = 16;
  c.pin_d2 = 18;
  c.pin_d1 = 17;
  c.pin_d0 = 15;
  c.pin_vsync = 38;
  c.pin_href = 47;
  c.pin_pclk = 13;
  c.xclk_freq_hz = 20000000;
  c.ledc_timer = LEDC_TIMER_3;
  c.ledc_channel = LEDC_CHANNEL_7;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = SIZES[CAMERA_RES_MAX - CAMERA_RES_MIN];
  c.jpeg_quality = CAMERA_JPEG_QUALITY;
  c.fb_count = 2;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;
  initErr = esp_camera_init(&c);
  if (initErr != ESP_OK) return false;
  sensor_t *s = esp_camera_sensor_get();
  camera_sensor_info_t *info = esp_camera_sensor_get_info(&s->id);
  sensorName = info ? info->name : "unknown sensor";
  if (s->id.PID == OV3660_PID) s->set_vflip(s, 1);
  s->set_framesize(s, SIZES[res - CAMERA_RES_MIN]);
  curRes = res;
  return true;
}

bool wake() {
  if (awake) return true;
  if (!startCamera(wantRes)) return false;
  awake = true;
  wakes = wakes + 1;
  return true;
}

void sleep() {
  if (!awake) return;
  sensor_t *s = esp_camera_sensor_get();
  if (s && (s->id.PID == OV3660_PID || s->id.PID == OV5640_PID)) s->set_reg(s, 0x3008, 0x40, 0x40);
  esp_camera_deinit();
  awake = false;
}

int openUdp() {
  int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0) return -1;
  sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons(CAMERA_PORT);
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(fd, (sockaddr *)&a, sizeof(a)) < 0) {
    close(fd);
    return -1;
  }
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
  return fd;
}

struct UdpViewer {
  sockaddr_in addr;
  uint32_t heardMs;
};

void dropViewer(UdpViewer &viewer) {
  viewer.heardMs = 0;
  viewerIp = 0;
}

void pollUdp(int fd, UdpViewer &viewer) {
  for (;;) {
    char buf[64];
    sockaddr_in from = {};
    socklen_t fromLen = sizeof(from);
    int n = recvfrom(fd, buf, sizeof(buf) - 1, 0, (sockaddr *)&from, &fromLen);
    if (n <= 0) return;
    buf[n] = 0;
    if (strncmp(buf, "video", 5) != 0) continue;
    unsigned long shown, lost, gap;
    if (sscanf(buf + 5, "%lu %lu %lu", &shown, &lost, &gap) == 3) {
      appShown = shown;
      appLost = lost;
      if (gap > appGapMaxMs) appGapMaxMs = gap;
    }
    if (strcmp(buf, "video off") == 0) {
      dropViewer(viewer);
      continue;
    }
    if (!wake()) continue;
    bool same = viewer.heardMs && viewer.addr.sin_addr.s_addr == from.sin_addr.s_addr
                && viewer.addr.sin_port == from.sin_port;
    if (!same) viewers = viewers + 1;
    viewer.addr = from;
    viewer.heardMs = millis();
    viewerIp = from.sin_addr.s_addr;
  }
}

bool sendUdpFrame(int fd, const sockaddr_in &to, const camera_fb_t *fb, uint16_t number) {
  size_t count = (fb->len + CAMERA_UDP_CHUNK - 1) / CAMERA_UDP_CHUNK;
  if (count == 0 || count > 255) return false;
  uint8_t packet[8 + CAMERA_UDP_CHUNK];
  packet[0] = 'S';
  packet[1] = 'V';
  packet[2] = 1;
  packet[4] = (uint8_t)count;
  packet[5] = 0;
  packet[6] = number & 0xff;
  packet[7] = number >> 8;
  for (size_t k = 0; k < count; k++) {
    size_t at = k * CAMERA_UDP_CHUNK, len = min(CAMERA_UDP_CHUNK, fb->len - at);
    packet[3] = (uint8_t)k;
    memcpy(packet + 8, fb->buf + at, len);
    int tries = 0;
    while (sendto(fd, packet, 8 + len, 0, (const sockaddr *)&to, sizeof(to)) < 0) {
      if (++tries > 3) return false;
      vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (k + 1 < count) vTaskDelay(1);
  }
  return true;
}

void cameraTask(void *) {
  if (!startCamera(wantRes)) {
    status = ABSENT;
    vTaskDelete(nullptr);
  }
  awake = true;
  int udp = openUdp();
  status = RUNNING;
  UdpViewer viewer = {};
  uint16_t frameNumber = 0;
  uint32_t fpsStartMs = millis(), fpsFrames = 0;
  uint32_t lastFetchMs = 0, lastSentMs = 0;
  uint32_t watchedMs = millis();
  for (;;) {
    int res = wantRes;
    if (awake && res != curRes) {
      sensor_t *s = esp_camera_sensor_get();
      s->set_framesize(s, SIZES[res - CAMERA_RES_MIN]);
      curRes = res;
    }

    bool had = viewer.heardMs != 0;
    if (udp >= 0) pollUdp(udp, viewer);
    if (viewer.heardMs && millis() - viewer.heardMs > CAMERA_UDP_TIMEOUT_MS) dropViewer(viewer);
    bool has = viewer.heardMs != 0;
    if (has != had) lastFetchMs = lastSentMs = 0;
    if (!has) {
      fps = 0;
      if (awake && millis() - watchedMs > CAMERA_IDLE_MS) sleep();
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    watchedMs = millis();
    const uint32_t periodMs = 1000 / CAMERA_MAX_FPS;
    if (lastFetchMs && millis() - lastFetchMs < periodMs) {
      vTaskDelay(pdMS_TO_TICKS(min<uint32_t>(periodMs - (millis() - lastFetchMs), 10)) + 1);
      continue;
    }
    lastFetchMs = millis();
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      misses = misses + 1;
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    uint32_t startMs = millis();
    if (!sendUdpFrame(udp, viewer.addr, fb, frameNumber)) sendErrors = sendErrors + 1;
    frameNumber++;
    lastBytes = fb->len;
    esp_camera_fb_return(fb);
    uint32_t now = millis();
    sendMaxMs = max((uint32_t)sendMaxMs, now - startMs);
    if (now - startMs > STALL_MS) stalls = stalls + 1;
    if (lastSentMs) gapMaxMs = max((uint32_t)gapMaxMs, now - lastSentMs);
    lastSentMs = now;
    frames = frames + 1;
    fpsFrames++;
    if (now - fpsStartMs >= 1000) {
      fps = fpsFrames * 1000.0f / (now - fpsStartMs);
      fpsStartMs = now;
      fpsFrames = 0;
    }
  }
}

volatile bool selfTestRunning = false;

void selfTestTask(void *arg) {
  uint32_t ms = (uint32_t)(uintptr_t)arg * 1000;
  sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons(CAMERA_PORT);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd >= 0) {
    timeval tv = {0, 50 * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    static uint8_t buf[8 + CAMERA_UDP_CHUNK];
    uint32_t shown = 0, lost = 0, gapMax = 0, lastShownMs = 0, lastAskMs = 0;
    int current = -1;
    uint8_t count = 0, got = 0;
    uint32_t have[8] = {};
    for (uint32_t start = millis(); millis() - start < ms;) {
      if (!lastAskMs || millis() - lastAskMs >= 500) {
        char ask[48];
        int n = snprintf(ask, sizeof(ask), "video %lu %lu %lu", (unsigned long)shown,
                         (unsigned long)lost, (unsigned long)gapMax);
        sendto(fd, ask, n, 0, (sockaddr *)&a, sizeof(a));
        gapMax = 0;
        lastAskMs = millis();
      }
      int n = recv(fd, buf, sizeof(buf), 0);
      if (n < 8 || buf[0] != 'S' || buf[1] != 'V') continue;
      int number = buf[6] | buf[7] << 8;
      uint8_t index = buf[3];
      if (number != current) {
        if (current >= 0 && got < count) lost++;
        current = number;
        count = buf[4];
        got = 0;
        memset(have, 0, sizeof(have));
      }
      if (index >= count || (have[index / 32] >> (index % 32)) & 1) continue;
      have[index / 32] |= 1u << (index % 32);
      if (++got < count) continue;
      shown++;
      uint32_t now = millis();
      if (lastShownMs) gapMax = max(gapMax, now - lastShownMs);
      lastShownMs = now;
    }
    const char off[] = "video off";
    sendto(fd, off, sizeof(off) - 1, 0, (sockaddr *)&a, sizeof(a));
    close(fd);
  }
  selfTestRunning = false;
  vTaskDelete(nullptr);
}

}

bool cameraSelfTest(uint32_t seconds) {
  if (selfTestRunning || status != RUNNING) return false;
  selfTestRunning = true;
  xTaskCreatePinnedToCore(selfTestTask, "camtest", 3072, (void *)(uintptr_t)seconds, 1, nullptr, 0);
  return true;
}

void cameraBegin(int res) {
  cameraConfigure(res);
  xTaskCreatePinnedToCore(cameraTask, "camera", TASK_STACK, nullptr, TASK_PRIORITY, nullptr, 0);
}

void cameraConfigure(int res) { wantRes = constrain(res, CAMERA_RES_MIN, CAMERA_RES_MAX); }

const char *cameraHardware() {
  switch (status) {
    case STARTING: return "starting";
    case ABSENT: return "none";
    case RUNNING: break;
  }
  return sensorName;
}

static void formatIp(char *out, size_t n, uint32_t ip) {
  if (!ip) {
    snprintf(out, n, "none");
    return;
  }
  snprintf(out, n, "%u.%u.%u.%u", (unsigned)(ip & 0xff), (unsigned)((ip >> 8) & 0xff),
           (unsigned)((ip >> 16) & 0xff), (unsigned)(ip >> 24));
}

void cameraStatus(char *camera, char *video, size_t n) {
  video[0] = 0;
  switch (status) {
    case STARTING:
      snprintf(camera, n, "camera starting");
      return;
    case ABSENT:
      snprintf(camera, n, "camera none (init 0x%x: no expansion board)", (unsigned)initErr);
      return;
    case RUNNING:
      break;
  }
  snprintf(camera, n,
           "camera %s %s q%d max %d fps on UDP :%u, %s, %.1f fps, %lu B/frame, "
           "%lu frames, %lu wakes, %lu missed",
           sensorName, SIZE_NAMES[wantRes - CAMERA_RES_MIN], CAMERA_JPEG_QUALITY, CAMERA_MAX_FPS,
           (unsigned)CAMERA_PORT,
           awake ? "capturing" : "asleep", (double)fps, (unsigned long)lastBytes,
           (unsigned long)frames, (unsigned long)wakes, (unsigned long)misses);
  char ip[16];
  formatIp(ip, sizeof(ip), viewerIp);
  snprintf(video, n,
           "video viewer %s; send max %lu ms, gap max %lu ms, %lu stalls, %lu send errors, "
           "%lu viewers; app %lu shown, %lu lost, gap max %lu ms",
           ip, (unsigned long)sendMaxMs, (unsigned long)gapMaxMs, (unsigned long)stalls,
           (unsigned long)sendErrors, (unsigned long)viewers, (unsigned long)appShown,
           (unsigned long)appLost, (unsigned long)appGapMaxMs);
  sendMaxMs = 0;
  gapMaxMs = 0;
  appGapMaxMs = 0;
}

}
