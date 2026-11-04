// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lileetung

#pragma once

#include <math.h>
#include <stdint.h>

#include "control_math.h"

namespace sparrow {

struct AutoLandConfig {
  float descentMmS = 200.0f;
  float kp = 0.02f;
  float ki = 0.03f;
  float integralMax = 15.0f;
  float belowHover = 15.0f;
  float aboveHover = 25.0f;
  float hoverTrackMmS = 150.0f;
  float hoverTrackTauS = 1.0f;
  float hoverPriorBand = 8.0f;
  float hoverMin = 30.0f, hoverMax = 85.0f;
  float hoverAdaptErrMmS = 150.0f;
  uint32_t hoverAdaptSettleMs = 50;
  float hoverAdaptTauS = 0.1f;
  float hoverAdaptMarginPct = 3.0f;
  float accLpTauS = 0.1f;
  float motorTauS = 0.1f;
  float thrustExp = 0.8f;
  float touchdownMm = 30.0f;
  float stallBelowMm = 120.0f;
  float stallProgressMm = 20.0f;
  uint32_t stallMs = 1000;
  float surfaceStepMm = 100.0f;
  float heightAlpha = 0.65f;
  float velocityAlpha = 0.40f;
  uint32_t tofLostMs = 300;
  uint32_t tofLostInertialMs = 800;
  float tofGapS = 0.25f;
  float bridgeGapS = 1.0f;
  uint8_t reacquireSamples = 4;
  float tofLostDropPct = 5.0f;
  float tofLostDecayPctS = 2.0f;
  uint32_t tofLostMaxMs = 5000;
  uint32_t fallbackRampMs = 1000;
  uint32_t maxMs = 15000;
  float holdKh = 1.5f;
  float holdMaxClimbMmS = 300.0f;
  float holdMaxSinkMmS = 300.0f;
  float holdBrakeMmS2 = 2000.0f;
  float holdEarliestMm = 150.0f;
  float holdEngageSettledMm = 50.0f;
  float holdFollowMmS = 150.0f;
  float holdFollowMinMm = 250.0f, holdFollowMinShare = 0.4f;
  float holdFollowSlowMmS = 40.0f;
  uint32_t holdFollowDownMs = 1500;
  float holdEdgeMmS = 150.0f;
  uint32_t holdEdgeMs = 3000;
  float holdEdgeDriftMm = 50.0f;
  float holdTargetTauS = 0.3f;
  float accTauS = 0.5f;
  float accGroundTauS = 0.25f;
  float accStepMm = 15.0f;
  float accFaultClipMm = 20.0f;
  float accBiasMaxMmS2 = 3000.0f;
  float accMaxMmS2 = 15000.0f;
  uint32_t accFreshMs = 50;
  float accFaultMm = 600.0f;
  float accFaultStepMm = 150.0f;
  float accFaultTauS = 2.0f;
};

enum AutoLandEnd : uint8_t {
  LAND_RUNNING = 0,
  LAND_TOUCHDOWN = 1,
  LAND_TIMEOUT = 2,
  LAND_TOF_RAMP = 3,
  LAND_STALLED = 4,
};

class AutoLand {
 public:
  explicit AutoLand(const AutoLandConfig &config = AutoLandConfig()) : config_(config) {}

  void observe(uint32_t nowMs, float heightMm) {
    float gap = (nowMs - lastSampleMs_) * 0.001f;
    bool first = !haveSample_;
    bool bridged = !first && gap > config_.tofGapS && gap <= config_.bridgeGapS && inertial_
                   && inertialUsable(nowMs);
    if (!first && gap > config_.tofGapS && inertial_) tofOffsetMm_ = offsetMm_;
    uint32_t steps = surfaceSteps_;
    bool faulted = accFault_;
    observeTof(heightMm, gap);
    lastSampleMs_ = nowMs;
    if (!first && gap > config_.tofGapS && !bridged) reacquire_ = config_.reacquireSamples;
    else if (reacquire_ > 0) reacquire_--;
    bool usable = inertialUsable(nowMs) && reacquire_ == 0;
    if (usable && !inertial_) {
      zMm_ = tofContinuousMm_;
      vMmS_ = tofSpeedMmS_;
      accJumpMm_ = 0;
      offsetMm_ = tofOffsetMm_;
      offsetDriftMm_ = 0;
    } else if (usable && !first) {
      correctInertial(heightMm, gap);
      if (bridged) {
        tofOffsetMm_ = offsetMm_;
        tofContinuousMm_ = heightMm + tofOffsetMm_;
        tofSpeedMmS_ = clampf(vMmS_, -kTofSpeedMaxMmS, kTofSpeedMaxMmS);
      }
    }
    inertial_ = usable && !accFault_;
    if (inertial_ ? fabsf(offsetDriftMm_) > config_.holdEdgeDriftMm : surfaceSteps_ != steps) {
      surfaceChanged_ = true;
      surfaceChangeMs_ = nowMs;
    }
    if (accFault_ && !faulted) surfaceChanged_ = false;
  }

  void predict(uint32_t nowMs, float dt, float accUpMmS2) {
    if (!useAcc_ || !(dt > 0) || dt > 0.05f) return;
    if (!(fabsf(accUpMmS2) <= config_.accMaxMmS2)) {
      accRejects_++;
      return;
    }
    accLastMs_ = nowMs;
    haveAcc_ = true;
    if (!inertial_) return;
    float a = accUpMmS2 - biasMmS2_;
    zMm_ += vMmS_ * dt + 0.5f * a * dt * dt;
    vMmS_ += a * dt;
    accLpMmS2_ += clampf(dt / config_.accLpTauS, 0.0f, 1.0f) * (a - accLpMmS2_);
  }

  void setAccelAided(bool on) { useAcc_ = on; }
  void setAccelTau(float tauS) { config_.accTauS = clampf(tauS, 0.2f, 5.0f); }
  void setGrounded(bool grounded) { grounded_ = grounded; }

  void rearm() {
    accFault_ = false;
    offsetDriftMm_ = 0;
    hoverShift_ = 0;
    tofSpeedMmS_ = vMmS_ = 0;
    tofOffsetMm_ = offsetMm_ = 0;
    tofContinuousMm_ = zMm_ = filteredMm_;
    reacquire_ = 0;
    surfaceChanged_ = false;
    tofJumpMm_ = accJumpMm_ = 0;
  }

  void setHoverPrior(float prior) { prior_ = prior; }

  void trackHover(float baseThrust, float dt) {
    if (!haveSample_ || fabsf(verticalSpeedMmS()) > config_.hoverTrackMmS) return;
    float p = shiftedPrior();
    if (!haveHover_) {
      haveHover_ = true;
      trackedHover_ = p > 0 ? p : baseThrust;
    }
    trackedHover_ += clampf(dt / config_.hoverTrackTauS, 0.0f, 1.0f) * (baseThrust - trackedHover_);
    if (p > 0)
      trackedHover_ = clampf(trackedHover_, p - config_.hoverPriorBand, p + config_.hoverPriorBand);
  }

  void forgetHover() { haveHover_ = false; }

  void start(uint32_t nowMs, float baseThrust) {
    active_ = true;
    hold_ = false;
    startMs_ = nowMs;
    float p = shiftedPrior();
    float hover = haveHover_ ? trackedHover_ : (p > 0 ? p : baseThrust);
    hover_ = clampf(hover, config_.hoverMin, config_.hoverMax);
    integral_ = 0;
    thrust_ = thrustMotor_ = thrustLp_ = baseThrust;
    flooredMs_ = 0;
    rampFrom_ = 0;
    rampStartMs_ = 0;
    lowestMm_ = filteredMm_;
    progressMs_ = nowMs;
  }

  bool holdDue(uint32_t nowMs, float engageMm) const {
    if (!heightFresh(nowMs) || filteredMm_ < config_.holdEarliestMm) return false;
    if ((inertial_ ? accJumpMm_ : tofJumpMm_) > config_.holdEngageSettledMm) return false;
    float v = fmaxf(verticalSpeedMmS(), 0.0f);
    return filteredMm_ + v * v / (2 * config_.holdBrakeMmS2) >= engageMm;
  }

  void startHold(uint32_t nowMs, float baseThrust, float targetMm) {
    start(nowMs, baseThrust);
    hold_ = true;
    holdSpeedOn_ = false;
    holdTargetMm_ = targetMm;
    tofOffsetMm_ = offsetMm_ = 0;
    followMm_ = 0;
    following_ = lowerPending_ = false;
    surfaceChanged_ = false;
    tofContinuousMm_ = zMm_ = previousMm_;
  }

  void setHoldTarget(float targetMm, float dt) {
    holdTargetMm_ += clampf(dt / config_.holdTargetTauS, 0.0f, 1.0f) * (targetMm - holdTargetMm_);
  }

  void setHoldSpeed(float speedMmS) {
    holdSpeedOn_ = true;
    holdSpeedMmS_ = speedMmS;
  }
  void clearHoldSpeed() { holdSpeedOn_ = false; }
  bool holdSpeedOn() const { return holdSpeedOn_; }
  void setHoldTargetNow(float targetMm) { holdTargetMm_ = targetMm; }
  float holdAboveMm() const { return heldHeightMm() - followMm_; }

  void stop() { active_ = false; }

  float update(uint32_t nowMs, float dt, AutoLandEnd &end) {
    end = LAND_RUNNING;
    if (!active_) return 0;
    if (inertial_ && !inertialUsable(nowMs)) inertial_ = false;
    bool stale = !heightFresh(nowMs);
    bool lost = stale && (!haveSample_ || !inertial_
                          || nowMs - lastSampleMs_ > config_.tofLostInertialMs);
    uint32_t lostSince = haveSample_ && lastSampleMs_ > startMs_ ? lastSampleMs_ : startMs_;
    uint32_t lostFor = lost ? nowMs - lostSince : 0;
    if (rampStartMs_ != 0 || lostFor > config_.tofLostMaxMs) {
      if (rampStartMs_ == 0) {
        rampStartMs_ = nowMs;
        rampFrom_ = thrust_;
      }
      float k = 1.0f - (nowMs - rampStartMs_) / (float)config_.fallbackRampMs;
      thrust_ = rampFrom_ * clampf(k, 0.0f, 1.0f);
      if (k <= 0) end = LAND_TOF_RAMP;
    } else if (lost) {
      float drop = config_.tofLostDropPct + config_.tofLostDecayPctS * lostFor * 0.001f;
      thrust_ = hover_ - fminf(drop, config_.belowHover);
      lowestMm_ = 1e9f;
    } else if (hold_) {
      float apart = surfaceMm() - followMm_;
      float need = fminf(config_.holdFollowMinMm, config_.holdFollowMinShare * holdTargetMm_);
      bool drop = apart < -need;
      if (apart > need) following_ = true;
      if (drop) {
        if (!lowerPending_) lowerSinceMs_ = nowMs;
        lowerPending_ = true;
        if (nowMs - lowerSinceMs_ >= config_.holdFollowDownMs) following_ = true;
      } else if (apart > -0.5f * need) {
        lowerPending_ = false;
      }
      float rate = following_ ? config_.holdFollowMmS : (drop ? 0.0f : config_.holdFollowSlowMmS);
      float step = rate * dt;
      followMm_ += clampf(apart, -step, step);
      if (following_ && fabsf(surfaceMm() - followMm_) < 1) following_ = false;
      bool edge = holdEdge(nowMs);
      float climb = edge ? fminf(config_.holdEdgeMmS, config_.holdMaxClimbMmS) : config_.holdMaxClimbMmS;
      float sink = edge ? fminf(config_.holdEdgeMmS, config_.holdMaxSinkMmS) : config_.holdMaxSinkMmS;
      float target;
      if (holdSpeedOn_) {
        holdTargetMm_ = holdAboveMm();
        target = clampf(holdSpeedMmS_, -sink, climb);
      } else {
        target = clampf(config_.holdKh * (followMm_ + holdTargetMm_ - heldHeightMm()), -sink, climb);
      }
      thrust_ = speedControl(target, dt);
    } else if (stale) {
      thrust_ = speedControl(-config_.descentMmS, dt);
      lowestMm_ = 1e9f;
    } else if (filteredMm_ <= config_.touchdownMm) {
      thrust_ = 0;
      end = LAND_TOUCHDOWN;
    } else if (stalled(nowMs)) {
      thrust_ = 0;
      end = LAND_STALLED;
    } else if (nowMs - startMs_ > config_.maxMs) {
      thrust_ = 0;
      end = LAND_TIMEOUT;
    } else {
      thrust_ = speedControl(-config_.descentMmS, dt);
    }
    if (end != LAND_RUNNING) active_ = false;
    thrustMotor_ += clampf(dt / config_.motorTauS, 0.0f, 1.0f) * (thrust_ - thrustMotor_);
    thrustLp_ += clampf(dt / config_.accLpTauS, 0.0f, 1.0f) * (thrustMotor_ - thrustLp_);
    return thrust_;
  }

  bool active() const { return active_; }
  bool holding() const { return active_ && hold_; }
  bool heightFresh(uint32_t nowMs) const {
    return haveSample_ && nowMs - lastSampleMs_ <= config_.tofLostMs;
  }
  float heldHeightMm() const { return inertial_ ? zMm_ : tofContinuousMm_; }
  float holdTargetMm() const { return holdTargetMm_; }
  float surfaceMm() const { return inertial_ ? offsetMm_ : tofOffsetMm_; }
  float holdSurfaceMm() const { return followMm_; }
  bool holdEdge(uint32_t nowMs) const {
    return surfaceChanged_ && nowMs - surfaceChangeMs_ < config_.holdEdgeMs;
  }
  float filteredHeightMm() const { return filteredMm_; }
  float verticalSpeedMmS() const { return inertial_ ? vMmS_ : tofSpeedMmS_; }
  float tofSpeedMmS() const { return tofSpeedMmS_; }
  float thrust() const { return thrust_; }
  float hoverEstimate() const { return haveHover_ ? trackedHover_ : shiftedPrior(); }
  bool hoverTracked() const { return haveHover_; }
  float hoverUsed() const { return hover_; }
  float hoverShift() const { return hoverShift_; }
  uint32_t surfaceSteps() const { return surfaceSteps_; }
  bool inertial() const { return inertial_; }
  bool accFault() const { return accFault_; }
  float accBiasMmS2() const { return biasMmS2_; }
  uint32_t clippedSamples() const { return clippedSamples_; }
  uint32_t accRejects() const { return accRejects_; }
  float offsetDriftMm() const { return offsetDriftMm_; }
  float accTauS() const { return config_.accTauS; }

 private:
  float shiftedPrior() const {
    return prior_ > 0 ? fmaxf(prior_ + hoverShift_, config_.hoverMin) : 0;
  }

  bool inertialUsable(uint32_t nowMs) const {
    return useAcc_ && !accFault_ && haveSample_ && haveAcc_
        && nowMs - accLastMs_ <= config_.accFreshMs;
  }

  void observeTof(float heightMm, float gap) {
    if (!haveSample_) {
      haveSample_ = true;
      filteredMm_ = previousMm_ = heightMm;
      tofSpeedMmS_ = tofJumpMm_ = 0;
      tofContinuousMm_ = heightMm + tofOffsetMm_;
      return;
    }
    if (gap > 0.25f) {
      tofSpeedMmS_ = clampf((heightMm - previousMm_) / gap, -1500.0f, 1500.0f);
      tofJumpMm_ = 0;
      filteredMm_ = previousMm_ = heightMm;
      tofContinuousMm_ = heightMm + tofOffsetMm_;
      return;
    }
    float dt = gap < 0.01f ? 0.05f : gap;
    float reach = 0.75f * config_.surfaceStepMm;
    float predicted = previousMm_ + clampf(tofSpeedMmS_ * dt, -reach, reach);
    tofJumpMm_ = fabsf(heightMm - predicted);
    if (fabsf(heightMm - predicted) > config_.surfaceStepMm) {
      if (!inertial_) surfaceSteps_++;
      tofOffsetMm_ += predicted - heightMm;
    } else {
      float rate = clampf((heightMm - previousMm_) / dt, -kTofSpeedMaxMmS, kTofSpeedMaxMmS);
      tofSpeedMmS_ += config_.velocityAlpha * (rate - tofSpeedMmS_);
    }
    filteredMm_ += config_.heightAlpha * (heightMm - filteredMm_);
    tofContinuousMm_ += config_.heightAlpha * (heightMm + tofOffsetMm_ - tofContinuousMm_);
    previousMm_ = heightMm;
  }

  void correctInertial(float heightMm, float gap) {
    float dt = clampf(gap, 0.0f, 0.1f);
    float e = heightMm + offsetMm_ - zMm_;
    accJumpMm_ = fabsf(e);
    float c = clampf(e, -config_.accStepMm, config_.accStepMm);
    offsetDriftMm_ *= expf(-dt / config_.accFaultTauS);
    if (c != e) {
      offsetMm_ -= e - c;
      float beyond = e - clampf(e, -config_.accFaultClipMm, config_.accFaultClipMm);
      offsetDriftMm_ -= clampf(beyond, -config_.accFaultStepMm, config_.accFaultStepMm);
      surfaceSteps_++;
      clippedSamples_++;
      if (fabsf(offsetDriftMm_) > config_.accFaultMm) {
        accFault_ = true;
        accFaults_++;
        return;
      }
    }
    float tau = grounded_ ? config_.accGroundTauS : config_.accTauS;
    zMm_ += 3.0f / tau * c * dt;
    vMmS_ += 3.0f / (tau * tau) * c * dt;
    biasMmS2_ = clampf(biasMmS2_ - c * dt / (tau * tau * tau), -config_.accBiasMaxMmS2,
                       config_.accBiasMaxMmS2);
  }

  float speedControl(float targetMmS, float dt) {
    float error = targetMmS - verticalSpeedMmS();
    float p = config_.kp * error;
    float next = clampf(integral_ + config_.ki * error * dt, -config_.integralMax, config_.integralMax);
    bool deeper = (p + next <= -config_.belowHover && next < integral_)
                  || (p + next >= config_.aboveHover && next > integral_);
    if (!deeper) integral_ = next;
    float out = p + integral_;
    bool floored = inertial_ && out <= -config_.belowHover && error < -config_.hoverAdaptErrMmS
                   && targetMmS - tofSpeedMmS_ < -config_.hoverAdaptErrMmS;
    if (!floored) flooredMs_ = 0;
    else if (flooredMs_ < config_.hoverAdaptSettleMs) flooredMs_ += (uint32_t)(dt * 1000 + 0.5f);
    if (floored && flooredMs_ >= config_.hoverAdaptSettleMs) {
      const float g = 9806.65f;
      float lift = powf(fmaxf(1.0f + accLpMmS2_ / g, 0.3f), 1.0f / config_.thrustExp);
      float measured = thrustLp_ / lift;
      measured += config_.hoverAdaptMarginPct;
      if (measured < hover_) {
        float step = (hover_ - fmaxf(measured, config_.hoverMin))
                     * clampf(dt / config_.hoverAdaptTauS, 0.0f, 1.0f);
        hover_ -= step;
        hoverShift_ -= step;
        if (haveHover_) trackedHover_ -= step;
      }
    }
    return clampf(hover_ + out, hover_ - config_.belowHover, hover_ + config_.aboveHover);
  }

  bool stalled(uint32_t nowMs) {
    if (filteredMm_ < lowestMm_ - config_.stallProgressMm) {
      lowestMm_ = filteredMm_;
      progressMs_ = nowMs;
    }
    return filteredMm_ < config_.stallBelowMm && nowMs - progressMs_ > config_.stallMs;
  }

  AutoLandConfig config_;
  bool haveSample_ = false, active_ = false, haveHover_ = false, hold_ = false;
  float trackedHover_ = 0, prior_ = 0, hoverShift_ = 0;
  float filteredMm_ = 0, previousMm_ = 0;
  float hover_ = 0, integral_ = 0, thrust_ = 0, rampFrom_ = 0, lowestMm_ = 0;
  float holdTargetMm_ = 0, followMm_ = 0;
  bool holdSpeedOn_ = false;
  float holdSpeedMmS_ = 0;
  bool following_ = false;
  bool lowerPending_ = false;
  uint32_t lowerSinceMs_ = 0;
  uint32_t lastSampleMs_ = 0, startMs_ = 0, rampStartMs_ = 0, progressMs_ = 0;
  uint32_t surfaceSteps_ = 0;
  uint32_t surfaceChangeMs_ = 0;
  bool surfaceChanged_ = false;
  static constexpr float kTofSpeedMaxMmS = 3000.0f;
  float tofSpeedMmS_ = 0, tofContinuousMm_ = 0, tofOffsetMm_ = 0;
  float tofJumpMm_ = 0;
  bool useAcc_ = true, haveAcc_ = false, inertial_ = false, accFault_ = false, grounded_ = true;
  float zMm_ = 0, vMmS_ = 0, offsetMm_ = 0, biasMmS2_ = 0;
  float accJumpMm_ = 0;
  float accLpMmS2_ = 0;
  float thrustMotor_ = 0;
  float thrustLp_ = 0;
  uint32_t flooredMs_ = 0;
  float offsetDriftMm_ = 0;
  uint8_t reacquire_ = 0;
  uint32_t accLastMs_ = 0, clippedSamples_ = 0, accRejects_ = 0, accFaults_ = 0;
};

inline float hoverFromBattery(float vbat, float dutyPct, float refPct, float refV, float sagV) {
  float k = sagV / 100, ocv = vbat + k * dutyPct, c = refPct * refV;
  float disc = ocv * ocv - 4 * k * c;
  if (disc <= 0) return 100;
  return (ocv - sqrtf(disc)) / (2 * k);
}

inline float holdTargetForStick(float thrustPct, float minMm, float maxMm, float loPct = 50.0f,
                                float hiPct = 80.0f) {
  return minMm + (maxMm - minMm) * clampf((thrustPct - loPct) / (hiPct - loPct), 0.0f, 1.0f);
}

}
