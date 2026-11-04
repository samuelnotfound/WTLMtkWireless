// <wtl> project: mtkwlannative | file: src/MtkWlanNativeReq.cpp | author: watchthelight </wtl>
// SPDX-License-Identifier: ISC
// Request handlers of the interface with layouts read from Apple's driver (apple80211_* payloads are the userland ioctl
// ABI of IOKit/80211/apple80211_ioctl.h; the offsets below were taken from AppleBCMWLANCore's handlers on 24H32 by
// ga403wm-hackintosh and re-checked in the 25G241 dump where the handler is dumped). Everything not implemented here is
// answered by the generated stubs at the end of this file (kIOReturnUnsupported, counted per slot).
#include "MtkWlanNativeIf.hpp"
#include "MtkWlanNative.hpp"
#include "MtkNativeTrace.hpp"
#include "MtkChanspec.h"
#include <IOKit/IOLib.h>
#include <string.h>

namespace {
template <typename T>
inline void store(void *d, size_t off, T v) { memcpy((uint8_t *)d + off, &v, sizeof(v)); }
template <typename T>
inline T load(const void *d, size_t off) { T v; memcpy(&v, (const uint8_t *)d + off, sizeof(v)); return v; }
// what Apple's getRSSI / getRATE return while not associated, and getCHANNEL while no current channel can be read
const IOReturn kNotAssociated = (IOReturn)0xe0822403;
const IOReturn kNoChannel = (IOReturn)0xe0822801;
// apple80211_channel flags: band 0x8 = 2.4 GHz, 0x10 = 5 GHz; 0x2 = 20 MHz; 0x80 = active scan allowed
inline uint32_t channelFlags(uint32_t ch) { return (ch > 14 ? 0x10u : 0x8u) | 0x2u; }
// the current channel as ChanSpecConvToApple80211Channel writes it (25G241): 0x4 40 MHz (+0x200 extension above),
// 0x400 80 MHz, 0x100 DFS (52-144)
inline uint32_t linkChannelFlags(const mtk_link_info &li) {
    uint32_t f = li.channel > 14 ? 0x10u : 0x8u;
    if (li.channel >= 52 && li.channel <= 144) f |= 0x100;
    if (li.bw == 2) f |= 0x400;
    else if (li.bw == 1) f |= 0x4u | (li.center > li.channel ? 0x200u : 0u);
    else f |= 0x2;
    return f;
}
} // namespace

// the core's rate details (v5 core) while associated
bool MtkWlanNativeIf::rateInfo(mtk_rate_info *r) {
    bzero(r, sizeof(*r));
    r->size = sizeof(*r);
    return ops5 && linkUp() && ops5->get_rate_info(core, r) == 0;
}

// apple80211_channel_data: u32 version, then apple80211_channel {u32 version, u32 channel, u32 flags} at +4
IOReturn MtkWlanNativeIf::getCHANNEL(apple80211_channel_data *d) {
    nativeCall(ctl, 'S', 471);
    if (!d) return kIOReturnBadArgument;
    mtk_link_info li;
    if (!linkUp() || ops->v2.v1.get_link_info(core, &li) != 0 || !li.up) return kNoChannel;
    store<uint32_t>(d, 0, 1);
    store<uint32_t>(d, 4, 1);
    store<uint32_t>(d, 8, li.channel);
    store<uint32_t>(d, 12, linkChannelFlags(li));
    return kIOReturnSuccess;
}

// [data+4] = stored power-save mode; never configured here, so 0
IOReturn MtkWlanNativeIf::getPOWERSAVE(apple80211_powersave_data *d) {
    nativeCall(ctl, 'S', 472);
    if (!d) return kIOReturnBadArgument;
    store<uint32_t>(d, 0, 1);
    store<uint32_t>(d, 4, 0);
    return kIOReturnSuccess;
}

// [data+8] = current rate in Mb/s; not associated -> not-associated error
IOReturn MtkWlanNativeIf::getRATE(apple80211_rate_data *d) {
    nativeCall(ctl, 'S', 474);
    if (!d) return kIOReturnBadArgument;
    mtk_link_info li;
    if (!linkUp() || ops->v2.v1.get_link_info(core, &li) != 0 || !li.up) return kNotAssociated;
    store<uint32_t>(d, 0, 1);
    store<uint32_t>(d, 4, 1);
    mtk_rate_info r;
    store<uint32_t>(d, 8, rateInfo(&r) ? r.tx_kbps / 1000 : (uint32_t)(li.speed / 1000000ull));
    return kIOReturnSuccess;
}

// AppleBCMWLANCore (25G241) reads the firmware's nrate: getGUARD_INTERVAL [+4] = ns (800 / 400) for HT/VHT; getMCS
// [+4] = the HT index (streams included) or the VHT MCS, nothing for legacy; getMCS_VHT for VHT only: [+4] MCS, [+8] NSS,
// [+0xc] width in MHz, [+0x10] guard interval in ns. All return success when associated.
IOReturn MtkWlanNativeIf::getGUARD_INTERVAL(apple80211_guard_interval_data *d) {
    nativeCall(ctl, 'S', 478);
    if (!d) return kIOReturnBadArgument;
    mtk_rate_info r;
    if (!rateInfo(&r)) return kNotAssociated;
    store<uint32_t>(d, 0, 1);
    store<uint32_t>(d, 4, r.short_gi ? 400u : 800u);
    return kIOReturnSuccess;
}

IOReturn MtkWlanNativeIf::getMCS(apple80211_mcs_data *d) {
    nativeCall(ctl, 'S', 479);
    if (!d) return kIOReturnBadArgument;
    mtk_rate_info r;
    if (!rateInfo(&r)) return kNotAssociated;
    store<uint32_t>(d, 0, 1);
    if (r.phy == MTK_PHY_HT) store<uint32_t>(d, 4, (uint32_t)(r.mcs + 8 * (r.nss ? r.nss - 1 : 0)));
    else if (r.phy >= MTK_PHY_VHT) store<uint32_t>(d, 4, r.mcs);
    return kIOReturnSuccess;
}

IOReturn MtkWlanNativeIf::getMCS_VHT(apple80211_mcs_vht_data *d) {
    nativeCall(ctl, 'S', 482);
    if (!d) return kIOReturnBadArgument;
    mtk_rate_info r;
    if (!rateInfo(&r)) return kNotAssociated;
    store<uint32_t>(d, 0, 1);
    if (r.phy == MTK_PHY_VHT) {
        store<uint32_t>(d, 4, r.mcs);
        store<uint32_t>(d, 8, r.nss);
        store<uint32_t>(d, 0xc, r.bw_mhz);
        store<uint32_t>(d, 0x10, r.short_gi ? 400u : 800u);
    }
    return kIOReturnSuccess;
}

// qword at +0 = 1 (version 1, mode word 0), then +4 |= APPLE80211_M_STA (1) when associated
IOReturn MtkWlanNativeIf::getOP_MODE(apple80211_opmode_data *d) {
    nativeCall(ctl, 'S', 475);
    if (!d) return kIOReturnBadArgument;
    store<uint32_t>(d, 0, 1);
    store<uint32_t>(d, 4, linkUp() ? 1u : 0u);
    return kIOReturnSuccess;
}

// associated -> 0x30 bytes at +4 cleared, [+0] = 1, number of chains at +4, per-chain RSSI (four dwords) at +0xc, their
// average at +0x1c; not associated -> not-associated error. The core reports one combined value: chain 0, one chain.
IOReturn MtkWlanNativeIf::getRSSI(apple80211_rssi_data *d) {
    nativeCall(ctl, 'S', 476);
    if (!d) return kIOReturnBadArgument;
    mtk_status st;
    if (!linkUp() || ops->v2.get_status(core, &st) != 0 || !st.link_up) return kNotAssociated;
    bzero((uint8_t *)d + 4, 0x30);
    store<uint32_t>(d, 0, 1);
    store<uint32_t>(d, 4, 1);
    store<int32_t>(d, 0xc, st.rssi);
    store<int32_t>(d, 0x1c, st.rssi);
    return kIOReturnSuccess;
}

// Apple forwards the notification to USB-related firmware iovars and returns 0 for chips without them; the MT7925 is on PCIe
IOReturn MtkWlanNativeIf::setUSB_HOST_NOTIFICATION(apple80211_usb_host_notification_data *) {
    nativeCall(ctl, 'S', 0x16b);
    return kIOReturnSuccess;
}

// the qword at +0 is stored by Apple's core as feature bits that gate firmware setup; recorded, returns 0
IOReturn MtkWlanNativeIf::setOS_FEATURE_FLAGS(apple80211_feature_flags *d) {
    nativeCall(ctl, 'S', 0x1d5);
    if (d) setProperty("MtkNativeOsFeatureFlags", load<uint64_t>(d, 0), 64);
    return kIOReturnSuccess;
}

// non-null argument -> 0, null -> kIOReturnError; nothing else is done
IOReturn MtkWlanNativeIf::setINFRA_ENUMERATED(apple80211_infra_enumerated *d) {
    nativeCall(ctl, 'S', 0x200);
    return d ? kIOReturnSuccess : kIOReturnError;
}

// apple80211_sup_channel_data: u32 version (1), u32 count, then 12-byte apple80211_channel entries {version 1, channel, flags};
// the channels come from the core's regulatory domain (every entry is one the core scans and joins)
namespace {
IOReturn fillChannels(MtkWlanNativeIf *, const mtk_core_ops_v3 *ops, void *core, void *d) {
    if (!d) return kIOReturnBadArgument;
    uint8_t list[64];
    uint32_t n = 0;
    if (ops->get_channels(core, list, sizeof(list), &n) != 0) n = 0;
    store<uint32_t>(d, 0, 1);
    store<uint32_t>(d, 4, n);
    for (uint32_t i = 0; i < n; i++) {
        store<uint32_t>(d, 8 + 12 * i, 1);
        store<uint32_t>(d, 8 + 12 * i + 4, list[i]);
        store<uint32_t>(d, 8 + 12 * i + 8, channelFlags(list[i]) | 0x80u);
    }
    return kIOReturnSuccess;
}
} // namespace

IOReturn MtkWlanNativeIf::getSUPPORTED_CHANNELS(apple80211_sup_channel_data *d) {
    nativeCall(ctl, 'S', 477);
    return fillChannels(this, ops, core, d);
}

IOReturn MtkWlanNativeIf::getHW_SUPPORTED_CHANNELS(apple80211_sup_channel_data *d) {
    nativeCall(ctl, 'S', 496);
    return fillChannels(this, ops, core, d);
}

// Slot 470 on 25G241: setMacAddress(mloAddrArray&). The Family's IO80211MacAddressAgent builds the array on its stack
// (link i = the address with byte 5 + i; the first six bytes are the address, which is what laobamac's Tahoe front end
// reads too) and applies the address to the BSD interface only when this returns 0 (setLinkLayerAddress, message 0x3b,
// the IOMACAddress property). The core re-registers its firmware station interface with the address, synchronously on
// its gate, so the private address macOS picks per network is the one the hardware accepts unicast for (I-058: it
// associated and DHCP never saw a reply while this refused and the firmware kept the factory address).
IOReturn MtkWlanNativeIf::setMacAddress(mloAddrArray &addresses) {
    nativeCall(ctl, 'S', 470);
    uint8_t m[6];
    memcpy(m, &addresses, 6);
    if ((m[0] & 1) || !(m[0] | m[1] | m[2] | m[3] | m[4] | m[5])) {
        nativeTrace(ctl, "if:setMacAddress %02x:%02x:%02x:%02x:%02x:%02x refused (not unicast)", m[0], m[1], m[2], m[3], m[4], m[5]);
        return kIOReturnBadArgument;
    }
    if (!ops4 || !ops4->set_mac || !coreLoop) return kIOReturnNotReady;
    int rc = -2;
    if (coreLoop->runAction(gatedSetMac, this, m, &rc) != kIOReturnSuccess) rc = -2;
    nativeTrace(ctl, "if:setMacAddress %02x:%02x:%02x:%02x:%02x:%02x -> %d", m[0], m[1], m[2], m[3], m[4], m[5], rc);
    if (rc == 0) memcpy(curMac, m, 6);
    // WCLJoinManager issues 0x170 before every join and tolerates success and NotPermitted (an address not applied), nothing else
    return rc == 0 ? kIOReturnSuccess : rc == -1 ? kIOReturnBadArgument : kIOReturnNotPermitted;
}

IOReturn MtkWlanNativeIf::gatedSetMac(OSObject *owner, void *mac, void *rc, void *, void *) {
    MtkWlanNativeIf *self = OSDynamicCast(MtkWlanNativeIf, owner);
    if (!self || !self->ops4 || !self->core) { *(int *)rc = -2; return kIOReturnSuccess; }
    *(int *)rc = self->ops4->set_mac(self->core, (const uint8_t *)mac);
    return kIOReturnSuccess;
}

// ---- stage C: scan requests and scan-result events ---------------------------------------------------------------------------
//
// The Family is event driven: setWCL_SCAN_REQ returns at once and the results arrive as messages posted through
// IO80211Controller::postMessage. On 25G241 (WCLMngFrameRouter::scanResultEventHandler -> WCLScanCacheStore::updateOrAddBeacon
// -> IO80211BSSBeacon::setBeaconDataFromMsg): one 0xc9 event per BSS, message = header + information elements, the IEs at
// +0x44. Header: +0 u32 ie_len, +4 u16 channel spec (d11ac: bits 0-7 center channel, 8-10 control sideband, 0x1000/0x1800/0x2000 = 20/40/80 MHz, 0xc000 = 5 GHz; MtkChanspec.h), +6 ssid[32],
// +0x26 ssid_len, +0x27 channel, +0x29 bssid[6], +0x30 s32 rssi, +0x34 s32 noise, +0x38 u16 beacon interval, +0x3a u16
// capability, +0x3c u32 (new on 25G241, read only when flag bit 18 is set: left 0), +0x40 u32 flags (2 always, 0x80 from a
// beacon rather than a probe response, 0x4000 rssi valid). 24H32 had the flags at +0x3c and the IEs at +0x40 (itlmt7925Native).
// Then 0xed, scan done, a 4-byte status (0 = ok), which WCLScanManager::scanDoneEventHandler ends the request with.
namespace {
const uint32_t kEventScanResult = 0xc9, kEventScanDone = 0xed, kBeaconHeader = 0x44;   // == MtkWlanNativeIf::kBeaconHeader
void buildBeacon(uint8_t *m, const mtk_bss_info &b) {
    const uint32_t ieLen = b.ie_len > MTK_BSS_IE_MAX ? MTK_BSS_IE_MAX : b.ie_len;
    const uint8_t ssidLen = b.ssid_len > 32 ? 32 : b.ssid_len;
    bzero(m, kBeaconHeader);
    store<uint32_t>(m, 0, ieLen);
    store<uint16_t>(m, 4, mtkChanspec(b.channel, b.ie, ieLen));
    memcpy(m + 6, b.ssid, ssidLen);
    m[0x26] = ssidLen;
    m[0x27] = b.channel;
    memcpy(m + 0x29, b.bssid, 6);
    store<int32_t>(m, 0x30, b.rssi);
    store<uint16_t>(m, 0x38, b.beacon_interval);
    store<uint16_t>(m, 0x3a, b.capability);
    store<uint32_t>(m, 0x40, 2u | (b.probe_resp ? 0u : 0x80u) | (b.rssi ? 0x4000u : 0u));
    memcpy(m + kBeaconHeader, b.ie, ieLen);
}
} // namespace

void MtkWlanNativeIf::postEvent(uint32_t code, void *data, unsigned long length) {
    nativeCall(ctl, 'P', code);
    if (ctl) ctl->postMessage(this, code, data, length, true);
}

IOReturn MtkWlanNativeIf::gatedScan(OSObject *owner, void *rc, void *, void *, void *) {
    MtkWlanNativeIf *self = OSDynamicCast(MtkWlanNativeIf, owner);
    *(int *)rc = (self && self->ops && self->core) ? self->ops->v2.scan(self->core) : -2;
    return kIOReturnSuccess;
}

// the cached BSS table, best first, copied under the core gate into bssSnap; *count = entries copied
IOReturn MtkWlanNativeIf::gatedSnapBss(OSObject *owner, void *count, void *, void *, void *) {
    MtkWlanNativeIf *self = OSDynamicCast(MtkWlanNativeIf, owner);
    uint32_t n = 0;
    if (self && self->ops && self->core && self->bssSnap)
        while (n < kBssSnap && self->ops->get_bss(self->core, n, &self->bssSnap[n]) == 0) n++;
    *(uint32_t *)count = n;
    return kIOReturnSuccess;
}

// apple80211ScanRequest (the part AppleBCMWLANCore's handler reads, unchanged since 24H32): +0x1c u32 ssid_len, +0x20 ssid[32].
// The core scans every channel of its domain with a wildcard probe; a requested SSID only filters what is reported. A scan the
// core refuses (one already running, or the radio busy with a join) still answers from the cache.
IOReturn MtkWlanNativeIf::setWCL_SCAN_REQ(apple80211ScanRequest *d) {
    nativeCall(ctl, 'S', 603);
    if (!d) return kIOReturnBadArgument;
    if (!bound || !ops || !core || !coreLoop) return kIOReturnNotReady;
    uint32_t ssidLen = load<uint32_t>(d, 0x1c);
    if (ssidLen > 32) return kIOReturnBadArgument;
    int rc = -2;
    if (coreLoop->runAction(gatedScan, this, &rc) != kIOReturnSuccess) rc = -2;
    IOLockLock(lock);
    scanSsidLen = (uint8_t)ssidLen;
    memcpy(scanSsid, (const uint8_t *)d + 0x20, ssidLen);
    scanDone = false;
    scanActive = true;
    scanDeliverNow = rc != 0;           // not started: the cache is what there is, delivered on the next poll, never here
    scanPolls = 0;
    scansRequested++;
    IOLockUnlock(lock);
    if (scanTraced++ < 6) nativeTrace(ctl, "scan:request rc=%d ssidLen=%u", rc, ssidLen);
    return kIOReturnSuccess;
}

IOReturn MtkWlanNativeIf::setWCL_SCAN_ABORT(void *) {
    nativeCall(ctl, 'S', 598);
    IOLockLock(lock);
    bool was = scanActive;
    scanActive = false;
    scanDeliverNow = false;
    IOLockUnlock(lock);
    if (was) {
        uint32_t status = 0;
        postEvent(kEventScanDone, &status, sizeof(status));
    }
    return kIOReturnSuccess;
}

// every 10 ms on the work queue: a finished (or refused, or timed out) scan is delivered from here
void MtkWlanNativeIf::serviceScan() {
    IOLockLock(lock);
    bool active = scanActive, deliver = false;
    if (active) {
        deliver = scanDeliverNow || scanDone || ++scanPolls >= kScanTimeoutPolls;
        if (deliver) { scanActive = false; scanDeliverNow = false; }
    }
    IOLockUnlock(lock);
    if (deliver) deliverScanResults();
}

void MtkWlanNativeIf::deliverScanResults() {
    uint32_t n = 0, posted = 0;
    uint8_t wantLen;
    uint8_t want[32];
    IOLockLock(lock);
    wantLen = scanSsidLen;
    memcpy(want, scanSsid, sizeof(want));
    IOLockUnlock(lock);
    if (coreLoop && coreLoop->runAction(gatedSnapBss, this, &n) != kIOReturnSuccess) n = 0;
    for (uint32_t i = 0; i < n && beaconMsg; i++) {
        const mtk_bss_info &b = bssSnap[i];
        if (!b.channel || b.age_ms > 60000) continue;                                        // not heard in the last minute
        if (wantLen && (b.ssid_len != wantLen || memcmp(b.ssid, want, wantLen))) continue;
        buildBeacon(beaconMsg, b);
        postEvent(kEventScanResult, beaconMsg, kBeaconHeader + load<uint32_t>(beaconMsg, 0));
        posted++;
    }
    uint32_t status = 0;
    postEvent(kEventScanDone, &status, sizeof(status));
    scansDelivered++;
    scanResultsPosted += posted;
    char s[96];
    snprintf(s, sizeof(s), "requests %u delivered %u results %u (last %u of %u cached)", scansRequested, scansDelivered, scanResultsPosted, posted, n);
    setProperty("MtkNativeScan", s);
    if (scanTraced++ < 12) nativeTrace(ctl, "scan:delivered %u of %u", posted, n);
}

// ---- stage D: the Family drives the association ------------------------------------------------------------------------------
//
// WCLJoinManager::handleJoinRequest (25G241, same shape as 24H32) first GETs 0x1ab WCL_LOW_LATENCY_INFO (4 bytes, nonzero aborts
// the join), then SETs 0x1ba WCL_ASSOCIATE with an apple80211AssocCandidates: +0x1c u32 ssid_len, +0x20 ssid[32], an
// apple80211_key at +0x40 (+0x44 u32 key_len, +0x48 u32 cipher, +0x50 key[64]; AppleBCMWLANJoinAdapter::performJoin hands
// [candidates + 0x40] to setKey when the cipher is 6..8), auth at +0x10/+0x14/+0x18 (AppleBCMWLANCore::setWCL_ASSOCIATE). The
// result goes back as events: 0xd8 link status first (WCLNetManager::linkStatusInd: 16 bytes, bssid, +6 u8 up, +8 u32 reason;
// only this takes NetManager out of LINK_DOWN), then 0x4e (8 bytes, u32 status), 0xd3 auth/assoc complete (0x1c bytes),
// 0xd4 first beacon (0x14 bytes) and 0xd5 connect complete (0xa4 bytes); WCLJoinManager checks exactly those sizes. The
// Family's link-up then GETs 0x1b1 WCL_BSS_INFO (the current BSS as a 0xc9-style beacon message), 0x1cc WCL_EXTENDED_BSS_INFO
// (0x214 bytes on 25G241), 0x1b2 WCL_TRAFFIC_COUNTERS, and SETs 0x1f6, 0x1bb, 0x1b5, 0x1c6 LINK_STATE_UPDATE, 0x1bc
// LINK_UP_DONE, 0x1be. A link loss is 0xd8 with +6 = 0 and reason 1; the Family answers with 0x1a9 WCL_LEAVE_NETWORK.
// Event layouts and the sequence follow ga403wm itlmt7925Native (BSD-3-Clause-Clear, Copyright (c) 2026 thanhnndev and
// contributors), re-checked against the 25G241 handlers.
namespace {
const uint32_t kEventAssoc = 0x4e, kEventJoin = 0xd3, kEventFirstBeacon = 0xd4, kEventConnect = 0xd5, kEventLinkStatus = 0xd8;
const uint32_t kEventBeaconCounter = 0xaf;   // WCLNetManager::updateBeaconCounter: u32 beacons since the last report at +0x54
// WCLNetManager::handleLqmUpdate (event 0x27, exactly 0x1dc bytes): the Family's assoc timer declares "missed beacons" when
// neither of two timestamps moved for 60 s. This message refreshes both: the beacon one when +0x1d8, +0x1d9 and +0x30 are
// nonzero and the u32 beacon count at +0x28 is nonzero; the traffic one when +0x1d8, +0x1d9 are nonzero and the u32 at
// +0x1c exceeds the one at +0x14 or the u32 at +0x24 is nonzero. +0 nonzero makes the s32 at +4 the RSSI; +0x54 nonzero
// skips a callback whose contract is not read. Everything else is left zero (optional sections, each with its own flag).
const uint32_t kEventLqmUpdate = 0x27, kLqmSize = 0x1dc;
}

IOReturn MtkWlanNativeIf::gatedJoin(OSObject *owner, void *params, void *rc, void *, void *) {
    MtkWlanNativeIf *self = OSDynamicCast(MtkWlanNativeIf, owner);
    *(int *)rc = (self && self->ops && self->core) ? self->ops->v2.join(self->core, (const mtk_join_params *)params) : -2;
    return kIOReturnSuccess;
}

IOReturn MtkWlanNativeIf::gatedDisconnect(OSObject *owner, void *, void *, void *, void *) {
    MtkWlanNativeIf *self = OSDynamicCast(MtkWlanNativeIf, owner);
    if (self && self->ops && self->core) self->ops->v2.disconnect(self->core);
    return kIOReturnSuccess;
}

IOReturn MtkWlanNativeIf::gatedStatus(OSObject *owner, void *, void *, void *, void *) {
    MtkWlanNativeIf *self = OSDynamicCast(MtkWlanNativeIf, owner);
    if (self && self->ops && self->core && self->statusBuf) self->ops->v2.get_status(self->core, self->statusBuf);
    return kIOReturnSuccess;
}

IOReturn MtkWlanNativeIf::gatedFindBss(OSObject *owner, void *bssid, void *found, void *, void *) {
    MtkWlanNativeIf *self = OSDynamicCast(MtkWlanNativeIf, owner);
    *(bool *)found = false;
    if (!self || !self->ops || !self->core || !self->curBss) return kIOReturnSuccess;
    for (uint32_t n = 0; n < 128 && self->ops->get_bss(self->core, n, self->curBss) == 0; n++)
        if (!memcmp(self->curBss->bssid, bssid, 6)) { *(bool *)found = true; break; }
    return kIOReturnSuccess;
}

bool MtkWlanNativeIf::currentBss(uint8_t bssid[6]) {
    if (!coreLoop || !statusBuf || !curBss) return false;
    if (coreLoop->runAction(gatedStatus, this) != kIOReturnSuccess) return false;
    if (statusBuf->state != MTK_ST_CONNECTED) return false;
    memcpy(bssid, statusBuf->bssid, 6);
    bool found = false;
    if (coreLoop->runAction(gatedFindBss, this, bssid, &found) != kIOReturnSuccess) return false;
    return found;
}

IOReturn MtkWlanNativeIf::getWCL_LOW_LATENCY_INFO(apple80211_low_latency_info *d) {
    nativeCall(ctl, 'S', 525);
    if (!d) return kIOReturnError;
    store<uint32_t>(d, 0, 0);                   // no low-latency session: the join may go ahead
    return kIOReturnSuccess;
}

IOReturn MtkWlanNativeIf::setWCL_ASSOCIATE(apple80211AssocCandidates *d) {
    nativeCall(ctl, 'S', 604);
    if (!d) return kIOReturnBadArgument;
    if (!bound || !ops || !core || !coreLoop) return kIOReturnNotReady;
    const uint8_t *b = (const uint8_t *)d;
    uint32_t ssidLen = load<uint32_t>(b, 0x1c), keyLen = load<uint32_t>(b, 0x44), cipher = load<uint32_t>(b, 0x48);
    if (assocTraced++ < 8) nativeTrace(ctl, "assoc:request ssidLen=%u cipher=%u keyLen=%u", ssidLen, cipher, keyLen);
    if (ssidLen == 0 || ssidLen > 32) return kIOReturnBadArgument;
    mtk_join_params p;
    bzero(&p, sizeof(p));
    p.version = MTK_USER_API_VERSION;
    p.ssid_len = (uint8_t)ssidLen;
    memcpy(p.ssid, b + 0x20, ssidLen);
    if (keyLen == 0) p.key_type = MTK_KEY_NONE;                                   // open network
    else if (cipher >= 6 && cipher <= 9 && keyLen == 32) { p.key_type = MTK_KEY_PMK; memcpy(p.pmk, b + 0x50, 32); }
    else return kIOReturnUnsupported;                                             // WEP, SAE, enterprise: not offered
    // The candidate list (WCLJoinRequest::fillAssocCandidatesList / addAssocCandidates on 25G241): u32 count at +0x214,
    // entries of 18 bytes from +0x21c, the BSSID first (N-146 traced the associated AP's address there),
    // then the channel and a u16. The Family picks the BSS; a join that ignores it lands on the core's own favourite and
    // the Family re-joins forever (N-145: it wanted channels 52, 11, 108 in turn while the core sat on 52).
    uint32_t nCand = load<uint32_t>(b, 0x214);
    if (nCand >= 1) {
        memcpy(p.bssid, b + 0x21c, 6);
        if ((p.bssid[0] & 1) || !(p.bssid[0] | p.bssid[1] | p.bssid[2] | p.bssid[3] | p.bssid[4] | p.bssid[5])) bzero(p.bssid, 6);
    }
    if (assocTraced < 8) nativeTrace(ctl, "assoc:candidates %u bssid %02x:%02x:%02x:%02x:%02x:%02x rest %02x%02x%02x%02x%02x%02x %02x%02x%02x%02x%02x%02x",
                                     nCand, p.bssid[0], p.bssid[1], p.bssid[2], p.bssid[3], p.bssid[4], p.bssid[5],
                                     b[0x222], b[0x223], b[0x224], b[0x225], b[0x226], b[0x227], b[0x228], b[0x229], b[0x22a], b[0x22b], b[0x22c], b[0x22d]);
    int rc = -2;
    if (coreLoop->runAction(gatedJoin, this, &p, &rc) != kIOReturnSuccess) rc = -2;
    bzero(&p, sizeof(p));
    if (rc != 0) { nativeTrace(ctl, "assoc:core join=%d", rc); return rc == -1 ? kIOReturnBadArgument : kIOReturnNotReady; }
    IOLockLock(lock);
    wantSsidLen = (uint8_t)ssidLen;
    memcpy(wantSsid, b + 0x20, ssidLen);
    joinDone = false;
    joinActive = true;
    joinPolls = 0;
    joinsRequested++;
    IOLockUnlock(lock);
    return kIOReturnSuccess;
}

IOReturn MtkWlanNativeIf::setWCL_LEAVE_NETWORK(apple80211_leave_network *) {
    nativeCall(ctl, 'S', 592);
    IOLockLock(lock);
    joinActive = false;
    leaves++;
    IOLockUnlock(lock);
    if (coreLoop && bound) coreLoop->runAction(gatedDisconnect, this);
    if (miscTraced++ < 12) nativeTrace(ctl, "assoc:leave");
    return kIOReturnSuccess;
}

IOReturn MtkWlanNativeIf::setWCL_JOIN_ABORT(apple80211_wcl_abort_join *) {
    nativeCall(ctl, 'S', 601);
    IOLockLock(lock);
    bool was = joinActive;
    joinActive = false;
    IOLockUnlock(lock);
    if (was && coreLoop && bound) coreLoop->runAction(gatedDisconnect, this);
    if (miscTraced++ < 12) nativeTrace(ctl, "assoc:abort (active=%u)", was);
    return kIOReturnSuccess;
}

// Link-path requests that configure features the core does not have: accepted and counted (a failure here makes
// WCLNetManager::setCurrentBSS abort the link-up and leave the network).
IOReturn MtkWlanNativeIf::setWCL_LINK_UP_DONE(void *) { nativeCall(ctl, 'S', 606); if (miscTraced++ < 12) nativeTrace(ctl, "wcl:link up done"); return kIOReturnSuccess; }
IOReturn MtkWlanNativeIf::setWCL_TRIGGER_CC(triggerCC *) { nativeCall(ctl, 'S', 602); return kIOReturnSuccess; }
IOReturn MtkWlanNativeIf::setWCL_SET_SCAN_HOME_AWAY_TIME(scanHomeAndAwayTime *) { nativeCall(ctl, 'S', 607); return kIOReturnSuccess; }
IOReturn MtkWlanNativeIf::setWCL_QOS_PARAMS(apple80211_wcl_qos_params *) { nativeCall(ctl, 'S', 605); return kIOReturnSuccess; }
IOReturn MtkWlanNativeIf::setWCL_ARP_MODE(apple80211_wcl_arp_mode *) { nativeCall(ctl, 'S', 600); return kIOReturnSuccess; }
IOReturn MtkWlanNativeIf::setWCL_SET_ROAM_LOCK(apple80211_set_roam_lock *) { nativeCall(ctl, 'S', 594); return kIOReturnSuccess; }
IOReturn MtkWlanNativeIf::setWCL_WNM_OPS(apple80211_wcl_wnm_config_t *d) { nativeCall(ctl, 'S', 628); return d ? kIOReturnSuccess : kIOReturnError; }
IOReturn MtkWlanNativeIf::setWCL_UPDATE_FAST_LANE(apple80211_fastlane *d) { nativeCall(ctl, 'S', 635); return d ? kIOReturnSuccess : kIOReturnBadArgument; }

// 0x1c6 apple80211_wcl_update_link_state: +0 bssid, +6 u8 up, +7/+8 flags, +0xc u32 down reason. Apple's driver drives its net
// adapter's link from it; ours follows the core's association, so it is recorded.
IOReturn MtkWlanNativeIf::setWCL_LINK_STATE_UPDATE(apple80211_wcl_update_link_state *d) {
    nativeCall(ctl, 'S', 468);
    if (!d) return kIOReturnBadArgument;
    const uint8_t *b = (const uint8_t *)d;
    if (linkStateTraced++ < 8) nativeTrace(ctl, "wcl:link state up=%u flags=%u/%u reason=%u radio=%u", b[6], b[7], b[8], load<uint32_t>(b, 0xc), linkUp());
    return kIOReturnSuccess;
}

IOReturn MtkWlanNativeIf::getWCL_TRAFFIC_COUNTERS(apple80211_wcl_traffic_counters *d) {
    nativeCall(ctl, 'S', 527);
    if (!d) return kIOReturnError;
    bzero(d, 0x38);
    return kIOReturnSuccess;
}

// The current BSS as a beacon message (0x844 bytes at most: the 0x44 header and the IEs), 0xe00002bc while not associated.
IOReturn MtkWlanNativeIf::getWCL_BSS_INFO(apple80211_beacon_msg *d) {
    nativeCall(ctl, 'S', 526);
    if (!d) return kIOReturnBadArgument;
    uint8_t bssid[6];
    if (!currentBss(bssid) || !beaconMsg) return kIOReturnError;
    buildBeacon(beaconMsg, *curBss);
    memcpy(d, beaconMsg, kBeaconHeader + load<uint32_t>(beaconMsg, 0));
    if (bssInfoTraced++ < 4) nativeTrace(ctl, "wcl:bss info answered ch=%u ssidLen=%u ie=%u", curBss->channel, curBss->ssid_len, curBss->ie_len);
    return kIOReturnSuccess;
}

// 0x1cc, 0x214 bytes on 25G241 (AppleBCMWLANNetAdapter::getExtendedBssInfo): +0 apple80211_rate_set_data (u32 version kept, u16
// count at +4, entries at +8 of 12 bytes: u32 version, u32 Mb/s, u32 basic; up to 15), +0xbc HT MCS set (u32 version, 10 bytes
// of RX MCS bitmask), +0xcc VHT map (u32 version, u16 map, 0xffff none), +0xd4 HE map (same), +0xdc apple_mlo_context (0x37
// bytes, zero without MLO), +0x113 the associated WPA/RSN element as is (0x101 bytes). 24H32 had the element at +0xdd.
namespace {
// VHT MCS maps, 2 bits per stream (0 = MCS 0-7, 1 = 0-8, 2 = 0-9, 3 = none): what both sides support
inline uint16_t vhtMapAnd(uint16_t a, uint16_t b) {
    uint16_t r = 0;
    for (int s = 0; s < 8; s++) {
        uint16_t x = (a >> (2 * s)) & 3, y = (b >> (2 * s)) & 3;
        r |= (uint16_t)((x == 3 || y == 3 ? 3 : (x < y ? x : y)) << (2 * s));
    }
    return r;
}
} // namespace

IOReturn MtkWlanNativeIf::getWCL_EXTENDED_BSS_INFO(apple80211_extended_bss_info *d) {
    nativeCall(ctl, 'S', 533);
    if (!d) return kIOReturnError;
    uint8_t *b = (uint8_t *)d;
    static const uint32_t verOff[4] = {0, 0xbc, 0xcc, 0xd4};
    uint32_t ver[4];
    for (int i = 0; i < 4; i++) ver[i] = load<uint32_t>(b, verOff[i]);
    bzero(b, 0x214);
    for (int i = 0; i < 4; i++) store<uint32_t>(b, verOff[i], ver[i]);
    store<uint16_t>(b, 0xcc + 4, 0xffff);
    store<uint16_t>(b, 0xd4 + 4, 0xffff);
    uint8_t bssid[6];
    uint32_t nrates = 0, rsnLen = 0;
    if (currentBss(bssid)) {
        mtk_link_info negotiated = {};
        ops->v2.v1.get_link_info(core, &negotiated);
        const uint8_t *ie = curBss->ie;
        const uint32_t n = curBss->ie_len < MTK_BSS_IE_MAX ? curBss->ie_len : MTK_BSS_IE_MAX;
        const uint8_t *wpa = nullptr;
        uint32_t wpaLen = 0;
        for (uint32_t p = 0; p + 2 <= n; p += 2 + ie[p + 1]) {
            const uint8_t id = ie[p], l = ie[p + 1];
            if (p + 2 + l > n) break;
            const uint8_t *v = ie + p + 2;
            if ((id == 1 || id == 50) && nrates < 15) {                              // Supported / Extended Supported Rates
                for (uint8_t k = 0; k < l && nrates < 15; k++, nrates++) {
                    store<uint32_t>(b, 8 + 12 * nrates + 4, (v[k] >> 1) & 0x3f);
                    store<uint32_t>(b, 8 + 12 * nrates + 8, (uint32_t)(v[k] >> 7));
                }
            } else if (id == 45 && l >= 13 && negotiated.ht) {                        // negotiated streams only
                for (int k = 0; k < 10; k++) b[0xbc + 4 + k] = k < negotiated.nss ? v[3 + k] : 0;
            } else if (id == 191 && l >= 12 && curBss->channel > 14 && negotiated.vht) {
                store<uint16_t>(b, 0xcc + 4, vhtMapAnd(load<uint16_t>(v, 4), 0xfffa)); // ours: 2 streams, MCS 0-9
            } else if (id == 0x30 && !rsnLen && l + 2u <= 0x101) {                   // RSN
                memcpy(b + 0x113, ie + p, l + 2u);
                rsnLen = l + 2u;
            } else if (id == 0xdd && !wpa && l >= 4 && v[0] == 0x00 && v[1] == 0x50 && v[2] == 0xf2 && v[3] == 0x01 && l + 2u <= 0x101) {
                wpa = ie + p;                                                        // WPA vendor element
                wpaLen = l + 2u;
            }
        }
        if (!rsnLen && wpa) { memcpy(b + 0x113, wpa, wpaLen); rsnLen = wpaLen; }
        store<uint16_t>(b, 4, (uint16_t)nrates);
    }
    if (extBssTraced++ < 4) nativeTrace(ctl, "wcl:extended bss info rates=%u rsn=%u", nrates, rsnLen);
    return kIOReturnSuccess;
}

void MtkWlanNativeIf::postLinkStatus(const uint8_t bssid[6], bool up, uint32_t reason) {
    uint8_t p[0x10];
    bzero(p, sizeof(p));
    memcpy(p, bssid, 6);
    p[6] = up ? 1 : 0;
    if (!up) store<uint32_t>(p, 8, reason);
    postEvent(kEventLinkStatus, p, sizeof(p));
    if (linkStatusTraced++ < 8) nativeTrace(ctl, "wcl:link status posted up=%u reason=%u", up, reason);
}

// The sequence the Family's WCL layer expects after a join: 0xd8 up (only this leaves LINK_DOWN; its link-up asks 0x1b1 for the
// current BSS, which is what GET SSID / BSSID / CHANNEL then answer from), then the join manager's four events.
void MtkWlanNativeIf::finishJoin(bool ok, const uint8_t bssid[6]) {
    if (ok) {
        IOLockLock(lock);
        memcpy(nmBssid, bssid, 6);
        nmLinkUp = true;
        nmEverJoined = true;
        linkDownPending = false;
        IOLockUnlock(lock);
        postLinkStatus(bssid, true, 0);
    }
    uint32_t assoc[2] = {ok ? 0u : 1u, 0};
    postEvent(kEventAssoc, assoc, sizeof(assoc));
    uint8_t join[0x1c];
    bzero(join, sizeof(join));
    store<uint16_t>(join, 0, ok ? 0 : 1);
    memcpy(join + 5, bssid, 6);
    store<uint32_t>(join, 0x14, assoc[0]);
    postEvent(kEventJoin, join, sizeof(join));
    if (ok) {
        uint8_t first[0x14];                    // u16 status, u16 ieee status, +4 bssid, +0xc u64 (left zero)
        bzero(first, sizeof(first));
        memcpy(first + 4, bssid, 6);
        postEvent(kEventFirstBeacon, first, sizeof(first));
        uint8_t done[0xa4];
        bzero(done, sizeof(done));
        memcpy(done + 4, bssid, 6);
        postEvent(kEventConnect, done, sizeof(done));
        joinsOk++;
    } else joinsFailed++;
    char s[112];
    snprintf(s, sizeof(s), "requests %u ok %u failed %u leaves %u linkdowns %u linkup %u beaconreports %u", joinsRequested, joinsOk, joinsFailed, leaves, linkDowns, nmLinkUp, beaconReports);
    setProperty("MtkNativeJoin", s);
    if (assocTraced++ < 16) nativeTrace(ctl, "assoc:result ok=%u", ok);
}

// The interface's link quality (ifnet LQM: -1 unknown, 20 minimally viable, 50 poor, 100 good). Without it
// State:/Network/Interface/<if>/LinkQuality stays unset and apsd treats the link as bad (isBadLQ), so push and with
// it iMessage, FaceTime and Handoff never connect over Wi-Fi.
void MtkWlanNativeIf::reportLinkQuality(int32_t lq) {
    if (lq == lqReported) return;
    lqReported = lq;
    IOReturn r = setLQM((uint64_t)(int64_t)lq);
    if (miscTraced++ < 12) nativeTrace(ctl, "lqm:%d rc=0x%x", lq, r);
}

// Every 10 ms on the work queue. A link the core dropped is reported down; an outstanding join is watched through the core's
// join_result callback and its status (a join the core answered "already there" fires no callback); a link the core restored
// by itself (its manager rejoins the wanted network after a loss) is announced again with the full sequence.
void MtkWlanNativeIf::serviceJoin() {
    if (!bound || !ops || !core) return;
    IOLockLock(lock);
    bool active = joinActive, down = false;
    uint8_t downBssid[6];
    if (linkDownPending) {
        linkDownPending = false;
        if (nmLinkUp) { nmLinkUp = false; down = true; memcpy(downBssid, nmBssid, 6); linkDowns++; }
    }
    IOLockUnlock(lock);
    if (down) { postLinkStatus(downBssid, false, 1); reportLinkQuality(-1); }
    if (!active) {
        if ((++reannouncePolls % 50) != 0) return;                                 // every 500 ms
        // The Family's net manager wants to hear that beacons keep arriving: WCLNetManager::updateBeaconCounter (event 0xaf,
        // u32 count at +0x54) sets a flag its missed-beacons check clears; without the event it raises the fault
        // "missed beacons timeout" about 60 s after every join and leaves the network (N-147). The core refreshes the BSS
        // cache entry on every beacon or probe response it hears, so the entry's age says whether the AP is still heard.
        IOLockLock(lock);
        bool up = nmLinkUp && radioUp;                                                 // the core still has the association
        uint8_t curBssid[6];
        memcpy(curBssid, nmBssid, 6);
        IOLockUnlock(lock);
        if (up && (reannouncePolls % 100) == 0 && coreLoop && curBss) {                // once a second
            // N-148: gating this on the cache entry's age starved on a channel the core only refreshes during scans (the
            // firmware keeps beacons of the associated AP to itself), and the Family raised BeaconLoss again. The core's
            // own link state is the truth here: it drops the link when the AP goes away, and that reaches the Family as 0xd8.
            bool found = false;
            if (coreLoop->runAction(gatedFindBss, this, curBssid, &found) != kIOReturnSuccess) found = false;
            uint8_t m[0x58];
            bzero(m, sizeof(m));
            uint32_t interval = (found && curBss->beacon_interval) ? curBss->beacon_interval : 100;
            uint32_t perSecond = (1000u * 1000u / 1024u) / interval + 1;               // beacons in the last second, at least 1
            store<uint32_t>(m, 0x54, perSecond);
            postEvent(kEventBeaconCounter, m, sizeof(m));
            if (lqmMsg) {
                bzero(lqmMsg, kLqmSize);
                lqmMsg[0] = 1;
                store<int32_t>(lqmMsg, 4, found ? curBss->rssi : -60);
                store<uint32_t>(lqmMsg, 0x14, lqmTick);                                // a counter that grows: +0x1c > +0x14
                store<uint32_t>(lqmMsg, 0x1c, lqmTick + 1);
                store<uint32_t>(lqmMsg, 0x24, rxDelivered ? rxDelivered : 1);           // frames received
                store<uint32_t>(lqmMsg, 0x28, perSecond);                               // beacons heard
                lqmMsg[0x30] = 1;
                lqmMsg[0x54] = 1;
                lqmMsg[0x1d8] = 1;
                lqmMsg[0x1d9] = 1;
                lqmTick++;
                postEvent(kEventLqmUpdate, lqmMsg, kLqmSize);
            }
            int32_t rssi = found ? curBss->rssi : -60;
            if (found && ctl)
                for (const uint8_t *e = curBss->ie, *end = curBss->ie + (curBss->ie_len > MTK_BSS_IE_MAX ? MTK_BSS_IE_MAX : curBss->ie_len);
                     e + 2 <= end && e + 2 + e[1] <= end; e += 2 + e[1])
                    if (e[0] == 7 && e[1] >= 3) { ctl->noteApCountry(e + 2); break; }   // Country element
            reportLinkQuality(rssi >= -75 ? 100 : rssi >= -85 ? 50 : 20);
            beaconReports++;
        }
        IOLockLock(lock);
        bool want = nmEverJoined && radioUp && !nmLinkUp && !linkDownPending;
        IOLockUnlock(lock);
        uint8_t bssid[6];
        if (want && currentBss(bssid)) {
            if (miscTraced++ < 12) nativeTrace(ctl, "wcl:link restored by core, announcing again ch=%u", curBss->channel);
            finishJoin(true, bssid);
        }
        return;
    }
    IOLockLock(lock);
    bool done = joinDone, timeout = ++joinPolls >= kJoinTimeoutPolls;
    int result = joinResult;
    uint8_t bssid[6];
    memcpy(bssid, joinBssid, 6);
    IOLockUnlock(lock);
    bool poll = (joinPolls % 10) == 0;                                             // the status every 100 ms
    bool up = false;
    if (poll && !done && coreLoop && statusBuf && coreLoop->runAction(gatedStatus, this) == kIOReturnSuccess)
        up = statusBuf->state == MTK_ST_CONNECTED && statusBuf->port_open && statusBuf->ssid_len == wantSsidLen &&
             !memcmp(statusBuf->ssid, wantSsid, wantSsidLen);
    if (up) memcpy(bssid, statusBuf->bssid, 6);
    if (done && result == MTK_JOIN_OK) up = true;
    bool failed = done && result != MTK_JOIN_OK;
    if (!up && !failed && !timeout) return;
    IOLockLock(lock);
    joinActive = false;
    joinDone = false;
    IOLockUnlock(lock);
    if (up) finishJoin(true, bssid);
    else {
        if (timeout && !failed && coreLoop) coreLoop->runAction(gatedDisconnect, this);
        nativeTrace(ctl, "assoc:failed result=%d timeout=%u", result, timeout);
        finishJoin(false, bssid);
    }
}

// 0x22d DBRG_ENTROPY (slot 662, new on 25G241): the Family hands the driver entropy for its random generator; ours has no DRBG,
// the bytes are accepted and dropped (a refusal is counted against the join path).
IOReturn MtkWlanNativeIf::setDBRG_ENTROPY(apple80211_drbg_entropy *d) { nativeCall(ctl, 'S', 662); return d ? kIOReturnSuccess : kIOReturnBadArgument; }

// ---- everything else: generated stubs (unsupported, counted per slot) ---------------------------------------------------------
#define MTK_IFACE_DEFS
#include "iface_stubs.inc"
#undef MTK_IFACE_DEFS
