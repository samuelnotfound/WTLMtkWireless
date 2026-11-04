// <wtl> project: mtkwlannative | file: abi/25G332/IOSkywalkNetworkInterface.h | author: watchthelight </wtl>
// SPDX-License-Identifier: ISC
// GENERATED from the kcabi dump of 25G332 (macOS 26.7.2 build 25G332, xnu-12377.161.15.701.48~2). Do not edit by hand: re-run genhdr.py.
//
// IOSkywalkNetworkInterface : IOSkywalkInterface as Apple's kernel collection exports it on this build. Slot numbers count the two vtable header words
// (byte offset in the vtable symbol = slot * 8; from a vptr = slot * 8 - 0x10). Instance size 0xe0 (MetaClass constructor).
// Names are Apple's own symbols; pure slots are named by the first derived class that implements them. Return types are not
// in mangled names: "ret: guess" is a guess from the name. Checked with genhdr.py check (clang's vtable layout of a probe
// subclass against the dump). Method adapted from ga403wm-hackintosh tools/io80211_abi.py (BSD-3-Clause-Clear).
#pragma once
#include "IOSkywalkInterface.h"
#include <IOKit/IOReturn.h>
#include <stdint.h>

struct ether_addr;
struct __mbuf;
class IOSkywalkLogicalLink;
class IOSkywalkPacketBufferPool;
class IOSkywalkPacketQueue;
struct __ifnet;
struct if_link_status;
struct ifdrv;
struct ifnet_init_eparams;
struct ifnet_traffic_descriptor_common;
struct sockaddr_dl;

class IOSkywalkNetworkInterface : public IOSkywalkInterface {
    OSDeclareAbstractStructors(IOSkywalkNetworkInterface)

public:
    struct IOSkywalkTSOOptions;   // opaque here
    struct RegistrationInfo;   // opaque here
    // overrides of inherited virtuals (declared so that super:: reaches Apple's code)
    /* 20 */ virtual void free() override;
    /* 75 */ virtual bool init(OSDictionary* a1) override;
    /* 130 */ virtual bool willTerminate(IOService* a1, uint32_t a2) override;
    /* 187 */ virtual void stop(IOService* a1) override;
    /* 246 */ virtual void joinPMtree(IOService* a1) override;
    /* 250 */ virtual IOReturn setAggressiveness(unsigned long a1, unsigned long a2) override;
    /* 268 */ virtual IOReturn enable(uint32_t a1) override;
    /* 269 */ virtual IOReturn disable(uint32_t a1) override;

    // new virtuals, slots 284..334
    /* 284 */ virtual IOReturn registerNetworkInterfaceWithLogicalLink(IOSkywalkNetworkInterface::RegistrationInfo const* a1, IOSkywalkLogicalLink* a2, IOSkywalkPacketBufferPool* a3, IOSkywalkPacketBufferPool* a4, uint32_t a5);   // ret: guess
    /* 285 */ virtual IOReturn deregisterLogicalLink();   // ret: guess
    /* 286 */ virtual IOReturn initBSDInterfaceParameters(ifnet_init_eparams* a1, sockaddr_dl** a2) = 0;   // ret: guess
    /* 287 */ virtual IOReturn prepareBSDInterface(__ifnet* a1, uint32_t a2);   // ret: guess
    /* 288 */ virtual IOReturn finalizeBSDInterface(__ifnet* a1, uint32_t a2);   // ret: guess
    /* 289 */ virtual __ifnet * getBSDInterface() const;   // ret: guess from name
    /* 290 */ virtual void setBSDName(char const* a1);   // ret: guess from name
    /* 291 */ virtual const char * getBSDName() const;   // ret: guess from name
    /* 292 */ virtual IOReturn processBSDCommand(__ifnet* a1, uint32_t a2, void* a3);   // ret: guess
    /* 293 */ virtual IOReturn processInterfaceCommand(ifdrv* a1);   // ret: guess
    /* 294 */ virtual IOReturn interfaceAdvisoryEnable(bool a1);   // ret: guess
    /* 295 */ virtual void setRxFlowSteering(uint32_t a1, ifnet_traffic_descriptor_common* a2, uint32_t a3);   // ret: guess from name
    /* 296 */ virtual void setInterfaceEnable(bool a1);   // ret: guess from name
    /* 297 */ virtual void setRunningState(bool a1);   // ret: guess from name
    /* 298 */ virtual IOReturn handleChosenMedia(uint32_t a1);   // ret: guess
    /* 299 */ virtual IOReturn getSupportedMediaArray(uint32_t* a1, uint32_t* a2);   // ret: guess
    /* 300 */ virtual IOReturn getPacketTapInfo(uint32_t* a1, uint32_t* a2);   // ret: guess
    /* 301 */ virtual IOReturn getUnsentDataByteCount(uint32_t* a1, uint32_t* a2, uint32_t a3) const;   // ret: guess
    /* 302 */ virtual IOReturn getSupportedWakeFlags(uint32_t* a1);   // ret: guess
    /* 303 */ virtual IOReturn enableNetworkWake(uint32_t a1);   // ret: guess
    /* 304 */ virtual IOReturn calculateRingSizeForQueue(IOSkywalkPacketQueue const* a1, uint32_t* a2) const;   // ret: guess
    /* 305 */ virtual uint32_t getMaxTransferUnit();   // ret: guess from name
    /* 306 */ virtual void setMaxTransferUnit(uint32_t a1);   // ret: guess from name
    /* 307 */ virtual uint32_t getMinPacketSize();   // ret: guess from name
    /* 308 */ virtual uint32_t getHardwareAssists();   // ret: guess from name
    /* 309 */ virtual void setHardwareAssists(uint32_t a1, uint32_t a2);   // ret: guess from name
    /* 310 */ virtual uint32_t getInterfaceFamily();   // ret: guess from name
    /* 311 */ virtual uint32_t getInterfaceSubFamily();   // ret: guess from name
    /* 312 */ virtual uint32_t getInitialMedia();   // ret: guess from name
    /* 313 */ virtual uint32_t getFeatureFlags();   // ret: guess from name
    /* 314 */ virtual uint32_t getTxDataOffset();   // ret: guess from name
    /* 315 */ virtual IOReturn captureInterfaceState(uint32_t a1);   // ret: guess
    /* 316 */ virtual IOReturn restoreInterfaceState(uint32_t a1);   // ret: guess
    /* 317 */ virtual IOReturn setMTU(uint32_t a1);   // ret: request handler: the ioctl status
    /* 318 */ virtual IOReturn bpfTap(uint32_t a1, uint32_t a2);   // ret: guess
    /* 319 */ virtual const char * getBSDNamePrefix();   // ret: guess from name
    /* 320 */ virtual uint32_t getBSDUnitNumber();   // ret: guess from name
    /* 321 */ virtual const char * classNameOverride();   // ret: guess from name
    /* 322 */ virtual IOReturn deferBSDAttach(bool a1);   // ret: guess
    /* 323 */ virtual IOReturn reportDetailedLinkStatus(if_link_status const* a1);   // ret: guess
    /* 324 */ virtual IOReturn getTSOOptions(IOSkywalkNetworkInterface::IOSkywalkTSOOptions* a1);   // ret: guess
    /* 325 */ virtual void _RESERVEDIOSkywalkNetworkInterface0();
    /* 326 */ virtual void _RESERVEDIOSkywalkNetworkInterface1();
    /* 327 */ virtual void _RESERVEDIOSkywalkNetworkInterface2();
    /* 328 */ virtual void _RESERVEDIOSkywalkNetworkInterface3();
    /* 329 */ virtual void _RESERVEDIOSkywalkNetworkInterface4();
    /* 330 */ virtual void _RESERVEDIOSkywalkNetworkInterface5();
    /* 331 */ virtual void _RESERVEDIOSkywalkNetworkInterface6();
    /* 332 */ virtual void _RESERVEDIOSkywalkNetworkInterface7();
    /* 333 */ virtual void _RESERVEDIOSkywalkNetworkInterface8();
    /* 334 */ virtual void _RESERVEDIOSkywalkNetworkInterface9();

protected:
    uint8_t iOSkywalkNetworkInterfaceOpaque[0xe0 - sizeof(IOSkywalkInterface)];   // ivars of Apple's class: layout not determined
};
static_assert(sizeof(IOSkywalkNetworkInterface) == 0xe0, "IOSkywalkNetworkInterface must be 0xe0 bytes (25G332)");
