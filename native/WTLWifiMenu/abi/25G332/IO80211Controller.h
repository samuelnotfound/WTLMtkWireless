// <wtl> project: mtkwlannative | file: abi/25G332/IO80211Controller.h | author: watchthelight </wtl>
// SPDX-License-Identifier: ISC
// GENERATED from the kcabi dump of 25G332 (macOS 26.7.2 build 25G332, xnu-12377.161.15.701.48~2). Do not edit by hand: re-run genhdr.py.
//
// IO80211Controller : IOEthernetController as Apple's kernel collection exports it on this build. Slot numbers count the two vtable header words
// (byte offset in the vtable symbol = slot * 8; from a vptr = slot * 8 - 0x10). Instance size 0x128 (MetaClass constructor).
// Names are Apple's own symbols; pure slots are named by the first derived class that implements them. Return types are not
// in mangled names: "ret: guess" is a guess from the name. Checked with genhdr.py check (clang's vtable layout of a probe
// subclass against the dump). Method adapted from ga403wm-hackintosh tools/io80211_abi.py (BSD-3-Clause-Clear).
#pragma once
#include <IOKit/network/IOEthernetController.h>
#include <IOKit/IOReturn.h>
#include <stdint.h>

struct ether_addr;
struct __mbuf;
class CCLogStream;
class CCPipe;
struct FlowIdMetadata;
class IO80211CommandGate;
class IO80211FaultReporter;
class IO80211FlowQueue;
class IO80211InterfaceAVCAdvisory;
class IO80211PostOffice;
class IO80211SkywalkInterface;
class IO80211VirtualInterface;
class IO80211WorkQueue;
struct apple80211_capability_data;
struct apple80211_country_code_data;
struct apple80211_debug_command;
struct apple80211_platform_config;
struct apple80211_power_data;
struct apple80211_version_data;
#ifndef IO80211_ENUM_IO80211CountryCodeOp
#define IO80211_ENUM_IO80211CountryCodeOp
enum IO80211CountryCodeOp : uint32_t {};
#endif
#ifndef IO80211_ENUM_IO80211FeatureCode
#define IO80211_ENUM_IO80211FeatureCode
enum IO80211FeatureCode : uint32_t {};
#endif

class IO80211Controller : public IOEthernetController {
    OSDeclareAbstractStructors(IO80211Controller)

public:
    // overrides of inherited virtuals (declared so that super:: reaches Apple's code)
    /* 20 */ virtual void free() override;
    /* 75 */ virtual bool init(OSDictionary* a1) override;
    /* 135 */ virtual IOReturn configureReport(IOReportChannelList* a1, uint32_t a2, void* a3, void* a4) override;
    /* 136 */ virtual IOReturn updateReport(IOReportChannelList* a1, uint32_t a2, void* a3, void* a4) override;
    /* 186 */ virtual bool start(IOService* a1) override;
    /* 187 */ virtual void stop(IOService* a1) override;
    /* 211 */ virtual IOWorkLoop * getWorkLoop() const override;
    /* 242 */ virtual const char * stringFromReturn(int a1) override;
    /* 243 */ virtual int errnoFromReturn(int a1) override;
    /* 270 */ virtual UInt32 getFeatures() const override;
    /* 271 */ virtual const OSString * newVendorString() const override;
    /* 272 */ virtual const OSString * newModelString() const override;
    /* 308 */ virtual bool createWorkLoop() override;
    /* 358 */ virtual IOReturn getHardwareAddress(IOEthernetAddress* a1) override;
    /* 360 */ virtual IOReturn setMulticastMode(bool a1) override;
    /* 362 */ virtual IOReturn setPromiscuousMode(bool a1) override;

    // new virtuals, slots 396..464
    /* 396 */ virtual int isCommandProhibited(int a1) = 0;   // ret: errno: 0 = allowed, 0x3a (ENOTSUP) = refused, returned to the ioctl caller
    /* 397 */ virtual bool createWorkQueue();   // ret: Apple's driver returns a success flag; the base returns 0
    /* 398 */ virtual IOReturn debugStateInit();   // ret: guess
    /* 399 */ virtual IO80211WorkQueue * getWorkQueue() const;   // ret: result passed to IO80211CommandGate::allocWithParams
    /* 400 */ virtual IOReturn requestPacketTx(void* a1, uint32_t a2);   // ret: guess
    /* 401 */ virtual IO80211CommandGate * getIO80211CommandGate() const;   // ret: guess from name
    /* 402 */ virtual IO80211SkywalkInterface * getPrimarySkywalkInterface();   // ret: guess from name
    /* 403 */ virtual IOReturn bpfOutputPacket(OSObject* a1, uint32_t a2, __mbuf* a3);   // ret: guess
    /* 404 */ virtual bool monitorModeSetEnabled(bool a1, uint32_t a2);   // ret: guess from name
    /* 405 */ virtual IOReturn handleCardSpecific(IO80211SkywalkInterface* a1, unsigned long a2, void* a3, bool a4) = 0;   // ret: guess
    /* 406 */ virtual IOReturn hardwareOutputQueueDepth();   // ret: guess
    /* 407 */ virtual IOReturn performCountryCodeOperation(IO80211CountryCodeOp a1);   // ret: guess
    /* 408 */ virtual IOReturn dataLinkLayerAttachComplete();   // ret: guess
    /* 409 */ virtual IOReturn enableFeature(IO80211FeatureCode a1, void* a2);   // ret: guess
    /* 410 */ virtual IOReturn getDRIVER_VERSION(IO80211SkywalkInterface* a1, apple80211_version_data* a2) = 0;   // ret: request handler: the ioctl status
    /* 411 */ virtual IOReturn getHARDWARE_VERSION(IO80211SkywalkInterface* a1, apple80211_version_data* a2) = 0;   // ret: request handler: the ioctl status
    /* 412 */ virtual IOReturn getCARD_CAPABILITIES(IO80211SkywalkInterface* a1, apple80211_capability_data* a2) = 0;   // ret: request handler: the ioctl status
    /* 413 */ virtual IOReturn getPOWER(IO80211SkywalkInterface* a1, apple80211_power_data* a2) = 0;   // ret: request handler: the ioctl status
    /* 414 */ virtual IOReturn setPOWER(IO80211SkywalkInterface* a1, apple80211_power_data* a2) = 0;   // ret: request handler: the ioctl status
    /* 415 */ virtual IOReturn getCOUNTRY_CODE(IO80211SkywalkInterface* a1, apple80211_country_code_data* a2) = 0;   // ret: request handler: the ioctl status
    /* 416 */ virtual IOReturn setCOUNTRY_CODE(IO80211SkywalkInterface* a1, apple80211_country_code_data* a2) = 0;   // ret: request handler: the ioctl status
    /* 417 */ virtual IOReturn setGET_DEBUG_INFO(IO80211SkywalkInterface* a1, apple80211_debug_command* a2) = 0;   // ret: request handler: the ioctl status
    /* 418 */ virtual IOReturn getPLATFORM_CONFIG(IO80211SkywalkInterface* a1, apple80211_platform_config* a2);   // ret: request handler: the ioctl status
    /* 419 */ virtual IOReturn enableVirtualInterface(IO80211VirtualInterface* a1);   // ret: guess
    /* 420 */ virtual IOReturn disableVirtualInterface(IO80211VirtualInterface* a1);   // ret: guess
    /* 421 */ virtual IOReturn requiresExplicitMBufRelease();   // ret: guess
    /* 422 */ virtual IOReturn flowIdSupported();   // ret: guess
    /* 423 */ virtual IOReturn requestFlowQueue(FlowIdMetadata const* a1);   // ret: guess
    /* 424 */ virtual IOReturn releaseFlowQueue(IO80211FlowQueue* a1);   // ret: guess
    /* 425 */ virtual IOReturn getLogPipes(CCPipe** a1, CCPipe** a2, CCPipe** a3);   // ret: guess
    /* 426 */ virtual CCLogStream * getLogger() const = 0;   // ret: result used with CCLogStream::shouldLog
    /* 427 */ virtual IOReturn enableFeatureForLoggingFlags(uint64_t a1);   // ret: guess
    /* 428 */ virtual IOReturn requestQueueSizeAndTimeout(uint16_t* a1, uint16_t* a2);   // ret: guess
    /* 429 */ virtual IOReturn enablePacketTimestamping();   // ret: guess
    /* 430 */ virtual IOReturn disablePacketTimestamping();   // ret: guess
    /* 431 */ virtual IOReturn getPacketTSCounter();   // ret: guess
    /* 432 */ virtual IOReturn getDriverTextLog();   // ret: guess
    /* 433 */ virtual IOReturn selfDiagnosticsReport(int a1, char const* a2, uint32_t a3);   // ret: guess
    /* 434 */ virtual IO80211FaultReporter * getFaultReporterFromDriver() = 0;   // ret: start() retains it and panics when null
    /* 435 */ virtual IOReturn allocIO80211RecursiveLock();   // ret: guess
    /* 436 */ virtual uint32_t getDataQueueDepth(OSObject* a1);   // ret: guess from name
    /* 437 */ virtual bool wasDynSARInFailSafeMode();   // ret: guess from name
    /* 438 */ virtual void updateAdvisoryScoresIfNeed();   // ret: guess from name
    /* 439 */ virtual IOReturn getAVCAdvisoryInfo(IO80211InterfaceAVCAdvisory* a1);   // ret: guess
    /* 440 */ virtual IOReturn getActionFramePoolCapacity();   // ret: guess
    /* 441 */ virtual IO80211PostOffice * getPostOffice();   // ret: guess from name
    /* 442 */ virtual IO80211PostOffice * CreatePostOffice();   // ret: result stored in the ivars, null-checked
    /* 443 */ virtual bool attachInterface(OSObject* a1, IOService* a2);   // ret: base returns setne al
    /* 444 */ virtual IOReturn detachInterface(OSObject* a1, bool a2);   // ret: guess
    /* 445 */ virtual IOReturn createVirtualInterface(ether_addr* a1, uint32_t a2);   // ret: guess
    /* 446 */ virtual IOReturn attachVirtualInterface(IO80211VirtualInterface** a1, ether_addr* a2, uint32_t a3, bool a4);   // ret: guess
    /* 447 */ virtual IOReturn detachVirtualInterface(IO80211VirtualInterface* a1, bool a2);   // ret: guess
    /* 448 */ virtual void _RESERVEDIO80211Controller0();
    /* 449 */ virtual void _RESERVEDIO80211Controller1();
    /* 450 */ virtual void _RESERVEDIO80211Controller2();
    /* 451 */ virtual void _RESERVEDIO80211Controller3();
    /* 452 */ virtual void _RESERVEDIO80211Controller4();
    /* 453 */ virtual void _RESERVEDIO80211Controller5();
    /* 454 */ virtual void _RESERVEDIO80211Controller6();
    /* 455 */ virtual void _RESERVEDIO80211Controller7();
    /* 456 */ virtual void _RESERVEDIO80211Controller8();
    /* 457 */ virtual void _RESERVEDIO80211Controller9();
    /* 458 */ virtual void _RESERVEDIO80211Controller10();
    /* 459 */ virtual void _RESERVEDIO80211Controller11();
    /* 460 */ virtual void _RESERVEDIO80211Controller12();
    /* 461 */ virtual void _RESERVEDIO80211Controller13();
    /* 462 */ virtual void _RESERVEDIO80211Controller14();
    /* 463 */ virtual void _RESERVEDIO80211Controller15();
    /* 464 */ virtual void setMulticastList(ether_addr const* a1, uint32_t a2);   // ret: guess from name

    IOReturn postMessage(IO80211SkywalkInterface* iface, uint32_t code, void* data, unsigned long length, bool flag);   // exported: __ZN17IO80211Controller11postMessageEP23IO80211SkywalkInterfacejPvmb; hands a driver event to the post office; what Apple's driver calls for every event

protected:
    uint8_t iO80211ControllerOpaque[0x128 - sizeof(IOEthernetController)];   // ivars of Apple's class: layout not determined
};
static_assert(sizeof(IO80211Controller) == 0x128, "IO80211Controller must be 0x128 bytes (25G332)");
