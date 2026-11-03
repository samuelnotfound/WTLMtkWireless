// SPDX-License-Identifier: BSD-3-Clause-Clear
// Connac2 worker backend for the MtkWlan core. No firmware calls on the Mtk work loop.
#pragma once
#include "MtkCoreApi.h"
#include <IOKit/IOLocks.h>

class MtkWlan;
class itlmt7902;
class IOService;

struct Mtk7902Backend {
    struct Packet { uint32_t len; uint8_t data[1514]; };
    enum { QueueSize = 64 };
    MtkWlan *core;
    itlmt7902 *radio;
    IOLock *lock;
    Packet rx[QueueSize], tx[QueueSize];
    uint32_t rxHead, rxTail, txHead, txTail, rxDrops;
    mtk_status status;
    mtk_scan_result results[MTK_SCAN_MAX];
    uint32_t count, generation, lastPublish, pending, scanStartGen;
    mtk_join_params joinRequest;
    bool ready, macReady, scanPending, joinPending, stopping, radioStarted;
    uint8_t mac[6];

    static Mtk7902Backend *create(MtkWlan *core);
    bool start(IOService *provider);
    void stop();
    void destroy();
    // The following three methods run only on the radio's worker.
    void receive(const uint8_t *frame, uint32_t len);
    void publish(bool networks = false);
    void workerPoll();
    // The following methods run only on the Mtk work loop gate.
    void tick();
    int scan();
    int join(const mtk_join_params *request);
    int disconnect();
    int transmit(const uint8_t *frame, uint32_t len);
    void snapshot(mtk_status *out);
    uint32_t getResults(mtk_scan_result *out, uint32_t max);
};
