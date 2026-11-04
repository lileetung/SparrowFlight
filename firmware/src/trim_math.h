// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lileetung

#pragma once

#include <math.h>

namespace sparrow {

static inline bool computeMotorTrim(const float loaded[4], const float noload[4], float lo,
                                    float hi, float minLoad, float out[4]) {
  float load[4], sum = 0;
  for (int i = 0; i < 4; i++) {
    load[i] = loaded[i] - noload[i];
    if (!(load[i] >= minLoad)) return false;
    sum += load[i];
  }
  float mean = sum / 4, k[4], kSum = 0;
  for (int i = 0; i < 4; i++) {
    k[i] = mean / load[i];
    kSum += k[i];
  }
  for (int i = 0; i < 4; i++) {
    k[i] *= 4 / kSum;
    if (k[i] < lo || k[i] > hi) return false;
  }
  for (int i = 0; i < 4; i++) out[i] = k[i];
  return true;
}

static inline void scaleTrim(const float raw[4], float gain, float out[4]) {
  for (int i = 0; i < 4; i++) out[i] = 1 + gain * (raw[i] - 1);
}

}
