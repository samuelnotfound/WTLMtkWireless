// <wtl> project: mtkwlannative | file: abi/25G241/IO80211CommandGate.h | author: watchthelight </wtl>
// SPDX-License-Identifier: ISC
// GENERATED from the kcabi dump of 25G241 (macOS 26.7.1 build 25G241, xnu-12377.161.15.700.19~2). Do not edit by hand: re-run genhdr.py.
//
// IO80211CommandGate : OSObject as Apple's kernel collection exports it on this build. Slot numbers count the two vtable header words
// (byte offset in the vtable symbol = slot * 8; from a vptr = slot * 8 - 0x10). Instance size 0x18 (MetaClass constructor).
// Names are Apple's own symbols; pure slots are named by the first derived class that implements them. Return types are not
// in mangled names: "ret: guess" is a guess from the name. Checked with genhdr.py check (clang's vtable layout of a probe
// subclass against the dump). Method adapted from ga403wm-hackintosh tools/io80211_abi.py (BSD-3-Clause-Clear).
#pragma once
#include <libkern/c++/OSObject.h>
#include <IOKit/IOReturn.h>
#include <stdint.h>

struct ether_addr;
struct __mbuf;
class IO80211WorkQueue;

class IO80211CommandGate : public OSObject {
    OSDeclareDefaultStructors(IO80211CommandGate)

public:
    // overrides of inherited virtuals (declared so that super:: reaches Apple's code)
    /* 20 */ virtual void free() override;

    // new virtuals, slots 37..42
    /* 37 */ virtual IOReturn runAction(int (*a1)(OSObject*, void*, void*, void*, void*), void* a2, void* a3, void* a4, void* a5);   // ret: guess
    /* 38 */ virtual IOReturn runAction(OSObject* a1, int (*a2)(OSObject*, void*, void*, void*, void*), void* a3, void* a4, void* a5, void* a6);   // ret: guess
    /* 39 */ virtual IOReturn runActionBlock(int (^a1)());   // ret: guess
    /* 40 */ virtual IOReturn commandSleep(void* a1, uint64_t a2);   // ret: guess
    /* 41 */ virtual IOReturn commandWakeup(void* a1);   // ret: guess
    /* 42 */ virtual IOReturn initWithParams(OSObject* a1, IO80211WorkQueue* a2);   // ret: guess

protected:
    uint8_t iO80211CommandGateOpaque[0x18 - sizeof(OSObject)];   // ivars of Apple's class: layout not determined
};
static_assert(sizeof(IO80211CommandGate) == 0x18, "IO80211CommandGate must be 0x18 bytes (25G241)");
