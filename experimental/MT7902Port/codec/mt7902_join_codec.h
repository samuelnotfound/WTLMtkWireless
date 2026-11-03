// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <stddef.h>
#include <stdint.h>
// First connection checkpoint: legacy 20 MHz, non-DFS, WPA2-PSK/CCMP only.
bool mt7902_join_channel(uint8_t channel);
bool mt7902_join_rsn(const uint8_t *ie, size_t length);
size_t mt7902_encode_join_domain(uint8_t *out, size_t capacity, uint8_t channel);
uint8_t mt7902_fixed_spe(uint8_t chain_mask);
// One stream, HT20, long GI; no LDPC/STBC/40 MHz or two-stream MCS claims.
size_t mt7902_encode_ht20(uint8_t *out, size_t capacity, uint8_t mcs_mask);
uint32_t mt7902_ht20_rate(uint8_t mcs_mask);
// mt76_connac_mcu_set_deep_sleep(false): CE CHIP_CONFIG, 328-byte payload.
size_t mt7902_encode_keep_awake(uint8_t *out, size_t capacity);
// UNI DEV_INFO_UPDATE: activate OMAC0/band0 with the firmware-reported MAC.
size_t mt7902_encode_device_info(uint8_t *out, size_t capacity, const uint8_t mac[6]);
// Legacy 20 MHz home-channel switch, one TX/RX stream, normal switch reason.
size_t mt7902_encode_channel_switch(uint8_t *out, size_t capacity, uint8_t channel);
bool mt7902_decode_register_reply(const uint8_t *event, size_t length,
                                  uint32_t address, uint32_t *value);
// Connac2 disconnect: BASIC(disconnected) + empty RESET_AND_SET WTBL.
size_t mt7902_encode_sta_remove(uint8_t *out, size_t capacity, uint8_t bss,
                                uint8_t wcid, const uint8_t peer[6]);
