// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lileetung

#pragma once

// DHCP replies (OFFER, ACK, NAK) go to the client's own WiFi address instead of the broadcast
// one; see dhcp_unicast.cpp for why.
namespace sparrow {

// After the access point is up.
void dhcpUnicastBegin();

}  // namespace sparrow
