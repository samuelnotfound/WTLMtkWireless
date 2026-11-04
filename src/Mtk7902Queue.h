// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <stdint.h>
#include <string.h>

// Caller holds the bridge lock. Monotonic tickets stop a delayed TX completion consuming a frame
// queued after disconnect flushed the old session. No dynamic allocation or hardware ownership here.
struct Mtk7902Queue {
    enum { Capacity = 64, FrameMax = 1514 };
    struct Packet { uint32_t len; uint8_t data[FrameMax]; };
    Packet packets[Capacity];
    uint32_t head, tail;
    bool empty() const { return head == tail; }
    void clear() { tail = head; }
    bool push(const uint8_t *frame, uint32_t len) {
        if (!frame || len < 14 || len > FrameMax || head - tail >= Capacity) return false;
        Packet &p = packets[head++ % Capacity];
        p.len = len; memcpy(p.data, frame, len);
        return true;
    }
    bool peek(Packet &out, uint32_t &ticket) const {
        if (empty()) return false;
        ticket = tail; out = packets[tail % Capacity];
        return true;
    }
    bool complete(uint32_t ticket) {
        if (empty() || tail != ticket) return false;
        ++tail;
        return true;
    }
};
