// SPDX-License-Identifier: BSD-3-Clause-Clear
// Copyright (c) 2026 thanhnndev and contributors
// itlmt7902 - Ethernet interface on top of the associated MT7902 link (the "network card" macOS sees).
// Created by itlmt7902::m3Link() after the 4-way handshake; frames to send arrive in outputPacket(), frames received from the AP are
// pushed in with rxPacket(). The 802.11 <-> Ethernet conversion is done in itlmt7902_m3.cpp.
// Output goes through an IOGatedOutputQueue: without a queue the stack calls outputPacket() from several threads at once (they
// shared txb), and a frame that found the TX pool full was dropped while reporting success (25 % of the frames under load).
// Now such a frame stalls the queue, and the driver restarts it with txKick() once the firmware returns buffers.
#include "itlmt7902.hpp"
#ifdef MTK_WITH_MT7902
#include "Mtk7902Backend.hpp"
#endif

#include <IOKit/IOWorkLoop.h>
#include <IOKit/network/IOEthernetController.h>
#include <IOKit/network/IOEthernetInterface.h>
#include <IOKit/network/IONetworkMedium.h>
#include <IOKit/network/IOOutputQueue.h>
#include <IOKit/network/IOGatedOutputQueue.h>
#include <sys/kpi_mbuf.h>
#include <string.h>

class itlmt7902Eth : public IOEthernetController {
    OSDeclareDefaultStructors(itlmt7902Eth)

public:
    bool setup(itlmt7902 *d, const uint8_t *m);
    void rxPacket(const uint8_t *e, uint32_t len);
    void setLink(bool up);
    void txKick();
    void flush();

    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    void free() override;
    bool createWorkLoop() override;
    IOWorkLoop *getWorkLoop() const override;
    IOOutputQueue *createOutputQueue() override;
    IOReturn getHardwareAddress(IOEthernetAddress *addr) override;
    IOReturn enable(IONetworkInterface *netif) override;
    IOReturn disable(IONetworkInterface *netif) override;
    UInt32 outputPacket(mbuf_t m, void *param) override;
    IOReturn setPromiscuousMode(bool active) override;
    IOReturn setMulticastMode(bool active) override;
    IOReturn setMulticastList(IOEthernetAddress *addrs, UInt32 count) override;
    IOReturn getPacketFilters(const OSSymbol *group, UInt32 *filters) const override;

private:
    itlmt7902 *drv = nullptr;
    uint8_t addr[6] = {0};
    IOEthernetInterface *netif = nullptr;
    IOWorkLoop *wl = nullptr;
    IONetworkMedium *medium = nullptr;
    uint8_t txb[1600];                    // outputPacket() runs under the queue's command gate, one frame at a time
    static const UInt32 kTxQueueLen = 512;
    uint32_t rxQueued = 0;                // frames queued on netif since the last flush()
};

#define super IOEthernetController
OSDefineMetaClassAndStructors(itlmt7902Eth, IOEthernetController)

bool itlmt7902Eth::setup(itlmt7902 *d, const uint8_t *m)
{
    drv = d;
    memcpy(addr, m, 6);
    return true;
}

bool itlmt7902Eth::createWorkLoop()
{
    if (!wl) wl = IOWorkLoop::workLoop();
    return wl != nullptr;
}

IOWorkLoop *itlmt7902Eth::getWorkLoop() const
{
    return wl;
}

IOOutputQueue *itlmt7902Eth::createOutputQueue()
{
    if (!createWorkLoop()) return nullptr;
    return IOGatedOutputQueue::withTarget(this, wl, kTxQueueLen);
}

bool itlmt7902Eth::start(IOService *provider)
{
    if (!super::start(provider)) return false;

    UInt64 speed = drv ? drv->linkSpeedBps() : 54ULL * 1000000ULL;      // estimated PHY rate (54 Mb/s unless HT/VHT is in use)
    OSDictionary *media = OSDictionary::withCapacity(1);
    IONetworkMedium *med = IONetworkMedium::medium(kIOMediumEthernetAuto, speed);
    if (!media || !med) {
        if (med) med->release();
        if (media) media->release();
        return false;
    }
    IONetworkMedium::addMedium(media, med);
    bool ok = publishMediumDictionary(media) && setCurrentMedium(med) && setSelectedMedium(med);
    if (ok) setLinkStatus(kIONetworkLinkValid); // publish link only after session state is ready
    medium = med;                                                          // kept for setLink()
    medium->retain();
    med->release();
    media->release();
    if (!ok) return false;

    if (!attachInterface((IONetworkInterface **)&netif, true)) return false;
    if (IOOutputQueue *q = getOutputQueue()) q->start();
    registerService();
    return true;
}

void itlmt7902Eth::stop(IOService *provider)
{
    if (netif) { detachInterface(netif, true); netif = nullptr; }
    super::stop(provider);
}

void itlmt7902Eth::free()
{
    if (medium) { medium->release(); medium = nullptr; }
    if (wl) { wl->release(); wl = nullptr; }
    super::free();
}

IOReturn itlmt7902Eth::getHardwareAddress(IOEthernetAddress *a)
{
    memcpy(a->bytes, addr, 6);
    return kIOReturnSuccess;
}

IOReturn itlmt7902Eth::enable(IONetworkInterface *)
{
    if (IOOutputQueue *q = getOutputQueue()) q->start();
    return kIOReturnSuccess;
}

IOReturn itlmt7902Eth::disable(IONetworkInterface *)
{
    if (IOOutputQueue *q = getOutputQueue()) { q->stop(); q->flush(); }
    return kIOReturnSuccess;
}

IOReturn itlmt7902Eth::setPromiscuousMode(bool)
{
    return kIOReturnSuccess;
}

IOReturn itlmt7902Eth::setMulticastMode(bool)
{
    return kIOReturnSuccess;
}

IOReturn itlmt7902Eth::setMulticastList(IOEthernetAddress *, UInt32)
{
    return kIOReturnSuccess;
}

IOReturn itlmt7902Eth::getPacketFilters(const OSSymbol *group, UInt32 *filters) const
{
    if (group == gIONetworkFilterGroup) {
        *filters = kIOPacketFilterUnicast | kIOPacketFilterBroadcast | kIOPacketFilterMulticast | kIOPacketFilterPromiscuous;
        return kIOReturnSuccess;
    }
    return super::getPacketFilters(group, filters);
}

UInt32 itlmt7902Eth::outputPacket(mbuf_t m, void *)
{
    if (!m) return kIOReturnOutputDropped;
    size_t len = mbuf_pkthdr_len(m);
    int r = -1;
    if (len >= 14 && len <= sizeof(txb) && drv) {
        mbuf_copydata(m, 0, len, txb);
        r = drv->ethTx(txb, (uint32_t)len);
    }
    if (r > 0) return kIOReturnOutputStall;                               // TX pool full: the queue keeps m and retries after txKick()
    freePacket(m);
    return r == 0 ? kIOReturnOutputSuccess : kIOReturnOutputDropped;
}

void itlmt7902Eth::txKick()
{
    if (IOOutputQueue *q = getOutputQueue()) q->service(IOBasicOutputQueue::kServiceAsync);
}

// Link state for macOS: down while the driver is between networks, so DHCP and the routes follow the Wi-Fi link.
void itlmt7902Eth::setLink(bool up)
{
    if (!medium) return;
    UInt64 speed = drv ? drv->linkSpeedBps() : 54ULL * 1000000ULL;
    setLinkStatus(kIONetworkLinkValid | (up ? kIONetworkLinkActive : 0), up ? medium : nullptr, up ? speed : 0);
}

void itlmt7902Eth::rxPacket(const uint8_t *e, uint32_t len)
{
    if (!netif || len < 14) return;
    mbuf_t m = allocatePacket(len);
    if (!m) return;
    memcpy(mbuf_data(m), e, len);
    netif->inputPacket(m, len, IONetworkInterface::kInputOptionQueuePacket);
    if (++rxQueued >= 64) flush();
}

// One wake-up of the stack per batch instead of one per frame (the driver calls it after each pass over the rings).
void itlmt7902Eth::flush()
{
    if (!netif || !rxQueued) return;
    rxQueued = 0;
    netif->flushInputQueue();
}

// ---- driver side ----------------------------------------------------------------------------------------------------------------
bool itlmt7902::ethCreate()
{
    itlmt7902Eth *e = OSTypeAlloc(itlmt7902Eth);
    if (!e) return false;
    if (!e->init(nullptr) || !e->setup(this, mac)) { e->release(); return false; }
    if (!e->attach(this)) { e->release(); return false; }
    if (!e->start(this)) { e->detach(this); e->release(); return false; }
    eth = e;
    return true;
}

void itlmt7902::ethRx(const uint8_t *frame, uint32_t len)
{
#ifdef MTK_WITH_MT7902
    if (mtkBridge) { mtkBridge->receive(frame, len); return; }
#endif
    if (nativeMode && nativeLock) {
        IOLockLock(nativeLock);
        if (nativeSink.owner && nativeSink.receive) nativeSink.receive(nativeSink.owner, frame, len);
        IOLockUnlock(nativeLock);
        return;
    }
    if (eth) eth->rxPacket(frame, len);
}

void itlmt7902::ethLink(bool up)
{
#ifdef MTK_WITH_MT7902
    if (mtkBridge) {
        IOLockLock(mtkBridge->lock);
        if (!up) { mtkBridge->tx.clear(); mtkBridge->rx.clear(); }
        IOLockUnlock(mtkBridge->lock);
        mtkBridge->lastPublish = 0; mtkBridge->publish();
        return;
    }
#endif
    if (nativeMode && nativeLock) {
        IOLockLock(nativeLock);
        ItlNativeSink sink = nativeSink;
        if (sink.owner && sink.lifetime && sink.link) { sink.owner->retain(); sink.lifetime->retain(); }
        else sink.owner = nullptr;
        IOLockUnlock(nativeLock);
        // Family link updates can sleep on the native gate. Never hold bridge
        // lock while entering it: gated stop/unbind acquires that lock.
        if (sink.owner) {
            sink.link(sink.owner, up, up ? linkSpeedBps() : 0);
            sink.owner->release(); sink.lifetime->release();
        }
        return;
    }
    if (!eth) return;
    eth->setLink(up);
    eth->txKick();                                                         // a stalled queue drains (link down: drops) or resumes
}

void itlmt7902::ethFlush()
{
#ifdef MTK_WITH_MT7902
    if (mtkBridge) { mtkBridge->workerPoll(); return; }
#endif
    if (eth) eth->flush();
}

void itlmt7902::ethTxKick()
{
    if (eth) eth->txKick();
}

IOReturn itlmt7902::callPlatformFunction(const OSSymbol *, bool, void *, void *, void *, void *)
{
    return kIOReturnUnsupported; // Native frontend is outside the bootstrap scope.
}
