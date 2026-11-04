// <wtl> project: mtkwlannative | file: src/MtkNativeTrace.cpp | author: watchthelight </wtl>
// SPDX-License-Identifier: ISC
// See MtkNativeTrace.hpp. The append-to-property idea comes from ga403wm-hackintosh (itlmtTrace); this is a rewrite.
#include "MtkNativeTrace.hpp"
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <libkern/c++/OSString.h>
#include <libkern/libkern.h>
#include <stdarg.h>
#include <string.h>

namespace {
constexpr size_t kTraceCap = 1500;
constexpr uint32_t kCallCap = 192;
constexpr size_t kCallText = 6144;

struct CallEntry { uint32_t id, count; char kind; };
CallEntry *gCalls = nullptr;
uint32_t gCallN = 0, gCallOverflow = 0;
char *gCallText = nullptr;
IOLock *gLock = nullptr;

void appendProperty(IORegistryEntry *e, const char *key, const char *add, bool &full) {
    char buf[kTraceCap + 64];
    buf[0] = 0;
    OSObject *snapshot = e->copyProperty(key);
    if (OSString *old = OSDynamicCast(OSString, snapshot)) strlcpy(buf, old->getCStringNoCopy(), sizeof(buf));
    if (snapshot) snapshot->release();
    if (buf[0]) strlcat(buf, "; ", sizeof(buf));
    full = strlcat(buf, add, kTraceCap) >= kTraceCap;
    if (!full) e->setProperty(key, buf);
}
} // namespace

void nativeTrace(IORegistryEntry *e, const char *fmt, ...) {
    if (!e) return;
    char add[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(add, sizeof(add), fmt, ap);
    va_end(ap);
    IOLog("MtkWlanNative: %s\n", add);
    e->setProperty("MtkNativeLast", add);
    bool full = false;
    appendProperty(e, "MtkNativeTrace", add, full);
    if (full) appendProperty(e, "MtkNativeTraceTail", add, full);
}

void nativeCallInit() {
    if (gCalls) return;
    gLock = IOLockAlloc();
    gCalls = (CallEntry *)IOMallocZero(sizeof(CallEntry) * kCallCap);
    gCallText = (char *)IOMallocZero(kCallText);
}

void nativeCallFree() {
    if (gCalls) IOFree(gCalls, sizeof(CallEntry) * kCallCap);
    if (gCallText) IOFree(gCallText, kCallText);
    if (gLock) IOLockFree(gLock);
    gCalls = nullptr;
    gCallText = nullptr;
    gLock = nullptr;
    gCallN = gCallOverflow = 0;
}

void nativeCall(IORegistryEntry *e, char kind, uint32_t id) {
    if (!gCalls || !gLock) return;
    IOLockLock(gLock);
    uint32_t i = 0;
    for (; i < gCallN; i++)
        if (gCalls[i].kind == kind && gCalls[i].id == id) break;
    bool publish = false;
    if (i == gCallN) {
        if (gCallN < kCallCap) {
            gCalls[gCallN++] = {id, 1, kind};
            publish = true;
        } else {
            gCallOverflow++;
        }
    } else {
        uint32_t c = ++gCalls[i].count;
        publish = (c & (c - 1)) == 0;          // 2, 4, 8, ...: the counts stay readable without costing the hot path
    }
    if (publish && e && gCallText) {
        size_t k = 0;
        for (uint32_t j = 0; j < gCallN && k + 24 < kCallText; j++)
            k += (size_t)snprintf(gCallText + k, kCallText - k, "%s%c:%x=%u", j ? " " : "", gCalls[j].kind, gCalls[j].id, gCalls[j].count);
        if (gCallOverflow && k + 24 < kCallText) snprintf(gCallText + k, kCallText - k, " overflow=%u", gCallOverflow);
        e->setProperty("MtkNativeCalls", gCallText);
    }
    IOLockUnlock(gLock);
}
