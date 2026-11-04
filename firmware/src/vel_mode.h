// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lileetung

#pragma once

#include <cmath>
#include <cstdint>

#include "auto_land.h"

namespace sparrow {

struct VelModeConfig {
  float maxMmS = 300.0f;
  float idleShare = 0.1f;
  float takeoffMarginPct = 8.0f;
  float takeoffRampPctS = 60.0f;
  float takeoffFallbackPct = 55.0f;
  uint32_t takeoffBoostAfterMs = 1500;
  float takeoffBoostPctS = 5.0f;
  float takeoffMaxPct = 80.0f;
  uint32_t takeoffGiveUpMs = 6000;
  uint32_t landPullMs = 1500;
  float landPullShare = 0.8f;
  float landPullMarginMm = 20.0f;
  float cancelLandShare = 0.3f;
};

class VelMode {
 public:
  explicit VelMode(const VelModeConfig &config = VelModeConfig()) : config_(config) {}

  VelModeConfig &config() { return config_; }
  const VelModeConfig &config() const { return config_; }

  float share(float speedMmS) const { return clampf(speedMmS / config_.maxMmS, -1.0f, 1.0f); }
  bool centred(float speedMmS) const { return fabsf(share(speedMmS)) < config_.idleShare; }
  bool cancelsLanding(float speedMmS) const { return share(speedMmS) >= config_.cancelLandShare; }

  void startTakeoff(uint32_t nowMs) {
    takeoffMs_ = nowMs;
    boostPct_ = 0;
    climbing_ = true;
    pullSinceMs_ = 0;
  }
  bool climbingOut() const { return climbing_; }

  float takeoffThrust(uint32_t nowMs, float hoverPrior, bool airborne) {
    float t = (nowMs - takeoffMs_) * 0.001f;
    float late = t - config_.takeoffBoostAfterMs * 0.001f;
    if (!airborne && late > 0) boostPct_ = fmaxf(boostPct_, config_.takeoffBoostPctS * late);
    float hover = hoverPrior > 0 ? hoverPrior : config_.takeoffFallbackPct;
    float wanted = fminf(hover + config_.takeoffMarginPct + boostPct_, config_.takeoffMaxPct);
    return fminf(config_.takeoffRampPctS * t, wanted);
  }
  bool takeoffFailed(uint32_t nowMs, bool airborne) const {
    return climbing_ && !airborne && nowMs - takeoffMs_ > config_.takeoffGiveUpMs;
  }

  void engaged() {
    climbing_ = false;
    pullSinceMs_ = 0;
  }
  void reset() {
    climbing_ = false;
    pullSinceMs_ = 0;
  }

  void steer(AutoLand &lander, float speedMmS, float floorMm, float ceilingMm) const {
    float v = clampf(speedMmS, -config_.maxMmS, config_.maxMmS);
    float above = lander.holdAboveMm();
    bool blocked = (v < 0 && above <= floorMm) || (v > 0 && above >= ceilingMm);
    if (!centred(v) && !blocked) {
      lander.setHoldSpeed(v);
      return;
    }
    lander.clearHoldSpeed();
    float target = lander.holdTargetMm();
    if (target < floorMm || target > ceilingMm)
      lander.setHoldTargetNow(clampf(target, floorMm, ceilingMm));
  }

  bool pulledDown(uint32_t nowMs, const AutoLand &lander, float speedMmS, float floorMm) {
    bool down = share(speedMmS) <= -config_.landPullShare
                && lander.holdAboveMm() <= floorMm + config_.landPullMarginMm;
    if (!down) {
      pullSinceMs_ = 0;
      return false;
    }
    if (pullSinceMs_ == 0) pullSinceMs_ = nowMs;
    return nowMs - pullSinceMs_ >= config_.landPullMs;
  }

 private:
  VelModeConfig config_;
  bool climbing_ = false;
  uint32_t takeoffMs_ = 0, pullSinceMs_ = 0;
  float boostPct_ = 0;
};

}
