// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lileetung

#pragma once

#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_reg.h>

namespace sparrow {

class SoftI2C {
 public:
  static const uint32_t STRETCH_MAX_US = 100;

  void begin(int sda, int scl, uint32_t hz) {
    sda_ = 1u << sda;
    scl_ = 1u << scl;
    uint32_t mhz = getCpuFrequencyMhz();
    half_ = mhz * 1000000u / hz / 2;
    stretchMax_ = mhz * STRETCH_MAX_US;
    for (int pin : {sda, scl}) {
      gpio_config_t cfg = {};
      cfg.pin_bit_mask = 1ull << pin;
      cfg.mode = GPIO_MODE_INPUT_OUTPUT_OD;
      cfg.pull_up_en = GPIO_PULLUP_ENABLE;
      cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
      cfg.intr_type = GPIO_INTR_DISABLE;
      gpio_config(&cfg);
      esp_rom_gpio_connect_out_signal(pin, SIG_GPIO_OUT_IDX, false, false);
    }
    sdaHigh();
    sclHigh();
    delayMicroseconds(10);
    if (!idle()) recover();
  }

  bool read(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n) {
    bool ok = start() && writeByte(addr << 1) && writeByte(reg);
    ok = stop() && ok;
    if (!ok) return fail();
    ok = start() && writeByte((addr << 1) | 1);
    for (size_t i = 0; ok && i < n; i++) ok = readByte(buf[i], i + 1 < n);
    ok = stop() && ok;
    return ok || fail();
  }

  bool write(uint8_t addr, uint8_t reg, uint8_t value) {
    bool ok = start() && writeByte(addr << 1) && writeByte(reg) && writeByte(value);
    ok = stop() && ok;
    return ok || fail();
  }

  uint32_t errors() const { return errors_; }
  uint32_t recoveries() const { return recoveries_; }

 private:
  uint32_t sda_ = 0, scl_ = 0, half_ = 0, stretchMax_ = 0;
  uint32_t errors_ = 0, recoveries_ = 0;

  inline void sdaLow() { REG_WRITE(GPIO_OUT_W1TC_REG, sda_); }
  inline void sdaHigh() { REG_WRITE(GPIO_OUT_W1TS_REG, sda_); }
  inline void sclLow() { REG_WRITE(GPIO_OUT_W1TC_REG, scl_); }
  inline void sclHigh() { REG_WRITE(GPIO_OUT_W1TS_REG, scl_); }
  inline bool sdaIn() const { return REG_READ(GPIO_IN_REG) & sda_; }
  inline bool sclIn() const { return REG_READ(GPIO_IN_REG) & scl_; }
  inline bool idle() const { return sdaIn() && sclIn(); }

  inline void wait() const {
    uint32_t t0 = ESP.getCycleCount();
    while (ESP.getCycleCount() - t0 < half_) {
    }
  }

  bool sclRise() {
    sclHigh();
    uint32_t t0 = ESP.getCycleCount();
    while (!sclIn())
      if (ESP.getCycleCount() - t0 > stretchMax_) return false;
    return true;
  }

  bool fail() {
    errors_++;
    sdaHigh();
    sclHigh();
    return false;
  }

  bool recover() {
    recoveries_++;
    sdaHigh();
    for (int i = 0; i < 9 && !sdaIn(); i++) {
      sclLow();
      wait();
      sclHigh();
      wait();
    }
    sclLow();
    wait();
    sdaLow();
    wait();
    sclHigh();
    wait();
    sdaHigh();
    wait();
    return idle();
  }

  bool start() {
    if (!idle() && !recover()) return false;
    sdaLow();
    wait();
    sclLow();
    wait();
    return true;
  }

  bool stop() {
    sdaLow();
    wait();
    bool ok = sclRise();
    wait();
    sdaHigh();
    wait();
    return ok && sdaIn();
  }

  bool writeByte(uint8_t b) {
    for (int i = 0; i < 8; i++, b <<= 1) {
      if (b & 0x80) sdaHigh(); else sdaLow();
      wait();
      if (!sclRise()) return false;
      wait();
      sclLow();
    }
    sdaHigh();
    wait();
    if (!sclRise()) return false;
    bool ack = !sdaIn();
    wait();
    sclLow();
    return ack;
  }

  bool readByte(uint8_t &b, bool ack) {
    b = 0;
    sdaHigh();
    for (int i = 0; i < 8; i++) {
      wait();
      if (!sclRise()) return false;
      b = (b << 1) | (sdaIn() ? 1 : 0);
      wait();
      sclLow();
    }
    if (ack) sdaLow(); else sdaHigh();
    wait();
    if (!sclRise()) return false;
    wait();
    sclLow();
    sdaHigh();
    return true;
  }
};

}
