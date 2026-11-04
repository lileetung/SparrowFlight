// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lileetung

#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace sparrow {

void netSendBegin(uint16_t port);
// A reply: queued and sent in order.
bool netSend(uint32_t ip, const char *text);
// A telemetry line: only the newest per device waits, and replaces an older one not sent yet.
void netSendLatest(uint32_t ip, const char *text);
void netSendStatus(char *out, size_t n);

}
