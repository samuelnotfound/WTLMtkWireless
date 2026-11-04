#!/usr/bin/env python3
# <wtl> project: mtkwlannative | file: tools/kcabi/linkcheck.py | author: watchthelight </wtl>
# SPDX-License-Identifier: ISC
# Project Legion: read-only link check of a built kext against this Mac's kernel collection. Every undefined symbol of the
# kext's executable must be exported (N_EXT, defined in a section) by the kernel or by one of the kext's OSBundleLibraries,
# or the kext fails to link when OpenCore injects it. The idea follows ga403wm-hackintosh tools/check_kext_links.py
# (BSD-3-Clause-Clear); this is a rewrite on top of kcfile.py.
"""
usage: linkcheck.py KEXT [--baseline KEXT2] [--kc PATH] [--own-lib BUNDLE_ID...]

  --baseline  a kext known to load: symbols it also lacks are expected (the SDK still names the IONetworkController slots
              _RESERVED2..5 where the kernel has outputStart & co; kxld fills them from the parent vtable)
  --own-lib   bundle ids of our own library kexts that are not in the collection (symbols from them are listed, not failed)
Exit status 1 when a symbol is exported by nobody or by a bundle the kext does not declare.
"""
import os
import plistlib
import re
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kcfile import KC  # noqa: E402

DEFAULT_KC = '/System/Library/KernelCollections/BootKernelExtensions.kc'


def exe_of(k):
    inf = plistlib.load(open(os.path.join(k, 'Contents', 'Info.plist'), 'rb'))
    return os.path.join(k, 'Contents', 'MacOS', inf['CFBundleExecutable']), inf


def undefined_of(k):
    path, _ = exe_of(k)
    text = subprocess.run(['nm', '-um', path], capture_output=True, text=True, check=True).stdout
    return sorted({m.group(1) for m in re.finditer(r'\(undefined\) external (\S+)', text)})


def exported(kc):
    """symbol -> owner bundle id, external symbols defined in a section only"""
    out = {}
    for owner, symoff, nsyms, stroff, strsize in kc.symtabs:
        for i in range(nsyms):
            strx, typ, _sect, _desc, val = struct.unpack_from('<IBBHQ', kc.d, symoff + i * 16)
            if not val or strx >= strsize or (typ & 0xe0) or (typ & 0x0e) != 0x0e or not (typ & 0x01):
                continue
            end = kc.d.find(b'\0', stroff + strx)
            out.setdefault(kc.d[stroff + strx:end].decode('latin1'), owner)
    return out


def main(argv):
    if not argv:
        sys.exit(__doc__)
    kext, baseline, kcpath, own = argv[0], None, DEFAULT_KC, []
    rest = argv[1:]
    while rest:
        if rest[0] == '--baseline':
            baseline, rest = rest[1], rest[2:]
        elif rest[0] == '--kc':
            kcpath, rest = rest[1], rest[2:]
        elif rest[0] == '--own-lib':
            rest = rest[1:]
            while rest and not rest[0].startswith('--'):
                own.append(rest[0])
                rest = rest[1:]
        else:
            sys.exit('unknown option ' + rest[0])
    exe, info = exe_of(kext)
    declared = set(info.get('OSBundleLibraries', {}))
    kc = KC(kcpath)
    provider = exported(kc)
    undefined = undefined_of(kext)
    expected = set()
    if baseline:
        expected = {s for s in undefined_of(baseline) if s not in provider}
    by_owner, missing = {}, []
    for s in undefined:
        o = provider.get(s)
        if o is None:
            missing.append(s)
        else:
            by_owner.setdefault(o, []).append(s)
    bad = 0
    print('%s: %d undefined symbols, collection %s' % (os.path.basename(kext), len(undefined), os.path.basename(kcpath)))
    for o in sorted(by_owner):
        ok = o.startswith('com.apple.kernel') or o == 'kernel' or o in declared
        if not ok:
            bad += 1
        print('  %-44s %4d  %s' % (o, len(by_owner[o]), 'ok' if ok else 'NOT IN OSBundleLibraries'))
    exp = [s for s in missing if s in expected]
    missing = [s for s in missing if s not in expected]
    if exp:
        print('  not exported, but the baseline %s lacks them too: %d' % (os.path.basename(baseline), len(exp)))
    own_declared = [lib for lib in declared if lib in own]
    if missing:
        print('  not exported by the collection (%d):' % len(missing))
        for s in missing:
            print('    ' + s)
        if own_declared:
            print('  (expected from %s, which is not in the collection)' % ', '.join(own_declared))
        else:
            bad += len(missing)
    print('link check %s' % ('ok' if bad == 0 else 'FAILED'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
