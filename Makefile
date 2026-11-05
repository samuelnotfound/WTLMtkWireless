# <wtl> project: mtkwlan | file: Makefile | author: watchthelight </wtl>
# Cross-builds MtkWlan.kext on Linux (or WSL). Toolchain: tools/build-toolchain.sh.
# On macOS use tools/build-mac.sh instead (Command Line Tools, no SDK clone).
#   make            -> out/MtkWlan.kext
#   make SDK=/path/to/MacKernelSDK
NAME         := MtkWlan
KEXT_ID      := org.watchthelight.MtkWlan
KEXT_VERSION := 0.13.1

SDK    ?= $(CURDIR)/MacKernelSDK
LD     := LD_LIBRARY_PATH=/opt/dispatch/lib /opt/cctools/bin/x86_64-apple-darwin-ld
CC     := clang
CXX    := clang++
TARGET := -target x86_64-apple-macos10.15
RES    := $(shell clang -print-resource-dir)/include

SRCS := src/MtkWlan.cpp src/MtkRegs.cpp src/MtkDma.cpp src/MtkFw.cpp src/MtkScan.cpp src/MtkJoin.cpp src/MtkSec.cpp \
        src/MtkNet.cpp src/MtkBa.cpp src/MtkPower.cpp src/MtkWlanEth.cpp src/MtkUserClient.cpp \
        src/MtkCrypto.c src/MtkSupp.c src/MtkSuper.c out/gen/MtkFirmware.c \
        experimental/MT7902Port/src/itlmt7902.cpp experimental/MT7902Port/src/itlmt7902_m1.cpp experimental/MT7902Port/src/itlmt7902_m2.cpp \
        experimental/MT7902Port/src/itlmt7902_m3.cpp experimental/MT7902Port/src/itlmt7902_txrx.cpp experimental/MT7902Port/src/itlmt7902_pwr.cpp \
        experimental/MT7902Port/src/itlmt7902_ctl.cpp experimental/MT7902Port/src/itlmt7902_eth.cpp experimental/MT7902Port/src/itlmt7902_ba.cpp \
        experimental/MT7902Port/src/itlmt7902_uc.cpp experimental/MT7902Port/src/itl_wpa.cpp experimental/MT7902Port/src/itl_caps.cpp \
        experimental/MT7902Port/src/itl_ba.cpp experimental/MT7902Port/codec/mt7902_ba_codec.cpp experimental/MT7902Port/codec/mt7902_data_codec.cpp \
        experimental/MT7902Port/codec/mt7902_join_codec.cpp experimental/MT7902Port/codec/mt7902_rx_codec.cpp experimental/MT7902Port/codec/mt7902_scan_codec.cpp \
        experimental/MT7902Port/codec/mt7902_tx_codec.cpp src/Mtk7902Backend.cpp src/Mtk7902Firmware.c

KFLAGS := $(TARGET) -mkernel -fapple-kext -nostdinc -fno-builtin -fno-common -fno-stack-protector \
          -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT -DMTK_WITH_MT7902 \
          -isystem $(RES) -I$(SDK)/Headers -Isrc -O2 -g0 -Wall -Wno-unused-parameter
CFLAGS   += $(KFLAGS) -std=gnu11
# the last three flags keep clang from emitting __cxa_atexit, which the kernel does not export
CXXFLAGS += $(KFLAGS) -std=gnu++17 -nostdinc++ -fno-exceptions -fno-rtti -fno-use-cxa-atexit \
            -fno-register-global-dtors-with-atexit -mllvm -disable-atexit-based-global-dtor-lowering \
            -Wno-inconsistent-missing-override

OBJS := $(addprefix out/obj/,$(notdir $(SRCS:=.o))) out/obj/kmod_info.o
BIN  := out/$(NAME).kext/Contents/MacOS/$(NAME)

all: $(BIN) out/$(NAME).kext/Contents/Info.plist check

out/obj/%.cpp.o: src/%.cpp src/*.hpp src/*.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

out/obj/%.cpp.o: experimental/MT7902Port/src/%.cpp experimental/MT7902Port/src/*.h experimental/MT7902Port/src/*.hpp src/Mtk7902Backend.hpp src/MtkAssocFrame.h src/MtkSupp.h src/MtkCrypto.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

out/obj/%.cpp.o: experimental/MT7902Port/codec/%.cpp experimental/MT7902Port/codec/*.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

out/obj/%.c.o: src/%.c src/*.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

out/obj/MtkFirmware.c.o: out/gen/MtkFirmware.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

out/obj/kmod_info.o: src/kmod_info.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DKEXT_ID=$(KEXT_ID) -DKEXT_VERSION='"$(KEXT_VERSION)"' -c $< -o $@

# firmware blobs from linux-firmware, checked against firmware/SHA256SUMS
out/gen/MtkFirmware.c: tools/gen_firmware.py firmware/SHA256SUMS
	python3 tools/gen_firmware.py $@

$(BIN): $(OBJS)
	@mkdir -p $(dir $@)
	$(LD) -arch x86_64 -kext -platform_version macos 10.15 11.0 -o $@ $(OBJS) \
	  $(SDK)/Library/universal/libkmodc++.a $(SDK)/Library/x86_64/libkmod.a

out/$(NAME).kext/Contents/Info.plist: Info.plist
	@mkdir -p $(dir $@)
	cp $< $@

# offline sanity: MH_KEXT_BUNDLE, kmod_info present, no C++ runtime symbols the kernel lacks
check: $(BIN)
	@llvm-objdump --macho --private-header $(BIN) | grep -q KEXTBUNDLE && echo "filetype: MH_KEXT_BUNDLE"
	@llvm-nm $(BIN) | grep -q ' _kmod_info' && echo "kmod_info: present"
	@echo "undefined symbols: $$(llvm-nm -u $(BIN) | wc -l)"
	@! llvm-nm -u $(BIN) | grep -E '^_{2,3}(cxa_|Unwind|gxx_)' || (echo "FAIL: C++ runtime symbols the kernel does not export"; false)

# host tests for the WPA2 code (clang, UBSan, Python oracle); writes only to out/tests/
test:
	bash tests/run.sh

clean:
	rm -rf out

.PHONY: all check test clean
