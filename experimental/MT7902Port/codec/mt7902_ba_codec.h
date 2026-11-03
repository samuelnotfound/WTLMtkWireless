// SPDX-License-Identifier: BSD-3-Clause-Clear
#ifndef MT7902_BA_CODEC_H
#define MT7902_BA_CODEC_H
#include <stdint.h>

bool mt7902_encode_wtbl_ba(uint8_t out[48], const uint8_t peer[6], uint8_t tid,
                            bool recipient, bool enable, uint16_t ssn, uint16_t window);
bool mt7902_encode_sta_ba(uint8_t out[20], uint8_t tid, bool recipient,
                           bool enable, uint16_t ssn, uint16_t window, bool amsdu);
struct Mt7902TxFreeCursor {
    const uint8_t *packet;
    uint32_t length, offset, count, total;
};
struct Mt7902TxFreeRecord { uint16_t token, attempts; uint8_t status; };
bool mt7902_tx_free_begin(const uint8_t *packet, uint32_t length, Mt7902TxFreeCursor *cursor);
bool mt7902_tx_free_next(Mt7902TxFreeCursor *cursor, Mt7902TxFreeRecord *record);
#endif
