// SPDX-License-Identifier: BSD-3-Clause-Clear
// Derived from mt76_connac_mac.c:mt76_connac2_mac_write_txwi*, MediaTek Inc.
#include "mt7902_tx_codec.h"
#include <string.h>

namespace {
void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
}

bool mt7902_encode_txwi(uint8_t out[32], const uint8_t *f, uint32_t len,
                         const Mt7902TxOptions &o)
{
    if (!out || !f || len < 24 || len > 65503 || o.wlan_index > 1023 ||
        o.queue > 127 || o.tid > 7 || o.fixed_rate > 0x3fff || o.spe_index > 31 ||
        o.sequence < -1 || o.sequence > 4095 || o.status_pid<3 || o.status_pid>126 || (f[0] & 3)) return false;
    const uint32_t type = (f[0] >> 2) & 3, subtype = f[0] >> 4;
    if (type != 0 && type != 2) return false;
    const bool qos = type == 2 && (f[0] & 0x80);
    const uint32_t hdr = 24 + (qos ? 2 : 0) + (qos && (f[1] & 0x80) ? 4 : 0);
    if (len < hdr) return false;
    uint32_t tid = o.tid, add_ba = 0;
    if (type == 0 && subtype == 13 && len >= 33 && f[24] == 3 && f[25] == 0) {
        tid = ((uint32_t)f[27] >> 2) & 15;
        if (tid > 7) return false;
        add_ba = 1u << 14;
    }
    const bool fixed = !o.rate_adapt;
    const bool multicast = f[4] & 1;
    memset(out, 0, 32);
    put32(out, (len + 32) | ((uint32_t)o.queue << 25)); // CT packet format is 0
    put32(out + 4, (1u << 31) | o.wlan_index | (2u << 16) |
                    ((hdr / 2) << 11) | (tid << 20));
    put32(out + 8, (type << 4) | subtype | (multicast ? (1u << 10) : 0u) |
                    (fixed ? ((1u << 31) | (1u << 13)) : 0u));
    put32(out + 12, (15u << 11) | ((!o.aggregate || fixed) ? (1u << 28) : 0u) |
                     (o.protect ? 2u : 0u) | (multicast ? 1u : 0u) |
                     (o.sequence >= 0 ? ((1u << 31) | ((uint32_t)o.sequence << 16)) : 0u));
    put32(out + 20, add_ba | (fixed ? ((uint32_t)o.status_pid | (1u << 10)) : 0u));
    put32(out + 24, fixed ? ((1u << 2) | ((uint32_t)o.fixed_rate << 16)) : 0u);
    put32(out + 28, (type << 20) | (subtype << 16) |
                     (fixed ? ((uint32_t)o.spe_index << 11) : 0u));
    return true;
}

bool mt7902_parse_txs(const uint8_t *p, uint32_t n, uint32_t record,
                       Mt7902TxStatus *out)
{
    if (!p || !out || n<8) return false;
    auto get32=[](const uint8_t *b)->uint32_t {
        return (uint32_t)b[0] | (uint32_t)b[1]<<8 | (uint32_t)b[2]<<16 | (uint32_t)b[3]<<24;
    };
    const uint32_t header=get32(p), bytes=header&0xffff;
    if ((header>>27)!=0 || bytes<40 || bytes>n || record>=(bytes-8)/32) return false;
    const uint8_t *s=p+8+record*32;
    Mt7902TxStatus status={};
    for (unsigned i=0;i<4;i++) status.word[i]=get32(s+4*i);
    if (((status.word[0]>>23)&3)>1) return false; // MPDU formats only.
    status.wcid=(uint16_t)((status.word[2]>>16)&0x3ff);
    status.pid=(uint8_t)(status.word[3]>>24);
    status.ack_errors=(uint8_t)((status.word[0]>>16)&7);
    *out=status;
    return true;
}
