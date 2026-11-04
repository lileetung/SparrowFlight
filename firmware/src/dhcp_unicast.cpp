// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 lileetung

#include "dhcp_unicast.h"

#include <esp_netif.h>
#include <esp_netif_net_stack.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <stdint.h>
#include <string.h>

// ESP-IDF's DHCP server sends every OFFER, ACK and NAK to the WiFi broadcast address, also to a
// client that did not ask for broadcast replies (RFC 2131 section 4.1: then send to its chaddr).
// A broadcast frame is not acknowledged or retried, and it waits for the next DTIM beacon, so a
// phone that dozes or scans another channel just after joining misses it. On 3 Nov 2026 an
// iPhone missed a NAK (it took the repeat 2.3 s later), and earlier the same morning got its ACK
// only after the fourth request, about 10 s after joining, when iOS gave the network up ("IP
// timed out"). Sent to the client's own address, a reply is acknowledged, retried and held for a
// dozing station like any unicast frame. The IP destination stays 255.255.255.255, which a
// client takes before it has an address; access points offer the same conversion for this.
namespace sparrow {
namespace {

netif_linkoutput_fn originalLinkOutput = nullptr;

const uint8_t BROADCAST[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
const size_t ETH_HEADER = 14;
const size_t UDP_HEADER = 8;
const size_t CHADDR_OFFSET = 28;  // in the DHCP message

err_t unicastDhcpReplies(struct netif *nif, struct pbuf *p) {
  // The DHCP server's pbuf holds the whole frame: Ethernet, IPv4, UDP and the message.
  uint8_t *frame = (uint8_t *)p->payload;
  const uint8_t *end = frame + p->len;
  if (p->len > ETH_HEADER + 20 && memcmp(frame, BROADCAST, 6) == 0 && frame[12] == 0x08
      && frame[13] == 0x00) {
    const uint8_t *ip = frame + ETH_HEADER;
    const uint8_t *udp = ip + (ip[0] & 0x0F) * 4;
    const uint8_t *dhcp = udp + UDP_HEADER;
    bool reply = ip[9] == 17 && dhcp + CHADDR_OFFSET + 6 <= end
                 && udp[0] == 0 && udp[1] == 67 && udp[2] == 0 && udp[3] == 68  // 67 -> 68
                 && dhcp[0] == 2 && dhcp[1] == 1 && dhcp[2] == 6;  // BOOTREPLY, Ethernet MAC
    if (reply) memcpy(frame, dhcp + CHADDR_OFFSET, 6);
  }
  return originalLinkOutput(nif, p);
}

}  // namespace

void dhcpUnicastBegin() {
  esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  struct netif *nif = ap ? (struct netif *)esp_netif_get_netif_impl(ap) : nullptr;
  if (!nif || originalLinkOutput) return;
  originalLinkOutput = nif->linkoutput;
  nif->linkoutput = unicastDhcpReplies;
}

}  // namespace sparrow
