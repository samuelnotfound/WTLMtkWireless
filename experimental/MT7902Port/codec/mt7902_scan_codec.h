// SPDX-License-Identifier: BSD-3-Clause-Clear
// Bounded Connac2 capability and passive-scan wire formats (mt76 c539593ef5649).
#ifndef MT7902_SCAN_CODEC_H
#define MT7902_SCAN_CODEC_H
#include <stddef.h>
#include <stdint.h>

struct Mt7902NicCaps {
    uint8_t mac[6];
    uint8_t nss, chain_mask, bands; // bands: bit0=2.4 GHz, bit1=5 GHz.
    uint8_t ht, vht, he, max_bw;
};
enum { MT7902_PASSIVE_SCAN_BYTES = 1186, MT7902_SCAN_DOMAIN_MAX = 188 };
// Payload begins after the 36-byte MCU RX descriptor. MAC and PHY TLVs are
// required; malformed/duplicate TLVs and invalid MAC/NSS values are rejected.
bool mt7902_decode_nic_caps(const uint8_t *payload, size_t length, Mt7902NicCaps *out);
// Version-1 CE scan: no probe requests, no SSIDs, no random MAC, no 6 GHz/DFS.
size_t mt7902_encode_passive_scan(uint8_t *out, size_t capacity, uint8_t seq, const Mt7902NicCaps &caps);
// World-domain list, 20 MHz, NO_IR on every listed channel. Domain and scan
// share the same band-filtered channel list, avoiding MT7922 antenna assumptions.
size_t mt7902_encode_scan_domain(uint8_t *out, size_t capacity, const Mt7902NicCaps &caps);
#endif
