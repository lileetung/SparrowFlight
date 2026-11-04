// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lileetung

#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace sparrow {

static const uint16_t CAMERA_PORT = 81;
static const uint32_t CAMERA_IDLE_MS = 5000;
static const uint32_t CAMERA_UDP_TIMEOUT_MS = 2000;
static const size_t CAMERA_UDP_CHUNK = 1400;

static const int CAMERA_RES_MIN = 1, CAMERA_RES_MAX = 3;
static const int CAMERA_MAX_FPS = 15;
static const int CAMERA_JPEG_QUALITY = 12;

void cameraBegin(int res);
void cameraConfigure(int res);
bool cameraSelfTest(uint32_t seconds);
const char *cameraHardware();
void cameraStatus(char *camera, char *video, size_t n);

}
