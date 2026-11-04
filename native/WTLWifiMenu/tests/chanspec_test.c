// <wtl> project: mtkwlannative | file: tests/chanspec_test.c | author: watchthelight </wtl>
// SPDX-License-Identifier: ISC
// mtkChanspec against the decoder in IO80211Family 25G332 (ChanSpecGetPrimaryChannel: primary = center - width/10 + 2
// + 4 * sideband; ChanSpecConvToApple80211Channel: width 3 = 40, 4 = 80, sideband 0 on 40 MHz = extension above).
#include "../src/MtkChanspec.h"
#include <stdio.h>

static int fails;
static unsigned primary(uint16_t cs) {
    static const unsigned w[] = { 5, 10, 20, 40, 80, 160, 160 };
    unsigned bw = (cs >> 11) & 7, c = cs & 0xff;
    if (w[bw] < 20) return c;
    return c - w[bw] / 10 + 2 + 4 * ((cs >> 8) & 7);
}
static void check(const char *name, uint8_t ch, const uint8_t *ie, uint32_t len, unsigned bw, unsigned center) {
    uint16_t cs = mtkChanspec(ch, ie, len);
    unsigned gotBw = (cs >> 11) & 7, band = cs >> 14;
    int ok = gotBw == bw && (cs & 0xff) == center && primary(cs) == ch && band == (ch > 14 ? 3u : 0u);
    printf("%s %-28s ch %3u -> cs 0x%04x bw %u center %u primary %u\n", ok ? "ok  " : "FAIL", name, ch, cs, gotBw,
           cs & 0xff, primary(cs));
    if (!ok) fails++;
}

int main(void) {
    const uint8_t none[] = { 0 };
    const uint8_t ht40a[] = { 61, 22, 36, 0x05, [24] = 0 };
    const uint8_t ht40b[] = { 61, 22, 40, 0x07, [24] = 0 };
    const uint8_t ht40no[] = { 61, 22, 36, 0x01, [24] = 0 };            // offset set, STA width bit clear
    const uint8_t vht80[] = { 61, 22, 0, 0x05, [24] = 192, 5, 1, 42, 0, 0, 0 };
    const uint8_t vht160[] = { 61, 22, 0, 0x05, [24] = 192, 5, 1, 106, 114, 0, 0 };
    const uint8_t bad[] = { 61, 40, 36, 0x05 };                          // element longer than the buffer
    const uint8_t g40[] = { 61, 22, 6, 0x07, [24] = 0 };
    check("20 MHz, no elements", 36, none, 0, 2, 36);
    check("2.4 GHz 20 MHz", 6, none, 0, 2, 6);
    check("HT40 above", 36, ht40a, sizeof ht40a, 3, 38);
    check("HT40 below", 40, ht40b, sizeof ht40b, 3, 38);
    check("HT offset without width bit", 36, ht40no, sizeof ht40no, 2, 36);
    for (uint8_t ch = 36; ch <= 48; ch += 4) check("VHT80 36-48", ch, vht80, sizeof vht80, 4, 42);
    for (uint8_t ch = 149; ch <= 161; ch += 4) check("VHT80 149-161", ch, vht80, sizeof vht80, 4, 155);
    check("VHT160 reported as 80", 100, vht160, sizeof vht160, 4, 106);
    check("VHT80 on 116", 116, vht80, sizeof vht80, 4, 122);
    check("VHT80 on 165 (no block)", 165, vht80, sizeof vht80, 2, 165);
    check("HT40 above on 40 (not a pair)", 40, ht40a, sizeof ht40a, 2, 40);
    const uint8_t g40a[] = { 61, 22, 11, 0x05, [24] = 0 };
    check("2.4 GHz HT40 above on 11", 11, g40a, sizeof g40a, 2, 11);
    check("truncated element", 36, bad, sizeof bad, 2, 36);
    check("2.4 GHz HT40 below", 6, g40, sizeof g40, 3, 4);
    printf(fails ? "chanspec: %d FAILED\n" : "chanspec: all ok\n", fails);
    return fails != 0;
}
