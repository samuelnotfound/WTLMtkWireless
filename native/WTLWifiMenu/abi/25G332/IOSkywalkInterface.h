// <wtl> project: mtkwlannative | file: abi/25G332/IOSkywalkInterface.h | author: watchthelight </wtl>
// SPDX-License-Identifier: ISC
// GENERATED from the kcabi dump of 25G332 (macOS 26.7.2 build 25G332, xnu-12377.161.15.701.48~2). Do not edit by hand: re-run genhdr.py.
//
// IOSkywalkInterface : IOService as Apple's kernel collection exports it on this build. Slot numbers count the two vtable header words
// (byte offset in the vtable symbol = slot * 8; from a vptr = slot * 8 - 0x10). Instance size 0xb0 (MetaClass constructor).
// Names are Apple's own symbols; pure slots are named by the first derived class that implements them. Return types are not
// in mangled names: "ret: guess" is a guess from the name. Checked with genhdr.py check (clang's vtable layout of a probe
// subclass against the dump). Method adapted from ga403wm-hackintosh tools/io80211_abi.py (BSD-3-Clause-Clear).
#pragma once
#include <IOKit/IOService.h>
#include <IOKit/IOReturn.h>
#include <stdint.h>

struct ether_addr;
struct __mbuf;

class IOSkywalkInterface : public IOService {
    OSDeclareAbstractStructors(IOSkywalkInterface)

public:
    // overrides of inherited virtuals (declared so that super:: reaches Apple's code)
    /* 20 */ virtual void free() override;
    /* 75 */ virtual bool init(OSDictionary* a1) override;
    /* 130 */ virtual bool willTerminate(IOService* a1, uint32_t a2) override;
    /* 131 */ virtual bool didTerminate(IOService* a1, uint32_t a2, bool* a3) override;
    /* 191 */ virtual bool handleOpen(IOService* a1, uint32_t a2, void* a3) override;
    /* 192 */ virtual void handleClose(IOService* a1, uint32_t a2) override;
    /* 193 */ virtual bool handleIsOpen(IOService const* a1) const override;

    // new virtuals, slots 268..283
    /* 268 */ virtual IOReturn enable(uint32_t a1) = 0;   // ret: guess
    /* 269 */ virtual IOReturn disable(uint32_t a1) = 0;   // ret: guess
    /* 270 */ virtual IOReturn clientConnectWithTask(task* a1, IOService* a2, uint32_t a3);   // ret: guess
    /* 271 */ virtual IOReturn clientDisconnect(IOService* a1, uint32_t a2);   // ret: guess
    /* 272 */ virtual bool isTerminating();   // ret: guess from name
    /* 273 */ virtual void _RESERVEDIOSkywalkInterface0();
    /* 274 */ virtual void _RESERVEDIOSkywalkInterface1();
    /* 275 */ virtual void _RESERVEDIOSkywalkInterface2();
    /* 276 */ virtual void _RESERVEDIOSkywalkInterface3();
    /* 277 */ virtual void _RESERVEDIOSkywalkInterface4();
    /* 278 */ virtual void _RESERVEDIOSkywalkInterface5();
    /* 279 */ virtual void _RESERVEDIOSkywalkInterface6();
    /* 280 */ virtual void _RESERVEDIOSkywalkInterface7();
    /* 281 */ virtual void _RESERVEDIOSkywalkInterface8();
    /* 282 */ virtual void _RESERVEDIOSkywalkInterface9();
    /* 283 */ virtual void _RESERVEDIOSkywalkInterface10();

protected:
    uint8_t iOSkywalkInterfaceOpaque[0xb0 - sizeof(IOService)];   // ivars of Apple's class: layout not determined
};
static_assert(sizeof(IOSkywalkInterface) == 0xb0, "IOSkywalkInterface must be 0xb0 bytes (25G332)");
