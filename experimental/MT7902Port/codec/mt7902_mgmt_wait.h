// SPDX-License-Identifier: BSD-3-Clause-Clear
#pragma once
#include <stdint.h>

// Bounded queue-to-status window followed by an ACK-to-response window.
// Separate status IDs prevent a delayed older TXS from moving this deadline.
struct Mt7902MgmtWait {
    uint32_t submitted=0, acknowledged=0;
    uint8_t pid=0;
    bool active=false, ack=false, failed=false;
    void start(uint32_t now,uint8_t id) {
        submitted=now; acknowledged=0; pid=id;
        active=true; ack=false; failed=false;
    }
    bool expired(uint32_t now) const {
        return !active || failed || (ack?(uint32_t)(now-acknowledged)>=150u:
                                               (uint32_t)(now-submitted)>=500u);
    }
    void status(uint32_t now,uint8_t id,uint8_t errors) {
        if (!active || id!=pid || ack || expired(now)) return;
        if (errors) failed=true;
        else { ack=true; acknowledged=now; }
    }
    void stop() { active=false; }
};
