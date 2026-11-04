// <wtl> project: mtkwlannative | file: src/MtkNativeAbi.hpp | author: watchthelight </wtl>
// SPDX-License-Identifier: ISC
// The runtime ABI guard. This kext subclasses private classes of IO80211Family and IOSkywalkFamily with headers generated
// from one build's kernel collection (abi/<build>/), so it may only engage on a build those headers were verified against.
// probe() checks two things from the kext's own MtkNativeAbi property (filled from abi/<build>/abi.json by build-mac.sh and
// extended by tools/kcabi/refresh.sh after an update):
//   1. the running kernel's version string (libkern/version.h) contains the xnu tag of one allow-listed build;
//   2. every class size recorded for that build equals OSMetaClass::getClassSize() of the live class.
// Anything else declines, and the core starts its Ethernet front end instead (Wi-Fi keeps working, only the native UI is
// missing until the headers are regenerated).
#pragma once
#include <IOKit/IOService.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSSymbol.h>
#include <libkern/version.h>
#include <libkern/libkern.h>
#include <string.h>

// the kernel's string.h has no strstr
inline bool mtkNativeContains(const char *hay, const char *needle) {
    size_t n = strlen(needle);
    if (!n) return false;
    for (const char *p = hay; *p; p++)
        if (!strncmp(p, needle, n)) return true;
    return false;
}

struct MtkNativeAbiResult {
    bool ok;
    char build[16];     // the matching allow-list entry, for the trace
    char why[160];      // the first failure
};

// Pure on purpose (no registry reads inside): host tests feed it fake inputs. sizeOf returns 0 for a class that does not exist.
template <typename SizeOf>
inline MtkNativeAbiResult mtkNativeAbiCheck(const char *kernelVersion, OSDictionary *abi, SizeOf sizeOf) {
    MtkNativeAbiResult r = {};
    OSArray *builds = abi ? OSDynamicCast(OSArray, abi->getObject("Builds")) : nullptr;
    if (!builds || !builds->getCount()) {
        snprintf(r.why, sizeof(r.why), "no allow-listed build in MtkNativeAbi");
        return r;
    }
    for (unsigned i = 0; i < builds->getCount(); i++) {
        OSDictionary *b = OSDynamicCast(OSDictionary, builds->getObject(i));
        OSString *xnu = b ? OSDynamicCast(OSString, b->getObject("Xnu")) : nullptr;
        OSString *name = b ? OSDynamicCast(OSString, b->getObject("Build")) : nullptr;
        OSDictionary *sizes = b ? OSDynamicCast(OSDictionary, b->getObject("Sizes")) : nullptr;
        if (!xnu || !name || !sizes || !xnu->getLength() || !mtkNativeContains(kernelVersion, xnu->getCStringNoCopy())) continue;
        strlcpy(r.build, name->getCStringNoCopy(), sizeof(r.build));
        OSCollectionIterator *it = OSCollectionIterator::withCollection(sizes);
        if (!it) {
            snprintf(r.why, sizeof(r.why), "no memory");
            return r;
        }
        while (OSSymbol *key = OSDynamicCast(OSSymbol, it->getNextObject())) {
            OSNumber *want = OSDynamicCast(OSNumber, sizes->getObject(key));
            unsigned have = sizeOf(key->getCStringNoCopy());
            if (!want || !have || have != want->unsigned32BitValue()) {
                snprintf(r.why, sizeof(r.why), "%s: class size %u, %s expects %u", key->getCStringNoCopy(), have,
                         r.build, want ? want->unsigned32BitValue() : 0u);
                it->release();
                return r;
            }
        }
        it->release();
        r.ok = true;
        return r;
    }
    // "Darwin Kernel Version 25.6.0: ...; root:xnu-12377.161.15.700.19~2/RELEASE_X86_64": keep the xnu tag for the log
    const char *tag = kernelVersion;
    while (*tag && strncmp(tag, "xnu-", 4)) tag++;
    snprintf(r.why, sizeof(r.why), "kernel %s is not in the allow-list", *tag ? tag : kernelVersion);
    return r;
}

// The live lookup: a class that is not registered reads as size 0.
inline unsigned mtkNativeClassSize(const char *name) {
    const OSSymbol *sym = OSSymbol::withCString(name);
    if (!sym) return 0;
    const OSMetaClass *mc = OSMetaClass::getMetaClassWithName(sym);
    sym->release();
    return mc ? mc->getClassSize() : 0;
}
