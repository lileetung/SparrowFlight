// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lileetung

#include "blackbox.h"

#include <esp_heap_caps.h>

namespace sparrow {

namespace {

// A slot: the header in its own first page, the samples from SAMPLES_OFFSET, and at DEAD_OFFSET
// a word that "bb erase" programs to 0 to drop the log without an erase. Each byte is programmed
// once between erases, which also suits flash chips with on-chip ECC.
const uint32_t SECTOR = 4096;
const uint32_t SLOT_SECTORS = 17;
const uint32_t SLOT_BYTES = SLOT_SECTORS * SECTOR;
const uint32_t DEAD_OFFSET = 128;
const uint32_t SAMPLES_OFFSET = 256;
static_assert(sizeof(BlackboxHeader) <= DEAD_OFFSET, "the header overlaps the drop mark");
static_assert(SAMPLES_OFFSET + BLACKBOX_CAPACITY * sizeof(BlackboxSample) <= SLOT_BYTES,
              "a log does not fit its slot");

const int ERASE_AHEAD = 2;        // slots kept erased for the next landings
const uint32_t WRITE_GAP_MS = 2;  // after each 256-byte page (about 1 ms with both cores held)
const uint32_t ERASE_GAP_MS = 200;  // after each 4 KB sector erase (about 45 ms with both held)
const uint32_t IDLE_MS = 50;
const uint32_t ERROR_PAUSE_MS = 1000;
const UBaseType_t TASK_PRIORITY = 1;  // below the network send task
const uint32_t TASK_STACK = 4096;

const uint32_t FNV_START = 2166136261u;

uint32_t fnv(uint32_t hash, const void *data, size_t len) {
  const uint8_t *bytes = (const uint8_t *)data;
  for (size_t i = 0; i < len; i++) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  return hash;
}

}

const char *blackboxTriggerName(BlackboxTrigger trigger) {
  switch (trigger) {
    case BB_KILL_TILT: return "KILL_TILT";
    case BB_KILL_COMMAND: return "KILL_COMMAND";
    case BB_KILL_IMU: return "KILL_IMU";
    case BB_LAND_THROTTLE: return "LAND_THROTTLE";
    case BB_LAND_LINK: return "LAND_LINK";
    case BB_LAND_TOF: return "LAND_TOF";
    default: return "NONE";
  }
}

uint32_t FlightBlackbox::slotOffset(int slot) const { return (uint32_t)slot * SLOT_BYTES; }

bool FlightBlackbox::validHeader(const BlackboxHeader &header) const {
  return header.magic == MAGIC && header.version >= 1 && header.version <= VERSION
      && header.headerSize == sizeof(BlackboxHeader)
      && header.sampleSize == sizeof(BlackboxSample)
      && header.count <= BLACKBOX_CAPACITY;
}

// A whole log, not dropped by "bb erase".
bool FlightBlackbox::readHeader(int slot, BlackboxHeader &header) const {
  uint32_t dropMark = 0;
  if (esp_partition_read(part_, slotOffset(slot), &header, sizeof(header)) != ESP_OK) return false;
  if (esp_partition_read(part_, slotOffset(slot) + DEAD_OFFSET, &dropMark, sizeof(dropMark)) != ESP_OK)
    return false;
  return dropMark == 0xFFFFFFFF && validHeader(header);
}

bool FlightBlackbox::verifySamples(int slot, const BlackboxHeader &header) const {
  uint32_t hash = FNV_START;
  uint8_t buffer[64];
  uint32_t at = slotOffset(slot) + SAMPLES_OFFSET;
  size_t remaining = (size_t)header.count * sizeof(BlackboxSample);
  while (remaining > 0) {
    size_t len = min(remaining, sizeof(buffer));
    if (esp_partition_read(part_, at, buffer, len) != ESP_OK) return false;
    hash = fnv(hash, buffer, len);
    at += len;
    remaining -= len;
  }
  return hash == header.checksum;
}

// Erased all through (reads 0xFF), so a log can go in without an erase.
bool FlightBlackbox::slotBlank(int slot) {
  for (uint32_t at = 0; at < SLOT_BYTES; at += sizeof(page_)) {
    if (esp_partition_read(part_, slotOffset(slot) + at, page_, sizeof(page_)) != ESP_OK) return false;
    for (uint8_t b : page_)
      if (b != 0xFF) return false;
  }
  return true;
}

bool FlightBlackbox::begin() {
  part_ = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
  if (!part_) return false;
  slots_ = min<int>(part_->size / SLOT_BYTES, MAX_SLOTS);
  queue_ = (QueuedLog *)heap_caps_malloc(sizeof(QueuedLog) * QUEUE_LOGS, MALLOC_CAP_SPIRAM);
  flashLock_ = xSemaphoreCreateMutex();
  if (slots_ <= ERASE_AHEAD || !queue_ || !flashLock_) return false;
  uint32_t newest = 0;
  int newestSlot = -1;
  for (int slot = 0; slot < slots_; slot++) {
    BlackboxHeader header = {};
    if (readHeader(slot, header) && header.sequence >= newest) {
      newest = header.sequence;
      newestSlot = slot;
    }
  }
  nextSequence_ = newest + 1;
  nextSlot_ = newestSlot < 0 ? 0 : (newestSlot + 1) % slots_;
  for (int k = 0; k < ERASE_AHEAD; k++) {
    int slot = (nextSlot_ + k) % slots_;
    erased_[slot] = slotBlank(slot);
  }
  if (xTaskCreatePinnedToCore(flashLoop, "bbflash", TASK_STACK, this, TASK_PRIORITY, nullptr, 0)
      != pdPASS)
    return false;
  ok_ = true;
  return true;
}

void FlightBlackbox::capture(const BlackboxSample &sample) {
  if (frozen_) return;
  ring_[head_] = sample;
  head_ = (head_ + 1) % BLACKBOX_CAPACITY;
  if (count_ < BLACKBOX_CAPACITY) count_++;
}

void FlightBlackbox::startFlight(bool motorsEnabled) {
  if (!motorsEnabled) return;
  flightActive_ = true;
  frozen_ = false;
  trigger_ = BB_NONE;
}

void FlightBlackbox::freeze(BlackboxTrigger trigger) {
  if (!flightActive_ || frozen_) return;
  trigger_ = trigger;
  frozen_ = true;
}

uint16_t FlightBlackbox::oldestIndex() const {
  return (head_ + BLACKBOX_CAPACITY - count_) % BLACKBOX_CAPACITY;
}

void FlightBlackbox::clearCapture() {
  head_ = count_ = 0;
  flightActive_ = frozen_ = false;
  trigger_ = BB_NONE;
}

bool FlightBlackbox::startSave(uint32_t boot) {
  if (!pending()) return false;
  if (count_ == 0) {
    clearCapture();
    return true;
  }
  bool queuedOk = false;
  portENTER_CRITICAL(&queueLock_);
  int used = queued_;
  int tail = (queueHead_ + used) % QUEUE_LOGS;
  portEXIT_CRITICAL(&queueLock_);
  if (ok_ && used < QUEUE_LOGS) {
    // The task reads only queued entries, so this one is the loop's until it is counted in.
    QueuedLog &log = queue_[tail];
    uint16_t oldest = oldestIndex();
    uint16_t first = min<uint16_t>(count_, BLACKBOX_CAPACITY - oldest);
    memcpy(log.samples, &ring_[oldest], first * sizeof(BlackboxSample));
    memcpy(log.samples + first, ring_, (count_ - first) * sizeof(BlackboxSample));
    BlackboxHeader &header = log.header;
    header = {};
    header.magic = MAGIC;
    header.version = VERSION;
    header.headerSize = sizeof(header);
    header.sampleSize = sizeof(BlackboxSample);
    header.samplePeriodMs = BLACKBOX_SAMPLE_PERIOD_MS;
    header.count = count_;
    header.trigger = trigger_;
    header.sequence = nextSequence_++;
    header.boot = boot;
    header.firstMs = log.samples[0].ms;
    header.lastMs = log.samples[count_ - 1].ms;
    portENTER_CRITICAL(&queueLock_);
    queued_ = queued_ + 1;
    portEXIT_CRITICAL(&queueLock_);
    queuedOk = true;
  } else {
    dropped_ = dropped_ + 1;
  }
  clearCapture();
  return queuedOk;
}

int FlightBlackbox::saveResult() {
  if (saved_ != savedSeen_) {
    savedSeen_++;
    return 1;
  }
  if (failed_ != failedSeen_) {
    failedSeen_++;
    return -1;
  }
  return 0;
}

void FlightBlackbox::flashLoop(void *self) {
  FlightBlackbox &bb = *(FlightBlackbox *)self;
  for (;;) {
    uint32_t pause = IDLE_MS;
    if (bb.flashWork_) {
      xSemaphoreTake(bb.flashLock_, portMAX_DELAY);
      if (bb.flashWork_) pause = bb.flashStep();
      xSemaphoreGive(bb.flashLock_);
    }
    if (pause) vTaskDelay(pdMS_TO_TICKS(pause));
  }
}

// At most one flash operation, and how long to wait before the next: writing a queued log comes
// first, then erasing ahead.
uint32_t FlightBlackbox::flashStep() {
  if (writing_) {
    QueuedLog &log = queue_[queueHead_];
    size_t total = (size_t)log.header.count * sizeof(BlackboxSample);
    uint32_t base = slotOffset(nextSlot_);
    bool last = writePos_ >= total;
    esp_err_t err;
    if (!last) {
      size_t len = min(sizeof(page_), total - writePos_);
      memcpy(page_, (const uint8_t *)log.samples + writePos_, len);
      err = esp_partition_write(part_, base + SAMPLES_OFFSET + writePos_, page_, len);
      writePos_ += len;
    } else {
      memcpy(page_, &log.header, sizeof(log.header));  // last: until then the slot is no log
      err = esp_partition_write(part_, base, page_, sizeof(log.header));
    }
    if (err == ESP_OK && !last) return WRITE_GAP_MS;
    // Written, or refused by the flash: either way the slot needs an erase before it is reused.
    erased_[nextSlot_] = false;
    if (err == ESP_OK) {
      nextSlot_ = (nextSlot_ + 1) % slots_;
      saved_ = saved_ + 1;
    } else {
      failed_ = failed_ + 1;
      flashErrors_ = flashErrors_ + 1;
    }
    writing_ = false;
    portENTER_CRITICAL(&queueLock_);
    queueHead_ = (queueHead_ + 1) % QUEUE_LOGS;
    queued_ = queued_ - 1;
    portEXIT_CRITICAL(&queueLock_);
    return WRITE_GAP_MS;
  }
  if (queued_ > 0 && erased_[nextSlot_]) {
    QueuedLog &log = queue_[queueHead_];
    log.header.checksum = fnv(FNV_START, log.samples, (size_t)log.header.count * sizeof(BlackboxSample));
    writing_ = true;
    writePos_ = 0;
    return 0;
  }
  uint32_t pause = IDLE_MS;
  eraseAheadStep(pause);
  return pause;
}

// One sector of the next slot not yet erased among the ERASE_AHEAD coming ones.
bool FlightBlackbox::eraseAheadStep(uint32_t &pause) {
  for (int k = 0; k < ERASE_AHEAD; k++) {
    int slot = (nextSlot_ + k) % slots_;
    if (erased_[slot]) continue;
    if (eraseSlot_ != slot) {
      eraseSlot_ = slot;
      eraseSector_ = 0;
    }
    if (esp_partition_erase_range(part_, slotOffset(slot) + eraseSector_ * SECTOR, SECTOR) != ESP_OK) {
      flashErrors_ = flashErrors_ + 1;
      pause = ERROR_PAUSE_MS;
      return true;
    }
    if (++eraseSector_ == (int)SLOT_SECTORS) {
      erased_[slot] = true;
      eraseSlot_ = -1;
    }
    pause = ERASE_GAP_MS;
    return true;
  }
  return false;
}

void FlightBlackbox::status(char *out, size_t n) const {
  if (!ok_) {
    snprintf(out, n, "blackbox not started (no partition or no PSRAM)");
    return;
  }
  int erased = 0;
  for (int slot = 0; slot < slots_; slot++) erased += erased_[slot] ? 1 : 0;
  snprintf(out, n,
           "blackbox: %d slots, %d erased ahead, %d waiting, %lu saved, %lu failed, %lu dropped "
           "(queue full), %lu flash errors",
           slots_, erased, (int)queued_, (unsigned long)saved_, (unsigned long)failed_,
           (unsigned long)dropped_, (unsigned long)flashErrors_);
}

void FlightBlackbox::list(Print &out) {
  if (!ok_) {
    out.printf("# BB fs=0 total=0 used=0 sample_size=%u capacity=%u\n",
               (unsigned)sizeof(BlackboxSample), (unsigned)BLACKBOX_CAPACITY);
    return;
  }
  xSemaphoreTake(flashLock_, portMAX_DELAY);
  int logs = 0;
  for (int slot = 0; slot < slots_; slot++) {
    BlackboxHeader header = {};
    if (readHeader(slot, header)) logs++;
  }
  out.printf("# BB fs=1 total=%u used=%u sample_size=%u capacity=%u\n",
             (unsigned)(slots_ * SLOT_BYTES), (unsigned)(logs * SLOT_BYTES),
             (unsigned)sizeof(BlackboxSample), (unsigned)BLACKBOX_CAPACITY);
  for (int slot = 0; slot < slots_; slot++) {
    BlackboxHeader header = {};
    if (!readHeader(slot, header)) continue;
    bool integrityOk = verifySamples(slot, header);
    out.printf("# BB slot=%u seq=%lu boot=%lu format=%u trigger=%s samples=%u time=%lu..%lu checksum=%08lx integrity=%s\n",
               slot, (unsigned long)header.sequence, (unsigned long)header.boot,
               header.version, blackboxTriggerName((BlackboxTrigger)header.trigger), header.count,
               (unsigned long)header.firstMs, (unsigned long)header.lastMs,
               (unsigned long)header.checksum, integrityOk ? "ok" : "BAD");
  }
  xSemaphoreGive(flashLock_);
}

bool FlightBlackbox::dump(Print &out, uint32_t sequence) {
  if (!ok_) return false;
  xSemaphoreTake(flashLock_, portMAX_DELAY);
  int newestSlot = -1;
  BlackboxHeader newest = {};
  for (int slot = 0; slot < slots_; slot++) {
    BlackboxHeader header = {};
    if (readHeader(slot, header) && verifySamples(slot, header)
        && (sequence == 0 || header.sequence == sequence)
        && (newestSlot < 0 || header.sequence > newest.sequence)) {
      newest = header;
      newestSlot = slot;
    }
  }
  if (newestSlot < 0) {
    xSemaphoreGive(flashLock_);
    return false;
  }
  out.printf("# BB_BEGIN seq=%lu boot=%lu trigger=%s samples=%u period_ms=%u\n",
             (unsigned long)newest.sequence, (unsigned long)newest.boot,
             blackboxTriggerName((BlackboxTrigger)newest.trigger), newest.count,
             newest.samplePeriodMs);
  bool v3 = newest.version >= 3;
  out.printf("# BB_FIELDS,ms,state,flags,event,roll,pitch,yaw,rate_r,rate_p,rate_y,raw_sp_r,raw_sp_p,raw_sp_y,sp_r,sp_p,sp_y,p_r,p_p,p_y,i_r,i_p,i_y,d_r,d_p,d_y,out_r,out_p,out_y,%s,manual,base,m1,m2,m3,m4,vbat,tof,height,%s,vertical_speed,alt_limit,link_age,loop_us,sat_mask,imu_miss_total,imu_miss_streak\n",
             v3 ? "acc_up" : "thrust", v3 ? "held_height" : "alt_height");
  uint32_t at = slotOffset(newestSlot) + SAMPLES_OFFSET;
  bool ok = true;
  for (uint16_t n = 0; ok && n < newest.count; n++, at += sizeof(BlackboxSample)) {
    BlackboxSample s = {};
    if (esp_partition_read(part_, at, &s, sizeof(s)) != ESP_OK) {
      ok = false;
      break;
    }
    if (newest.version == 1) s.imuMissTotal = 0;
    out.printf("B,%lu,%u,%u,%u", (unsigned long)s.ms, s.state, s.flags, s.event);
    for (int k = 0; k < 3; k++) out.printf(",%.2f", s.attitudeCd[k] / 100.0f);
    for (int k = 0; k < 3; k++) out.printf(",%.1f", s.rateDps10[k] / 10.0f);
    for (int k = 0; k < 3; k++) out.printf(",%.1f", s.rawSetpoint10[k] / 10.0f);
    for (int k = 0; k < 3; k++) out.printf(",%.1f", s.setpoint10[k] / 10.0f);
    for (int k = 0; k < 3; k++) out.printf(",%.2f", s.pidP100[k] / 100.0f);
    for (int k = 0; k < 3; k++) out.printf(",%.2f", s.pidI100[k] / 100.0f);
    for (int k = 0; k < 3; k++) out.printf(",%.2f", s.pidD100[k] / 100.0f);
    for (int k = 0; k < 3; k++) out.printf(",%.2f", s.pidOut100[k] / 100.0f);
    out.printf(",%ld,%.2f,%.2f", v3 ? (long)s.accUpMmS2 : (long)s.thrustRaw,
               s.manual100 / 100.0f, s.base100 / 100.0f);
    for (int k = 0; k < 4; k++) out.printf(",%.2f", s.motor100[k] / 100.0f);
    out.printf(",%.3f,%d,%d,%d,%d,%.2f,%u,%u,%u,%u,%u\n", s.vbatMv / 1000.0f,
               s.tofMm, s.heightMm, s.altitudeFilteredMm, s.verticalSpeedMmS,
               s.altitudeLimit100 / 100.0f, s.linkAgeMs, s.loopUs, s.saturationMask,
               s.imuMissTotal, newest.version >= 2 ? s.flags >> 5 : 0);
  }
  xSemaphoreGive(flashLock_);
  if (ok) out.printf("# BB_END seq=%lu\n", (unsigned long)newest.sequence);
  return ok;
}

// Drops every log by programming its mark, with no erase: the flash task erases the slots when
// their turn comes.
bool FlightBlackbox::erase() {
  if (!ok_ || saving()) return false;
  xSemaphoreTake(flashLock_, portMAX_DELAY);
  bool ok = true;
  const uint32_t dropped = 0;
  for (int slot = 0; slot < slots_; slot++) {
    BlackboxHeader header = {};
    if (readHeader(slot, header))
      ok = esp_partition_write(part_, slotOffset(slot) + DEAD_OFFSET, &dropped, sizeof(dropped)) == ESP_OK
           && ok;
  }
  nextSequence_ = 1;
  xSemaphoreGive(flashLock_);
  return ok;
}

}
