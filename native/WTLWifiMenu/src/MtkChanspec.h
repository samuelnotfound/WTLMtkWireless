// <wtl> project: mtkwlannative | file: src/MtkChanspec.h | author: watchthelight </wtl>
// SPDX-License-Identifier: ISC
// The chanspec of a scanned BSS (Broadcom d11ac layout, as the Family reads it from a scan-result event) from its
// HT and VHT Operation elements. Same width rules as the core's join (MtkJoin.cpp): 160 MHz is reported as 80.
#ifndef MTK_CHANSPEC_H
#define MTK_CHANSPEC_H
#include <stdint.h>

#define MTK_CS_BW_20  0x1000u
#define MTK_CS_BW_40  0x1800u
#define MTK_CS_BW_80  0x2000u
#define MTK_CS_BAND_5 0xc000u

static inline uint16_t mtkChanspec(uint8_t ch, const uint8_t *ie, uint32_t len) {
    const uint16_t band = ch > 14 ? MTK_CS_BAND_5 : 0;
    int ht40 = 0, above = 0, vht80 = 0;
    for (const uint8_t *p = ie, *end = ie + len; p + 2 <= end && p + 2 + p[1] <= end; p += 2 + p[1]) {
        if (p[0] == 61 && p[1] >= 2) {                  // HT Operation: STA width bit 2, secondary offset bits 0-1
            uint8_t op = p[3];
            if ((op & 4) && ((op & 3) == 1 || (op & 3) == 3)) { ht40 = 1; above = (op & 3) == 1; }
        } else if (p[0] == 192 && p[1] >= 3) {          // VHT Operation: width 1 = 80/160/80+80
            vht80 = p[2] >= 1;
        }
    }
    if (ch > 14 && vht80 && ch >= 36 && ch <= 165) {
        uint8_t center = ch >= 149 ? 155 : (uint8_t)(36 + ((ch - 36) / 16) * 16 + 6);
        if (ch + 6 >= center && ch <= center + 6) {
            uint16_t sb = (uint16_t)(((ch - (center - 6)) / 4) << 8);
            return (uint16_t)(center | sb | MTK_CS_BW_80 | band);
        }
    }
    if (ht40 && ch > 14 && (ch > 161 || ((ch - (ch >= 149 ? 149 : 36)) / 4) % 2 != (above ? 0 : 1))) ht40 = 0;
    if (ht40 && ch <= 14 && (above ? ch + 4 > 13 : ch < 5)) ht40 = 0;   // the secondary must be a 2.4 GHz channel
    if (ht40) {
        uint8_t center = above ? (uint8_t)(ch + 2) : (uint8_t)(ch - 2);
        return (uint16_t)(center | (above ? 0 : 0x100u) | MTK_CS_BW_40 | band);
    }
    return (uint16_t)(ch | MTK_CS_BW_20 | band);
}
#endif
