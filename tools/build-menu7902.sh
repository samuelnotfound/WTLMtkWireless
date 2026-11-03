#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause-Clear
set -euo pipefail
cd "$(dirname "$0")/.."
app=out/MtkWiFi7902.app
mkdir -p "$app/Contents/MacOS" out/swift-module-cache
python3 - <<'PY'
import plistlib
from pathlib import Path
p = plistlib.loads(Path('experimental/MT7902Port/app/Info.plist').read_bytes())
v = plistlib.loads(Path('Info.plist').read_bytes())['CFBundleVersion']
p.update(CFBundleExecutable='MtkWiFi7902', CFBundleName='MtkWiFi7902',
         CFBundleIdentifier='local.experimental.MtkWiFi7902', CFBundleVersion=v, CFBundleShortVersionString=v)
Path('out/MtkWiFi7902.app/Contents/Info.plist').write_bytes(plistlib.dumps(p))
PY
xcrun swiftc -O -DMTK_CORE -target x86_64-apple-macos13.0 -debug-prefix-map "$PWD=." \
    -module-cache-path "$PWD/out/swift-module-cache" \
    experimental/MT7902Port/app/ItlWiFi7902.swift experimental/MT7902Port/app/SpeedTest.swift \
    -o "$app/Contents/MacOS/MtkWiFi7902" -framework IOKit -framework SwiftUI
codesign --force --sign - "$app"
codesign --verify --deep --strict "$app"
