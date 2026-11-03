// SPDX-License-Identifier: BSD-3-Clause-Clear
#ifndef MT7902_TX_CODEC_H
#define MT7902_TX_CODEC_H
#include <stdint.h>

struct Mt7902TxOptions {
    uint16_t wlan_index;
    uint16_t fixed_rate;          // Connac2 mode/NSS/rate encoding, not a table index
    int16_t sequence;             // -1: hardware assigns the sequence number
    uint8_t queue;
    uint8_t tid;
    uint8_t spe_index;
    uint8_t rate_adapt;
    uint8_t protect;
    uint8_t aggregate;
    uint8_t status_pid = 3;
};

// Encodes the 32-byte Connac2 MMIO TXWI for an unmodified 802.11 frame.
// Payload DMA pointers and tokens use the shared mt76_connac_hw_txp separately.
bool mt7902_encode_txwi(uint8_t out[32], const uint8_t *frame, uint32_t length,
                         const Mt7902TxOptions &options);

struct Mt7902TxStatus {
    uint32_t word[4];
    uint16_t wcid;
    uint8_t pid;
    uint8_t ack_errors; // ACK, RTS, queue timeout bits, in that order.
};
// Connac2 TXS packet: 8-byte header followed by 32-byte status records.
bool mt7902_parse_txs(const uint8_t *packet, uint32_t length, uint32_t record,
                       Mt7902TxStatus *out);
#endif
