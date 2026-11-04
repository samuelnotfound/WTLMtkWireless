// <wtl> project: mtkwlannative | file: src/MtkWlanNativeIf.cpp | author: watchthelight </wtl>
// SPDX-License-Identifier: ISC
// See MtkWlanNativeIf.hpp: lifecycle, pool and queues, the data path. Request handlers are in MtkWlanNativeReq.cpp.
#include "MtkWlanNativeIf.hpp"
#include "MtkWlanNative.hpp"
#include "MtkNativeTrace.hpp"
#include <IOKit/IOLib.h>
#include <IOKit/network/IONetworkMedium.h>
#include <net/ethernet.h>
#include <net/kpi_interface.h>
#include <string.h>
#include <sys/kpi_mbuf.h>

#define super IO80211InfraProtocol
OSDefineMetaClassAndStructors(MtkWlanNativeIf, IO80211InfraProtocol)

namespace {
// Sizes Apple's PCIe layer uses with its bus-state words at 0: TxSubmission / TxCompletion 0x200, RxSubmission / RxCompletion
// 0x400, one pool of 0x1300 packets of 0x800 bytes.
const uint32_t kTxCapacity = 0x200;
const uint32_t kRxCapacity = 0x400;
const uint32_t kPoolPackets = 0x1300;
const uint32_t kPoolBufferSize = 0x800;

uint32_t txSubmissionAction(OSObject *owner, IOSkywalkTxSubmissionQueue *, IOSkywalkPacket *const *packets, uint32_t count, void *) {
    return ((MtkWlanNativeIf *)owner)->submitPackets(packets, count);
}
uint32_t rxSubmissionAction(OSObject *, IOSkywalkRxSubmissionQueue *, IOSkywalkPacket *const *, uint32_t, void *) { return 0; }
uint32_t txCompletionAction(OSObject *, IOSkywalkTxCompletionQueue *, IOSkywalkPacket **, uint32_t, void *) { return 0; }
uint32_t rxCompletionAction(OSObject *, IOSkywalkRxCompletionQueue *, IOSkywalkPacket **, uint32_t, void *) { return 0; }

template <typename T>
inline T &field(IOSkywalkEthernetInterface::RegistrationInfo *info, size_t off) { return *(T *)((uint8_t *)info + off); }
inline void rel(OSObject *o) { if (o) o->release(); }
inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
} // namespace

// ---- creation and start --------------------------------------------------------------------------------------------------

MtkWlanNativeIf *MtkWlanNativeIf::withController(MtkWlanNative *controller, uint32_t ifId, uint32_t role) {
    // the steps of Apple's initWithProviderAndParameters: init(), init(provider, address), role, id, attach
    MtkWlanNativeIf *i = OSTypeAlloc(MtkWlanNativeIf);
    if (!i) return nullptr;
    if (!i->init()) { i->release(); return nullptr; }
    ether_addr station;
    memcpy(&station, controller->stationAddress(), 6);
    if (!i->IO80211SkywalkInterface::init(controller, &station)) { i->release(); return nullptr; }
    i->setInterfaceRole(role);
    i->setInterfaceId(ifId);
    i->ctl = controller;
    if (!i->attach(controller)) { i->release(); return nullptr; }
    return i;
}

bool MtkWlanNativeIf::init() {
    if (!super::init()) return false;
    lock = IOLockAlloc();
    bssSnap = (struct mtk_bss_info *)IOMalloc(kBssSnap * sizeof(struct mtk_bss_info));
    beaconMsg = (uint8_t *)IOMalloc(kBeaconHeader + MTK_BSS_IE_MAX);
    statusBuf = (struct mtk_status *)IOMalloc(sizeof(struct mtk_status));
    curBss = (struct mtk_bss_info *)IOMalloc(sizeof(struct mtk_bss_info));
    lqmMsg = (uint8_t *)IOMalloc(0x1dc);
    return lock != nullptr && bssSnap != nullptr && beaconMsg != nullptr;
}

bool MtkWlanNativeIf::createPoolAndQueues() {
    IOSkywalkPacketBufferPool::PoolOptions po;
    bzero(&po, sizeof(po));
    po.packets0 = po.packets1 = kPoolPackets;
    po.bufferSize = kPoolBufferSize;
    po.one = 1;
    po.flags = 1;
    pool = IOSkywalkPacketBufferPool::withName("MtkWlanNativePool", this, 1, &po);
    nativeTrace(ctl, "sw:pool=%s", pool ? "ok" : "null");
    if (!pool) return false;
    rxSub = IOSkywalkRxSubmissionQueue::withPool(pool, kRxCapacity, 0, 0, this, rxSubmissionAction, this, 0);
    for (uint32_t i = 0; i < kNumTx; i++) {
        txSub[i] = IOSkywalkTxSubmissionQueue::withPool(pool, kTxCapacity, i, this, txSubmissionAction, this, 0);
        if (!txSub[i]) { nativeTrace(ctl, "sw:txSub[%u]=null", i); return false; }
    }
    txComp = IOSkywalkTxCompletionQueue::withPool(pool, kTxCapacity, kNumTx, this, txCompletionAction, this, 0);
    rxComp = IOSkywalkRxCompletionQueue::withPool(pool, kRxCapacity, kNumTx + 1, this, rxCompletionAction, this, 0);
    nativeTrace(ctl, "sw:queues rxSub=%d txComp=%d rxComp=%d", rxSub != nullptr, txComp != nullptr, rxComp != nullptr);
    return rxSub && txComp && rxComp;
}

bool MtkWlanNativeIf::startWithInfo(MtkWlanNative *controller, IOSkywalkEthernetInterface::RegistrationInfo *info) {
    ctl = controller;
    ops = controller->coreOps();
    ops4 = controller->coreOps4();
    ops5 = controller->coreOps5();
    core = controller->coreHandle();
    coreLoop = controller->coreLoop();
    if (coreLoop) coreLoop->retain();
    if (!info || field<uint32_t>(info, 0) != 1 || field<uint32_t>(info, 4) != 0x130 || !ops || !core || !coreLoop) {
        nativeTrace(ctl, "sw:bad info or no core");
        return false;
    }
    // Apple's interface start: for role 1, deferBSDAttach(true) before IO80211InfraInterface::start
    if (getInterfaceRole() == 1) {
        IOReturn dr = deferBSDAttach(true);
        nativeTrace(ctl, "sw:deferBSDAttach=%x", dr);
    }
    bool sup = super::start(controller);
    nativeTrace(ctl, "sw:IO80211InfraInterface::start=%d", sup);
    if (!sup) return false;
    IO80211WorkQueue *wq = getWorkQueue();
    nativeTrace(ctl, "sw:getWorkQueue=%s", wq ? "ok" : "null");
    if (!wq) return false;
    if (!createPoolAndQueues()) { destroyPoolAndQueues(); return false; }
    IOSkywalkPacketQueue *queues[kNumTx + 2];
    uint32_t n = 0;
    for (uint32_t i = 0; i < kNumTx; i++) queues[n++] = txSub[i];
    queues[n++] = txComp;
    queues[n++] = rxComp;
    wq->retain();
    queued = wq;
    for (uint32_t i = 0; i < n; i++) {
        IOReturn r = wq->addEventSource(queues[i]);
        if (r != kIOReturnSuccess) {
            nativeTrace(ctl, "sw:addEventSource[%u]=%x", i, r);
            destroyPoolAndQueues();
            return false;
        }
    }
    nativeTrace(ctl, "sw:addEventSource x%u ok", n);
    rxRing = (RxFrame *)IOMalloc(sizeof(RxFrame) * kRxRing);
    txBatch = (TxItem *)IOMalloc(sizeof(TxItem) * kTxBatch);
    dpTimer = IOTimerEventSource::timerEventSource(this, tick);
    if (!rxRing || !txBatch || !dpTimer || queued->addEventSource(dpTimer) != kIOReturnSuccess) {
        nativeTrace(ctl, "sw:timer or ring setup failed");
        destroyPoolAndQueues();
        return false;
    }
    if (!bindCore()) { destroyPoolAndQueues(); return false; }
    headroom = field<uint16_t>(info, 0x3c);
    field<uint32_t>(info, 0x48) -= headroom;
    field<uint32_t>(info, 0x4c) -= headroom;
    IOReturn rr = registerInfraEthernetInterface(info, queues, n, pool, pool);
    nativeTrace(ctl, "sw:registerInfraEthernetInterface=%x", rr);
    if (rr != kIOReturnSuccess) { destroyPoolAndQueues(); return false; }
    // Apple's interface start, after a successful registration: registerService(kIOServiceAsynchronous), setRunningState(false)
    if (getInterfaceRole() == 1) {
        registerService(kIOServiceAsynchronous);
        setRunningState(false);
        nativeTrace(ctl, "sw:published, running state false");
    }
    dpTimer->setTimeoutMS(10);
    return true;
}

// Register as the core's front end (v3 callbacks) and claim the data path. set_callbacks takes the core gate itself; set_mode
// needs it, so it goes through runAction. The auto-open fallback stays on until the native join path exists (stage D).
bool MtkWlanNativeIf::bindCore() {
    mtk_fe_callbacks_v2 cb = {};
    cb.v1.version = ops4 ? MTK_CORE_API_V4 : MTK_CORE_API_V3;
    cb.v1.size = sizeof(cb);
    cb.v1.ctx = this;
    cb.v1.rx_alloc = cbRxAlloc;
    cb.v1.rx = cbRx;
    cb.v1.rx_flush = cbRxFlush;
    cb.v1.link = cbLink;
    cb.v1.tx_space = cbTxSpace;
    cb.scan_done = cbScanDone;
    cb.join_result = cbJoinResult;
    if (ops->v2.v1.set_callbacks(core, &cb.v1) != 0) {
        nativeTrace(ctl, "dp:core refused the callbacks (another front end?)");
        return false;
    }
    IOReturn mr = coreLoop->runAction(gatedSetMode, this, (void *)(uintptr_t)MTK_FE_NATIVE, (void *)(uintptr_t)0);   // stage D: macOS drives every join, the core's open-network fallback is off
    nativeTrace(ctl, "dp:bound, set_mode=%x", mr);
    IOLockLock(lock);
    bound = true;
    IOLockUnlock(lock);
    return true;
}

void MtkWlanNativeIf::unbindCore() {
    bool was;
    IOLockLock(lock);
    was = bound;
    bound = false;
    IOLockUnlock(lock);
    if (was && ops && core) {
        coreLoop->runAction(gatedSetMode, this, (void *)(uintptr_t)MTK_FE_ETHERNET, (void *)0);
        ops->v2.v1.set_callbacks(core, nullptr);
        nativeTrace(ctl, "dp:unbound");
    }
}

// Reverse of start; Apple's stop does for each queue: disable, removeEventSource, release.
void MtkWlanNativeIf::destroyPoolAndQueues() {
    unbindCore();
    dpEnabled = false;
    if (dpTimer) {
        dpTimer->cancelTimeout();
        if (queued) queued->removeEventSource(dpTimer);
        dpTimer->release();
        dpTimer = nullptr;
    }
    while (pendingCount && txComp) {        // give the packets back while the completion queue still exists
        if (txComp->enqueuePackets(pendingTx, 1, 0) != kIOReturnSuccess) break;
        memmove(pendingTx, pendingTx + 1, (--pendingCount) * sizeof(pendingTx[0]));
    }
    if (rxRing) { IOFree(rxRing, sizeof(RxFrame) * kRxRing); rxRing = nullptr; }
    if (txBatch) { IOFree(txBatch, sizeof(TxItem) * kTxBatch); txBatch = nullptr; }
    IOSkywalkPacketQueue *all[kNumTx + 2];
    uint32_t n = 0;
    for (uint32_t i = 0; i < kNumTx; i++) { all[n++] = txSub[i]; txSub[i] = nullptr; }
    all[n++] = txComp;
    all[n++] = rxComp;
    txComp = nullptr;
    rxComp = nullptr;
    for (uint32_t i = 0; i < n; i++) {
        if (!all[i]) continue;
        all[i]->disable();
        if (queued) queued->removeEventSource(all[i]);
        all[i]->release();
    }
    rel(rxSub);     rxSub = nullptr;
    rel(queued);    queued = nullptr;
    rel(pool);      pool = nullptr;
    rel(coreLoop);  coreLoop = nullptr;
}

void MtkWlanNativeIf::stop(IOService *provider) {
    destroyPoolAndQueues();
    super::stop(provider);
}

void MtkWlanNativeIf::free() {
    destroyPoolAndQueues();
    if (bssSnap) { IOFree(bssSnap, kBssSnap * sizeof(struct mtk_bss_info)); bssSnap = nullptr; }
    if (beaconMsg) { IOFree(beaconMsg, kBeaconHeader + MTK_BSS_IE_MAX); beaconMsg = nullptr; }
    if (statusBuf) { IOFree(statusBuf, sizeof(struct mtk_status)); statusBuf = nullptr; }
    if (curBss) { IOFree(curBss, sizeof(struct mtk_bss_info)); curBss = nullptr; }
    if (lqmMsg) { IOFree(lqmMsg, 0x1dc); lqmMsg = nullptr; }
    if (lock) { IOLockFree(lock); lock = nullptr; }
    super::free();
}

// ---- accessors -----------------------------------------------------------------------------------------------------------

CCLogStream *MtkWlanNativeIf::getLogger() const { return ctl ? ctl->getLogger() : nullptr; }
IO80211Controller *MtkWlanNativeIf::getController() { return ctl; }
uint32_t MtkWlanNativeIf::getTxQueueDepth() { return kTxCapacity; }
uint32_t MtkWlanNativeIf::getRxQueueCapacity() { return kRxCapacity; }
IOSkywalkPacketQueue *MtkWlanNativeIf::getMultiCastQueue() { return nullptr; }   // optional in Apple's driver; not created
uint16_t MtkWlanNativeIf::getTxHeadroom() { return headroom; }
IOSkywalkRxCompletionQueue *MtkWlanNativeIf::getRxCompQueue() { return rxComp; }
IOSkywalkTxCompletionQueue *MtkWlanNativeIf::getTxCompQueue() { return txComp; }
IOSkywalkTxSubmissionQueue *MtkWlanNativeIf::getTxSubQueue(apple80211_wme_ac ac) { return (uint32_t)ac < kNumTx ? txSub[(uint32_t)ac] : nullptr; }
IOSkywalkPacketBufferPool *MtkWlanNativeIf::getTxPacketPool() { return pool; }
IOSkywalkPacketBufferPool *MtkWlanNativeIf::getRxPacketPool() { return pool; }
uint8_t MtkWlanNativeIf::getNumTxQueues() { return kNumTx; }

uint32_t MtkWlanNativeIf::getHardwareAssists() {
    // the core takes complete Ethernet frames; no checksum or segmentation offload
    if (!assistsTraced) { assistsTraced = true; nativeTrace(ctl, "if:hardware assists=0"); }
    return 0;
}

// ---- enable / disable -----------------------------------------------------------------------------------------------------

IOReturn MtkWlanNativeIf::enableDatapath() {
    if (!bound || !dpTimer) return kIOReturnNotReady;
    if (dpEnabled) return kIOReturnSuccess;
    nativeTrace(ctl, "dp:enable queues");
    txComp->enable();
    rxComp->enable();
    for (uint32_t i = 0; i < kNumTx; i++) txSub[i]->enable();
    dpEnabled = true;
    setRunningState(true);
    coreLoop->runAction(gatedSetEnabled, this, (void *)1);
    setProperty("MtkNativeDatapath", true);
    dpTimer->setTimeoutMS(kTickMs);
    nativeTrace(ctl, "dp:enabled");
    return kIOReturnSuccess;
}

IOReturn MtkWlanNativeIf::disableDatapath() {
    if (!dpEnabled) return kIOReturnSuccess;
    nativeTrace(ctl, "dp:disable queues");
    dpEnabled = false;
    setRunningState(false);
    if (coreLoop && bound) coreLoop->runAction(gatedSetEnabled, this, (void *)0);
    for (uint32_t i = 0; i < kNumTx; i++) if (txSub[i]) txSub[i]->disable();
    // the completion queues stay enabled until every submitted packet has been returned (completeTx on the tick)
    setProperty("MtkNativeDatapath", false);
    return kIOReturnSuccess;
}

IOReturn MtkWlanNativeIf::enable(uint32_t options) {
    nativeCall(ctl, 'E', 1);
    nativeTrace(ctl, "if:enable options=%x", options);
    IOReturn result = super::enable(options);
    if (result == kIOReturnSuccess && bound) result = enableDatapath();
    nativeTrace(ctl, "if:enable result=%x", result);
    return result;
}

IOReturn MtkWlanNativeIf::disable(uint32_t options) {
    nativeCall(ctl, 'E', 0);
    nativeTrace(ctl, "if:disable options=%x", options);
    disableDatapath();
    IOReturn result = super::disable(options);
    nativeTrace(ctl, "if:disable result=%x", result);
    return result;
}

void MtkWlanNativeIf::setInterfaceEnable(bool enabled) {
    nativeTrace(ctl, "if:setInterfaceEnable=%u", enabled);
    super::setInterfaceEnable(enabled);
}

IOReturn MtkWlanNativeIf::newUserClient(task_t task, void *securityID, uint32_t type, OSDictionary *properties, IOUserClient **handler) {
    nativeCall(ctl, 'W', type);
    return super::newUserClient(task, securityID, type, properties, handler);
}

IOReturn MtkWlanNativeIf::processBSDCommand(__ifnet *netif, uint32_t command, void *data) {
    nativeCall(ctl, 'B', command);
    if (netif && !bsdIf) { bsdIf = netif; nativeTrace(ctl, "if:bsd interface seen (command %x)", command); }
    bool media = command == 0xc02c6938 || command == 0xc0286938;   // SIOCGIFXMEDIA / SIOCGIFMEDIA
    IOReturn result = super::processBSDCommand(netif, command, data);
    if (media && !mediaTraced) { mediaTraced = true; nativeTrace(ctl, "if:first media query result=%x", result); }
    if (command == 0x8020693c && netif) followAddress();            // SIOCSIFLLADDR: apply at once, not at the next poll
    return result;
}

// The firmware accepts unicast only for the own-MAC in DEV_INFO. On N-137 the Family gave en1 4a:45:.. (the factory
// 48:45:.. with the locally administered bit) without calling slot 470, DHCP left from 4a:45:.. and no reply was ever
// accepted. Whatever path sets the BSD address, the core follows it: the tick compares the ifnet's link-layer address
// with the one the core was last given and calls set_mac on a change.
void MtkWlanNativeIf::followAddress() {
    if (!bsdIf || !ops4 || !ops4->set_mac || !coreLoop) return;
    uint8_t m[6];
    if (ifnet_lladdr_copy_bytes((ifnet_t)bsdIf, m, sizeof(m)) != 0) return;
    if ((m[0] & 1) || !(m[0] | m[1] | m[2] | m[3] | m[4] | m[5])) return;
    if (!memcmp(m, followedMac, 6)) {
        // refused before (busy firmware, a join in flight): with the old address in DEV_INFO no unicast reply reaches
        // the new one and the network looks dead until Private Wi-Fi Address is turned off (Game-5040, 2026-10-23)
        if (!followTries || followTries >= 6 || ++followWait < 4) return;
    } else {
        followTries = 0;
    }
    followWait = 0;
    int rc = -2;
    if (coreLoop->runAction(gatedSetMac, this, m, &rc) != kIOReturnSuccess) rc = -2;
    nativeTrace(ctl, "if:follow lladdr %02x:%02x:%02x:%02x:%02x:%02x -> %d", m[0], m[1], m[2], m[3], m[4], m[5], rc);
    if (rc == 0) { memcpy(followedMac, m, 6); memcpy(curMac, m, 6); macFollows++; followTries = 0; }
    else { macFollowFails++; followTries++; }
    char s[80];
    snprintf(s, sizeof(s), "follows %u failed %u last rc %d tries %u", macFollows, macFollowFails, rc, followTries);
    setProperty("MtkNativeMacFollow", s);
    if (rc != 0) memcpy(followedMac, m, 6);   // not every poll: the retry above paces it
}

// ---- core callbacks (core gate held) ---------------------------------------------------------------------------------------

// The core fills a packet mbuf (it may hold it in its block-ack reorder buffer before delivering it in order)
mbuf_t MtkWlanNativeIf::cbRxAlloc(void *, uint32_t len) {
    mbuf_t m = nullptr;
    unsigned int chunks = 1;
    if (mbuf_allocpacket(MBUF_DONTWAIT, len, &chunks, &m) != 0) return nullptr;
    return m;
}

// One delivered frame: into the ring, then the mbuf is ours to free
void MtkWlanNativeIf::cbRx(void *ctx, mbuf_t m) {
    MtkWlanNativeIf *i = (MtkWlanNativeIf *)ctx;
    uint32_t len = (uint32_t)mbuf_pkthdr_len(m);
    IOLockLock(i->lock);
    uint32_t next = (i->rxHead + 1) % kRxRing;
    if (!i->rxRing || len < 14 || len > sizeof(RxFrame::bytes)) { i->rxDropped++; i->rxOversize++; }
    else if (next == i->rxTail) { i->rxDropped++; i->rxRingFull++; }
    else {
        RxFrame &f = i->rxRing[i->rxHead];
        f.length = len;
        mbuf_copydata(m, 0, len, f.bytes);
        i->rxHead = next;
        i->rxReceived++;
        uint32_t depth = (next + kRxRing - i->rxTail) % kRxRing;
        if (depth > i->rxPeak) i->rxPeak = depth;
    }
    IOLockUnlock(i->lock);
    mbuf_freem(m);
}

void MtkWlanNativeIf::cbRxFlush(void *) {}

void MtkWlanNativeIf::cbLink(void *ctx, int up, uint64_t speed) {
    MtkWlanNativeIf *i = (MtkWlanNativeIf *)ctx;
    IOLockLock(i->lock);
    i->radioUp = up != 0;
    i->radioSpeed = speed;
    i->linkDirty = true;
    if (!up && i->nmLinkUp) i->linkDownPending = true;   // serviceJoin tells the Family (0xd8 down) from the tick
    IOLockUnlock(i->lock);
}

void MtkWlanNativeIf::cbTxSpace(void *ctx) {
    MtkWlanNativeIf *i = (MtkWlanNativeIf *)ctx;
    IOLockLock(i->lock);
    i->txSpace = true;
    IOLockUnlock(i->lock);
}

void MtkWlanNativeIf::cbScanDone(void *ctx, int status, uint32_t count) {
    MtkWlanNativeIf *i = (MtkWlanNativeIf *)ctx;
    IOLockLock(i->lock);
    i->scanDone = true;
    i->scanStatus = status;
    i->scanCount = count;
    IOLockUnlock(i->lock);
}

void MtkWlanNativeIf::cbJoinResult(void *ctx, int result, const uint8_t bssid[6]) {
    MtkWlanNativeIf *i = (MtkWlanNativeIf *)ctx;
    IOLockLock(i->lock);
    i->joinDone = true;
    i->joinResult = result;
    if (bssid) memcpy(i->joinBssid, bssid, 6);
    IOLockUnlock(i->lock);
}

// ---- gated calls into the core ----------------------------------------------------------------------------------------------

IOReturn MtkWlanNativeIf::gatedSetMode(OSObject *owner, void *mode, void *flags, void *, void *) {
    MtkWlanNativeIf *i = (MtkWlanNativeIf *)owner;
    return i->ops->set_mode(i->core, (uint32_t)(uintptr_t)mode, (uint32_t)(uintptr_t)flags) == 0 ? kIOReturnSuccess : kIOReturnError;
}

IOReturn MtkWlanNativeIf::gatedSetEnabled(OSObject *owner, void *arg, void *, void *, void *) {
    MtkWlanNativeIf *i = (MtkWlanNativeIf *)owner;
    i->ops->v2.v1.set_enabled(i->core, arg != nullptr);
    return kIOReturnSuccess;
}

// one batch of frames: the packet memory stays valid until the packets are completed, so the core reads it in place
IOReturn MtkWlanNativeIf::gatedTx(OSObject *owner, void *arg, void *, void *, void *) {
    MtkWlanNativeIf *i = (MtkWlanNativeIf *)owner;
    uint32_t n = (uint32_t)(uintptr_t)arg;
    for (uint32_t k = 0; k < n; k++) {
        TxItem &t = i->txBatch[k];
        t.result = i->ops->tx_bytes(i->core, t.bytes, t.length);
        if (t.result == MTK_TX_STALL) {
            for (uint32_t j = k + 1; j < n; j++) i->txBatch[j].result = MTK_TX_STALL;
            break;
        }
    }
    return kIOReturnSuccess;
}

// ---- data path ----------------------------------------------------------------------------------------------------------------

// Tx submission dequeue (work queue). Returns how many packets were consumed; a stalled frame and everything after it stay in
// the queue and are asked for again once the core reports space.
uint32_t MtkWlanNativeIf::submitPackets(IOSkywalkPacket *const *packets, uint32_t count) {
    if (!dpEnabled || !bound || !packets || !txBatch) return 0;
    bool up;
    IOLockLock(lock);
    up = radioUp;
    IOLockUnlock(lock);
    if (!up) { txStalled++; return 0; }
    uint32_t n = 0;
    if (pendingCount + 1 > 512) { txPendingFull = true; return 0; }
    for (; n < count && n < kTxBatch && pendingCount + n < 512; n++) {
        IOSkywalkPacket *p = packets[n];
        TxItem &t = txBatch[n];
        t.bytes = nullptr;
        t.length = 0;
        t.result = MTK_TX_DROPPED;
        if (!p) break;
        bool single = p->getPacketBufferCount() == 1;
        uint32_t len = p->getDataLength(), size = single ? p->getBufferSize() : 0;
        uint16_t offset = single ? p->getDataOffset() : 0;
        const uint8_t *base = single ? (const uint8_t *)p->getDataVirtualAddress() : nullptr;
        if (base && len >= 14 && len <= 1514 && offset <= size && len <= size - offset) {
            t.bytes = base + offset;
            t.length = len;
        }
    }
    if (!n) return 0;
    if (!firstTxTraced) { firstTxTraced = true; nativeTrace(ctl, "dp:first TX batch %u", n); }
    coreLoop->runAction(gatedTx, this, (void *)(uintptr_t)n);
    uint32_t used = 0;
    for (; used < n; used++) {
        TxItem &t = txBatch[used];
        if (t.result == MTK_TX_STALL) { txStalled++; break; }
        if (t.result == MTK_TX_OK) txSent++; else txDropped++;
        pendingTx[pendingCount++] = packets[used];     // completed on the next tick, after the queue recorded the consumption
    }
    if (used == n && n < count && pendingCount >= 512) txPendingFull = true;
    return used;
}

// 0.4.11: one shift at the end instead of a memmove per packet
void MtkWlanNativeIf::completeTx() {
    uint32_t done = 0;
    while (done < pendingCount && txComp) {
        if (txComp->enqueuePackets(&pendingTx[done], 1, 0) != kIOReturnSuccess) break;
        done++;
    }
    if (!done) return;
    pendingCount -= done;
    if (pendingCount) memmove(pendingTx, pendingTx + done, pendingCount * sizeof(pendingTx[0]));
    txCompleted += done;
}

// 0.4.11: the Tx queues restart on the tick after the core frees a slot (or completions free pending room), not on the
// 10 ms control poll; the 10 ms wait capped upload near 190 Mbit/s (128 frames per poll)
void MtkWlanNativeIf::restartTx() {
    bool space;
    IOLockLock(lock);
    space = txSpace;
    txSpace = false;
    IOLockUnlock(lock);
    if (txPendingFull && pendingCount < 512 - kTxBatch) { txPendingFull = false; space = true; }
    if (space && dpEnabled)
        for (uint32_t i = 0; i < kNumTx; i++) txSub[i]->requestDequeue(nullptr, 1);
}

// Ring -> pool packets -> Rx completion queue. Allocation acquires a handle; prepareWithQueue materialises the buffer for the
// copy; the enqueue finalises the packet for the stack.
void MtkWlanNativeIf::drainRx() {
    for (uint32_t batch = 0; batch < kRxBatch; batch++) {
        RxFrame f;
        IOLockLock(lock);
        bool ready = rxHead != rxTail;
        if (ready) { f = rxRing[rxTail]; rxTail = (rxTail + 1) % kRxRing; }
        IOLockUnlock(lock);
        if (!ready) break;
        IOSkywalkPacket *p = nullptr;
        IOReturn r = pool->allocatePacket(1, &p, 1);
        const char *step = "allocate";
        if (r == kIOReturnSuccess && p) {
            step = "prepare";
            r = p->prepareWithQueue(nullptr, 0, 0);
            void *data = r == kIOReturnSuccess && p->getPacketBufferCount() == 1 ? p->getDataVirtualAddress() : nullptr;
            if (r == kIOReturnSuccess) { step = "buffer"; if (!data || p->getBufferSize() < f.length) r = kIOReturnNoSpace; }
            if (r == kIOReturnSuccess) {
                memcpy(data, f.bytes, f.length);
                step = "length";
                r = p->setDataOffsetAndLength(0, f.length);
            }
            if (r == kIOReturnSuccess) {
                step = "metadata";
                IOSkywalkNetworkPacket *np = static_cast<IOSkywalkNetworkPacket *>(p);
                uint32_t l2 = 14;
                uint16_t type = rd16(f.bytes + 12);
                for (uint32_t tag = 0; tag < 2 && (type == 0x8100 || type == 0x88a8); tag++) {
                    if (f.length < l2 + 4) { r = kIOReturnBadArgument; break; }
                    type = rd16(f.bytes + l2 + 2);
                    l2 += 4;
                }
                bool broadcast = true;
                for (uint32_t b = 0; b < 6; b++) if (f.bytes[b] != 0xff) broadcast = false;
                if (r == kIOReturnSuccess) r = np->setHeadroom(0);
                if (r == kIOReturnSuccess) r = np->setLinkHeaderLength((uint8_t)l2);
                // the setters set the flag whatever the argument: call them only when the flag is true
                if (r == kIOReturnSuccess && broadcast) r = np->setIsLinkBroadcast(true);
                else if (r == kIOReturnSuccess && (f.bytes[0] & 1)) r = np->setIsLinkMulticast(true);
                if (r == kIOReturnSuccess) r = np->setRxChecksumInfo(0, 0);   // no hardware validation claimed
            }
            if (r == kIOReturnSuccess) { step = "enqueue"; r = rxComp->enqueuePackets(&p, 1, 0); }
            if (r != kIOReturnSuccess) pool->deallocatePacket(p);
        }
        if (r == kIOReturnSuccess && p) {
            if (++rxDelivered == 1) nativeTrace(ctl, "dp:first RX delivered");
        } else {
            IOLockLock(lock);
            rxDropped++;
            IOLockUnlock(lock);
            if (rxFailTraced++ < 8) nativeTrace(ctl, "dp:RX failed step=%s rc=%x len=%u", step, r, f.length);
        }
    }
}

bool MtkWlanNativeIf::linkUp() const {
    bool up;
    IOLockLock(lock);
    up = radioUp;
    IOLockUnlock(lock);
    return up;
}

// Every 10 ms on the work queue: apply what the core flagged. The link state reaches the Family here (setLinkState) and the
// BSD layer through the controller's setLinkStatus. Scan and join events are stage C / D work (MtkWlanNativeReq.cpp).
void MtkWlanNativeIf::pollControl() {
    bool dirty, up;
    uint64_t speed;
    IOLockLock(lock);
    dirty = linkDirty;
    linkDirty = false;
    up = radioUp;
    speed = radioSpeed;
    if (dirty && up) txSpace = true;     // frames refused while the link was down are asked for again
    IOLockUnlock(lock);
    if (dirty && ctl) {
        ctl->setLinkStatus(kIONetworkLinkValid | (up ? kIONetworkLinkActive : 0), up ? ctl->getSelectedMedium() : nullptr, up ? speed : 0);
        bool ok = IO80211InfraInterface::setLinkState((IO80211LinkState)(up ? 2 : 1), 0, false, 0, 0);
        nativeTrace(ctl, "dp:link=%u family=%u state=%u", up, ok, IO80211InfraInterface::linkState());
        setProperty("MtkNativeRadioLink", up);
    }
}

void MtkWlanNativeIf::tick(OSObject *owner, IOTimerEventSource *) {
    MtkWlanNativeIf *i = (MtkWlanNativeIf *)owner;
    if (!i->bound || !i->dpTimer) return;
    i->completeTx();
    if (i->dpEnabled) i->drainRx();
    if ((++i->ticks % (10 / kTickMs)) == 0) i->pollControl();
    i->restartTx();
    if ((i->ticks % 250) == 0) i->followAddress();   // twice a second: the BSD address the core must answer to
    if ((i->ticks % (10 / kTickMs)) == 0) i->serviceScan();
    if ((i->ticks % (10 / kTickMs)) == 0) i->serviceJoin();
    if ((i->ticks % 500) == 0) {                 // once a second at the 2 ms tick
        uint32_t received, dropped, full, oversize, peak;
        IOLockLock(i->lock);
        received = i->rxReceived; dropped = i->rxDropped; full = i->rxRingFull; oversize = i->rxOversize; peak = i->rxPeak;
        IOLockUnlock(i->lock);
        char s[160];
        snprintf(s, sizeof(s), "rx %u delivered %u dropped %u ringfull %u oversize %u peak %u", received, i->rxDelivered, dropped, full, oversize, peak);
        i->setProperty("MtkNativeRx", s);
        snprintf(s, sizeof(s), "tx %u dropped %u stalled %u completed %u pending %u", i->txSent, i->txDropped, i->txStalled, i->txCompleted, i->pendingCount);
        i->setProperty("MtkNativeTx", s);
    }
    i->dpTimer->setTimeoutMS(i->dpEnabled ? kTickMs : 10);
}
