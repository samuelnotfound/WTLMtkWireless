// SPDX-License-Identifier: BSD-3-Clause-Clear
// The hardware worker owns all Connac2 registers, rings, firmware and supplicant state. The Mtk gate only
// copies bounded packets/commands/snapshots. Never enter a frontend callback holding a backend lock.
#include "MtkWlan.hpp"
#include "Mtk7902Backend.hpp"
#include "Mtk7902Protocol.h"
#include "../experimental/MT7902Port/src/itlmt7902.hpp"
#include "../experimental/MT7902Port/codec/mt7902_join_codec.h"
#include <libkern/c++/OSData.h>

Mtk7902Backend *Mtk7902Backend::create(MtkWlan *owner) {
    auto *b = static_cast<Mtk7902Backend *>(IOMalloc(sizeof(Mtk7902Backend)));
    if (!b) return nullptr;
    bzero(b, sizeof(*b));
    b->core = owner;
    b->lock = IOLockAlloc();
    if (!b->lock) { IOFree(b, sizeof(*b)); return nullptr; }
    b->status.version = MTK_USER_API_VERSION;
    return b;
}

bool Mtk7902Backend::start(IOService *provider) {
    radio = OSTypeAlloc(itlmt7902);
    if (!radio) return false;
    if (!radio->init()) return false;
    radio->mtkBridge = this;
    if (!radio->attach(provider)) return false;
    radioStarted = radio->start(provider);
    // start's failure path stops any partially initialized radio; destroy still detaches/releases it.
    return radioStarted;
}

void Mtk7902Backend::stop() {
    IOLockLock(lock);
    stopping = true;
    ready = false;
    pending = 0;
    bzero(&joinRequest, sizeof(joinRequest));
    rxTail = rxHead; txTail = txHead;
    IOLockUnlock(lock);
    if (radioStarted) { radio->stop(core); radioStarted = false; }
}

void Mtk7902Backend::destroy() {
    stop();
    if (radio) {
        radio->mtkBridge = nullptr; // the worker was joined before this pointer is removed
        if (radio->getProvider()) radio->detach(core);
        radio->release(); radio = nullptr;
    }
    IOLockFree(lock);
    bzero(this, sizeof(*this));
    IOFree(this, sizeof(*this));
}

void Mtk7902Backend::receive(const uint8_t *frame, uint32_t len) {
    if (len < 14 || len > sizeof(Packet::data)) return;
    IOLockLock(lock);
    if (!stopping && rxHead - rxTail < QueueSize) {
        Packet &p = rx[rxHead++ % QueueSize];
        p.len = len; memcpy(p.data, frame, len);
    } else ++rxDrops;
    IOLockUnlock(lock);
}

void Mtk7902Backend::publish(bool networks) {
    // Worker only: all radio fields below have a single writer. The join mailbox is copied elsewhere.
    const uint32_t now = radio->nowMs();
    if (!networks && now - lastPublish < 10) return;
    lastPublish = now;
    uint8_t station[6] = {};
    OSData *address = OSDynamicCast(OSData, radio->copyProperty("IOMACAddress"));
    bool haveMac = address && address->getLength() == 6;
    if (haveMac) memcpy(station, address->getBytesNoCopy(), 6);
    OSSafeReleaseNULL(address);
    IOLockLock(lock);
    if (stopping) { IOLockUnlock(lock); return; }
    ready = __atomic_load_n(&radio->joinControlReady, __ATOMIC_ACQUIRE);
    if (!macReady && haveMac) {
        memcpy(mac, station, 6); macReady = true;
    }
    status.version = MTK_USER_API_VERSION;
    status.scanning = __atomic_load_n(&radio->scanBusy, __ATOMIC_ACQUIRE);
    status.link_up = status.port_open = radio->m3linkUp && !radio->firmwareFault;
    status.state = status.link_up ? MTK_ST_CONNECTED : radio->njState == ITL_NATIVE_CONNECTING ? MTK_ST_JOINING :
                   status.scanning ? MTK_ST_SCANNING : MTK_ST_IDLE;
    if (radio->njState == ITL_NATIVE_FAILED && status.last_result == MTK_JOIN_PENDING) {
        status.last_result = mtk7902_join_result(radio->m3res);
        status.want_active = false;
        ++status.join_failures;
    } else if (status.link_up) status.last_result = MTK_JOIN_OK;
    status.channel = radio->tgtCh;
    status.rssi = radio->tgtRssi ? radio->tgtRssi : -127;
    memcpy(status.bssid, radio->tgtBssid, 6);
    status.ssid_len = (uint8_t)strnlen(radio->m3ssid, 32);
    memcpy(status.ssid, radio->m3ssid, status.ssid_len);
    status.ssid[status.ssid_len] = 0;
    status.security = MTK_SEC_RSN;
    status.speed = status.link_up ? radio->linkSpeedBps() : 0;
    generation = radio->scanGen;
    status.scans = generation;
    if (networks) {
        count = 0;
        for (int i = 0; i < radio->nAps && count < MTK_SCAN_MAX; ++i) {
            const auto &a = radio->aps[i];
            if (!a.ssid[0] || now - a.seenMs > 180000) continue;
            auto &r = results[count++]; bzero(&r, sizeof(r));
            memcpy(r.bssid, a.bssid, 6);
            r.ssid_len = (uint8_t)strnlen(a.ssid, 32);
            memcpy(r.ssid, a.ssid, r.ssid_len);
            r.channel = a.ch; r.rssi = a.rssi ? a.rssi : -127; r.age_ms = now - a.seenMs;
            r.security = a.sec < 3 ? a.sec : MTK_SEC_RSN;
            for (unsigned off = 0; off + 2 <= a.ieLen;) {
                unsigned len = 2u + a.ie[off + 1];
                if (len > a.ieLen - off) break;
                if (a.ie[off] == 48 && mt7902_join_rsn(a.ie + off, len)) {
                    r.akm = MTK_AKM_PSK; r.pairwise = r.group = MTK_CIPHER_CCMP;
                    if (mt7902_join_channel(a.ch)) r.flags |= MTK_BSS_JOINABLE;
                    break;
                }
                off += len;
            }
        }
        // Preserve stable best-first ordering for the C API.
        for (uint32_t i = 1; i < count; ++i) {
            auto v = results[i]; uint32_t j = i;
            while (j && results[j - 1].rssi < v.rssi) { results[j] = results[j - 1]; --j; }
            results[j] = v;
        }
        status.bss_count = count;
    }
    IOLockUnlock(lock);
}

void Mtk7902Backend::workerPoll() {
    uint32_t commands;
    mtk_join_params request;
    IOLockLock(lock);
    commands = stopping ? 0 : pending; pending = 0;
    request = joinRequest; bzero(&joinRequest, sizeof(joinRequest));
    IOLockUnlock(lock);
    if (commands & 4) radio->ucDisconnect();
    if (commands & 2) {
        ItlNativeJoin j = {};
        j.version = 1; j.ssidLen = request.ssid_len;
        memcpy(j.ssid, request.ssid, j.ssidLen); memcpy(j.pmk, request.pmk, 32);
        IOReturn rc = radio->nativeJoin(&j); bzero(&j, sizeof(j));
        IOLockLock(lock);
        status.last_result = rc == kIOReturnSuccess ? MTK_JOIN_PENDING : MTK_JOIN_ROC;
        IOLockUnlock(lock);
    }
    bzero(&request, sizeof(request));
    if (commands & 1) {
        IOReturn rc = radio->ucScan();
        if (rc != kIOReturnSuccess) {
            IOLockLock(lock); scanPending = false; IOLockUnlock(lock);
            // Cache can still be read. scan() disallows on-link/off-channel scans up front.
        }
    }
    for (unsigned budget = 0; budget < 16; ++budget) {
        Packet p;
        uint32_t ticket;
        IOLockLock(lock);
        if (stopping || txHead == txTail) { IOLockUnlock(lock); break; }
        ticket = txTail; p = tx[ticket % QueueSize];
        IOLockUnlock(lock);
        int rc = radio->ethTx(p.data, p.len);
        if (rc == 1) break; // retain until TXFREE; never hand DMA buffers back on a timeout
        IOLockLock(lock);
        // The gate can discard the queue during a new join/disconnect. Do not consume a newer frame.
        if (txTail == ticket) ++txTail;
        IOLockUnlock(lock);
    }
    publish();
}

int Mtk7902Backend::scan() {
    IOLockLock(lock);
    int rc = !ready || stopping || status.link_up || joinPending || scanPending ? -2 : 0;
    if (!rc) { pending |= 1; scanPending = true; scanStartGen = generation; }
    IOLockUnlock(lock);
    return rc;
}

int Mtk7902Backend::join(const mtk_join_params *p) {
    if (!mtk7902_valid_join(p)) return -1;
    IOLockLock(lock);
    if (!ready || stopping) { IOLockUnlock(lock); return -2; }
    joinRequest = *p; pending = (pending & ~4u) | 2u;
    joinPending = true; status.want_active = true; status.last_result = MTK_JOIN_PENDING; ++status.joins;
    txTail = txHead; rxTail = rxHead;
    IOLockUnlock(lock);
    return 0;
}

int Mtk7902Backend::disconnect() {
    IOLockLock(lock);
    if (!ready || stopping) { IOLockUnlock(lock); return -2; }
    pending = (pending & ~2u) | 4u; bzero(&joinRequest, sizeof(joinRequest));
    joinPending = false; status.want_active = false; status.last_result = MTK_JOIN_CANCELLED;
    txTail = txHead; rxTail = rxHead;
    IOLockUnlock(lock);
    return 0;
}

int Mtk7902Backend::transmit(const uint8_t *frame, uint32_t len) {
    if (!frame || len < 14 || len > sizeof(Packet::data)) return MTK_TX_DROPPED;
    IOLockLock(lock);
    int rc = MTK_TX_DROPPED;
    if (!stopping && status.port_open && !(pending & 6)) {
        rc = MTK_TX_STALL;
        if (txHead - txTail < QueueSize) {
            auto &p = tx[txHead++ % QueueSize]; p.len = len; memcpy(p.data, frame, len); rc = MTK_TX_OK;
        }
    }
    IOLockUnlock(lock);
    return rc;
}

void Mtk7902Backend::snapshot(mtk_status *out) {
    IOLockLock(lock); *out = status; IOLockUnlock(lock);
}

uint32_t Mtk7902Backend::getResults(mtk_scan_result *out, uint32_t max) {
    IOLockLock(lock);
    uint32_t n = count < max ? count : max;
    memcpy(out, results, n * sizeof(*out));
    IOLockUnlock(lock);
    return n;
}

void Mtk7902Backend::tick() {
    core->timer->setTimeoutMS(5);
    ++core->ticks;
    bool notifyScan = false, notifyJoin = false, startFrontEnd = false;
    mtk_status s;
    IOLockLock(lock);
    if (stopping) { IOLockUnlock(lock); return; }
    s = status;
    if (macReady && ready && !core->feFallbackDone) {
        memcpy(core->mac, mac, 6); core->feFallbackDone = true; startFrontEnd = true;
    }
    if (scanPending && generation != scanStartGen) { scanPending = false; notifyScan = true; }
    if (joinPending && !(pending & 2) && s.last_result != MTK_JOIN_PENDING) { joinPending = false; notifyJoin = true; }
    IOLockUnlock(lock);
    core->linkUp = s.link_up; core->portOpen = s.port_open;
    core->reportLink();
    if (startFrontEnd) thread_call_enter(core->feFallbackCall);
    if (notifyScan && core->feScanDone) core->feScanDone(core->fe.ctx, 0, s.bss_count);
    if (notifyJoin && core->feJoinResult) core->feJoinResult(core->fe.ctx, (int)s.last_result, s.bssid);
    for (unsigned budget = 0; budget < QueueSize; ++budget) {
        Packet p;
        IOLockLock(lock);
        if (rxHead == rxTail) { IOLockUnlock(lock); break; }
        p = rx[rxTail++ % QueueSize];
        IOLockUnlock(lock);
        if (!core->ifEnabled || !s.port_open || !core->fe.rx_alloc) continue;
        mbuf_t m = core->fe.rx_alloc(core->fe.ctx, p.len);
        if (!m) continue;
        if (mbuf_copyback(m, 0, p.len, p.data, MBUF_DONTWAIT)) { mbuf_freem(m); continue; }
        mbuf_pkthdr_setlen(m, p.len);
        core->fe.rx(core->fe.ctx, m);
    }
    if (core->fe.rx_flush) core->fe.rx_flush(core->fe.ctx);
    if (core->fe.tx_space) core->fe.tx_space(core->fe.ctx);
    // Only public diagnostics are mirrored; credentials and the command mailbox are never published.
    if (!(core->ticks % 200)) {
        const char *keys[] = {"wifi-state", "wifi-error", "wifi-networks", "wifi-ssid", "wifi-channel", "wifi-rssi",
            "wifi-rate", "wifi-scan-gen", "wifi-events", "scan-supported", "join-supported", "firmware-ready",
            "m0-log", "m1-log", "m2-log", "m3-log", "m3-res", "wifi-attempt-history"};
        for (const char *key : keys) {
            if (OSObject *v = radio->copyProperty(key)) { core->setProperty(key, v); v->release(); }
        }
        core->setNum("Mtk7902RxQueueDrops", rxDrops);
    }
}
