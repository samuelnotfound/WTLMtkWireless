#!/usr/bin/env python3
# <wtl> project: mtkwlannative | file: tools/kcabi/dump.py | author: watchthelight </wtl>
# SPDX-License-Identifier: ISC
# Project Legion: dump the IO80211Family / IOSkywalkFamily / corecapture ABI facts a native Wi-Fi front end depends on,
# from the running system's kernel collection. Read-only: the collection is mmapped; carved copies go to the cache dir.
"""
usage: dump.py [--kc PATH] [--out DIR] [--cache DIR] [--ref DIR] [--sites FILE]

  --kc     BootKernelExtensions.kc (default: /System/Library/KernelCollections/BootKernelExtensions.kc)
  --out    output directory (default: abi/native-wifi/<build> in the repo)
  --cache  scratch directory for carved Mach-O copies (default: ~/.cache/legion/kc<build>)
  --sites  TSV of byte-pattern sites to re-verify (AWDL stage 0): symbol, hex pattern with ?? wildcards, label; written to
           awdl-sites.txt as found / not found with the match addresses (see awdl-sites.example.tsv)
  --ref    reference dump to take the function list from (default: the ga403wm 24H32 tree in tooling/community)

Writes: source.json, sizes.txt, <Class>.vtable.txt, <Class>.methods.txt, dispatch/ioctl-{get,set}-dispatch.txt,
asm/<symbol>.asm.txt, callbacks/*.txt. Then run compare.py.
"""
import argparse
import json
import os
import plistlib
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from kcfile import KC          # noqa: E402
from disasm import Disassembler  # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
REF_DEFAULT = os.path.join(REPO, 'tooling', 'community', 'ga403wm-hackintosh', 'abi', 'native-wifi', '24H32')

# Classes whose full vtable + method list is written (the 24H32 set, plus the ones a front end subclasses or calls).
CLASSES = '''
IO80211Controller IO80211SkywalkInterface IO80211InfraInterface IO80211InfraProtocol IO80211WorkQueue IO80211CommandGate
IO80211Peer IO80211VirtualInterface IO80211ControllerMonitor IO80211Glue IO80211FaultReporter IO80211PostOffice
IO80211TimerSource IO80211BSSBeacon IO80211ScanManager IO80211PeerManager IO80211MacAddressAgent IO80211NetworkPacket
IO80211AWDLProtocol IO80211AWDLPeerManager
IOSkywalkInterface IOSkywalkNetworkInterface IOSkywalkEthernetInterface IOSkywalkPacketQueue IOSkywalkPacketBufferPool
IOSkywalkTxSubmissionQueue IOSkywalkRxCompletionQueue IOSkywalkTxCompletionQueue IOSkywalkRxSubmissionQueue
IOSkywalkPacket IOSkywalkNetworkPacket IOSkywalkPacketBuffer IOSkywalkQueueSet IOSkywalkLogicalLink
IOSkywalkLegacyEthernet IOSkywalkLegacyEthernetInterface IOSkywalkNetworkBSDClient IOSkywalkBSDClient
CCPipe CCStream CCLogPipe CCLogStream CCDataPipe CCDataStream CCFaultReporter
AppleBCMWLANCore AppleBCMWLANSkywalkInterface AppleBCMWLANInfraProtocol AppleBCMWLANPCIeSkywalk
AppleBCMWLANPCIeSkywalkPacketPool
'''.split()

SIZE_FAMILIES = ('com.apple.iokit.IO80211Family', 'com.apple.iokit.IOSkywalkFamily', 'com.apple.driver.corecapture',
                 'com.apple.driver.AppleBCMWLANCoreMac', 'com.apple.iokit.IONetworkingFamily')
KEXT_PLISTS = {
    'com.apple.iokit.IO80211Family': '/System/Library/Extensions/IO80211Family.kext/Contents/Info.plist',
    'com.apple.iokit.IOSkywalkFamily': '/System/Library/Extensions/IOSkywalkFamily.kext/Contents/Info.plist',
    'com.apple.driver.corecapture': '/System/Library/Extensions/corecapture.kext/Contents/Info.plist',
    'com.apple.iokit.IONetworkingFamily': '/System/Library/Extensions/IONetworkingFamily.kext/Contents/Info.plist',
    'com.apple.driver.AppleBCMWLANCoreMac': '/System/Library/Extensions/AppleBCMWLANCoreMac.kext/Contents/Info.plist',
}


def sh(*cmd):
    try:
        return subprocess.run(cmd, capture_output=True, text=True).stdout
    except OSError:
        return ''


def system_version(root):
    try:
        with open(os.path.join(root, 'System/Library/CoreServices/SystemVersion.plist'), 'rb') as f:
            pl = plistlib.load(f)
        return pl.get('ProductVersion'), pl.get('ProductBuildVersion')
    except OSError:
        return None, None


def write(path, lines):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w') as f:
        f.write('\n'.join(lines) + '\n')


def ref_functions(ref):
    """Symbol names of every function the reference dump disassembled (first line '# 0x<addr> <symbol>')."""
    names = set()
    for root, _dirs, files in os.walk(ref):
        for fn in files:
            if not fn.endswith('.asm.txt'):
                continue
            with open(os.path.join(root, fn), errors='replace') as f:
                for line in f:
                    m = re.match(r'^# (0x[0-9a-f]+)\s+(_\S+)', line)
                    if m:
                        names.add(m.group(2))
    return sorted(names)


def is_code(kc, name):
    va = kc.find(name)
    if va is None or '_os_log_fmt' in name or 'kalloc_type_view' in name or name.startswith('__ZZ'):
        return False
    return any(s[1] <= va < s[1] + s[2] and s[5] & 4 for s in kc.segs)


def qualified_prefix(mangled):
    """'__ZN21IO80211InfraInterface12setLinkStateE...' -> '__ZN21IO80211InfraInterface12setLinkStateE'; free functions
    '__Z23apple80211RouteExternalP...' -> '__Z23apple80211RouteExternal'."""
    m = re.match(r'^(__ZNK?)(?=\d)', mangled)
    if m:
        p, i = m.group(1), len(m.group(1))
        while i < len(mangled) and mangled[i].isdigit():
            j = i
            while mangled[j].isdigit():
                j += 1
            ln = int(mangled[i:j])
            p += mangled[i:j + ln]
            i = j + ln
        return p + 'E' if i < len(mangled) and mangled[i] == 'E' else None
    m = re.match(r'^__Z(\d+)', mangled)
    if m:
        ln = int(m.group(1))
        return mangled[:3 + len(m.group(1)) + ln]
    return None


def safe(name):
    return re.sub(r'[^A-Za-z0-9_.-]', '_', name.lstrip('_'))[:180]


def dump_vtables(kc, out, classes):
    spans = {}
    for cls in classes:
        vt = kc.vtable(cls)
        if vt is None:
            spans[cls] = None
            continue
        spans[cls] = len(vt)
        lines = ['# %s: %d slots (%#x bytes)' % (cls, len(vt), len(vt) * 8)]
        lines += ['%d\t%#x\t%s' % (i, i * 8, nm) for i, _raw, _t, nm in vt]
        write(os.path.join(out, cls + '.vtable.txt'), lines)
        write(os.path.join(out, cls + '.methods.txt'), ['%#x\t%s' % (a, n) for a, n in kc.methods(cls)])
    return spans


def dump_sizes(kc, out):
    rows = []
    for cls in kc.classes():
        mva = kc.find('__ZN%d%s10gMetaClassE' % (len(cls), cls))
        owner = kc.sym_owner.get(mva) if mva else None
        if owner not in SIZE_FAMILIES:
            continue
        rows.append((cls, kc.metaclass_size(cls), owner))
    lines = ['# class\tinstance size (MetaClass ctor ECX)\towner']
    lines += ['%s\t%s\t%s' % (c, '%#x' % s if s is not None else '?', o) for c, s, o in sorted(rows)]
    write(os.path.join(out, 'sizes.txt'), lines)
    return {c: s for c, s, _ in rows}


def dump_dispatch(kc, dis, out):
    """apple80211RouteInternal: GET/SET handler tables indexed by (requestID - 1)."""
    cands = kc.find_prefix('__Z23apple80211RouteInternal')
    if not cands:
        return None
    va, name = cands[0]
    lines = dis.function(va)
    write(os.path.join(out, 'asm', safe(name) + '.asm.txt'), ['# %#x %s' % (va, name)] + lines)
    tables, limit = [], None
    for ln in lines:
        m = re.search(r'lea\s+r\w+, \[rip[^\]]*\]\s+;\s+(0x[0-9a-f]+)', ln)
        if m:
            t = int(m.group(1), 16)
            if t not in tables:
                tables.append(t)
        m = re.search(r'mov\s+eax, (0xfffff[0-9a-f]{3})$', ln)
        if m and limit is None:
            limit = (1 << 32) - int(m.group(1), 16)          # lea ecx,[id + imm]; cmp ecx, eax; jbe reject
    info = {'function': name, 'address': '%#x' % va, 'tables': ['%#x' % t for t in tables], 'limit_const': limit}
    # The routing code checks the SET-direction ioctl command first in 24H32; identify tables by their first entry name.
    for t in tables:
        n = (limit - 1) if limit else ((kc.next_sym(t) or t) - t) // 8
        rows, kind = [], None
        for i in range(n):
            raw = kc.q(t + i * 8)
            tgt = kc.ptr(raw) if raw else 0
            nm = kc.syms.get(tgt, '%#x' % tgt) if tgt else '-'
            if kind is None and nm.startswith('__Z'):
                kind = 'get' if 'apple80211get' in nm else 'set' if 'apple80211set' in nm else None
            rows.append('%d\t%#x\t%#x\t%s' % (i + 1, i + 1, tgt, nm))
        if kind is None or ('table_' + kind) in info:
            continue                                     # not a handler table (other lea targets in the function)
        info['table_' + kind] = '%#x' % t
        write(os.path.join(out, 'dispatch', 'ioctl-%s-dispatch.txt' % kind),
              ['# apple80211RouteInternal %s table at %#x: request IDs 1..%d' % (kind.upper(), t, n)] + rows)
    write(os.path.join(out, 'dispatch', 'route.json'), [json.dumps(info, indent=2)])
    return info


def dump_sites(kc, path, out):
    """Re-verify byte-pattern sites (symbol<TAB>pattern<TAB>label); a pattern may carry ?? wildcards. Read-only."""
    lines = ['# symbol\tlabel\tresult\tmatch addresses (first 4)']
    ok = bad = 0
    for raw in open(path):
        if raw.startswith('#') or not raw.strip():
            continue
        cols = raw.rstrip('\n').split('\t')
        sym, pat, label = cols[0], cols[1], (cols[2] if len(cols) > 2 else '')
        va = kc.find(sym)
        if va is None:
            lines.append('%s\t%s\tno symbol\t' % (sym, label))
            bad += 1
            continue
        end = kc.next_sym(va) or va + 0x400
        body = kc.read(va, end - va)
        rx = re.compile(b''.join(b'.' if t == '??' else re.escape(bytes([int(t, 16)])) for t in pat.split()), re.S)
        hits = ['%#x' % (va + m.start()) for m in rx.finditer(body)]
        lines.append('%s\t%s\t%s\t%s' % (sym, label, '%d match(es)' % len(hits) if hits else 'NOT FOUND', ' '.join(hits[:4])))
        ok += bool(hits)
        bad += not hits
    write(os.path.join(out, 'awdl-sites.txt'), lines)
    return {'sites_file': os.path.basename(path), 'found': ok, 'not_found': bad}


def dump_callbacks(kc, out):
    """WCL bulletin-board subscriber arrays (g<MANAGER>CallBackArray). Each entry is 0x18 bytes:
    u64 bulletin id ((event code << 16) | 2), then a C++ member-function pointer (function, this-adjust)."""
    found, allrows = {}, []
    for nm, va in sorted(kc.byname.items()):
        if 'CallBackArray' not in nm or kc.sym_owner.get(va) != 'com.apple.iokit.IO80211Family':
            continue
        end = kc.next_sym(va) or va
        rows = ['# %s at %#x: event code, bulletin id, handler' % (nm, va)]
        for off in range(0, end - va - 0x17, 0x18):
            bid, fn, adj = (kc.q(va + off + k) for k in (0, 8, 16))
            if not bid and not fn:
                continue
            h = kc.syms.get(kc.ptr(fn), '%#x' % kc.ptr(fn)) if fn else '-'
            rows.append('%#x\t%#x\t%s%s' % (bid >> 16, bid, h, '' if not adj else ' adj=%#x' % adj))
            allrows.append('%#x\t%s\t%s' % (bid >> 16, re.sub(r'^__ZL\d+g|CallBackArray$', '', nm), h))
        write(os.path.join(out, 'callbacks', safe(nm) + '.txt'), rows)
        found[nm] = len(rows) - 1
    allrows.sort(key=lambda r: int(r.split('\t')[0], 16))
    write(os.path.join(out, 'callbacks', 'by-event.txt'), ['# event code\tmanager\thandler'] + allrows)
    return found


def kext_versions(root):
    out = {}
    for bid, p in KEXT_PLISTS.items():
        try:
            with open(os.path.join(root, p.lstrip('/')), 'rb') as f:
                pl = plistlib.load(f)
            out[bid] = {'CFBundleVersion': pl.get('CFBundleVersion'),
                        'OSBundleCompatibleVersion': pl.get('OSBundleCompatibleVersion')}
        except OSError:
            out[bid] = None
    return out


def kernel_version(kc):
    va = kc.find('_version')
    b = kc.read(va, 200) if va else None
    return b.split(b'\0')[0].decode('latin1') if b else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default='/', help='system volume the collection belongs to (for versions and kext plists)')
    ap.add_argument('--kc', help='default: <root>/System/Library/KernelCollections/BootKernelExtensions.kc')
    ap.add_argument('--out')
    ap.add_argument('--cache')
    ap.add_argument('--ref', default=REF_DEFAULT)
    ap.add_argument('--sites')
    a = ap.parse_args()
    a.kc = a.kc or os.path.join(a.root, 'System/Library/KernelCollections/BootKernelExtensions.kc')
    version, build = system_version(a.root)
    build = build or 'unknown'
    out = a.out or os.path.join(REPO, 'abi', 'native-wifi', build)
    cache = a.cache or os.path.expanduser('~/.cache/legion/kc%s' % build)
    kc = KC(a.kc)
    dis = Disassembler(kc, cache)
    os.makedirs(out, exist_ok=True)

    spans = dump_vtables(kc, out, CLASSES)
    sizes = dump_sizes(kc, out)
    route = dump_dispatch(kc, dis, out)
    cbs = dump_callbacks(kc, out)
    sites = dump_sites(kc, a.sites, out) if a.sites else None

    # MetaClass constructors of the vtable classes, as asm evidence for sizes.txt
    funcs = {}
    for cls in CLASSES:
        for kind in ('C1Ev', 'C2Ev'):
            n = '__ZN%d%s9MetaClass%s' % (len(cls), cls, kind)
            if kc.find(n):
                funcs[n] = None
    missing, renamed = [], []
    for n in ref_functions(a.ref):
        if kc.find(n) is not None:
            funcs[n] = None
            continue
        cands = [nm for _va, nm in kc.find_prefix(qualified_prefix(n))] if qualified_prefix(n) else []
        if cands:
            for c in cands:
                funcs[c] = None
            renamed.append('%s\t->\t%s' % (n, ' | '.join(cands)))
        else:
            missing.append(n)
    extra = os.path.join(HERE, 'functions-extra.txt')
    if os.path.exists(extra):
        for line in open(extra):
            n = line.split('#')[0].strip()
            if not n:
                continue
            if n.endswith('*'):
                for _va, nm in kc.find_prefix(n[:-1]):
                    if not is_code(kc, nm):
                        continue
                    funcs[nm] = None
            elif kc.find(n):
                funcs[n] = None
            else:
                missing.append(n)
    index = []
    for n in sorted(funcs):
        va = kc.find(n)
        lines = dis.function(va)
        if lines is None:
            missing.append(n)
            continue
        fn = safe(n) + '.asm.txt'
        write(os.path.join(out, 'asm', fn), ['# %#x %s' % (va, n)] + lines)
        index.append('%#x\t%s\t%s\t%d' % (va, kc.sym_owner.get(va), n, len(lines)))
    write(os.path.join(out, 'asm', 'INDEX.txt'), ['# address\towner\tsymbol\tinstructions'] + index)
    write(os.path.join(out, 'missing-functions.txt'),
          ['# reference functions with no symbol of the same mangled name in this build',
           '# signature changed (same qualified name, new mangled parameters):'] + sorted(renamed) +
          ['# not found at all:'] + sorted(set(missing)))

    src = {
        'kc': a.kc,
        'sha256': kc.sha256(),
        'product_version': version,
        'build': build,
        'root': a.root,
        'kernel_version': kernel_version(kc),
        'base': '%#x' % kc.base,
        'entries': {bid: '%#x' % vm for bid, vm, _fo in kc.entries
                    if bid in SIZE_FAMILIES or 'WLAN' in bid or 'Skywalk' in bid},
        'kext_versions': kext_versions(a.root),
        'vtables_symbol_spans': spans,
        'dispatch': route,
        'callback_arrays': cbs,
        'awdl_sites': sites,
        'functions_dumped': len(index),
        'functions_missing': len(set(missing)),
        'tool': 'tools/kcabi/dump.py',
    }
    write(os.path.join(out, 'source.json'), [json.dumps(src, indent=2)])
    print('wrote %s: %d vtables, %d sizes, %d functions, %d missing' %
          (out, sum(1 for v in spans.values() if v), len(sizes), len(index), len(set(missing))))


if __name__ == '__main__':
    main()
