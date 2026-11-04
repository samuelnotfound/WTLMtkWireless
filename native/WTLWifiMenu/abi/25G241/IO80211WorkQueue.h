// <wtl> project: mtkwlannative | file: abi/25G241/IO80211WorkQueue.h | author: watchthelight </wtl>
// SPDX-License-Identifier: ISC
// GENERATED from the kcabi dump of 25G241 (macOS 26.7.1 build 25G241, xnu-12377.161.15.700.19~2). Do not edit by hand: re-run genhdr.py.
//
// IO80211WorkQueue : IOWorkLoop as Apple's kernel collection exports it on this build. Slot numbers count the two vtable header words
// (byte offset in the vtable symbol = slot * 8; from a vptr = slot * 8 - 0x10). Instance size 0x50 (MetaClass constructor).
// Names are Apple's own symbols; pure slots are named by the first derived class that implements them. Return types are not
// in mangled names: "ret: guess" is a guess from the name. Checked with genhdr.py check (clang's vtable layout of a probe
// subclass against the dump). Method adapted from ga403wm-hackintosh tools/io80211_abi.py (BSD-3-Clause-Clear).
#pragma once
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOReturn.h>
#include <stdint.h>

struct ether_addr;
struct __mbuf;

class IO80211WorkQueue : public IOWorkLoop {
    OSDeclareDefaultStructors(IO80211WorkQueue)

public:
    // overrides of inherited virtuals (declared so that super:: reaches Apple's code)
    /* 39 */ virtual IOThread getThread() const override;
    /* 46 */ virtual void enableAllInterrupts() const override;
    /* 47 */ virtual void disableAllInterrupts() const override;
    /* 54 */ virtual IOReturn runAction(int (*a1)(OSObject*, void*, void*, void*, void*), OSObject* a2, void* a3, void* a4, void* a5, void* a6) override;

    // new virtuals, slots 65..66
    /* 65 */ virtual IOReturn commandSleep(void* a1, uint64_t a2);   // ret: guess
    /* 66 */ virtual IOReturn commandWakeup(void* a1);   // ret: guess

    static IO80211WorkQueue *workQueue();   // exported: __ZN16IO80211WorkQueue9workQueueEv; factory; runs the virtual init

protected:
    uint8_t iO80211WorkQueueOpaque[0x50 - sizeof(IOWorkLoop)];   // ivars of Apple's class: layout not determined
};
static_assert(sizeof(IO80211WorkQueue) == 0x50, "IO80211WorkQueue must be 0x50 bytes (25G241)");
