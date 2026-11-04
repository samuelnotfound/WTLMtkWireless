#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Check the two kexts agree on the ABI without widening the native Apple ABI allowlist."""
import plistlib
import re
from pathlib import Path
root = Path(__file__).resolve().parents[1]
backend = (root / 'src/Mtk7902Backend.cpp').read_text()
gate_mac = backend.split('int Mtk7902Backend::setMac(', 1)[1].split('void Mtk7902Backend::workerControl()', 1)[0]
assert not any(call in gate_mac for call in ('IOLockSleep', 'IOSleep', 'm3Cmd(')), 'Native MAC gate must not wait on firmware'
native = root / 'native/WTLWifiMenu'
for name in ('MtkCoreApi.h', 'MtkUserApi.h'):
    def code(p): return re.sub(r'//[^\n]*', '', p.read_text()).split()
    assert code(root / 'src' / name) == code(native / 'src' / name), name
core = plistlib.loads((root / 'Info.plist').read_bytes())
p = core['IOKitPersonalities']['MT7902 PCIe']
assert p['IOPCIPrimaryMatch'] == '0x790214c3' and p['IOClass'] == 'MtkWlan'
assert p['MtkFrontEnd'] == 'native'
info = plistlib.loads((native / 'Info.plist').read_bytes())
frontend = info['IOKitPersonalities']['MtkWlan native front end']
assert frontend['MtkNativeInterface'] is True
builds = frontend['MtkNativeAbi']['Builds']
assert [b['Build'] for b in builds] == ['25G241', '25G332']
for b in builds:
    import json
    abi = json.loads((native / 'abi' / b['Build'] / 'abi.json').read_text())
    assert b['Xnu'] == abi['xnu']
    assert b['Sizes'] == abi['sizes']
version = re.search(r'#define MTK_NATIVE_VERSION "([^"]+)"', (native / 'src/MtkWlanNative.cpp').read_text()).group(1)
assert version == info['CFBundleVersion'] == info['CFBundleShortVersionString']
for filename in ('LICENSE', 'NOTICE.md', 'LICENSES/BSD-3-Clause-Clear-itlmt7925.txt'):
    assert (native / filename).is_file(), filename
print('MT7902 core/native ABI agreement, PCI dispatch, metadata and unchanged Apple ABI allowlist passed')
