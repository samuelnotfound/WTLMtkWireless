// <wtl> project: mtkwlannative | file: src/MtkWlanNative.cpp | author: watchthelight </wtl>
// SPDX-License-Identifier: ISC
// See MtkWlanNative.hpp. The start sequence, the CoreCapture option values and the request allow-list are adapted from
// ga403wm-hackintosh kexts/itlmt7925Native/itlmt7925_wifi.cpp (BSD-3-Clause-Clear, Copyright (c) 2026 thanhnndev and
// contributors); every offset was re-read in the 25G241 dump (the kcabi dump of 25G241) before use.
#include "MtkWlanNative.hpp"
#define MTK_NATIVE_VERSION "0.4.16"   // also Info.plist (build.sh checks they match)
#include "MtkWlanNativeIf.hpp"
#include "MtkNativeAbi.hpp"
#include "MtkNativeTrace.hpp"
#include <IOKit/IOLib.h>
#include <IOKit/network/IONetworkMedium.h>
#include <pexpert/pexpert.h>
#include <string.h>

#define super IO80211Controller
OSDefineMetaClassAndStructors(MtkWlanNative, IO80211Controller)

namespace {
const char kOwnerId[] = "org.watchthelight.MtkWlanNative";
const int kProhibited = 0x3a;              // ENOTSUP, what Apple's driver answers for a refused request

template <typename T>
inline void put(uint8_t *block, size_t off, T v) { memcpy(block + off, &v, sizeof(v)); }
inline void rel(void *o) { if (o) ((OSObject *)o)->release(); }

// Requests the controller or the interface answer with a handler whose layout was read from Apple's driver. Everything else is
// refused with ENOTSUP, which userland treats as "not supported" and carries on (the reference's call recorder proved it).
bool commandAllowed(int cmd) {
    switch (cmd) {
    case 0x2b: case 0x2c:                                   // DRIVER_VERSION, HARDWARE_VERSION (updateStaticProperties)
    case 0x0c: case 0x13: case 0x33:                        // CARD_CAPABILITIES, POWER, COUNTRY_CODE
    case 0x04: case 0x05: case 0x08: case 0x0f: case 0x10:  // CHANNEL, POWERSAVE, (0x8), OP_MODE, RSSI
    case 0x16b: case 0x1d5: case 0x200:                     // USB_HOST_NOTIFICATION, OS_FEATURE_FLAGS, INFRA_ENUMERATED
    case 0x1b: case 0xfe:                                   // SUPPORTED_CHANNELS, HW_SUPPORTED_CHANNELS
    case 0x1b9: case 0x1b3:                                 // WCL_SCAN_REQ, WCL_SCAN_ABORT: the Family asks before calling slots 603/598 (N-139: refused 32 times)
    case 0x1ab: case 0x1ba: case 0x1b6: case 0x1a9: case 0x1b7:   // stage D: LOW_LATENCY_INFO, ASSOCIATE, JOIN_ABORT, LEAVE_NETWORK, TRIGGER_CC
    case 0x1b1: case 0x1cc: case 0x1b2: case 0x1bc: case 0x1c6:   // BSS_INFO, EXTENDED_BSS_INFO, TRAFFIC_COUNTERS, LINK_UP_DONE, LINK_STATE_UPDATE
    case 0x1be: case 0x1f6: case 0x1bb: case 0x1b5: case 0x1ac: case 0x1eb:   // accepted and recorded on the link path
    case 0x170: case 0x22d:                                 // SET_MAC_ADDRESS (the private address, asked before every join; N-141 died here), DBRG_ENTROPY
    case 0x37: case 0x39: case 0xc3:                        // GUARD_INTERVAL, MCS, MCS_VHT (native #1: what Wi-Fi details show)
        return true;
    default:
        return false;
    }
}
} // namespace

// ---- probe: enabled and the ABI guard ----------------------------------------------------------------------------------

IOService *MtkWlanNative::probe(IOService *provider, SInt32 *score) {
    if (!super::probe(provider, score)) return nullptr;
    uint32_t on = 1;
    OSBoolean *enabled = OSDynamicCast(OSBoolean, getProperty("MtkNativeEnabled"));
    if (enabled) on = enabled->isTrue();
    PE_parse_boot_argn("mtknative", &on, sizeof(on));
    const char *why = nullptr;
    char buf[200];
    if (!on) {
        why = "disabled (MtkNativeEnabled false or boot-arg mtknative=0)";
    } else {
        MtkNativeAbiResult r = mtkNativeAbiCheck(version, OSDynamicCast(OSDictionary, getProperty("MtkNativeAbi")), mtkNativeClassSize);
        if (!r.ok) {
            snprintf(buf, sizeof(buf), "ABI not verified: %s", r.why);
            why = buf;
        } else {
            provider->setProperty("MtkNativeAbiBuild", r.build);
        }
    }
    if (why) {
        IOLog("MtkWlanNative: not starting, %s\n", why);
        provider->setProperty("MtkNativeDeclined", why);
        provider->callPlatformFunction(MTK_CORE_FE_DECLINED, false, (void *)why, nullptr, nullptr, nullptr);
        return nullptr;
    }
    // 0.4.10: claim the core now, before Apple's interface start can take longer than the core's 15 s fallback (N-161:
    // 17.2 s in IO80211SkywalkInterface start, the Ethernet front end won by 230 ms). Every give-up in start() declines.
    provider->callPlatformFunction(MTK_CORE_FE_CLAIM, false, nullptr, nullptr, nullptr, nullptr);
    return this;
}

// A claimed front end that gives up tells the core at once, so the Ethernet front end starts without waiting out the
// claim window. Safe to call more than once and after the fallback already ran.
void MtkWlanNative::declineCore(IOService *provider, const char *why) {
    if (!provider) return;
    provider->setProperty("MtkNativeDeclined", why);
    provider->callPlatformFunction(MTK_CORE_FE_DECLINED, false, (void *)why, nullptr, nullptr, nullptr);
}

// ---- CoreCapture objects IO80211Controller::start needs -----------------------------------------------------------------

// CCPipeOptions / CCStreamOptions as Apple fills them for its control-path logger (IO80211Controller::setupControlPathLogging):
// pipe +0 type (0 = CCLogPipe), +0x10 size, +0x18 min log size to notify, +0x20 notify threshold, +0x24 file name, +0x124 log
// identifier, +0x224 file size, +0x228 number of files, +0x244 directory; stream +0 type (0 = CCLogStream), +8 / +0xc level
// (-1 = Apple's default), +0x58 name.
// CCPipe / CCStream objects are IOServices (single inheritance, IOService first), so the opaque pointer is the IOService.
static bool startCC(void *obj, IOService *owner) { return obj ? ((IOService *)obj)->start(owner) : false; }

void MtkWlanNative::startCapturePipes(IOService *owner) {
    nativeTrace(this, "cc:log pipe start=%u", startCC(logPipe, owner));
    nativeTrace(this, "cc:fault pipe start=%u", startCC(dataPipe, owner));
    nativeTrace(this, "cc:fault stream start=%u", startCC(dataStream, owner));
}

bool MtkWlanNative::makeLogger(IOService *owner) {
    uint8_t *po = (uint8_t *)IOMallocZero(kCCPipeOptionsSize);
    uint8_t *so = (uint8_t *)IOMallocZero(kCCStreamOptionsSize);
    bool ok = false;
    if (po && so) {
        put<uint32_t>(po, 0x000, 0);
        put<uint64_t>(po, 0x010, 0x10000);
        put<uint64_t>(po, 0x018, 0x6666);
        put<uint32_t>(po, 0x020, 0x3e8);
        strlcpy((char *)po + 0x024, "MtkWlanNative", 0x100);
        strlcpy((char *)po + 0x124, "MtkWlanNative", 0x100);
        put<uint32_t>(po, 0x224, 0x200000);
        put<uint32_t>(po, 0x228, 2);
        strlcpy((char *)po + 0x244, "WiFi", 0x100);
        put<uint32_t>(so, 0x000, 0);
        put<uint32_t>(so, 0x008, 0xffffffffu);
        put<uint32_t>(so, 0x00c, 0xffffffffu);
        strlcpy((char *)so + 0x058, "MtkWlanNative", 0xf0);
        logPipe = CCPipe::withOwnerNameCapacity(owner, kOwnerId, "MtkWlanNative", (const CCPipeOptions *)po);
        // CCLogStream::initWithPipeAndName casts the pipe to CCLogPipe without a null check: never pass a null pipe
        if (logPipe) logStream = CCStream::withPipeAndName(logPipe, "MtkWlanNative", (const CCStreamOptions *)so);
        ok = logPipe && logStream;
    }
    if (po) IOFree(po, kCCPipeOptionsSize);
    if (so) IOFree(so, kCCStreamOptionsSize);
    return ok;
}

// Fault reporter: IO80211FaultReporter(CCFaultReporter(CCDataStream(CCDataPipe))), with the option values Apple's PCIe bus layer
// uses for its crash-tracer log: pipe type 1, +4 = 2 (log type), +8 = 6 (data type), +0x10 = 0x40 (size), +0x230 = the owner;
// stream type 1, +8 = -1 as a qword.
bool MtkWlanNative::makeFaultReporter(IOService *owner) {
    uint8_t *po = (uint8_t *)IOMallocZero(kCCPipeOptionsSize);
    uint8_t *so = (uint8_t *)IOMallocZero(kCCStreamOptionsSize);
    bool ok = false;
    if (po && so && wq) {
        put<uint32_t>(po, 0x000, 1);
        put<uint32_t>(po, 0x004, 2);
        put<uint64_t>(po, 0x008, 6);
        put<uint64_t>(po, 0x010, 0x40);
        strlcpy((char *)po + 0x024, "MtkWlanNativeFault", 0x100);
        strlcpy((char *)po + 0x124, "MtkWlanNativeFault", 0x100);
        strlcpy((char *)po + 0x244, "WiFi", 0x100);
        put<uint64_t>(po, 0x230, (uint64_t)(uintptr_t)owner);
        put<uint32_t>(so, 0x000, 1);
        put<uint64_t>(so, 0x008, ~0ull);
        dataPipe = CCPipe::withOwnerNameCapacity(owner, kOwnerId, "MtkWlanNativeFault", (const CCPipeOptions *)po);
        // The factories only alloc and init. CCDataPipe::start (an IOService start nobody else calls for a pipe that is not
        // matched by the catalogue) creates the pipe's notify timer; without it the first completed fault report panics in
        // CCDataPipe::enqueueBlob (N-143, null timer at pipe->priv+0x38). Start the pipe and the stream the way IOKit would.
        if (dataPipe) dataStream = CCStream::withPipeAndName(dataPipe, "MtkWlanNativeFault", (const CCStreamOptions *)so);
        if (dataStream) ccFault = CCFaultReporter::withStreamWorkloop((CCDataStream *)dataStream, (IOWorkLoop *)wq);
        if (ccFault) fault = IO80211FaultReporter::allocWithParams(ccFault);
        ok = fault != nullptr;
    }
    if (po) IOFree(po, kCCPipeOptionsSize);
    if (so) IOFree(so, kCCStreamOptionsSize);
    return ok;
}

// ---- start / stop --------------------------------------------------------------------------------------------------------

bool MtkWlanNative::start(IOService *provider) {
    nativeCallInit();
    nativeTrace(this, "ctl:start");
    // v5 (0.12.1) adds the rate details and the country code, v4 (0.11.0) set_mac; a v3 core (0.10.0) still works
    if (provider && provider->callPlatformFunction(MTK_CORE_OPS_V5, false, (void *)&ops5, &core, nullptr, nullptr) == kIOReturnSuccess &&
        ops5 && core && ops5->v4.v3.v2.v1.version == MTK_CORE_API_V5 && ops5->v4.v3.v2.v1.size >= sizeof(mtk_core_ops_v5)) {
        ops4 = &ops5->v4;
        ops = &ops4->v3;
    } else {
        ops5 = nullptr;
        core = nullptr;
    }
    if (!ops) {
        if (provider && provider->callPlatformFunction(MTK_CORE_OPS_V4, false, (void *)&ops4, &core, nullptr, nullptr) == kIOReturnSuccess &&
            ops4 && core && ops4->v3.v2.v1.version == MTK_CORE_API_V4 && ops4->v3.v2.v1.size >= sizeof(mtk_core_ops_v4)) {
            ops = &ops4->v3;
        } else {
            ops4 = nullptr;
            core = nullptr;
        }
    }
    if (!ops && (!provider || provider->callPlatformFunction(MTK_CORE_OPS_V3, false, (void *)&ops, &core, nullptr, nullptr) != kIOReturnSuccess ||
                 !ops || !core || ops->v2.v1.version != MTK_CORE_API_V3 || ops->v2.v1.size < sizeof(mtk_core_ops_v3))) {
        nativeTrace(this, "ctl:no core ops v3");
        declineCore(provider, "no core ops v3");
        return false;
    }
    nativeTrace(this, "ctl:core ops v%u", ops5 ? 5u : ops4 ? 4u : 3u);
    if (ops->v2.v1.get_mac(core, mac) != 0) {
        nativeTrace(this, "ctl:no station address");
        declineCore(provider, "no station address");
        return false;
    }
    provLoop = provider->getWorkLoop();
    if (provLoop) provLoop->retain();
    if (!createWorkQueue()) { nativeTrace(this, "ctl:no work queue"); declineCore(provider, "no work queue"); return false; }
    nativeTrace(this, "ctl:work queue ok");
    if (!makeLogger(provider)) { nativeTrace(this, "ctl:no logger"); declineCore(provider, "no logger"); return false; }
    nativeTrace(this, "ctl:logger ok");
    if (!makeFaultReporter(provider)) { nativeTrace(this, "ctl:no fault reporter"); declineCore(provider, "no fault reporter"); return false; }
    nativeTrace(this, "ctl:fault reporter ok; base start begin");
    if (!super::start(provider)) {
        nativeTrace(this, "ctl:IO80211Controller::start failed");
        declineCore(provider, "IO80211Controller::start failed");
        return false;
    }
    started = true;
    nativeTrace(this, "ctl:base start ok");
    // The inherited Skywalk media getter reads this dictionary; without it the BSD client's SIOCGIFXMEDIA fails with EIO.
    // Automatic Wi-Fi selection only; no link is claimed here.
    OSDictionary *media = OSDictionary::withCapacity(1);
    IONetworkMedium *automatic = IONetworkMedium::medium(kIOMediumIEEE80211Auto, 0);
    bool mediaOK = media && automatic && IONetworkMedium::addMedium(media, automatic) && publishMediumDictionary(media) &&
                   setSelectedMedium(automatic);
    rel(automatic);
    rel(media);
    nativeTrace(this, "ctl:media auto=%u", mediaOK);
    setProperty("MtkNativeVersion", MTK_NATIVE_VERSION);
    registerService();
    OSBoolean *withIf = OSDynamicCast(OSBoolean, getProperty("MtkNativeInterface"));
    uint32_t wantIf = withIf && withIf->isTrue();
    PE_parse_boot_argn("mtknativeif", &wantIf, sizeof(wantIf));
    if (!mediaOK || !wantIf) {
        nativeTrace(this, "ctl:controller only (%s)", mediaOK ? "no interface requested" : "no media dictionary");
        declineCore(provider, mediaOK ? "controller only, no interface requested" : "controller only, no media dictionary");
        return true;
    }
    // debug only: mtknativedelay=<ms> holds the interface start back, to prove the claim covers a slow Apple start
    uint32_t delayMs = 0;
    PE_parse_boot_argn("mtknativedelay", &delayMs, sizeof(delayMs));
    if (delayMs) {
        if (delayMs > 60000) delayMs = 60000;
        nativeTrace(this, "ctl:delay %u ms", delayMs);
        IOSleep(delayMs);
    }
    if (!makeInterface()) {
        nativeTrace(this, "ctl:interface failed, controller stays up");
        declineCore(provider, "interface start failed");
    }
    return true;
}

// Mirrors AppleBCMWLANCore::createSkywalkInterface: object -> attachInterface -> RegistrationInfo -> interface start (which
// registers pool and queues) -> detachInterface on failure. Role 1 is the primary station interface; the BSD unit is left to
// Apple's IOSkywalkLegacyEthernet shim (its probe refuses a provider that already carries IOInterfaceUnit).
bool MtkWlanNative::makeInterface() {
    MtkWlanNativeIf *ifc = MtkWlanNativeIf::withController(this, 0, 1);
    nativeTrace(this, "mk:withController=%s", ifc ? "ok" : "null");
    if (!ifc) return false;
    bool att = attachInterface(ifc, this);
    nativeTrace(this, "mk:attachInterface=%d", att);
    if (!att) { ifc->release(); return false; }
    uint8_t block[0x130];
    bzero(block, sizeof(block));
    IOSkywalkEthernetInterface::RegistrationInfo *info = (IOSkywalkEthernetInterface::RegistrationInfo *)block;
    ifc->initRegistrationInfo(info, 1, 0x130);
    // What createSkywalkInterface writes after initRegistrationInfo (24H32 reading, RegistrationInfo unchanged on 25G241 per
    // the dump README): +0x14 = 2, MAC at +0x108, +0x24 = +0x28 = 0x80, +0xc = 3, +0x3c (u16) = 0x40 headroom, +0x10 = 1 for
    // role 1. No BSD prefix / unit for role 1 (+0x38 stays 0xFFFFFFFF from initRegistrationInfo).
    put<uint32_t>(block, 0x14, 2);
    memcpy(block + 0x108, mac, 6);
    put<uint32_t>(block, 0x24, 0x80);
    put<uint32_t>(block, 0x28, 0x80);
    put<uint32_t>(block, 0x0c, 3);
    put<uint16_t>(block, 0x3c, 0x40);
    put<uint32_t>(block, 0x10, 1);
    if (!ifc->startWithInfo(this, info)) {
        nativeTrace(this, "mk:startWithInfo failed");
        detachInterface(ifc, false);
        ifc->release();
        return false;
    }
    nativeTrace(this, "mk:startWithInfo ok");
    // Only now start the CoreCapture pipes and streams (N-143: a fault report completing on an unstarted data pipe panics in
    // CCDataPipe::enqueueBlob; N-144: starting them before the interface registered its callbacks took the native front end
    // past the core's 15 s fallback and the Ethernet front end won). The core has its callbacks at this point.
    startCapturePipes(getProvider());
    iface = ifc;
    // AppleBCMWLANCore::setupDriver clears the deferral on its interface (removeProperty IODeferBSDAttach + registerService),
    // which lets IOSkywalkNetworkBSDClient attach and name the BSD interface.
    IOReturn dr = ifc->deferBSDAttach(false);
    nativeTrace(this, "mk:deferBSDAttach(false)=%x", dr);
    return true;
}

void MtkWlanNative::dropInterface() {
    if (!iface) return;
    MtkWlanNativeIf *ifc = iface;
    iface = nullptr;
    detachInterface(ifc, false);
    ifc->release();
}

void MtkWlanNative::stop(IOService *provider) {
    dropInterface();
    super::stop(provider);
}

void MtkWlanNative::free() {
    dropInterface();
    rel(fault);       fault = nullptr;
    rel(ccFault);     ccFault = nullptr;
    rel(dataStream);  dataStream = nullptr;
    rel(dataPipe);    dataPipe = nullptr;
    rel(logStream);   logStream = nullptr;
    rel(logPipe);     logPipe = nullptr;
    rel(wq);          wq = nullptr;
    rel(ownLoop);     ownLoop = nullptr;
    rel(provLoop);    provLoop = nullptr;
    nativeCallFree();
    super::free();
}

// ---- work queue and loops ------------------------------------------------------------------------------------------------

bool MtkWlanNative::createWorkQueue() {
    if (!wq) wq = IO80211WorkQueue::workQueue();
    return wq != nullptr;
}

IO80211WorkQueue *MtkWlanNative::getWorkQueue() const {
    return wq;
}

IOWorkLoop *MtkWlanNative::getWorkLoop() const {
    if (started && wq) return (IOWorkLoop *)wq;
    if (provLoop) return provLoop;
    MtkWlanNative *self = const_cast<MtkWlanNative *>(this);
    if (!self->ownLoop) self->ownLoop = IOWorkLoop::workLoop();
    return self->ownLoop;
}

CCLogStream *MtkWlanNative::getLogger() const {
    return (CCLogStream *)logStream;
}

IO80211FaultReporter *MtkWlanNative::getFaultReporterFromDriver() {
    return fault;
}

IOReturn MtkWlanNative::selectMedium(const IONetworkMedium *medium) {
    if (!medium || medium->getType() != kIOMediumIEEE80211Auto) return kIOReturnUnsupported;
    return setSelectedMedium(medium) ? kIOReturnSuccess : kIOReturnError;
}

IOReturn MtkWlanNative::getHardwareAddress(IOEthernetAddress *address) {
    if (!address) return kIOReturnBadArgument;
    memcpy(address, mac, 6);
    return kIOReturnSuccess;
}

IOReturn MtkWlanNative::newUserClient(task_t task, void *securityID, UInt32 type, OSDictionary *properties, IOUserClient **handler) {
    nativeCall(this, 'U', type);
    return super::newUserClient(task, securityID, type, properties, handler);
}

// ---- requests ------------------------------------------------------------------------------------------------------------

int MtkWlanNative::isCommandProhibited(int cmd) {
    nativeCall(this, 'I', (uint32_t)cmd);
    if (commandAllowed(cmd)) return 0;
    if ((++rejected & 63) == 1) setProperty("MtkNativeRejected", (uint64_t)rejected, 32);
    return kProhibited;
}

IOReturn MtkWlanNative::handleCardSpecific(IO80211SkywalkInterface *, unsigned long cmd, void *, bool set) {
    nativeCall(this, 'H', (uint32_t)cmd | (set ? 0x80000000u : 0));
    return kIOReturnUnsupported;
}

// apple80211_version_data (apple80211_ioctl.h, userland ABI): u32 version, u16 string_len, char string[256]
namespace {
IOReturn fillVersion(apple80211_version_data *data, const char *text) {
    if (!data) return kIOReturnBadArgument;
    uint8_t *d = (uint8_t *)data;
    size_t n = strlen(text);
    if (n > 255) n = 255;
    put<uint32_t>(d, 0, 1);
    put<uint16_t>(d, 4, (uint16_t)n);
    memcpy(d + 6, text, n);
    d[6 + n] = 0;
    return kIOReturnSuccess;
}
} // namespace

IOReturn MtkWlanNative::getDRIVER_VERSION(IO80211SkywalkInterface *, apple80211_version_data *d) {
    nativeCall(this, 'C', 0x2b);
    return fillVersion(d, "MtkWlanNative " MTK_NATIVE_VERSION);
}

IOReturn MtkWlanNative::getHARDWARE_VERSION(IO80211SkywalkInterface *, apple80211_version_data *d) {
    nativeCall(this, 'C', 0x2c);
    OSString *chip = getProvider() ? OSDynamicCast(OSString, getProvider()->getProperty("MtkChip")) : nullptr;
    return fillVersion(d, chip ? chip->getCStringNoCopy() : "MediaTek MT7925");
}

// apple80211_capability_data: u32 version at +0, then a capability bit array. Only what Apple's own handler stores without
// consulting chip state is reported (AppleBCMWLANCore::getCARD_CAPABILITIES, re-read in the 25G241 dump: the constant stores
// and the two unconditional ORs; the bits it sets when BCM firmware feature flags are absent are set too, since there is no
// BCM feature table here). Every bit Apple derives from firmware, chip revision or radio state stays clear.
IOReturn MtkWlanNative::getCARD_CAPABILITIES(IO80211SkywalkInterface *, apple80211_capability_data *data) {
    nativeCall(this, 'C', 0x0c);
    if (!data) return kIOReturnBadArgument;
    uint8_t *d = (uint8_t *)data;
    bzero(d, 0x18);
    put<uint32_t>(d, 0x00, 1);
    d[0x04] = 0x6f;
    d[0x05] = 0xe6;
    d[0x06] = 0x61 | 0x10 | 0x02;
    d[0x07] = 0x04;
    d[0x09] = 0x40 | 0x01;
    d[0x0a] = 0x04;
    d[0x0c] = 0x01;
    d[0x0d] = 0x02;
    if (!capsTraced) {
        capsTraced = true;
        nativeTrace(this, "ioctl:CARD_CAPABILITIES answered");
    }
    return kIOReturnSuccess;
}

// apple80211_power_data: [+4] = 1, [+8] = the power state; the state is what userland last set (the radio is the core's)
IOReturn MtkWlanNative::getPOWER(IO80211SkywalkInterface *, apple80211_power_data *data) {
    nativeCall(this, 'C', 0x13);
    if (!data) return kIOReturnBadArgument;
    uint8_t *d = (uint8_t *)data;
    put<uint32_t>(d, 0x04, 1);
    put<uint32_t>(d, 0x08, powerState);
    return kIOReturnSuccess;
}

IOReturn MtkWlanNative::setPOWER(IO80211SkywalkInterface *, apple80211_power_data *data) {
    nativeCall(this, 'C', 0x80000013);
    if (!data) return kIOReturnBadArgument;
    uint32_t want;
    memcpy(&want, (const uint8_t *)data + 8, sizeof(want));
    if (want != powerState) {
        powerState = want;
        nativeTrace(this, "ioctl:setPOWER=%u (recorded only)", want);
    }
    return kIOReturnSuccess;
}

// apple80211_country_code_data: three bytes at +4
IOReturn MtkWlanNative::getCOUNTRY_CODE(IO80211SkywalkInterface *, apple80211_country_code_data *data) {
    nativeCall(this, 'C', 0x33);
    if (!data) return kIOReturnBadArgument;
    const uint8_t *cc = (!countryFromOs && apCountry[0]) ? apCountry : countryCode;
    memcpy((uint8_t *)data + 4, cc, sizeof(countryCode));
    return kIOReturnSuccess;
}

// the associated AP's Country element; report-only, the core's regulatory domain does not change
void MtkWlanNative::noteApCountry(const uint8_t cc[2]) {
    if (cc[0] < 'A' || cc[0] > 'Z' || cc[1] < 'A' || cc[1] > 'Z' || !memcmp(apCountry, cc, 2)) return;
    apCountry[0] = cc[0];
    apCountry[1] = cc[1];
    apCountry[2] = 0;
    nativeTrace(this, "country:AP says %c%c", cc[0], cc[1]);
}

IOReturn MtkWlanNative::setCOUNTRY_CODE(IO80211SkywalkInterface *, apple80211_country_code_data *data) {
    nativeCall(this, 'C', 0x80000033);
    if (!data) return kIOReturnBadArgument;
    const uint8_t *d = (const uint8_t *)data + 4;
    countryFromOs = !(d[0] == 'X' && d[1] == 'Z');
    if (memcmp(countryCode, d, sizeof(countryCode)) != 0) {
        memcpy(countryCode, d, sizeof(countryCode));
        int rc = -2;
        if (ops5 && core && provLoop)
            provLoop->runAction(&MtkWlanNative::gatedSetCountry, this, &rc, nullptr, nullptr, nullptr);
        nativeTrace(this, "ioctl:setCOUNTRY_CODE %c%c (core %d)", countryCode[0] ? countryCode[0] : '?',
                    countryCode[1] ? countryCode[1] : '?', rc);
    }
    return kIOReturnSuccess;
}

IOReturn MtkWlanNative::gatedSetCountry(OSObject *owner, void *rc, void *, void *, void *) {
    MtkWlanNative *self = OSDynamicCast(MtkWlanNative, owner);
    if (!self || !self->ops5 || !self->core) { *(int *)rc = -2; return kIOReturnSuccess; }
    *(int *)rc = self->ops5->set_country(self->core, (const char *)self->countryCode);
    return kIOReturnSuccess;
}

IOReturn MtkWlanNative::setGET_DEBUG_INFO(IO80211SkywalkInterface *, apple80211_debug_command *) {
    nativeCall(this, 'C', 0x80000f00);
    return kIOReturnUnsupported;
}
