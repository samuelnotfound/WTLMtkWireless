#!/usr/bin/env python3
# <wtl> project: mtkwlannative | file: tools/kcabi/refresh.py | author: watchthelight </wtl>
# SPDX-License-Identifier: ISC
# Project Legion: the one command to run after a macOS update, on the updated system. It dumps the new build's IO80211 /
# IOSkywalk ABI (dump.py), compares it with the build MtkWlanNative.kext was last verified against (compare.py), and then:
#   unchanged  the nine classes the kext subclasses keep their vtables and the guarded classes their sizes: the headers are
#              regenerated for the new build (identical apart from the banner), the kext is rebuilt and its allow-list
#              (Info.plist MtkNativeAbi) gains the new build; swap the kext into OCEFI and the native UI is back;
#   relayout   slots or sizes moved: headers regenerated, kext rebuilt, slot and link checks run; a human still reads the
#              payload notes below before the first boot (request IDs, event layouts and struct sizes are not checked here);
#   review     the request tables or the WCL functions the front end depends on changed: the rebuild runs, but the kext
#              must not be enabled until docs/native-wifi.md "After a macOS update" has been walked through.
# Until this has run, the kext declines on the new build and the core keeps its Ethernet front end: Wi-Fi works, only the
# native UI is missing.
"""
usage: refresh.py [--dry-run] [--old BUILD] [--new BUILD] [--kc PATH]

  --dry-run   dump and compare only, print the verdict, change nothing in the kext
  --old       the build the kext is verified for (default: the last entry of its MtkNativeAbi allow-list)
  --new       the build to refresh for (default: sw_vers -buildVersion); its dump is made if missing
  --kc        the new build's kernel collection (default: the running one)
"""
import os
import plistlib
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
KEXT = os.path.join(REPO, 'native', 'drivers', 'MtkWlanNative')
DUMPS = os.path.join(REPO, 'abi', 'native-wifi')
PY = sys.executable
sys.path.insert(0, HERE)
from genhdr import ORDER, GUARD_CLASSES  # noqa: E402

# functions whose code the front end's payload layouts were read from: a change here is a review item
WATCH_FUNCTIONS = ('scanResultEventHandler', 'updateOrAddBeacon', 'setCurrentBSS', 'updateBss', 'linkStatusInd',
                   'setWCL_ASSOCIATE', 'setWCL_SCAN_REQ', 'getWCL_BSS_INFO', 'getWCL_EXTENDED_BSS_INFO', 'handleJoinRequest',
                   'getCARD_CAPABILITIES', 'initRegistrationInfo', 'registerInfraEthernetInterface', 'IO80211Controller::start',
                   'IO80211SkywalkInterface::start', 'IO80211InfraInterface::start')


def sh(cmd, **kw):
    print('+ ' + ' '.join(os.path.relpath(c, REPO) if c.startswith(REPO) else c for c in cmd))
    return subprocess.run(cmd, cwd=REPO, **kw)


def allow_list():
    pl = plistlib.load(open(os.path.join(KEXT, 'Info.plist'), 'rb'))
    return pl['IOKitPersonalities']['MtkWlan native front end'].get('MtkNativeAbi', {}).get('Builds', [])


def verdict(compare_dir):
    summary = open(os.path.join(compare_dir, 'summary.txt')).read().splitlines()
    section, vt_changed, size_changed, dispatch, fn_changed = '', [], [], [], []
    for line in summary:
        if line.startswith('## '):
            section = line[3:]
            continue
        if section.startswith('instance sizes') and line.strip():
            cls = line.split('\t')[0].strip()
            if cls in GUARD_CLASSES:
                size_changed.append(line.strip())
        elif section.startswith('vtables'):
            m = re.match(r'\s+(\S+)\s+(.*)$', line)
            if m and m.group(1) in ORDER and m.group(2) != 'unchanged':
                vt_changed.append('%s: %s' % (m.group(1), m.group(2)))
        elif section.startswith('apple80211 request tables') and 'differ' in line and ' 0 IDs differ' not in line:
            dispatch.append(line.strip())
        elif section.startswith('disassembled') and line.strip():
            fn_changed.append(line.strip())
    watched = []
    fpath = os.path.join(compare_dir, 'functions.txt')
    if os.path.exists(fpath):
        for line in open(fpath):
            if 'changed' in line.lower() and any(w in line for w in WATCH_FUNCTIONS):
                watched.append(line.strip()[:160])
    if not vt_changed and not size_changed and not dispatch and not watched:
        kind = 'unchanged'
    elif watched or dispatch:
        kind = 'review'
    else:
        kind = 'relayout'
    return kind, vt_changed, size_changed, dispatch, watched


def main(argv):
    dry, old, new, kc = '--dry-run' in argv, None, None, None
    args = [a for a in argv if a != '--dry-run']
    while args:
        if args[0] == '--old':
            old, args = args[1], args[2:]
        elif args[0] == '--new':
            new, args = args[1], args[2:]
        elif args[0] == '--kc':
            kc, args = args[1], args[2:]
        else:
            sys.exit(__doc__)
    builds = allow_list()
    old = old or (builds[-1]['Build'] if builds else None)
    new = new or subprocess.run(['sw_vers', '-buildVersion'], capture_output=True, text=True, check=True).stdout.strip()
    if not old:
        sys.exit('the kext has no allow-listed build yet; build it once with build-mac.sh')
    old_dir, new_dir = os.path.join(DUMPS, old), os.path.join(DUMPS, new)
    print('verified build %s, target build %s' % (old, new))
    if not os.path.isdir(old_dir):
        sys.exit('no dump for the verified build at %s' % old_dir)
    if not os.path.isdir(new_dir):
        cmd = [PY, '-I', os.path.join(HERE, 'dump.py'), '--out', new_dir]
        if kc:
            cmd += ['--kc', kc]
        if sh(cmd).returncode:
            sys.exit('dump failed')
    # a dry run (or a self-comparison) keeps its output out of the repo
    compare_dir = os.path.join(new_dir, 'compare-%s' % old) if not dry and old != new else \
        os.path.join(os.path.expanduser('~/.cache/legion'), 'kcabi-compare-%s-%s' % (old, new))
    stubs = os.path.join(KEXT, 'abi', old, 'iface_stubs.inc')
    cmd = [PY, '-I', os.path.join(HERE, 'compare.py'), old_dir, new_dir, '--out', compare_dir]
    if os.path.exists(stubs):
        cmd += ['--stubs', stubs]
    if sh(cmd, capture_output=True).returncode:
        sys.exit('compare failed')
    kind, vt, sizes, dispatch, watched = verdict(compare_dir)
    print('\nverdict for %s -> %s: %s' % (old, new, kind.upper()))
    for x in vt:
        print('  vtable   ' + x)
    for x in sizes:
        print('  size     ' + x)
    for x in dispatch:
        print('  dispatch ' + x)
    for x in watched:
        print('  function ' + x)
    print('  details: %s' % os.path.relpath(compare_dir, REPO))
    if old == new:
        print('  (same build on both sides: the pipeline itself is being tested)')
    if dry:
        return 0
    env = dict(os.environ, ABI=new)
    print('\nrebuilding MtkWlanNative for %s (headers abi/%s, allow-list entry, slot and link checks)' % (new, new))
    r = subprocess.run(['./build-mac.sh'], cwd=KEXT, env=env)
    if r.returncode:
        sys.exit('build failed: the new build needs hand work, see docs/native-wifi.md')
    if kind == 'unchanged':
        print('\nDONE: ABI unchanged. Swap src/out/MtkWlanNative.kext into OCEFI (Bash\'s OK first).')
    elif kind == 'relayout':
        print('\nREBUILT: slots or sizes moved and the kext follows them. Read the compare notes before the first boot.')
    else:
        print('\nREVIEW: the request tables or watched functions changed. Keep MtkNativeEnabled false until reviewed.')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
