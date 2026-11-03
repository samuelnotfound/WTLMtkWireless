// SPDX-License-Identifier: BSD-3-Clause-Clear
#ifndef MT7902_DATA_CODEC_H
#define MT7902_DATA_CODEC_H
#include "mt7902_rx_codec.h"

enum Mt7902DataStatus {
    MT7902_DATA_OK = 0,
    MT7902_DATA_NULL,
    MT7902_DATA_AMSDU,
    MT7902_DATA_SHORT,
    MT7902_DATA_FORMAT,
    MT7902_DATA_CIPHER,
    MT7902_DATA_FRAGMENT
};
struct Mt7902DataView {
    uint32_t body_offset;
    uint32_t body_length;
    uint16_t ethertype;
    uint16_t sequence;
    uint8_t tid;
    uint8_t qos;
    uint8_t qos_control;
    uint8_t protected_frame;
    uint8_t retry;
};
// Raw From-DS frame after RX descriptor removal. CCMP IV/MIC are stripped by
// hardware when SEC_MODE=4 and !CM/CLM, with ICV/MIC errors rejected upstream.
// This is mt7921_mac_fill_rx's decrypted test; SEC_DONE is diagnostic metadata,
// not a validity requirement in the upstream MT7902 path. Failed ciphertext
// is never searched for LLC or exposed as plaintext.
Mt7902DataStatus mt7902_parse_data(const uint8_t *frame, uint32_t length,
                                   const Mt7902RxView &rx, Mt7902DataView *out);

enum Mt7902AmsduStatus {
    MT7902_AMSDU_OK = 0,
    MT7902_AMSDU_MALFORMED,
    MT7902_AMSDU_LIMIT,
    MT7902_AMSDU_ADDRESS
};
// Validate the complete normalized body before delivering any subframe. Each
// subframe must target this station or a group address, with a unicast source,
// bounded length, valid RFC1042/802.1H SNAP and an Ethernet-II protocol field.
Mt7902AmsduStatus mt7902_validate_amsdu(const uint8_t *body, uint32_t length,
                                       const uint8_t local_address[6], uint32_t max_payload,
                                       uint32_t *subframes);
// Connac2 payload format: 0 = whole MPDU, 1 = last deaggregated subframe,
// 2 = middle, 3 = first. Only whole/last completes a reorder sequence.
inline bool mt7902_amsdu_complete(uint8_t payload_format)
{
    return payload_format == 0 || payload_format == 1;
}
#endif
