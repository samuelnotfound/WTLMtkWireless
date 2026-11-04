#!/usr/bin/env python3
# <wtl> project: mtkwlannative | file: tools/kcabi/genhdr.py | author: watchthelight </wtl>
# SPDX-License-Identifier: ISC
# Project Legion: slot-exact C++ headers for the private IO80211Family / IOSkywalkFamily classes a native Wi-Fi front end
# subclasses, generated from one build's kcabi dump (dump.py), and the checks that prove them. The method (names from Apple's
# own vtable symbols, pure slots named by the first derived class that overrides them, return types from the SDK header
# that declares an override or from call-site evidence, otherwise a tagged guess) and the tables of known return types are
# adapted from tools/io80211_abi.py, gen_io80211_header.py, io80211_check.py, gen_native_iface.py and check_native_slots.py
# of ga403wm-hackintosh (BSD-3-Clause-Clear, Copyright (c) 2026 thanhnndev and contributors; audit in
# research/repository-audits/ga403wm-hackintosh.md). Differences: class sizes and the build come from the dump, every class
# has its own vtable dump, the SDK is the Command Line Tools kernel SDK, and the stub list is derived from the kext source.
"""
usage: genhdr.py gen   DUMP OUT [--iface-class NAME --impl SRC...]   write OUT/<Class>.h, OUT/iface_stubs.inc, OUT/abi.json
       genhdr.py check DUMP OUT                                       probe-compile every header, compare slots with the dump
       genhdr.py check-native DUMP CLASS APPLECLASS SRC -- CLANGFLAGS  compile SRC with the kext flags, compare CLASS's
                                                                      overrides with APPLECLASS's vtable (names per slot)
Exit status 1 on any mismatch. Needs clang++ and c++filt (Command Line Tools).
"""
import json
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..', '..'))

# class -> parent, the header the parent comes from, derived dumps that name its pure slots (first wins)
CLASSES = {
    'IOSkywalkInterface': dict(parent='IOService', include='<IOKit/IOService.h>', children=['IOSkywalkNetworkInterface']),
    'IO80211WorkQueue': dict(parent='IOWorkLoop', include='<IOKit/IOWorkLoop.h>', children=[]),
    'IO80211CommandGate': dict(parent='OSObject', include='<libkern/c++/OSObject.h>', children=[]),
    'IOSkywalkNetworkInterface': dict(parent='IOSkywalkInterface', include='"IOSkywalkInterface.h"',
                                      children=['IOSkywalkEthernetInterface']),
    'IOSkywalkEthernetInterface': dict(parent='IOSkywalkNetworkInterface', include='"IOSkywalkNetworkInterface.h"',
                                       children=['IO80211SkywalkInterface']),
    'IO80211SkywalkInterface': dict(parent='IOSkywalkEthernetInterface', include='"IOSkywalkEthernetInterface.h"',
                                    children=['IO80211InfraInterface', 'AppleBCMWLANSkywalkInterface']),
    'IO80211InfraInterface': dict(parent='IO80211SkywalkInterface', include='"IO80211SkywalkInterface.h"',
                                  children=['IO80211InfraProtocol', 'AppleBCMWLANSkywalkInterface']),
    'IO80211InfraProtocol': dict(parent='IO80211InfraInterface', include='"IO80211InfraInterface.h"',
                                 children=['AppleBCMWLANInfraProtocol', 'AppleBCMWLANSkywalkInterface']),
    'IO80211Controller': dict(parent='IOEthernetController', include='<IOKit/network/IOEthernetController.h>',
                              children=['AppleBCMWLANCore']),
}
ORDER = ['IOSkywalkInterface', 'IO80211WorkQueue', 'IO80211CommandGate', 'IOSkywalkNetworkInterface',
         'IOSkywalkEthernetInterface', 'IO80211SkywalkInterface', 'IO80211InfraInterface', 'IO80211InfraProtocol',
         'IO80211Controller']
# classes whose instance size the kext's runtime guard checks (abi.json); all must be in sizes.txt
GUARD_CLASSES = ['IO80211Controller', 'IO80211WorkQueue', 'IO80211CommandGate', 'IOSkywalkInterface',
                 'IOSkywalkNetworkInterface', 'IOSkywalkEthernetInterface', 'IO80211SkywalkInterface',
                 'IO80211InfraInterface', 'IO80211InfraProtocol', 'IOSkywalkPacketBufferPool', 'IOSkywalkPacketQueue',
                 'IOSkywalkTxSubmissionQueue', 'IOSkywalkRxSubmissionQueue', 'IOSkywalkTxCompletionQueue',
                 'IOSkywalkRxCompletionQueue']

# exported non-virtual members the front end calls (no layout effect); the symbol must be in <Class>.methods.txt
STATICS = {
    'IOSkywalkEthernetInterface': [
        (None, 'struct RegistrationInfo;', 'opaque 0x130-byte block (version 1, size 0x130, MAC at +0x108)'),
        ('__ZN26IOSkywalkEthernetInterface20initRegistrationInfoEPNS_16RegistrationInfoEjm',
         'IOReturn initRegistrationInfo(RegistrationInfo* info, uint32_t version, unsigned long size);',
         'called with (info, 1, 0x130) on a zeroed block'),
    ],
    'IO80211SkywalkInterface': [
        ('__ZN23IO80211SkywalkInterface16getInterfaceRoleEv', 'uint32_t getInterfaceRole();', 'compared with 1 by Apple\'s driver'),
        ('__ZN23IO80211SkywalkInterface16setInterfaceRoleEj', 'void setInterfaceRole(uint32_t role);', 'set before start'),
        ('__ZN23IO80211SkywalkInterface14getInterfaceIdEv', 'uint32_t getInterfaceId();', 'queue factories take it as ifId'),
        ('__ZN23IO80211SkywalkInterface12getWorkQueueEv', 'IO80211WorkQueue* getWorkQueue();', 'the interface\'s work queue'),
        ('__ZN23IO80211SkywalkInterface14setInterfaceIdEj', 'void setInterfaceId(uint32_t id);', 'set before start'),
    ],
    'IO80211InfraInterface': [
        ('__ZN21IO80211InfraInterface30registerInfraEthernetInterfaceEPN26IOSkywalkEthernetInterface16RegistrationInfoEPP20IOSkywalkPacketQueuejP25IOSkywalkPacketBufferPoolS7_',
         'IOReturn registerInfraEthernetInterface(IOSkywalkEthernetInterface::RegistrationInfo* info, IOSkywalkPacketQueue** queues, uint32_t count, IOSkywalkPacketBufferPool* txPool, IOSkywalkPacketBufferPool* rxPool);',
         'registers the queue array and the pools'),
    ],
    'IO80211WorkQueue': [
        ('__ZN16IO80211WorkQueue9workQueueEv', 'static IO80211WorkQueue *workQueue();', 'factory; runs the virtual init'),
    ],
    'IO80211Controller': [
        ('__ZN17IO80211Controller11postMessageEP23IO80211SkywalkInterfacejPvmb',
         'IOReturn postMessage(IO80211SkywalkInterface* iface, uint32_t code, void* data, unsigned long length, bool flag);',
         'hands a driver event to the post office; what Apple\'s driver calls for every event'),
    ],
}

# overrides whose declaring class is outside the SDK subset or not unique in it: name -> return type (None = skip)
RET_OVERRIDE = {
    'init': 'bool', 'free': 'void', 'start': 'bool', 'stop': 'void', 'joinPMtree': 'void', 'setAggressiveness': 'IOReturn',
    'setPowerState': 'IOReturn', 'maxCapabilityForDomainState': 'unsigned long',
    'initialPowerStateForDomainState': 'unsigned long', 'newUserClient': 'IOReturn', 'stringFromReturn': 'const char *',
    'errnoFromReturn': 'int', 'configureReport': 'IOReturn', 'updateReport': 'IOReturn', 'enable': 'IOReturn',
    'disable': 'IOReturn', 'getRunLoop': None, 'getWorkLoop': 'IOWorkLoop *', 'createWorkLoop': 'bool',
    'getHardwareAddress': 'IOReturn', 'setPromiscuousMode': 'IOReturn', 'setMulticastMode': 'IOReturn',
}
# new virtuals: name -> (return type, evidence); everything else goes through ret_for's rules (guess)
RET = {
    'getLogger': ('CCLogStream *', 'result used with CCLogStream::shouldLog'),
    'getNumTxQueues': ('uint8_t', 'Apple\'s interface returns a byte'),
    'getTxHeadroom': ('uint16_t', 'Apple\'s interface returns a word'),
    'getTxQueueDepth': ('uint32_t', 'dword'),
    'getRxQueueCapacity': ('uint32_t', 'dword'),
    'setMacAddress': ('IOReturn', 'Apple\'s interface returns its core\'s status'),
    'createWorkQueue': ('bool', 'Apple\'s driver returns a success flag; the base returns 0'),
    'getWorkQueue': ('IO80211WorkQueue *', 'result passed to IO80211CommandGate::allocWithParams'),
    'getIO80211CommandGate': ('IO80211CommandGate *', 'guess from name'),
    'getPrimarySkywalkInterface': ('IO80211SkywalkInterface *', 'guess from name'),
    'getFaultReporterFromDriver': ('IO80211FaultReporter *', 'start() retains it and panics when null'),
    'attachInterface': ('bool', 'base returns setne al'),
    'CreatePostOffice': ('IO80211PostOffice *', 'result stored in the ivars, null-checked'),
    'getPostOffice': ('IO80211PostOffice *', 'guess from name'),
    'isCommandProhibited': ('int', 'errno: 0 = allowed, 0x3a (ENOTSUP) = refused, returned to the ioctl caller'),
    'detachInterface': ('IOReturn', 'guess'),
}
# request handlers (get/set + an upper-case request name) return the ioctl status whatever class declares them
REQUEST_HANDLER = re.compile(r'^(get|set)[A-Z0-9][A-Z0-9_]*$')
PROTOCOL_HANDLER = re.compile(r'^(get|set)[A-Z0-9][A-Z0-9_]*$')
PTR = {
    'createPeerManager': 'IO80211PeerManager *', 'createPeer': 'IO80211Peer *', 'getScanManager': 'IO80211ScanManager *',
    'getController': 'IO80211Controller *', 'getP2PSkywalkPeerMgr': 'IO80211PeerManager *', 'findPeer': 'IO80211Peer *',
    'getWorkerMatchingDict': 'OSDictionary *', 'getSelfMacAddr': 'ether_addr *', 'getPacketPool': 'IOSkywalkPacketBufferPool *',
    'getMultiCastQueue': 'IOSkywalkPacketQueue *', 'getRxCompQueue': 'IOSkywalkRxCompletionQueue *',
    'getTxCompQueue': 'IOSkywalkTxCompletionQueue *', 'getTxSubQueue': 'IOSkywalkTxSubmissionQueue *',
    'getTxPacketPool': 'IOSkywalkPacketBufferPool *', 'getRxPacketPool': 'IOSkywalkPacketBufferPool *',
    'getRingMD': 'IOMemoryDescriptor *', 'classNameOverride': 'const char *', 'getBSDName': 'const char *',
    'getBSDNamePrefix': 'const char *', 'getBSDInterface': '__ifnet *', 'findOrCreateFlowQueue': 'IO80211FlowQueue *',
    'findOrCreateFlowQueueWithCache': 'IO80211FlowQueue *', 'findExistingFlowQueue': 'IO80211FlowQueue *',
    'getLinkLayerAddress': 'ether_addr *',
}
UINT = {'getMaxTransferUnit', 'getMinPacketSize', 'getHardwareAssists', 'getInterfaceFamily', 'getInterfaceSubFamily',
        'getInitialMedia', 'getFeatureFlags', 'getTxDataOffset', 'getBSDUnitNumber', 'getAssocState', 'getEventPipeSize',
        'getDataQueueDepth', 'linkState', 'debugFlags'}
VOID_PREFIX = ('log', 'record', 'update', 'notify', 'flush', 'destroy', 'set', 'vlog')
ENUMS = {'IO80211CountryCodeOp', 'IO80211FeatureCode', 'apple80211_wme_ac', 'IO80211LinkState'}
BUILTIN = {'void', 'bool', 'int', 'char', 'const', 'unsigned', 'long', 'short', 'uint8_t', 'uint16_t', 'uint32_t',
           'uint64_t', 'struct', 'size_t', '__builtin_va_list', 'block_pointer'}
SDK_TYPES = {'IOReportChannelList', 'OSObject', 'OSDictionary', 'OSString', 'OSSymbol', 'IOService', 'IOReturn',
             'IOUserClient', 'IOMemoryDescriptor', 'IOWorkLoop', 'IORegistryEntry', 'OSMetaClass', 'IONetworkMedium',
             'IOEthernetAddress', 'IONetworkInterface', 'OSData', 'IOEthernetController', 'IOOutputQueue', 'IOEventSource',
           'IOTimerEventSource', 'IOPMPowerState', 'IOOptionBits', 'UInt32', 'UInt64', 'UInt8', 'UInt16', 'SInt32',
           'IOByteCount', 'IOPhysicalAddress', 'IOVirtualAddress', 'IOKernelDebugger', 'IOMbufMemoryCursor', 'IOPacketQueue',
           'IONetworkStack', 'OSArray', 'OSSet', 'OSNumber', 'OSBoolean', 'OSIterator', 'OSCollection', 'IORegistryPlane',
           'IOCommandGate', 'IOInterruptEventSource', 'IOFilterInterruptEventSource', 'IOServiceNotification',
           'IOServiceInterestContent', 'IONotifier', 'task', 'task_t', 'mach_timespec_t', 'IOPMrootDomain', 'IOPMDriverAssertionID',
           'thread_call', 'thread_call_t', 'kmod_info_t', 'IOFixed', 'IOPMPowerFlags', 'IOPMPowerStateIndex', 'IOPMRequestTag',
           'IOReportConfigureAction', 'IOReportUpdateAction', 'IOReportLegend', 'IOReporter', 'IOReportChannelType',
           'IOReportCategories', 'IOReportUnit', 'IOReportSampleUnit', 'IOReportCategoriesType', 'IOReportChannelListPointer',
           'mbuf_t', 'ifnet_t', 'errno_t', 'IOOptionBits', 'IOPMPowerStateIndex', 'IOEthernetInterface', 'IOBasicOutputQueue'}
# by-value parameters of virtuals: complete dummy definitions so that the probe can override the slot
BYVALUE_STUBS = ['IO80211FlowQueueHash', 'apple80211_channel', 'nicproxy_limits_info_s']

# --------------------------------------------------------------------------------------------------- dump reading


def vt(d, cls):
    out = {}
    path = os.path.join(d, cls + '.vtable.txt')
    if not os.path.exists(path):
        return out
    for line in open(path):
        p = line.rstrip('\n').split('\t')
        if len(p) == 3 and p[0].isdigit():
            out[int(p[0])] = p[2]
    return out


def sizes(d):
    out = {}
    for line in open(os.path.join(d, 'sizes.txt')):
        p = line.rstrip('\n').split('\t')
        if len(p) >= 2 and not p[0].startswith('#') and p[1].startswith('0x'):
            out[p[0]] = int(p[1], 16)
    return out


def source_info(d):
    src = json.load(open(os.path.join(d, 'source.json')))
    m = re.search(r'xnu-[\w.~-]+', src.get('kernel_version', ''))
    return dict(build=src.get('build', '?'), product=src.get('product_version', '?'), xnu=m.group(0) if m else '?',
                kernel_version=src.get('kernel_version', '?'), sha256=src.get('sha256', '?'))


def demangle_all(syms):
    syms = sorted(set(syms))
    if not syms:
        return {}
    res = subprocess.run(['c++filt', '-n'], input='\n'.join(s[1:] if s.startswith('_') else s for s in syms),
                         capture_output=True, text=True, check=True).stdout.split('\n')
    return dict(zip(syms, res))


def split_params(s):
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch in '(<[':
            depth += 1
        elif ch in ')>]':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def norm(t):
    t = t.replace('unsigned long long', 'uint64_t').replace('unsigned short', 'uint16_t')
    t = t.replace('unsigned char', 'uint8_t').replace('unsigned int', 'uint32_t')
    t = t.replace('__va_list_tag*', '__builtin_va_list').replace('__va_list_tag *', '__builtin_va_list')
    return t


def declarator(t, n):
    t = norm(t)
    if '(*)' in t:
        return t.replace('(*)', '(*a%d)' % n, 1)
    if t.endswith(' block_pointer'):
        m = re.match(r'(.*?) (\(.*\))$', t[:-len(' block_pointer')])
        return '%s (^a%d)%s' % (m.group(1), n, m.group(2))
    return '%s a%d' % (t, n)


def parse_method(dem):
    m = re.match(r'(.+?)::(~?[\w]+)\((.*)\)( const)?$', dem)
    if not m:
        return None
    owner, name, args, const = m.groups()
    return owner, name, split_params(args), bool(const)


def ret_for(name, params, const, cls=None):
    if name == 'init' and len(params) == 2 and 'ether_addr' in params[1]:
        return 'bool', 'init(provider, address) returns bool'
    if name in ('setLinkState', 'setLinkStateInternal') and len(params) >= 4:
        return 'bool', 'returns a success flag'
    if name == 'setHardwareAddress':
        return 'IOReturn', 'status of the MAC address agent'
    if name in RET:
        return RET[name][0], RET[name][1]
    if REQUEST_HANDLER.match(name):
        return 'IOReturn', 'request handler: the ioctl status'
    if name in PTR:
        return PTR[name], 'guess from name'
    if name in UINT:
        return 'uint32_t', 'guess from name'
    if re.match(r'(is|should|was|has)[A-Z]', name) or name.endswith('Enabled') or (name.endswith('Enable') and not params):
        return 'bool', 'guess from name'
    if name.startswith(VOID_PREFIX) and name not in ('setLinkState', 'setScanningState'):
        return 'void', 'guess from name'
    return 'IOReturn', 'guess'


def analyse(d, cls):
    """overrides and new virtuals of cls: slot, name, params, const, pure, ret, why; parent_last, last, unnamed pure slots"""
    conf = CLASSES[cls]
    mine = vt(d, cls)
    if not mine:
        raise SystemExit('%s: no vtable dump in %s' % (cls, d))
    last = max(s for s, v in mine.items() if v != '-')
    kids = [vt(d, c) for c in conf['children']]
    kids = [k for k in kids if k]

    def sym_at(slot):
        s = mine[slot]
        if s != '___cxa_pure_virtual':
            return s, False
        for k in kids:
            if slot in k and k[slot] not in ('___cxa_pure_virtual', '-'):
                return k[slot], True
        return None, True

    dem = demangle_all({v for v in mine.values() if v.startswith('__Z')} |
                       {k[s] for k in kids for s in k if k[s].startswith('__Z')})
    owner_of = {}
    for s in range(2, last + 1):
        v = mine[s]
        pm = parse_method(dem[v]) if v.startswith('__Z') else None
        owner_of[s] = pm[0] if pm else None
    parent_last = max(s for s in range(2, last + 1) if owner_of[s] not in (None, cls))
    new, over, pure_unnamed = [], [], []
    for s in range(2, last + 1):
        sym, pure = sym_at(s)
        if mine[s] == '___cxa_pure_virtual' and s <= parent_last:
            continue                           # inherited pure virtual: not declared here
        if sym is None:
            pure_unnamed.append(s)
            continue
        pm = parse_method(dem[sym])
        if pm is None:
            continue
        owner, name, params, const = pm
        if not pure and owner != cls:
            continue
        if s in (2, 3, 9):
            continue                           # destructor pair, getMetaClass: OSDeclare*Structors
        rec = dict(slot=s, name=name, params=params, const=const, pure=pure)
        if s > parent_last:
            rec['ret'], rec['why'] = ret_for(name, params, const, cls)
            new.append(rec)
        else:
            over.append(rec)
    return dict(conf=conf, new=new, over=over, parent_last=parent_last, last=last, pure_unnamed=pure_unnamed)


def sdk_headers():
    sdk = subprocess.run(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], capture_output=True, text=True, check=True).stdout.strip()
    return os.path.join(sdk, 'System/Library/Frameworks/Kernel.framework/Headers')


SDK_FILES = {
    'OSObject': ['libkern/c++/OSObject.h', 'libkern/c++/OSMetaClass.h'],
    'IOService': ['libkern/c++/OSObject.h', 'libkern/c++/OSMetaClass.h', 'IOKit/IORegistryEntry.h', 'IOKit/IOService.h'],
    'IOWorkLoop': ['libkern/c++/OSObject.h', 'libkern/c++/OSMetaClass.h', 'IOKit/IOWorkLoop.h'],
    'IOEthernetController': ['libkern/c++/OSObject.h', 'libkern/c++/OSMetaClass.h', 'IOKit/IORegistryEntry.h',
                             'IOKit/IOService.h', 'IOKit/network/IONetworkController.h',
                             'IOKit/network/IOEthernetController.h'],
}


def sdk_root(cls):
    while cls in CLASSES:
        cls = CLASSES[cls]['parent']
    return cls


def sdk_virtual_rets(k, root):
    """name -> return types declared virtual in the SDK headers of the class chain that ends in `root`"""
    files = SDK_FILES[root]
    rx = re.compile(r'virtual\s+([\w:\s\*&<>]+?)\s+\*?\s*(\w+)\s*\(', re.S)
    out = {}
    for f in files:
        p = os.path.join(k, f)
        if not os.path.exists(p):
            continue
        text = re.sub(r'/\*.*?\*/', '', open(p, errors='replace').read(), flags=re.S)
        text = re.sub(r'//[^\n]*', '', text)
        for m in rx.finditer(text):
            ret = ' '.join(m.group(1).split())
            ptr = '*' if text[m.end(1):m.end()].count('*') else ''
            out.setdefault(m.group(2), set()).add((ret + ' ' + ptr).strip())
    return out


# --------------------------------------------------------------------------------------------------- rendering


def ancestors(cls):
    out = set()
    while cls in CLASSES:
        cls = CLASSES[cls]['parent']
        out.add(cls)
    return out


def generate(d, cls, sdkret, known_rets):
    a = analyse(d, cls)
    lines_over, lines_new, used, skipped = [], [], set(), []
    for r in a['over']:
        plist = [declarator(t, i + 1) for i, t in enumerate(r['params'])]
        for t in r['params']:
            used.update(re.findall(r'[A-Za-z_]\w*', norm(t)))
        cands = sdkret.get(r['name'])
        ret = None
        if r['name'] in RET_OVERRIDE:
            ret = RET_OVERRIDE[r['name']]
        elif known_rets.get(r['name']):
            ret = known_rets[r['name']]          # declared by a generated ancestor: that declaration wins
        elif cands and len(cands) == 1:
            ret = next(iter(cands))
        if ret is None:
            skipped.append(r)
            continue
        lines_over.append('    /* %d */ virtual %s %s(%s)%s override;' % (
            r['slot'], ret, r['name'], ', '.join(plist), ' const' if r['const'] else ''))
    for r in a['new']:
        plist = [declarator(t, i + 1) for i, t in enumerate(r['params'])]
        for t in r['params']:
            used.update(re.findall(r'[A-Za-z_]\w*', norm(t)))
        if r['name'].startswith('_RESERVED'):
            lines_new.append('    /* %d */ virtual void %s();' % (r['slot'], r['name']))
            continue
        used.update(re.findall(r'[A-Za-z_]\w*', r['ret']))
        lines_new.append('    /* %d */ virtual %s %s(%s)%s%s;   // ret: %s' % (
            r['slot'], r['ret'], r['name'], ', '.join(plist), ' const' if r['const'] else '',
            ' = 0' if r['pure'] else '', r['why']))
    return dict(analysis=a, over=lines_over, new=lines_new, skipped=skipped, used=used)


def types_of(params_lists):
    return {w for p in params_lists for t in p for w in re.findall(r'[A-Za-z_][\w:]*', norm(t))}


def render(d, cls, sdkret, texts, sz, info):
    """texts: already rendered ancestors (for the return types of overrides declared there)"""
    known = {}
    for p in [c for c in ORDER if c in ancestors(cls)]:
        for m in re.finditer(r'virtual ([\w:\s\*&]+?) \*?(\w+)\(', texts.get(p, '')):
            known[m.group(2)] = m.group(1).strip()
    g = generate(d, cls, sdkret[sdk_root(cls)], known)
    a = g['analysis']
    conf = a['conf']
    size = sz[cls]
    toks = types_of([r['params'] for r in a['new'] + a['over']])
    for r in a['new']:
        toks.update(re.findall(r'[A-Za-z_][\w:]*', r['ret']))
    for sym, decl, why in STATICS.get(cls, ()):
        toks.update(t for t in re.findall(r'[A-Za-z_][\w:]*', decl) if re.match(r'(IO|OS|CC)[A-Z0-9]\w*', t) and t != 'IOReturn')
    nested, plain = {}, set()
    for t in toks:
        if t in BUILTIN or t in SDK_TYPES or t in ENUMS or t == cls or t in ancestors(cls):
            continue
        if '::' in t:
            o, n = t.split('::', 1)
            nested.setdefault(o, set()).add(n)
        else:
            plain.add(t)
    for sym, decl, why in STATICS.get(cls, ()):
        for t in re.findall(r'[A-Za-z_]\w*', decl):
            if t in CLASSES and t != cls and t not in ancestors(cls):
                plain.add(t)
    mine_nested = sorted(nested.get(cls, ()))
    macro = 'OSDeclareAbstractStructors' if any(r['pure'] for r in a['new']) else 'OSDeclareDefaultStructors'
    hdr = ['''// <wtl> project: mtkwlannative | file: generated by tools/kcabi/genhdr.py | author: watchthelight </wtl>
// SPDX-License-Identifier: ISC
// GENERATED from %s (macOS %s build %s, %s). Do not edit by hand: re-run genhdr.py.
//
// %s : %s as Apple's kernel collection exports it on this build. Slot numbers count the two vtable header words
// (byte offset in the vtable symbol = slot * 8; from a vptr = slot * 8 - 0x10). Instance size 0x%x (MetaClass constructor).
// Names are Apple's own symbols; pure slots are named by the first derived class that implements them. Return types are not
// in mangled names: "ret: guess" is a guess from the name. Checked with genhdr.py check (clang's vtable layout of a probe
// subclass against the dump). Method adapted from ga403wm-hackintosh tools/io80211_abi.py (BSD-3-Clause-Clear).
#pragma once
#include %s
#include <IOKit/IOReturn.h>
#include <stdint.h>
''' % (os.path.relpath(d, REPO), info['product'], info['build'], info['xnu'], cls, conf['parent'], size, conf['include'])]
    hdr.append('struct ether_addr;\nstruct __mbuf;')
    for t in sorted(plain):
        if t in ('ether_addr', '__mbuf'):
            continue
        hdr.append('%s %s;' % ('class' if re.match(r'(IO|OS|CC)', t) else 'struct', t))
    for o in sorted(nested):
        if o != cls and o not in CLASSES:
            hdr.append('// nested type of %s: %s' % (o, ', '.join(sorted(nested[o]))))
    for e in sorted(ENUMS & toks):
        hdr.append('#ifndef IO80211_ENUM_%s\n#define IO80211_ENUM_%s\nenum %s : uint32_t {};\n#endif' % (e, e, e))
    hdr.append('')
    hdr.append('class %s : public %s {' % (cls, conf['parent']))
    hdr.append('    %s(%s)' % (macro, cls))
    hdr.append('')
    hdr.append('public:')
    for n in mine_nested:
        hdr.append('    struct %s;   // opaque here' % n)
    if g['over']:
        hdr.append('    // overrides of inherited virtuals (declared so that super:: reaches Apple\'s code)')
        hdr.extend(g['over'])
    if g['skipped']:
        hdr.append('    // overridden by Apple\'s class too, not redeclared (return type not verified):')
        for r in g['skipped']:
            hdr.append('    //   /* %d */ %s(%s)%s' % (r['slot'], r['name'], ', '.join(norm(t) for t in r['params']),
                                                          ' const' if r['const'] else ''))
    hdr.append('')
    hdr.append('    // new virtuals, slots %d..%d' % (a['parent_last'] + 1, a['last']))
    hdr.extend(g['new'])
    if a['pure_unnamed']:
        hdr.append('    // pure slots no dumped class names: %s' % ', '.join(str(s) for s in a['pure_unnamed']))
    hdr.append('')
    methods = open(os.path.join(d, cls + '.methods.txt')).read() if os.path.exists(os.path.join(d, cls + '.methods.txt')) else ''
    for sym, decl, why in STATICS.get(cls, ()):
        if sym is None:
            hdr.append('    %s   // %s' % (decl, why))
            continue
        if sym not in methods:
            raise SystemExit('%s: %s is not in the method dump' % (cls, sym))
        hdr.append('    %s   // exported: %s; %s' % (decl, sym, why))
    if STATICS.get(cls):
        hdr.append('')
    parent_size = sz.get(conf['parent'])
    if parent_size != size:
        hdr.append('protected:')
        hdr.append('    uint8_t %sOpaque[0x%x - sizeof(%s)];   // ivars of Apple\'s class: layout not determined' % (
            cls[0].lower() + cls[1:], size, conf['parent']))
    hdr.append('};')
    hdr.append('static_assert(sizeof(%s) == 0x%x, "%s must be 0x%x bytes (%s)");' % (cls, size, cls, size, info['build']))
    return '\n'.join(hdr) + '\n', g


def render_all(d):
    sz, info, k = sizes(d), source_info(d), sdk_headers()
    sdkret = {root: sdk_virtual_rets(k, root) for root in SDK_FILES}
    texts, gens = {}, {}
    for c in ORDER:
        texts[c], gens[c] = render(d, c, sdkret, texts, sz, info)
    return texts, gens, sz, info


# --------------------------------------------------------------------------------------------------- stubs + abi.json

PURE = re.compile(r'/\*\s*(\d+)\s*\*/\s*virtual\s+(\S+)\s+(\w+)\((.*?)\)\s*=\s*0;')


def pure_slots(texts, classes):
    out = []
    for c in classes:
        for line in texts[c].splitlines():
            m = PURE.search(line)
            if m:
                out.append((int(m.group(1)), m.group(2), m.group(3), m.group(4)))
    return sorted(out)


def implemented_in(srcs, cls):
    rx = re.compile(r'\b%s::(\w+)\s*\(' % re.escape(cls))
    out = set()
    for s in srcs:
        out.update(rx.findall(open(s).read()))
    return out


def render_stubs(texts, cls, custom, info):
    slots = pure_slots(texts, ['IO80211SkywalkInterface', 'IO80211InfraInterface', 'IO80211InfraProtocol'])
    seen, uniq = set(), []
    for s in slots:                       # a pure slot redeclared by a derived header appears twice: keep the first
        if s[2] not in seen:
            seen.add(s[2])
            uniq.append(s)
    o = ['// GENERATED by tools/kcabi/genhdr.py (build %s) from the interface headers. Do not edit by hand.' % info['build'],
         '// %d pure virtuals the Family routes requests to. Every one is declared on %s; the ones the kext implements' % (len(uniq), cls),
         '// by hand are left out of the definitions (detected from the source files given to genhdr.py).',
         '#ifdef MTK_IFACE_DECLS']
    for slot, ret, name, params in uniq:
        o.append('    %s %s(%s) override;   // slot %d' % (ret, name, params, slot))
    o.append('#endif')
    o.append('#ifdef MTK_IFACE_DEFS')
    for slot, ret, name, params in uniq:
        if name in custom:
            continue
        o.append('%s %s::%s(%s) { nativeCall(ctl, \'S\', %d); return kIOReturnUnsupported; }' % (ret, cls, name, params, slot))
    o.append('#endif')
    return '\n'.join(o) + '\n', uniq


def abi_json(sz, info):
    missing = [c for c in GUARD_CLASSES if c not in sz]
    if missing:
        raise SystemExit('sizes.txt lacks ' + ', '.join(missing))
    return dict(build=info['build'], product=info['product'], xnu=info['xnu'], kernel_version=info['kernel_version'],
                kc_sha256=info['sha256'], sizes={c: sz[c] for c in GUARD_CLASSES})


# --------------------------------------------------------------------------------------------------- checks


def kext_flags(k):
    res = subprocess.run(['clang', '-print-resource-dir'], capture_output=True, text=True, check=True).stdout.strip()
    return ['-target', 'x86_64-apple-macosx10.15', '-mkernel', '-nostdinc', '-nostdinc++', '-fno-builtin', '-fno-common',
            '-fapple-kext', '-std=gnu++17', '-fno-exceptions', '-fno-rtti', '-DKERNEL', '-DKERNEL_PRIVATE', '-DDRIVER_PRIVATE',
            '-DAPPLE', '-DNeXT', '-Wno-deprecated-declarations', '-w', '-isystem', os.path.join(res, 'include'), '-I' + k]


def clang_vtable(src, cls, flags):
    cmd = ['clang++', '-S', '-emit-llvm', '-o', os.devnull] + flags + [src, '-Xclang', '-fdump-vtable-layouts']
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        return None, r.stderr.strip()[-2500:]
    out, on = {}, False
    for line in r.stdout.splitlines():
        if line.startswith("Vtable for '%s'" % cls):
            on = True
            continue
        if on and (line.startswith('VTable indices') or line.startswith('Vtable for') or not line.strip()):
            break
        m = re.match(r'\s*(\d+) \| (.*)$', line) if on else None
        if m:
            out[int(m.group(1))] = m.group(2).strip()
    return out, ''


def check_class(d, cls, tmp, texts, flags):
    conf = CLASSES[cls]
    mine = vt(d, cls)
    last = max(s for s, v in mine.items() if v != '-')
    kids = [k for k in (vt(d, c) for c in conf['children']) if k]
    dem = demangle_all({v for v in mine.values() if v.startswith('__Z')} |
                       {k[s] for k in kids for s in k if k[s].startswith('__Z')})
    gen_text = '\n'.join(texts.values())

    def ret_of(name):
        m = re.findall(r'virtual ([\w:\s\*&]+?) \*?%s\(' % re.escape(name), gen_text)
        return m[0] if m else None

    probe = ['#include <stdint.h>']
    for t in BYVALUE_STUBS:
        probe.append('struct %s { uint64_t pad[3]; };' % t)
    for e in sorted(ENUMS):
        probe.append('#define IO80211_ENUM_%s' % e)
        probe.append('enum %s : uint32_t {};' % e)
    probe += ['#include "%s.h"' % cls, 'class Probe : public %s { public:' % cls,
              '    Probe() : %s((const OSMetaClass *)0) {}' % cls]
    unnamed = []
    for s in range(2, last + 1):
        if mine[s] != '___cxa_pure_virtual':
            continue
        sym = next((k[s] for k in kids if s in k and k[s] not in ('___cxa_pure_virtual', '-')), None)
        if sym is None:
            unnamed.append(s)
            continue
        owner, name, params, const = parse_method(dem[sym])
        plist = [declarator(t, i + 1) for i, t in enumerate(params)]
        ret = ret_of(name) or 'void'
        body = '{ return {}; }' if ret != 'void' else '{}'
        probe.append('    %s %s(%s)%s override %s' % (ret, name, ', '.join(plist), ' const' if const else '', body))
    probe.append('};\nvoid make_probe() { Probe p; }')
    src = os.path.join(tmp, 'probe_%s.cpp' % cls)
    open(src, 'w').write('\n'.join(probe) + '\n')
    got, err = clang_vtable(src, 'Probe', flags + ['-I' + tmp])
    if got is None:
        return dict(ok=False, msg='compile failed: ' + err, unnamed=unnamed)
    bad, notes, compared = [], [], 0
    for s in range(2, last + 1):
        sym = mine[s]
        if sym == '___cxa_pure_virtual' or s in (2, 3):
            continue
        compared += 1
        if not sym.startswith('__Z'):
            continue
        pm = parse_method(dem[sym])
        if not pm:
            continue
        a_owner, a_name, a_params, a_const = pm
        txt = got.get(s, '(missing)')
        m = re.search(r'([~\w]+)::([~\w]+)\((.*)\)', txt)
        if m and a_name != m.group(2) and m.group(2).startswith('_RESERVED') and a_owner != cls:
            notes.append((s, '%s::%s' % (a_owner, a_name), txt))      # the SDK header still reserves the slot Apple filled
        elif not m or a_name != m.group(2):
            bad.append((s, '%s::%s' % (a_owner, a_name), txt))
    extra = sorted((s, got[s]) for s in got if s > last and 'vcall_offset' not in got[s])   # kext ABI trailer
    return dict(ok=not bad and not extra, bad=bad, notes=notes, compared=compared, extra=extra, unnamed=unnamed,
                clang_slots=max(got) if got else 0, apple_slots=last)


def cmd_check(d, out):
    texts, gens, sz, info = render_all(d)
    for c in ORDER:
        p = os.path.join(out, c + '.h')
        if not os.path.exists(p) or open(p).read() != texts[c]:
            print('%s: differs from a fresh generation (run gen)' % p)
            return 1
    tmp = tempfile.mkdtemp(prefix='kcabi-probe-')
    for c in ORDER:
        open(os.path.join(tmp, c + '.h'), 'w').write(texts[c])
    flags = kext_flags(sdk_headers())
    rc = 0
    for c in ORDER:
        r = check_class(d, c, tmp, texts, flags)
        if r.get('msg'):
            print('%-28s CANNOT BE VERIFIED: %s' % (c, r['msg']))
            rc = 1
            continue
        print('%-28s slots apple=%d clang=%d compared=%d mismatches=%d unnamed-pure=%s' % (
            c, r['apple_slots'], r['clang_slots'], r['compared'], len(r['bad']) + len(r['extra']), r['unnamed'] or '-'))
        for s, a, b in r['bad'][:25]:
            print('    slot %d: apple %-60s clang %s' % (s, a, b))
        for s, txt in r['extra'][:10]:
            print('    slot %d: present in clang layout, not in Apple vtable: %s' % (s, txt))
        for s, a, b in r['notes'][:8]:
            print('    note slot %d: Apple names it %s, the SDK header still calls it %s (same slot)' % (s, a, b))
        if not r['ok']:
            rc = 1
    print('headers %s (build %s)' % ('verified' if rc == 0 else 'NOT verified', info['build']))
    return rc


def cmd_check_native(d, cls, apple, src, flags):
    a = {s: v for s, v in vt(d, apple).items() if v.startswith('__Z')}
    if not a:
        raise SystemExit('no vtable dump for ' + apple)
    dem = demangle_all(a.values())
    got, err = clang_vtable(src, cls, flags)
    if got is None:
        print(err)
        return 1
    own = re.compile(r'^(?:.*?[\s*&])?%s::(~?\w+)\((.*?)\)( const)?( \[\w+\])?$' % re.escape(cls))
    ok = bad = 0
    for slot in sorted(got):
        m = own.match(got[slot])
        if not m or m.group(1).startswith('~'):
            continue
        name = m.group(1)
        sym = a.get(slot)
        if not sym:
            print('slot %d: %s -> Apple has no function there' % (slot, got[slot]))
            bad += 1
            continue
        am = re.match(r'^(?:[\w:]+::)?(~?\w+)\(', dem[sym])
        if am and am.group(1) == name:
            ok += 1
        else:
            print('slot %d: %s -> Apple has %s' % (slot, got[slot], dem[sym]))
            bad += 1
    print('%s vs %s: %d overrides on the right slot, %d wrong (clang slots %d, Apple slots %d)' % (
        cls, apple, ok, bad, len(got), len(a)))
    return 1 if bad else 0


# --------------------------------------------------------------------------------------------------- main


def cmd_gen(d, out, iface_class, impl):
    texts, gens, sz, info = render_all(d)
    os.makedirs(out, exist_ok=True)
    for c in ORDER:
        open(os.path.join(out, c + '.h'), 'w').write(texts[c])
        g = gens[c]
        print('%-28s overrides %d (%d not redeclared), new virtuals %d, pure unnamed %s' % (
            c, len(g['over']), len(g['skipped']), len(g['new']), g['analysis']['pure_unnamed'] or '-'))
    custom = implemented_in(impl, iface_class) if impl else set()
    stubs, uniq = render_stubs(texts, iface_class, custom, info)
    open(os.path.join(out, 'iface_stubs.inc'), 'w').write(stubs)
    print('iface_stubs.inc: %d pure slots, %d implemented by hand' % (len(uniq), len([u for u in uniq if u[2] in custom])))
    json.dump(abi_json(sz, info), open(os.path.join(out, 'abi.json'), 'w'), indent=2)
    print('abi.json: build %s %s, %d class sizes' % (info['build'], info['xnu'], len(GUARD_CLASSES)))
    return 0


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    cmd, d = argv[0], os.path.abspath(argv[1])
    if cmd == 'gen':
        out = os.path.abspath(argv[2])
        iface_class = 'MtkWlanNativeIf'
        impl = []
        rest = argv[3:]
        while rest:
            if rest[0] == '--iface-class':
                iface_class, rest = rest[1], rest[2:]
            elif rest[0] == '--impl':
                rest = rest[1:]
                while rest and not rest[0].startswith('--'):
                    impl.append(rest[0])
                    rest = rest[1:]
            else:
                sys.exit('unknown option ' + rest[0])
        return cmd_gen(d, out, iface_class, impl)
    if cmd == 'check':
        return cmd_check(d, os.path.abspath(argv[2]))
    if cmd == 'check-native':
        if '--' not in argv:
            sys.exit(__doc__)
        i = argv.index('--')
        cls, apple, src = argv[2], argv[3], argv[4]
        return cmd_check_native(d, cls, apple, src, argv[i + 1:])
    sys.exit(__doc__)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
