// SPDX-License-Identifier: BSD-3-Clause-Clear
// Connac2 worker backend for the MtkWlan core. No firmware calls on the Mtk work loop.
#pragma once
#include "MtkCoreApi.h"
#include "Mtk7902Queue.h"
#include <IOKit/IOLocks.h>

class MtkWlan;
class itlmt7902;
class IOService;

struct Mtk7902Backend {
    using Packet = Mtk7902Queue::Packet;
    enum { QueueSize = Mtk7902Queue::Capacity };
    MtkWlan *core;
    itlmt7902 *radio;
    IOLock *lock;
    Mtk7902Queue rx, tx;
    uint32_t rxDrops;
    mtk_status status;
    mtk_scan_result results[MTK_SCAN_MAX];
    mtk_bss_info bss[MTK_SCAN_MAX];
    mtk_link_info link;
    uint32_t count, generation, lastPublish, pending, scanStartGen, lastScanMs, requestSerial, bssSnapshotMs;
    mtk_join_params joinRequest;
    bool ready, macReady, scanPending, joinPending, stopping, radioStarted, commandBusy, scanFailed, scanObserved;
    uint8_t mac[6];
    uint8_t requestedMac[6];
    bool macPending, macApplying, macCancelled, macOverride, publishQueued, published;
    uint32_t macSerial, macCompleted;
    int macResult;

    static Mtk7902Backend *create(MtkWlan *core);
    bool start(IOService *provider);
    void stop();
    void destroy();
    // The following three methods run only on the radio's worker.
    void receive(const uint8_t *frame, uint32_t len);
    void publish(bool networks = false);
    void workerPoll();
    void workerControl(); // supervisor boundary only: no firmware command or association is on the stack
    void restoreMac();    // after NIC capability, before the recovered firmware registers its station
    // The following methods run only on the Mtk work loop gate.
    void tick();
    int scan();
    int join(const mtk_join_params *request);
    int disconnect();
    int transmit(const uint8_t *frame, uint32_t len);
    void snapshot(mtk_status *out);
    uint32_t getResults(mtk_scan_result *out, uint32_t max);
    int getBss(uint32_t index, mtk_bss_info *out);
    int setMac(const uint8_t address[6]);
    int queueMac(const uint8_t address[6]);
    int getLink(mtk_link_info *out);
    int getMac(uint8_t out[6]);
};
