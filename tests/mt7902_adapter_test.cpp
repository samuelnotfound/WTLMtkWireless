// SPDX-License-Identifier: BSD-3-Clause-Clear
#include "../src/Mtk7902Protocol.h"
#include "../src/Mtk7902Queue.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
int main() {
    static Mtk7902Queue q = {};
    uint8_t frame[1514] = {};
    Mtk7902Queue::Packet packet;
    uint32_t ticket;
    assert(!q.push(nullptr, 14)); assert(!q.push(frame, 13)); assert(!q.push(frame, 1515));
    for (unsigned i = 0; i < q.Capacity; ++i) { frame[0] = (uint8_t)i; assert(q.push(frame, sizeof(frame))); }
    assert(!q.push(frame, 14));
    for (unsigned i = 0; i < q.Capacity; ++i) {
        assert(q.peek(packet, ticket)); assert(packet.data[0] == i); assert(packet.len == sizeof(frame));
        assert(q.complete(ticket)); assert(!q.complete(ticket));
    }
    assert(q.empty());
    // TX stalls: peek does not release a frame. A disconnect clears it; its completion must not eat the next session.
    assert(q.push(frame, 14)); assert(q.peek(packet, ticket));
    uint32_t oldTicket = ticket; q.clear(); assert(q.push(frame, 60));
    assert(!q.complete(oldTicket)); assert(q.peek(packet, ticket)); assert(packet.len == 60); assert(q.complete(ticket));
    q.head = q.tail = UINT32_MAX - 1;
    for (unsigned i = 0; i < 4; ++i) assert(q.push(frame, 14));
    for (unsigned i = 0; i < 4; ++i) { assert(q.peek(packet, ticket)); assert(q.complete(ticket)); }
    assert(q.empty());
    static_assert(sizeof(mtk_join_params) == 76, "menu app join ABI");
    static_assert(offsetof(mtk_join_params, ssid) == 12, "SSID ABI");
    static_assert(offsetof(mtk_join_params, pmk) == 44, "PMK ABI");
    mtk_join_params p = {};
    p.version = MTK_USER_API_VERSION; p.key_type = MTK_KEY_PMK; p.ssid_len = 3;
    memcpy(p.ssid, "AP1", 3);
    assert(mtk7902_valid_join(&p));
    p.bssid[0] = 2; assert(mtk7902_valid_join(&p)); p.bssid[0] = 1; assert(!mtk7902_valid_join(&p)); p.bssid[0] = 0;
    p.key_type = MTK_KEY_NONE; assert(!mtk7902_valid_join(&p)); p.key_type = MTK_KEY_PMK;
    p.ssid[1] = 0; assert(!mtk7902_valid_join(&p)); p.ssid[1] = 'P';
    p.ssid_len = 33; assert(!mtk7902_valid_join(&p));
    assert(!mtk7902_valid_join(nullptr));
    assert(mtk7902_join_result("auth-timeout-fail tgt=1") == MTK_JOIN_AUTH_TIMEOUT);
    assert(mtk7902_join_result("auth-fail tgt=1") == MTK_JOIN_AUTH_REJECT);
    assert(mtk7902_join_result("assoc-timeout-fail") == MTK_JOIN_ASSOC_TIMEOUT);
    assert(mtk7902_join_result("assoc-fail") == MTK_JOIN_ASSOC_REJECT);
    assert(mtk7902_join_result("hs-fail") == MTK_JOIN_HANDSHAKE_TIMEOUT);
    puts("MT7902 Mtk API: queue bounds/wrap/flush ownership, ABI and request/error translation passed");
}
