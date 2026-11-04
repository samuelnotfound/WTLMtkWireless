// <wtl> project: mtkwlannative | file: src/corecapture_min.h | author: watchthelight </wtl>
// SPDX-License-Identifier: ISC
// The CoreCapture / IO80211 factory functions the native front end calls to give IO80211Controller::start the logger and
// fault reporter it needs (a null one panics in start). The classes are opaque: created through these exported static
// factories (mangled names in the kcabi dump of 25G241/CC*.methods.txt and IO80211FaultReporter.methods.txt) and only
// retained / released afterwards. Option block layouts: research of ga403wm-hackintosh docs/IO80211-ABI-2.md section 1
// (BSD-3-Clause-Clear, thanhnndev and contributors), re-checked against the 25G241 dump (CCPipe / CCStream unchanged).
#pragma once
#include <IOKit/IOService.h>
#include <IOKit/IOWorkLoop.h>
#include <stdint.h>

// Apple allocates and zeroes the option blocks; only the documented offsets are written.
static const size_t kCCPipeOptionsSize = 0x350;
static const size_t kCCStreamOptionsSize = 0x358;
struct CCPipeOptions;
struct CCStreamOptions;

class CCPipe {
public:
    // __ZN6CCPipe21withOwnerNameCapacityEP9IOServicePKcS3_PK13CCPipeOptions; options+0 selects the class: 0 CCLogPipe, 1 CCDataPipe
    static CCPipe *withOwnerNameCapacity(IOService *owner, const char *ownerId, const char *name, const CCPipeOptions *opts);
};

class CCStream {
public:
    // __ZN8CCStream15withPipeAndNameEP6CCPipePKcPK15CCStreamOptions; options+0 selects the class: 0 CCLogStream, 1 CCDataStream
    static CCStream *withPipeAndName(CCPipe *pipe, const char *name, const CCStreamOptions *opts);
};

class CCDataStream;
class CCFaultReporter {
public:
    // __ZN15CCFaultReporter18withStreamWorkloopEP12CCDataStreamP10IOWorkLoop; both arguments must be non-null
    static CCFaultReporter *withStreamWorkloop(CCDataStream *stream, IOWorkLoop *wl);
};

class IO80211FaultReporter {
public:
    // __ZN20IO80211FaultReporter15allocWithParamsEP15CCFaultReporter; what getFaultReporterFromDriver returns
    static IO80211FaultReporter *allocWithParams(CCFaultReporter *reporter);
};
