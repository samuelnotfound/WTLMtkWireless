#!/bin/bash
# Build MtkWlanNative.kext on macOS with the Command Line Tools SDK. Needs the ABI headers for your macOS build in
# abi/<build>/ (25G241 ships here); for another build, run tools/kcabi/dump.py on that machine first and then
# tools/kcabi/genhdr.py gen <dump> abi/<build> --iface-class MtkWlanNativeIf --impl src/MtkWlanNativeIf.cpp src/MtkWlanNativeReq.cpp.
#   ABI=25G241 ./build.sh      (default: the build this Mac runs)
set -euo pipefail
cd "$(dirname "$0")"
NAME=MtkWlanNative
ABI=${ABI:-$(sw_vers -buildVersion)}
[ -d "abi/$ABI" ] || { echo "FAIL: no ABI headers in abi/$ABI (see the comment at the top of this script)"; exit 1; }
PB=/usr/libexec/PlistBuddy
KEXT_ID=$($PB -c 'Print :CFBundleIdentifier' Info.plist)
KEXT_VERSION=$($PB -c 'Print :CFBundleVersion' Info.plist)
SRC_VERSION=$(sed -n 's/^#define MTK_NATIVE_VERSION "\([^"]*\)".*/\1/p' src/MtkWlanNative.cpp)
[ "$SRC_VERSION" = "$KEXT_VERSION" ] || { echo "FAIL: MTK_NATIVE_VERSION is $SRC_VERSION, Info.plist says $KEXT_VERSION"; exit 1; }
SRCS="src/MtkNativeTrace.cpp src/MtkWlanNative.cpp src/MtkWlanNativeIf.cpp src/MtkWlanNativeReq.cpp"
SDK=$(xcrun --sdk macosx --show-sdk-path)
K=$SDK/System/Library/Frameworks/Kernel.framework/Headers
RES=$(clang -print-resource-dir)
OUT=out/$NAME.kext/Contents
rm -rf out/$NAME.kext out/obj
mkdir -p "$OUT/MacOS" out/obj
FLAGS="-arch x86_64 -mmacosx-version-min=10.15 -mkernel -nostdinc -fno-builtin -fno-common -fno-stack-protector
 -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT -isystem $RES/include -I$K -O2 -g0 -Wall -Wno-unused-parameter
 -Wno-deprecated-declarations"
CXXFLAGS="$FLAGS -fstack-usage -fapple-kext -std=gnu++17 -nostdinc++ -fno-exceptions -fno-rtti -fno-use-cxa-atexit
 -fno-register-global-dtors-with-atexit -Wno-inconsistent-missing-override -Iabi/$ABI -Isrc"
OBJS=""
echo "== compile ($ABI)"
for s in $SRCS; do
  o=out/obj/$(basename "${s%.*}").o
  clang++ $CXXFLAGS -c "$s" -o "$o"
  OBJS="$OBJS $o"
done
clang $FLAGS -std=gnu11 -DKEXT_ID=$KEXT_ID -DKEXT_VERSION="\"$KEXT_VERSION\"" -c src/kmod_info.c -o out/obj/kmod_info.o
clang++ -arch x86_64 -mmacosx-version-min=10.15 -nostdlib -Xlinker -kext -Xlinker -export_dynamic \
  $OBJS out/obj/kmod_info.o -L"$SDK/usr/lib" -lkmodc++ -lkmod "$RES/lib/darwin/libclang_rt.cc_kext.a" \
  -o "$OUT/MacOS/$NAME"
[ -f "$OUT/MacOS/$NAME" ] || { echo "FAIL: link"; exit 1; }
cp Info.plist "$OUT/Info.plist"
echo "== checks"
otool -hv "$OUT/MacOS/$NAME" | grep -q KEXTBUNDLE || { echo "FAIL: not MH_KEXT_BUNDLE"; exit 1; }
nm -u "$OUT/MacOS/$NAME" | sort > out/undefined.txt
if grep -E '^_{2,3}(cxa_|Unwind|gxx_)|^__ZTV.*__cxxabiv|^__Znw|^__Zdl' out/undefined.txt; then echo "FAIL: C++ runtime symbols"; exit 1; fi
# NO_LINKCHECK=1 skips the whole-collection read (heavy I/O; Legion I-116)
if [ -z "${NO_LINKCHECK:-}" ] && [ -f /System/Library/KernelCollections/BootKernelExtensions.kc ] && [ -f tools/kcabi/linkcheck.py ]; then
  python3 -I tools/kcabi/linkcheck.py out/$NAME.kext --own-lib org.watchthelight.MtkWlan || echo "(link check: see above)"
fi
echo "built $KEXT_ID $KEXT_VERSION for $ABI: $OUT/MacOS/$NAME ($(stat -f %z "$OUT/MacOS/$NAME") bytes)"
