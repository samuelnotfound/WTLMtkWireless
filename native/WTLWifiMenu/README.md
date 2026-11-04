# WTLWifiMenu: the native Wi-Fi menu for the MediaTek MT7925 on macOS Hackintosh

Renamed from `mt7925-hackintosh-wifi-native` on 2026-10-31; the kext is still `MtkWlanNative.kext`, so existing OpenCore configs keep working. Old links redirect here.

The native Wi-Fi menu for the MediaTek MT7925 (Filogic 360, Wi-Fi 7, PCI `14C3:7925`, AMD RZ717 `14C3:0717`) on a
Hackintosh. [WTLMtkWireless](https://github.com/watchthelight-labs/WTLMtkWireless) is the driver; on
its own it shows macOS an Ethernet port and you join networks with a tool, like SimpleMtkWlan does for the MT7921 and
MT7922. This second kext is an `IO80211Controller` plus an `IO80211InfraProtocol` interface built against Apple's
IO80211 and Skywalk families, so macOS sees a real Wi-Fi card: the Wi-Fi icon in the menu bar, the Wi-Fi pane in
System Settings, scans and joins run by macOS, saved networks, auto-join at boot, a private address per network and the
data path on Skywalk. You don't need a client app or the command line. The driver still owns the hardware (firmware, DMA, scan,
association, WPA2) and this kext talks to it through a C ops table (`MtkCoreApi.h`), not through C++ linking.

It is built against two macOS builds, Tahoe 26.7.1 (25G241) and 26.7.2 (25G332), and refuses to load on any other, so a wrong build can
never take your network away: the driver falls back to its Ethernet front end by itself.

## The MT7925 set

| Repo | What it does |
|---|---|
| [WTLMtkWireless](https://github.com/watchthelight-labs/WTLMtkWireless) | the driver (required); its README has the cards table and the comparison with SimpleMtkWlan and AirportMtkWlan |
| WTLWifiMenu (this one) | the native Wi-Fi menu on top of it |
| [WTLMtkBluetooth](https://github.com/watchthelight-labs/WTLMtkBluetooth) | the Bluetooth firmware loader for the same card |

The bundle zip on the driver's [releases page](https://github.com/watchthelight-labs/WTLMtkWireless/releases)
carries both Wi-Fi kexts with an `INSTALL.md`. A complete EFI that uses the set:
[WTLLegionOpenCore](https://github.com/watchthelight-labs/WTLLegionOpenCore).

## Status

Tested by me on one laptop (Legion 5 15IAX10, MT7925, OpenCore 1.0.8) on macOS 26.7.1 (25G241) with MtkWlan 0.12.1 and this kext at 0.4.13, and on 26.7.2 (25G332) with 0.4.14 (2026-10-28: the native interface came up at boot and joined the saved network on its own) and 0.4.15 (2026-10-31).

| Version | What it does | Result |
|---|---|---|
| 0.4.15 | link quality to macOS (`setLQM`), real width for every scan result, the AP's country code, HT and VHT maps in the extended BSS info | `LinkQuality : 100` for en1, apsd `isBadLQ? NO` and a push received over Wi-Fi (before, push never connected, so iMessage, FaceTime and Handoff did not work on this card); Country Code US instead of XZ; scan results at 5 GHz/80 MHz instead of 20 |
| 0.4.13 | real width, MCS, streams, guard interval and rate in the Wi-Fi details (needs MtkWlan 0.12.1); country code goes to the core; a refused private address is retried | `wdutil info` shows PHY 11ac, 5g100/80, MCS 9, NSS 2, guard interval 400, 866 Mbps; joins at boot |
| 0.4.12 | pairs with MtkWlan 0.12.0 (link supervisor reset comes back through the restored-link path); 128 networks to the menu | booted together with 0.4.13, joins at boot |
| 0.4.9 | link quality update events (0x27) once a second, which refresh the timestamps the Family's assoc timer checks | **stable**: macOS auto-joins at boot, holds the link (ten quiet minutes, zero leaves), DHCP, real default route, internet for apps. 0.4.7 and 0.4.8 posted beacon counters only and the Family still raised "missed beacons timeout" 60 s after each join |
| 0.4.8 | beacon-counter events while the core holds the link | still lost the link every 60 s: the beacon counter feeds a different check |
| 0.4.6 | the join lands on the access point macOS chose (the candidate BSSID at +0x21c) | confirmed; 0.4.4 and 0.4.5 re-joined every 15 to 60 s because the core joined its own favourite BSS |
| 0.4.4 | CoreCapture pipes started after the core binding (an earlier order lost the core's 15 s front-end race) | macOS joined the network by itself at boot, lease, real default route, internet for apps |
| 0.4.3 | CoreCapture data pipe and stream started (a completed fault report panicked in `CCDataPipe::enqueueBlob` on an unstarted pipe) | no panic |
| 0.4.1 | the join path: `WCL_LOW_LATENCY_INFO`, `SET_MAC_ADDRESS` (private address into the firmware), `WCL_ASSOCIATE`, the link status / assoc / join / first beacon / connect complete events, BSS info and extended BSS info answers, link state update, link up done | macOS-driven join of an open network, DHCP on the private address, network saved and auto-joined on later boots |
| 0.3.1 | scan requests answered with one scan-result event per BSS in the 25G241 layout | airportd parsed 48 networks with security, channel, phy and RSSI |
| 0.2.1 | the station address follows the interface (macOS sets a private address per network; the firmware must answer to it) | DHCP works on the native data path |
| 0.1.0 | controller and interface come up, Skywalk packet pool and queues, data path bridged to the core | packets flow |

WPA2-PSK through the Wi-Fi menu works: oliverchu joined a WPA/WPA2 Personal network from the menu on an AM5 desktop
(AMD RZ717 card, MtkWlan 0.12.1 and this kext at 0.4.13, macOS 25G241), with auto-join at boot and a DHCP lease
([#3](https://github.com/watchthelight-labs/WTLWifiMenu/issues/3)). On 26.7.2 (25G332) a user with an ASUS ROG
Strix B860-G desktop (on-board MT7925) and a user with an HP Omen 16 (board 8F06) report Wi-Fi and Bluetooth working
with 0.4.14 and LegionBtFix 0.3.0 ([WTLMtkWireless #4](https://github.com/watchthelight-labs/WTLMtkWireless/issues/4)).

What's missing: no roaming (the Family asks us to associate to one access point and we do that),
no 802.11ax/be, no sleep, no AirDrop/Continuity (no AWDL interface), no WPA3-SAE, no enterprise, Country Code is reported
as XZ. The captive-portal flow is macOS's own: on a portal network the Captive Network Assistant opens and the interface
is ranked "Never" until you log in, as on a real Mac.

## Requirements

- MtkWlan 0.11.1 or later (0.11.0 works but can lose the start-up race, see the changelog) installed and working with its Ethernet front end first (core ops table v4).
- macOS 26.7.1 (25G241) or 26.7.2 (25G332). The kext is built against those builds' class layouts and vtable slots and
  refuses to load on any other build (`MtkNativeAbi` in `Info.plist` lists the builds it was generated for). For another build,
  see "Building".
  25G332 support is new in 0.4.14. Its class sizes, vtables and request slots are identical to 25G241's (the
  headers in `abi/25G332/` differ only in the build name), so the same binary serves both. On 0.4.13 and older the kext
  stays out on 26.7.2 and MtkWlan falls back to its Ethernet port
  ([#4](https://github.com/watchthelight-labs/WTLMtkWireless/issues/4)).

## Installation

1. Put `MtkWlanNative.kext` next to `MtkWlan.kext` in your OpenCore `Kexts` folder and add it to `config.plist` after
   MtkWlan (it links against MtkWlan's core).
2. Add `mtkfe=native mtknativeif=1` to `boot-args`. Without them MtkWlan keeps its Ethernet front end and this kext
   stays a controller without an interface (safe to leave installed).
3. Reboot. The Wi-Fi menu appears; join your network from it. If the native front end declines, or claims the core
   and still has not registered after 90 s (15 s with MtkWlan before 0.11.1), the core falls back to the Ethernet front
   end by itself, so a broken build still leaves you with network.

OpenCore's `NVRAM > Add` does not replace a `boot-args` that is already in NVRAM (the OpenCore log says `ignored,
exists`), so the new flags can be silently skipped. List `boot-args` under `NVRAM > Delete` for
`7C436110-AB2A-4BBB-A880-FE41995C9F82`, or pick Reset NVRAM in the OpenCore picker once. Without the flags
`MtkNativeLast` reads `ctl:controller only (no interface requested)` and there is no Wi-Fi port at all.

If the card ran as an Ethernet port before, macOS keeps that port's network service (often named "Ethernet 2") and
moves it to the Wi-Fi port. The link works, but the service keeps its Ethernet name and System Information can still
say "Network Service Inactive" while you are connected. `networksetup -removenetworkservice` refuses it because it is
the only service on the Wi-Fi port. Renaming it fixes the name without dropping the link
(`networksetup -renamenetworkservice "Ethernet 2" "Wi-Fi"`). For a fresh service, add one on the Wi-Fi port first and
then remove the old one: `networksetup -createnetworkservice "Wi-Fi 2" Wi-Fi`, then
`networksetup -removenetworkservice "Ethernet 2"` (not tested yet; report back in
[#3](https://github.com/watchthelight-labs/WTLWifiMenu/issues/3) if you try it).

## Diagnostics

`ioreg -r -c MtkWlanNative -l -w0` holds the controller's state: `MtkNativeTrace` (what happened, in order),
`MtkNativeCalls` (every request the Family made, by slot or ioctl code, and every event we posted), `MtkNativeScan`,
`MtkNativeJoin`, `MtkNativeMacFollow`, the data path counters `MtkNativeTx` / `MtkNativeRx`, and `MtkNativeAbi`. For
the Family's own view: `log show --last 5m --info --predicate 'process == "airportd" OR process == "kernel"' | grep -E
'IOC DEBUG|AUTO-JOIN'`. `docs/HOW-IT-WORKS.md` describes the design, and `docs/NOTES-NATIVE.md` is the lab notebook:
what each version found in the 25G241 binaries.

## Other MediaTek cards: collecting logs and reporting back

I run the driver on one card (MT7925 `14c3:7925`, subsystem `17aa:e0ff`); users run it on the AMD RZ717 (`14c3:0717`)
and on an ASUS B860 board's MT7925.
This front end only needs the driver, so it is the driver that decides whether your card works; the front end then
depends on the macOS build. The MT7921 and MT7922 are not supported by the driver yet (the plan and the call for
testers are in [its issue #2](https://github.com/watchthelight-labs/WTLMtkWireless/issues/2)). If you try it:

1. Get MtkWlan working with its Ethernet front end first and report there if it does not.
2. Install this kext, add the boot-args, reboot, wait a minute.
3. Run `bash tools/mtk-report.sh` (no sudo). It writes `~/Desktop/mtk-report-<date>.tar.gz` with the PCI ids, the
   loaded kexts, every `ioreg` property of both kexts, interfaces and routes, the Wi-Fi system profile, the kernel log
   lines of the drivers and the IO80211 family, the Wi-Fi daemons' log, and the last kernel panic reports. MAC addresses
   and SSIDs are replaced by placeholders (`--no-redact` keeps them). Nothing in it is a key or passphrase.
4. Open an issue at github.com/watchthelight-labs/WTLWifiMenu/issues with the archive and three lines: the card,
   the macOS build (`sw_vers`), and what you saw (did the Wi-Fi menu list networks, did a join get a lease, did the
   machine panic). A panic file in the report tells me where.

## Building

`./build.sh` compiles against the Command Line Tools SDK with the ABI headers in `abi/<build>/` (25G241 and 25G332 ship here). Those headers are
generated from a dump of the macOS build's kernel collection (class sizes, vtables, the 200 request slots the Family
routes ioctls to); `tools/kcabi/dump.py` makes that dump on the target machine and `tools/kcabi/genhdr.py gen` turns it
into headers for another build. The build script then checks that every override sits on Apple's slot and that every
undefined symbol is exported by the running kernel collection (`tools/kcabi/linkcheck.py`).

## Credits

- [oliverchu](https://github.com/oliverchu): the first WPA2-PSK join on air, on a COLORFUL B850M desktop with an AMD
  RZ717 card, and the boot-args and network-service notes under Installation
  ([#3](https://github.com/watchthelight-labs/WTLWifiMenu/issues/3)).
- [ga403wm-hackintosh](https://github.com/thanhnndev/ga403wm-hackintosh) (BSD-3-Clause-Clear, Copyright (c) 2026
  thanhnndev and contributors): the shape of a native front end for a MediaTek core on macOS 15, the interface start
  sequence, the join event sequence and several payload layouts, re-read against the 25G241 binaries here. See
  `NOTICE.md`.
- [AirportMtkWlan](https://github.com/mnnobre/AirportMtkWlan) and laobamac's itlwm / SimpleMtkWlan (GPL-2.0): read as
  references for what macOS expects of a Wi-Fi driver; no code from them is in this repository.
- [qwz-macos](https://github.com/f0xeri/qwz-macos) (GPL-2.0, f0xeri): an independent native front end on 25G241 for a
  Qualcomm WCN7850 core; its current-channel width flags and its power-state replay after wake were read as references;
  no code from it is in this repository.
- [mt7925-macos-wifi](https://github.com/thanhnndev/mt7925-macos-wifi) (BSD-3-Clause-Clear, thanhnndev): the itlmt7925
  driver split out of ga403wm-hackintosh, with per-build ABI folders like ours and AWDL work in progress that we follow
  for [#2](https://github.com/watchthelight-labs/WTLWifiMenu/issues/2); no code from it is in this
  repository.
- [AirportRTW89-MT7922](https://github.com/DGBooth/AirportRTW89-MT7922) (GPL-2.0, DGBooth): MT7922 as a native AirPort
  device on Tahoe through OCLP's legacy IO80211 stack, the other route to the Wi-Fi menu; no code from it is in this
  repository.
- [RTL8814Native](https://github.com/DrogaBox/RTL8814Native) (GPL-2.0, DrogaBox): a native IO80211 and Skywalk front
  end for the USB RTL8814AU on 26.7.1 that dumps IO80211's vtables per build and gates its build on them, the same
  per-build pin we use; an independent dump to check ours against; its AWDL notes (the role 6 creation path, the
  wifip2pd registration contract and what the kernel peer manager needs from a driver; since 2026-10-30 its layer
  creates `awdl0` and gets the peer manager through configure and enableAWDL on 25G241, with no peer in the air yet)
  are read as references for
  [#2](https://github.com/watchthelight-labs/WTLWifiMenu/issues/2); no code from it is in this repository.
- [owly](https://github.com/t4t5/owly) (MIT, t4t5): AirDrop and AWDL for non-Apple Wi-Fi cards on Linux. It lists the
  MT7925 as working in shared mode and ships iOS 26 captures taken on an `mt7925e`, plus a protocol spec
  (`docs/SPEC.md`); read as a reference for what the radio has to send for [#2](https://github.com/watchthelight-labs/WTLWifiMenu/issues/2);
  no code from it is in this repository.
- [AirPort-RTW](https://github.com/JoMei9019-real/AirPort-RTW) (GPL-2.0, JoMei9019-real): Realtek PCIe cards as native
  AirPort devices on Tahoe through the same OCLP legacy stack. Its 1.1.0 adds 802.11k/v roaming and an AWDL transmit
  gate, and its install guide lists which legacy components Sonoma to Tahoe need (the system's IOSkywalkFamily blocked,
  OCLP's IOSkywalkFamily and IO80211FamilyLegacy added); no code from it is in this repository.
- Apple's IO80211Family, IOSkywalkFamily and corecapture, read in disassembly to learn the ABI. No Apple code is in this
  repository; the generated headers hold class and method names and slot numbers only.

## Licence

ISC, see `LICENSE`. `LICENSES/` and `NOTICE.md` cover the adapted parts.
