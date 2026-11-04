// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lileetung

#include "net_send.h"

#include <errno.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>
#include <freertos/task.h>
#include <lwip/sockets.h>

// Telemetry keeps no backlog: only the newest line per device waits. A backlog of telemetry kept
// the WiFi driver's queue full while a phone was off scanning other channels, and the drone's ARP
// answers, which need room in it too, did not get out: the phone then gave up on the drone for
// 20 s (2 Nov 2026). A send the driver refuses for want of a buffer (ENOMEM, usually for a moment)
// is tried again 2 ms later, up to SEND_TRIES in all: sent only once, one telemetry line in six
// was lost in flight at -67 dBm, against almost none with the retries.

namespace sparrow {
namespace {

const size_t QUEUE_BYTES = 16 * 1024;
const size_t MAX_TEXT = 640;
const int LATEST_SLOTS = 3;  // one per telemetry device (MAX_TEL_PEERS)
const int SEND_TRIES = 3;
const uint32_t RETRY_MS = 2;
const UBaseType_t TASK_PRIORITY = 3;
const uint32_t TASK_STACK = 4096;

struct Item {
  uint32_t ip;
  char text[MAX_TEXT];
};

struct Latest {
  uint32_t ip;
  bool pending;
  size_t len;
  char text[MAX_TEXT];
};

RingbufHandle_t queue = nullptr;
TaskHandle_t task = nullptr;
portMUX_TYPE latestLock = portMUX_INITIALIZER_UNLOCKED;
Latest latest[LATEST_SLOTS] = {};
int nextSlot = 0;
int fd = -1;
uint16_t destPort = 0;
volatile uint32_t queued = 0, lines = 0, replaced = 0, sent = 0, retried = 0, dropped = 0, failed = 0;
volatile int lastErrno = 0;
volatile uint32_t firstFailMs = 0, lastFailMs = 0;
volatile uint32_t sendMaxUs = 0;
volatile size_t lowestFree = QUEUE_BYTES;

void sendOne(uint32_t ip, const char *text, size_t len) {
  sockaddr_in to = {};
  to.sin_family = AF_INET;
  to.sin_port = htons(destPort);
  to.sin_addr.s_addr = ip;
  uint32_t t = micros();
  int n = -1, err = 0, tries = 0;
  while (n < 0 && tries < SEND_TRIES && (tries == 0 || err == ENOMEM)) {
    if (tries++) vTaskDelay(pdMS_TO_TICKS(RETRY_MS));
    n = sendto(fd, text, len, 0, (sockaddr *)&to, sizeof(to));
    if (n < 0) err = errno;
  }
  uint32_t us = micros() - t;
  if (us > sendMaxUs) sendMaxUs = us;
  if (n >= 0) {
    sent = sent + 1;
    if (tries > 1) retried = retried + 1;
    return;
  }
  lastErrno = err;
  if (!failed) firstFailMs = millis();
  lastFailMs = millis();
  failed = failed + 1;
}

// The next telemetry line waiting, taking turns between the devices.
bool takeLatest(Item &out, size_t &len) {
  bool found = false;
  portENTER_CRITICAL(&latestLock);
  for (int k = 0; k < LATEST_SLOTS && !found; k++) {
    Latest &slot = latest[(nextSlot + k) % LATEST_SLOTS];
    if (!slot.pending) continue;
    out.ip = slot.ip;
    len = slot.len;
    memcpy(out.text, slot.text, len);
    slot.pending = false;
    nextSlot = (nextSlot + k + 1) % LATEST_SLOTS;
    found = true;
  }
  portEXIT_CRITICAL(&latestLock);
  return found;
}

// Replies first, in order, then the telemetry lines.
void sendTask(void *) {
  static Item line;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    for (;;) {
      size_t size = 0;
      Item *reply = (Item *)xRingbufferReceive(queue, &size, 0);
      if (reply) {
        sendOne(reply->ip, reply->text, size - sizeof(reply->ip));
        vRingbufferReturnItem(queue, reply);
        continue;
      }
      size_t len = 0;
      if (!takeLatest(line, len)) break;
      sendOne(line.ip, line.text, len);
    }
  }
}

}

void netSendBegin(uint16_t port) {
  destPort = port;
  fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  queue = xRingbufferCreate(QUEUE_BYTES, RINGBUF_TYPE_NOSPLIT);
  xTaskCreatePinnedToCore(sendTask, "netsend", TASK_STACK, nullptr, TASK_PRIORITY, &task, 0);
}

bool netSend(uint32_t ip, const char *text) {
  if (!queue || !task || fd < 0) return false;
  Item item;
  item.ip = ip;
  size_t len = strnlen(text, MAX_TEXT);
  memcpy(item.text, text, len);
  if (xRingbufferSend(queue, &item, sizeof(item.ip) + len, 0) != pdTRUE) {
    dropped = dropped + 1;
    return false;
  }
  queued = queued + 1;
  size_t free = xRingbufferGetCurFreeSize(queue);
  if (free < lowestFree) lowestFree = free;
  xTaskNotifyGive(task);
  return true;
}

void netSendLatest(uint32_t ip, const char *text) {
  if (!task || fd < 0) return;
  size_t len = strnlen(text, MAX_TEXT);
  portENTER_CRITICAL(&latestLock);
  Latest *slot = nullptr;
  for (auto &s : latest)
    if (!slot && s.pending && s.ip == ip) slot = &s;
  for (auto &s : latest)
    if (!slot && !s.pending) slot = &s;
  if (!slot) slot = &latest[0];
  if (slot->pending) replaced = replaced + 1;
  slot->ip = ip;
  slot->len = len;
  memcpy(slot->text, text, len);
  slot->pending = true;
  lines = lines + 1;
  portEXIT_CRITICAL(&latestLock);
  xTaskNotifyGive(task);
}

void netSendStatus(char *out, size_t n) {
  snprintf(out, n,
           "net send task: %lu replies queued, %lu telemetry lines (%lu replaced unsent), %lu sent "
           "(%lu after a retry), %lu dropped (queue full), %lu failed (last errno %d %s, at %lu-%lu ms), "
           "send max %lu us, queue low %u of %u B free",
           (unsigned long)queued, (unsigned long)lines, (unsigned long)replaced, (unsigned long)sent,
           (unsigned long)retried, (unsigned long)dropped, (unsigned long)failed, lastErrno,
           lastErrno ? strerror(lastErrno) : "-",
           (unsigned long)firstFailMs, (unsigned long)lastFailMs,
           (unsigned long)sendMaxUs, (unsigned)lowestFree, (unsigned)QUEUE_BYTES);
  sendMaxUs = 0;
}

}
