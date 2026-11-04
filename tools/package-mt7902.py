#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Package only explicit public artifacts; verify both chips' firmware is embedded once."""
import hashlib
import json
import plistlib
import shutil
import subprocess
import zipfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
out = root / 'out'
info = plistlib.loads((root / 'Info.plist').read_bytes())
version = info['CFBundleVersion']
assert plistlib.loads((out / 'MtkWlan.kext/Contents/Info.plist').read_bytes()) == info
binary = (out / 'MtkWlan.kext/Contents/MacOS/MtkWlan').read_bytes()
firmware = {}
for directory in (root / 'firmware', root / 'experimental/MT7902Port/fw'):
    for line in (directory / 'SHA256SUMS').read_text().splitlines():
        if not line.strip(): continue
        digest, name = line.split()
        name = name.lstrip('*')
        data = (directory / name).read_bytes()
        assert hashlib.sha256(data).hexdigest() == digest, name
        assert binary.count(data) == 1, f'{name}: firmware missing or duplicated'
        firmware[name] = digest
app = out / 'MtkWiFi7902.app'
assert plistlib.loads((app / 'Contents/Info.plist').read_bytes())['CFBundleVersion'] == version
subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
dist = out / 'dist'
dist.mkdir(exist_ok=True)
package = out / f'MtkWlan-MT7902-{version}'
if package.exists(): shutil.rmtree(package)
package.mkdir()
for name in ('MtkWlan.kext', 'MtkWiFi7902.app'): shutil.copytree(out / name, package / name)
native = root / 'native/WTLWifiMenu'
shutil.copytree(native / 'out/MtkWlanNative.kext', package / 'MtkWlanNative.kext')
native_info = plistlib.loads((package / 'MtkWlanNative.kext/Contents/Info.plist').read_bytes())
assert [b['Build'] for b in native_info['IOKitPersonalities']['MtkWlan native front end']['MtkNativeAbi']['Builds']] == ['25G241', '25G332']
shutil.copy2(root / 'tools/mtkwifi/out/mtkwifi', package / 'mtkwifi')
shutil.copy2(root / 'tools/mtk-report.sh', package / 'collect-logs.sh')
shutil.copy2(root / 'docs/MT7902.md', package / 'READ-ME-FIRST.md')
for name in ('LICENSE', 'NOTICE.md'): shutil.copy2(root / name, package / name)
shutil.copytree(root / 'LICENSES', package / 'LICENSES')
shutil.copy2(native / 'LICENSE', package / 'LICENSES/WTLWifiMenu-LICENSE')
shutil.copy2(native / 'NOTICE.md', package / 'LICENSES/WTLWifiMenu-NOTICE.md')
shutil.copytree(native / 'LICENSES', package / 'LICENSES/WTLWifiMenu')
notices = package / 'LICENSES/MT7902'
notices.mkdir()
for name in ('LICENSE', 'NOTICE.md', 'fw/LICENCE.mediatek', 'fw/SHA256SUMS', 'licenses/mt76-LICENSE.txt'):
    shutil.copy2(root / 'experimental/MT7902Port' / name, notices / Path(name).name)
manifest = {'version': version, 'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root).decode().strip(),
            'architecture': 'x86_64', 'core_api_mt7902': [1, 2, 3, 4], 'firmware': firmware,
            'native_source_commit': 'a768120b0477930357289dcdfed45a34ec60673d', 'native_abi_builds': ['25G241', '25G332'],
            'hardware_validated_integrated_build': False,
            'backend_source_commit': 'a4869dfc513f1a4ac2bc802657ba1854ad4aaaa8',
            'binary_sha256': hashlib.sha256(binary).hexdigest()}
(package / 'build-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
archive = dist / f'{package.name}.zip'
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for p in sorted(package.rglob('*')):
        if p.is_file(): z.write(p, p.relative_to(package.parent))
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    assert not any('networks.json' in n or 'networks.plist' in n or '.git/' in n for n in z.namelist())
(dist / 'SHA256SUMS.txt').write_text(hashlib.sha256(archive.read_bytes()).hexdigest() + '  ' + archive.name + '\n')
(dist / 'build-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(archive)
