// <wtl> project: mtkwlannative | file: src/MtkWlanNative.hpp | author: watchthelight </wtl>
// SPDX-License-Identifier: ISC
// MtkWlanNative: the IO80211Controller subclass that makes macOS treat the MT7925 (driven by the MtkWlan core) as Wi-Fi:
// System Settings pane, menu bar, scan and join from the UI. It matches the core (IOProviderClass MtkWlan) in its own
// category and engages only when the ABI guard (MtkNativeAbi.hpp) accepts the running build. Headers under abi/<build>/
// are generated from that build's kernel collection (tools/kcabi/genhdr.py).
//
// The design follows the native front end of ga403wm-hackintosh (kexts/itlmt7925Native, BSD-3-Clause-Clear, Copyright (c)
// 2026 thanhnndev and contributors), which carried traffic on Sequoia 24H32; its two hard-won rules are kept:
//   * getWorkQueue() must return an IO80211WorkQueue before IO80211Controller::start runs (the base never calls
//     createWorkQueue), and getLogger() / getFaultReporterFromDriver() must return real CoreCapture objects (a null one
//     panics in start);
//   * getWorkLoop() answers the provider's loop until the base start has returned (IONetworkController::start otherwise
//     calls ml_thread_policy on the work queue's NULL thread) and the work queue afterwards (the Family's Glue checks that
//     the BSD client's gate sits on that queue).
#pragma once
#include "IO80211Controller.h"
#include "IO80211WorkQueue.h"
#include "corecapture_min.h"
#include "MtkCoreApi.h"
#include <IOKit/IOTimerEventSource.h>

class MtkWlanNativeIf;

class MtkWlanNative : public IO80211Controller {
    OSDeclareDefaultStructors(MtkWlanNative)

public:
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void declineCore(IOService *provider, const char *why);   // MTK_CORE_FE_DECLINED after a claim (0.4.10)
    void stop(IOService *provider) override;
    void free() override;
    IOReturn selectMedium(const IONetworkMedium *medium) override;
    IOReturn getHardwareAddress(IOEthernetAddress *address) override;
    IOWorkLoop *getWorkLoop() const override;
    IOReturn newUserClient(task_t task, void *securityID, UInt32 type, OSDictionary *properties, IOUserClient **handler) override;
    using IO80211Controller::newUserClient;

    // IO80211Controller virtuals a subclass must provide (pure in abi/<build>/IO80211Controller.h)
    bool createWorkQueue() override;
    IO80211WorkQueue *getWorkQueue() const override;
    CCLogStream *getLogger() const override;
    IO80211FaultReporter *getFaultReporterFromDriver() override;
    int isCommandProhibited(int cmd) override;
    IOReturn handleCardSpecific(IO80211SkywalkInterface *i, unsigned long cmd, void *data, bool set) override;
    IOReturn getDRIVER_VERSION(IO80211SkywalkInterface *i, apple80211_version_data *d) override;
    IOReturn getHARDWARE_VERSION(IO80211SkywalkInterface *i, apple80211_version_data *d) override;
    IOReturn getCARD_CAPABILITIES(IO80211SkywalkInterface *i, apple80211_capability_data *d) override;
    IOReturn getPOWER(IO80211SkywalkInterface *i, apple80211_power_data *d) override;
    IOReturn setPOWER(IO80211SkywalkInterface *i, apple80211_power_data *d) override;
    IOReturn getCOUNTRY_CODE(IO80211SkywalkInterface *i, apple80211_country_code_data *d) override;
    IOReturn setCOUNTRY_CODE(IO80211SkywalkInterface *i, apple80211_country_code_data *d) override;
    void noteApCountry(const uint8_t cc[2]);
    IOReturn setGET_DEBUG_INFO(IO80211SkywalkInterface *i, apple80211_debug_command *d) override;

    // for the interface
    const mtk_core_ops_v3 *coreOps() const { return ops; }
    const mtk_core_ops_v4 *coreOps4() const { return ops4; }   // null on a v3 core (0.10.0): no set_mac
    const mtk_core_ops_v5 *coreOps5() const { return ops5; }   // null before core 0.12.1: no rate details, no country
    void *coreHandle() const { return core; }
    IOWorkLoop *coreLoop() const { return provLoop; }
    const uint8_t *stationAddress() const { return mac; }

private:
    static IOReturn gatedSetCountry(OSObject *owner, void *rc, void *, void *, void *);
    bool makeLogger(IOService *owner);
    bool makeFaultReporter(IOService *owner);
    void startCapturePipes(IOService *owner);   // after the interface bound the core: CCDataPipe::start creates the notify timer
    bool makeInterface();
    void dropInterface();

    const mtk_core_ops_v3 *ops = nullptr;
    const mtk_core_ops_v4 *ops4 = nullptr;
    const mtk_core_ops_v5 *ops5 = nullptr;
    void *core = nullptr;
    IOWorkLoop *provLoop = nullptr;         // the core's work loop (retained)
    IOWorkLoop *ownLoop = nullptr;          // fallback when the provider has none
    IO80211WorkQueue *wq = nullptr;
    bool started = false;                   // IO80211Controller::start returned: getWorkLoop() answers wq from here on
    CCPipe *logPipe = nullptr;
    CCStream *logStream = nullptr;          // a CCLogStream (options type 0)
    CCPipe *dataPipe = nullptr;
    CCStream *dataStream = nullptr;         // a CCDataStream (options type 1)
    CCFaultReporter *ccFault = nullptr;
    IO80211FaultReporter *fault = nullptr;
    MtkWlanNativeIf *iface = nullptr;
    uint8_t mac[6] = {};
    uint32_t powerState = 0;                // what userland last set (apple80211_power_data +8)
    uint8_t countryCode[3] = {'X', 'Z', 0}; // Apple's "unknown country" code until the core's domain is published
    bool countryFromOs = false;             // macOS set countryCode; it wins over the AP's Country element
    uint8_t apCountry[3] = {0, 0, 0};       // Country element of the associated AP (802.11d), reported until macOS sets one
    uint32_t rejected = 0;
    bool capsTraced = false;
};
