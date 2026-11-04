// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lileetung

#pragma once

#include <math.h>
#include <stdint.h>

namespace sparrow {

static inline float clampf(float value, float lo, float hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

static inline float expo(float value, float fullScale, float amount) {
  fullScale = fabsf(fullScale);
  if (fullScale < 0.001f) return 0.0f;
  amount = clampf(amount, 0.0f, 1.0f);
  float x = clampf(value / fullScale, -1.0f, 1.0f);
  return fullScale * x * ((1.0f - amount) + amount * x * x);
}

enum SaturationBit : uint8_t {
  SAT_ROLL_POS  = 1u << 0,
  SAT_PITCH_POS = 1u << 1,
  SAT_YAW_POS   = 1u << 2,
  SAT_ROLL_NEG  = 1u << 3,
  SAT_PITCH_NEG = 1u << 4,
  SAT_YAW_NEG   = 1u << 5,
};

struct MixerResult {
  float raw[4];
  float motor[4];
  float rpScale;
  float yawScale;
  float collective;
  uint8_t satMask;
};

static inline void corrections(float roll, float pitch, float yaw, float c[4]) {
  c[0] =  roll - pitch + yaw;
  c[1] =  roll + pitch - yaw;
  c[2] = -roll + pitch + yaw;
  c[3] = -roll - pitch - yaw;
}

static inline void minMax4(const float v[4], float &lo, float &hi) {
  lo = hi = v[0];
  for (int i = 1; i < 4; i++) {
    if (v[i] < lo) lo = v[i];
    if (v[i] > hi) hi = v[i];
  }
}

static inline bool correctionsFit(float roll, float pitch, float yaw, float base) {
  float c[4], lo, hi;
  corrections(roll, pitch, yaw, c);
  minMax4(c, lo, hi);
  return lo >= -base - 0.0001f && hi - lo <= 100.0001f;
}

static inline float largestFeasibleScale(float roll, float pitch, float yaw,
                                         float base, bool scaleRp) {
  float low = 0.0f, high = 1.0f;
  for (int i = 0; i < 16; i++) {
    float mid = (low + high) * 0.5f;
    bool fits = scaleRp ? correctionsFit(roll * mid, pitch * mid, yaw, base)
                        : correctionsFit(roll, pitch, yaw * mid, base);
    if (fits) low = mid;
    else high = mid;
  }
  return low;
}

static inline MixerResult mixX(float base, float roll, float pitch, float yaw) {
  MixerResult result = {};
  base = clampf(base, 0.0f, 100.0f);

  float requested[4];
  corrections(roll, pitch, yaw, requested);
  for (int i = 0; i < 4; i++) result.raw[i] = base + requested[i];

  result.rpScale = correctionsFit(roll, pitch, 0.0f, base)
                     ? 1.0f : largestFeasibleScale(roll, pitch, 0.0f, base, true);
  float actualRoll = roll * result.rpScale;
  float actualPitch = pitch * result.rpScale;
  result.yawScale = correctionsFit(actualRoll, actualPitch, yaw, base)
                      ? 1.0f
                      : largestFeasibleScale(actualRoll, actualPitch, yaw, base, false);
  float actualYaw = yaw * result.yawScale;

  float c[4], lo, hi;
  corrections(actualRoll, actualPitch, actualYaw, c);
  minMax4(c, lo, hi);
  result.collective = clampf(base, -lo, 100.0f - hi);
  for (int i = 0; i < 4; i++) result.motor[i] = clampf(result.collective + c[i], 0.0f, 100.0f);

  auto markScaleLimit = [&](int axis, float command, float scale) {
    if (scale >= 0.999f || fabsf(command) < 0.001f) return;
    result.satMask |= command > 0 ? (1u << axis) : (1u << (axis + 3));
  };
  markScaleLimit(0, roll, result.rpScale);
  markScaleLimit(1, pitch, result.rpScale);
  markScaleLimit(2, yaw, result.yawScale);

  return result;
}

static inline bool saturationBlocks(uint8_t mask, int axis, float error) {
  if (error > 0.0f) return mask & (1u << axis);
  if (error < 0.0f) return mask & (1u << (axis + 3));
  return false;
}

}
