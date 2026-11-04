// <wtl> project: mtkwlannative | file: src/MtkWlanNativeIf.hpp | author: watchthelight </wtl>
// SPDX-License-Identifier: ISC
// MtkWlanNativeIf: the Wi-Fi interface macOS talks to, a subclass of the Family's IO80211InfraProtocol (IO80211InfraInterface
// plus the 200 pure get/set request handlers the Family routes ioctls to). Created by the controller, attached to it, started
// with a RegistrationInfo block; its start registers one packet pool and [TxSubmission x4, TxCompletion, RxCompletion] with
// registerInfraEthernetInterface. The data path bridges Skywalk packets to the MtkWlan core's byte-level ops (v3):
//   * RX: the core hands each Ethernet frame as an mbuf (core gate; its block-ack reorder buffer holds mbufs), the rx
//     callback copies it into a 512-entry ring and frees it; a 2 ms timer on the interface's work queue moves the frames
//     into pool packets and enqueues them on the Rx completion queue;
//   * TX: the Tx submission dequeue callback (work queue) hands each packet's bytes to the core under the core's gate
//     (runAction) and completes the packets on the next tick, once the queue has recorded their consumption.
// No call ever goes from the core gate into the work queue gate (the core calls back with its gate held; everything it
// tells us is only flagged and applied from the tick), so the two gates cannot deadlock.
// Shape adapted from ga403wm-hackintosh kexts/itlmt7925Native/itlmt7925_iface.{h,cpp} (BSD-3-Clause-Clear, Copyright (c)
// 2026 thanhnndev and contributors), re-read against the 25G241 dump.
#pragma once
#include "IO80211InfraProtocol.h"
#include "MtkCoreApi.h"
#include "skywalk_min.h"
#include <IOKit/IOLocks.h>
#include <IOKit/IOTimerEventSource.h>

class MtkWlanNative;

class MtkWlanNativeIf : public IO80211InfraProtocol {
    OSDeclareDefaultStructors(MtkWlanNativeIf)

public:
    static const uint32_t kNumTx = 4;        // Apple's interface fixes four Tx submission queues (ACs 0..3)

    // alloc, init(), the station address, role and id, attach(controller). Not started yet.
    static MtkWlanNativeIf *withController(MtkWlanNative *controller, uint32_t ifId, uint32_t role);
    // the equivalent of Apple's interface start(core, RegistrationInfo*): base start, pool + queues, registration
    bool startWithInfo(MtkWlanNative *controller, IOSkywalkEthernetInterface::RegistrationInfo *info);

    bool init() override;
    void free() override;
    void stop(IOService *provider) override;
    IOReturn enable(uint32_t options) override;
    IOReturn disable(uint32_t options) override;
    void setInterfaceEnable(bool enabled) override;
    uint32_t getHardwareAssists() override;
    IOReturn processBSDCommand(__ifnet *netif, uint32_t command, void *data) override;
    IOReturn newUserClient(task_t task, void *securityID, uint32_t type, OSDictionary *properties, IOUserClient **handler) override;
    using IO80211InfraProtocol::newUserClient;

    // accessors the Family calls on its interface (IO80211SkywalkInterface slots 419..439)
    CCLogStream *getLogger() const override;
    IO80211Controller *getController() override;
    uint32_t getTxQueueDepth() override;
    uint32_t getRxQueueCapacity() override;
    IOSkywalkPacketQueue *getMultiCastQueue() override;
    uint16_t getTxHeadroom() override;
    IOSkywalkRxCompletionQueue *getRxCompQueue() override;
    IOSkywalkTxCompletionQueue *getTxCompQueue() override;
    IOSkywalkTxSubmissionQueue *getTxSubQueue(apple80211_wme_ac ac) override;
    IOSkywalkPacketBufferPool *getTxPacketPool() override;
    IOSkywalkPacketBufferPool *getRxPacketPool() override;
    IOReturn enableDatapath() override;
    IOReturn disableDatapath() override;
    uint8_t getNumTxQueues() override;

    // data path
    uint32_t submitPackets(IOSkywalkPacket *const *packets, uint32_t count);
    static void tick(OSObject *owner, IOTimerEventSource *);

#define MTK_IFACE_DECLS
#include "iface_stubs.inc"
#undef MTK_IFACE_DECLS

private:
    bool createPoolAndQueues();
    void destroyPoolAndQueues();
    bool bindCore();
    void unbindCore();
    void drainRx();
    void completeTx();
    void pollControl();
    void restartTx();
    bool linkUp() const;
    // core callbacks (core gate held: flag and copy only)
    static mbuf_t cbRxAlloc(void *ctx, uint32_t len);
    static void cbRx(void *ctx, mbuf_t m);
    static void cbRxFlush(void *ctx);
    static void cbLink(void *ctx, int up, uint64_t speed);
    static void cbTxSpace(void *ctx);
    static void cbScanDone(void *ctx, int status, uint32_t count);
    static void cbJoinResult(void *ctx, int result, const uint8_t bssid[6]);
    static IOReturn gatedTx(OSObject *owner, void *arg, void *, void *, void *);
    static IOReturn gatedSetEnabled(OSObject *owner, void *arg, void *, void *, void *);
    static IOReturn gatedSetMode(OSObject *owner, void *arg, void *flags, void *, void *);
    static IOReturn gatedSetMac(OSObject *owner, void *mac, void *rc, void *, void *);

    struct RxFrame { uint32_t length; uint8_t bytes[1600]; };
    struct TxItem { const uint8_t *bytes; uint32_t length; int result; };
    static const uint32_t kRxRing = 512, kRxBatch = 128, kTickMs = 2, kTxBatch = 64;

    MtkWlanNative *ctl = nullptr;            // not retained: the controller owns the interface's lifetime
    const mtk_core_ops_v3 *ops = nullptr;
    const mtk_core_ops_v4 *ops4 = nullptr;   // the same table as ops, with set_mac (version 4 of the core API)
    const mtk_core_ops_v5 *ops5 = nullptr;   // the same table again, with the rate details (version 5, core 0.12.1)
    bool rateInfo(mtk_rate_info *r);
    void *core = nullptr;
    uint8_t curMac[6] = {};                  // the last address the Family set through slot 470 (trace only)
    // the BSD interface (from the first processBSDCommand) and the link-layer address the core was last told to use:
    // the Family gave en1 its address (factory with the local bit) without slot 470 (N-137), so the tick follows the ifnet
    void *bsdIf = nullptr;
    uint8_t followedMac[6] = {};
    uint32_t macFollows = 0, macFollowFails = 0;
    uint8_t followTries = 0, followWait = 0;   // a refused address is tried again every 2 s, 6 times in all
    void followAddress();
    // stage C: a WCL scan request runs the core's scan; the results are posted as one 0xc9 event per BSS, then 0xed
    bool scanActive = false, scanDeliverNow = false;
    uint32_t scanPolls = 0, scansRequested = 0, scansDelivered = 0, scanResultsPosted = 0, scanTraced = 0;
    uint8_t scanSsidLen = 0;
    uint8_t scanSsid[32] = {};
    struct mtk_bss_info *bssSnap = nullptr;  // kBssSnap entries, filled under the core gate
    uint8_t *beaconMsg = nullptr;            // kBeaconHeader + MTK_BSS_IE_MAX
    static const uint32_t kBssSnap = 128, kBeaconHeader = 0x44, kScanTimeoutPolls = 2500;   // 25 s at the 10 ms poll
    void serviceScan();
    void deliverScanResults();
    void postEvent(uint32_t code, void *data, unsigned long length);
    static IOReturn gatedScan(OSObject *owner, void *rc, void *, void *, void *);
    static IOReturn gatedSnapBss(OSObject *owner, void *count, void *, void *, void *);
    // stage D: the Family drives the association (setWCL_ASSOCIATE -> core join; the result and the link reach it as the
    // 0xd8 / 0x4e / 0xd3 / 0xd4 / 0xd5 events), and answers about the current BSS come from the core's cache
    bool joinActive = false;                 // a setWCL_ASSOCIATE is outstanding
    bool nmLinkUp = false;                   // what the Family was last told (0xd8 up)
    bool nmEverJoined = false;
    bool linkDownPending = false;            // the core dropped the link while the Family thinks it is up (set from cbLink)
    uint8_t nmBssid[6] = {};
    uint8_t wantSsidLen = 0;
    uint8_t wantSsid[32] = {};
    uint32_t joinPolls = 0, reannouncePolls = 0;
    uint32_t joinsRequested = 0, joinsOk = 0, joinsFailed = 0, leaves = 0, linkDowns = 0;
    uint32_t beaconReports = 0;              // 0xaf events posted (the Family's missed-beacons check)
    uint8_t *lqmMsg = nullptr;               // 0x1dc bytes, the 0x27 link quality update posted with each beacon report
    uint32_t lqmTick = 0;
    int32_t lqReported = -3;               // last ifnet link quality given to setLQM; -3 = never
    void reportLinkQuality(int32_t lq);
    uint32_t assocTraced = 0, linkStatusTraced = 0, linkStateTraced = 0, bssInfoTraced = 0, extBssTraced = 0, miscTraced = 0;
    struct mtk_status *statusBuf = nullptr;  // filled under the core gate by gatedStatus
    struct mtk_bss_info *curBss = nullptr;   // the associated BSS, looked up in the cache by gatedFindBss
    static const uint32_t kJoinTimeoutPolls = 3000;   // 30 s at the 10 ms poll
    void serviceJoin();
    void finishJoin(bool ok, const uint8_t bssid[6]);
    void postLinkStatus(const uint8_t bssid[6], bool up, uint32_t reason);
    bool currentBss(uint8_t bssid[6]);       // copies the associated BSS into curBss; false when not associated / not cached
    static IOReturn gatedJoin(OSObject *owner, void *params, void *rc, void *, void *);
    static IOReturn gatedDisconnect(OSObject *owner, void *, void *, void *, void *);
    static IOReturn gatedStatus(OSObject *owner, void *, void *, void *, void *);
    static IOReturn gatedFindBss(OSObject *owner, void *bssid, void *found, void *, void *);
public:
    IOReturn setWCL_LINK_STATE_UPDATE(apple80211_wcl_update_link_state *a1) override;   // IO80211InfraInterface slot 468
private:
    IOWorkLoop *coreLoop = nullptr;          // retained
    IOWorkLoop *queued = nullptr;            // the work queue the event sources were added to (retained)
    IOSkywalkPacketBufferPool *pool = nullptr;
    IOSkywalkTxSubmissionQueue *txSub[kNumTx] = {};
    IOSkywalkTxCompletionQueue *txComp = nullptr;
    IOSkywalkRxCompletionQueue *rxComp = nullptr;
    IOSkywalkRxSubmissionQueue *rxSub = nullptr;   // the common Rx submission queue (created, not registered, as Apple's bus layer)
    IOTimerEventSource *dpTimer = nullptr;
    IOLock *lock = nullptr;                  // the ring and the flags set from the core gate
    RxFrame *rxRing = nullptr;
    TxItem *txBatch = nullptr;
    IOSkywalkPacket *pendingTx[512] = {};
    uint32_t pendingCount = 0;
    uint32_t rxHead = 0, rxTail = 0;
    uint16_t headroom = 0;
    bool bound = false, dpEnabled = false;
    // flagged from the core gate, applied from the tick
    bool radioUp = false, linkDirty = false, txSpace = false, txPendingFull = false, scanDone = false, joinDone = false;
    uint64_t radioSpeed = 0;
    int scanStatus = 0, joinResult = 0;
    uint32_t scanCount = 0;
    uint8_t joinBssid[6] = {};
    uint32_t ticks = 0;
    // counters (published once a second)
    uint32_t rxReceived = 0, rxDelivered = 0, rxDropped = 0, rxRingFull = 0, rxOversize = 0, rxPeak = 0;
    uint32_t txSent = 0, txDropped = 0, txStalled = 0, txCompleted = 0;
    uint32_t rxFailTraced = 0;
    bool firstTxTraced = false, firstRxTraced = false, assistsTraced = false, mediaTraced = false;
};
