// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lileetung

#pragma once

#include <Arduino.h>
#include <esp_partition.h>
#include <freertos/semphr.h>
#include <stdint.h>

namespace sparrow {

static const uint16_t BLACKBOX_CAPACITY = 750;
static const uint16_t BLACKBOX_SAMPLE_PERIOD_MS = 20;

enum BlackboxTrigger : uint8_t {
  BB_NONE = 0,
  BB_KILL_TILT = 1,
  BB_KILL_COMMAND = 2,
  BB_KILL_IMU = 3,
  BB_LAND_THROTTLE = 4,
  BB_LAND_LINK = 5,
  BB_LAND_TOF = 6,
};

const char *blackboxTriggerName(BlackboxTrigger trigger);

struct BlackboxSample {
  uint32_t ms;
  int16_t attitudeCd[3];
  int16_t rateDps10[3];
  int16_t rawSetpoint10[3];
  int16_t setpoint10[3];
  int16_t pidP100[3];
  int16_t pidI100[3];
  int16_t pidD100[3];
  int16_t pidOut100[3];
  union {
    uint16_t thrustRaw;
    int16_t accUpMmS2;
  };
  uint16_t manual100;
  uint16_t base100;
  uint16_t motor100[4];
  uint16_t vbatMv;
  int16_t tofMm;
  int16_t heightMm;
  int16_t altitudeFilteredMm;
  int16_t verticalSpeedMmS;
  uint16_t altitudeLimit100;
  uint16_t linkAgeMs;
  uint16_t loopUs;
  uint8_t state;
  uint8_t flags;
  uint8_t saturationMask;
  uint8_t event;
  uint16_t imuMissTotal;
};

static_assert(sizeof(BlackboxSample) == 88,
              "blackbox wire format or reserved RAM budget changed");

struct __attribute__((packed)) BlackboxHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t headerSize;
  uint16_t sampleSize;
  uint16_t samplePeriodMs;
  uint16_t count;
  uint8_t trigger;
  uint8_t reserved;
  uint32_t sequence;
  uint32_t boot;
  uint32_t firstMs;
  uint32_t lastMs;
  uint32_t checksum;
};

// The flash code runs from and the logs share one chip: while it erases or writes, both cores
// stop. A file system erased 66 KB at each save, sector after sector (45 ms each), and for 1-2 s
// after every landing the WiFi had no time left: the app's telemetry stopped. So, as dedicated
// flight loggers do, the slow part comes first and apart: the logs go into fixed slots of the
// raw partition, a task on core 0 erases the next slots ahead of time, one sector at a time with
// a long pause between, and a landing's log is then only written, a 256-byte page (about 1 ms)
// at a time. The task touches the flash only while setFlashWork allows it (on the ground, the
// motors off). A log waits in PSRAM until written; the header goes last, so a slot written halfway
// (power cut) does not count.
class FlightBlackbox {
 public:
  // Finds the partition, reserves the PSRAM queue and starts the flash task; false without them.
  bool begin();
  void capture(const BlackboxSample &sample);
  void startFlight(bool motorsEnabled);
  void freeze(BlackboxTrigger trigger);
  bool pending() const { return frozen_ && flightActive_; }
  // Queues the frozen flight for the flash task (a copy in PSRAM) and frees the capture for the
  // next flight at once. False, the log dropped, when the blackbox did not start or the queue is
  // full.
  bool startSave(uint32_t boot);
  // The flash task may erase and write (the caller's loop sets it every time round).
  void setFlashWork(bool allowed) { flashWork_ = allowed; }
  // Once per log the task finished: 1 saved, -1 failed; 0 when none has finished since.
  int saveResult();
  // A log queued or being written: the bb commands wait for it.
  bool saving() const { return queued_ > 0; }
  void status(char *out, size_t n) const;
  void list(Print &out);
  bool dump(Print &out, uint32_t sequence = 0);
  bool erase();
  bool fsOk() const { return ok_; }
  uint16_t count() const { return count_; }

 private:
  static const uint32_t MAGIC = 0x31424253;
  static const uint16_t VERSION = 3;
  static const int MAX_SLOTS = 32;
  static const int QUEUE_LOGS = 3;

  struct QueuedLog {
    BlackboxHeader header;
    BlackboxSample samples[BLACKBOX_CAPACITY];
  };

  BlackboxSample ring_[BLACKBOX_CAPACITY] = {};
  uint16_t head_ = 0, count_ = 0;
  uint32_t nextSequence_ = 1;
  bool ok_ = false, flightActive_ = false, frozen_ = false;
  BlackboxTrigger trigger_ = BB_NONE;

  const esp_partition_t *part_ = nullptr;
  int slots_ = 0;
  SemaphoreHandle_t flashLock_ = nullptr;  // one flash step, or a bb command
  volatile bool flashWork_ = false;
  // Owned by the flash task, under flashLock_.
  bool erased_[MAX_SLOTS] = {};
  int nextSlot_ = 0;
  int eraseSlot_ = -1, eraseSector_ = 0;
  bool writing_ = false;
  size_t writePos_ = 0;
  uint8_t page_[256];  // internal RAM: the flash cannot read PSRAM while it writes
  // The PSRAM queue: the loop adds at the tail, the task takes from the head.
  QueuedLog *queue_ = nullptr;
  portMUX_TYPE queueLock_ = portMUX_INITIALIZER_UNLOCKED;
  int queueHead_ = 0;
  volatile int queued_ = 0;
  volatile uint32_t saved_ = 0, failed_ = 0, dropped_ = 0, flashErrors_ = 0;
  uint32_t savedSeen_ = 0, failedSeen_ = 0;

  static void flashLoop(void *self);
  uint32_t flashStep();
  bool eraseAheadStep(uint32_t &pause);
  uint32_t slotOffset(int slot) const;
  bool slotBlank(int slot);
  bool readHeader(int slot, BlackboxHeader &header) const;
  bool validHeader(const BlackboxHeader &header) const;
  bool verifySamples(int slot, const BlackboxHeader &header) const;
  uint16_t oldestIndex() const;
  void clearCapture();
};

}
