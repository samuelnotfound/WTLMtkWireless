// <wtl> project: mtkwlannative | file: src/MtkNativeTrace.hpp | author: watchthelight </wtl>
// SPDX-License-Identifier: ISC
// Evidence that survives a boot: IOLog lines from third-party kexts do not reach the unified log on this machine, a registry
// property does (ioreg -l). nativeTrace appends "; <text>" to MtkNativeTrace on the entry (first 1500 bytes), then to
// MtkNativeTraceTail; MtkNativeLast always holds the newest line. nativeCall counts (kind, id) pairs, for example which
// requests userland sends and which handler stubs it reaches, and publishes them as MtkNativeCalls when an entry is new or
// its count reaches a power of two. Nothing here touches packet bytes, keys or network names.
#pragma once
#include <IOKit/IORegistryEntry.h>
#include <stdint.h>

void nativeTrace(IORegistryEntry *e, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
// kinds: 'I' controller isCommandProhibited(cmd), 'C' controller handler (request id), 'S' interface stub (slot),
// 'H' handleCardSpecific, 'U' newUserClient type (controller), 'W' newUserClient type (interface), 'B' BSD command,
// 'P' event posted (code), 'E' enable (1) / disable (0)
void nativeCall(IORegistryEntry *e, char kind, uint32_t id);
void nativeCallInit();
void nativeCallFree();
