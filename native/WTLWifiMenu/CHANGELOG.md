# Changelog

## 0.4.15 (2026-10-31)

- Link quality goes to macOS. The interface now calls `setLQM` (slot 378, which ends in `IOSkywalkNetworkInterface::reportLinkQuality`) once a second while associated: 100 at -75 dBm or better, 50 down to -85 dBm, 20 below that, and -1 (unknown) when the link drops. Before, `State:/Network/Interface/en1/LinkQuality` was never set, apsd logged `isBadLQ? YES` and `Push is connected? NO`, so Apple push, and with it iMessage, FaceTime and Handoff, did not connect over Wi-Fi. Lead from the 2026-10-31 research run (an itlwm fork calls it the same way); checked live on the Legion before the change.
- Scan results carry each network's real width ([#1](https://github.com/watchthelight-labs/WTLWifiMenu/issues/1)). The chanspec in each scan-result event is built from the HT and VHT Operation elements, in the Broadcom d11ac layout that `ChanSpecGetPrimaryChannel` and `ChanSpecConvToApple80211Channel` decode on 25G332 (center channel, control sideband, 0x1800 for 40 MHz, 0x2000 for 80 MHz). 160 MHz is reported as 80, the width the core joins at. Host test in `tests/chanspec_test.c` checks every case against Apple's decoder math.
- The country code is the associated AP's Country element (802.11d) until macOS sets one, instead of XZ. Report only: the core's regulatory domain does not change.
- The extended BSS info has the AP's HT RX MCS set (two streams) and the VHT MCS map both sides support, instead of "none".
- `build.sh` skips the kernel-collection link check with `NO_LINKCHECK=1`.
- Booted on 26.7.2 (25G332) on the Legion 5 with MtkWlan 0.12.1. `LinkQuality : 100` appears for en1, apsd logs `isBadLQ? NO` and received a push over Wi-Fi, `wdutil info` shows Country Code US and 5g108/80, and a CoreWLAN scan reports 8 of 8 networks at 5 GHz/80 MHz.

## 0.4.14 (2026-10-28)

- macOS 26.7.2 (25G332) is allow-listed ([#4](https://github.com/watchthelight-labs/WTLMtkWireless/issues/4)). The kcabi dump of 25G332's boot kernel collection, taken from Apple's OTA, has the same instance sizes, vtables and 200 request slots as 25G241, and `genhdr.py` makes headers that differ only in the build name (`abi/25G332/`). Every undefined symbol of the kext is exported by the 25G332 collection. Of the functions Apple changed, only `kern_netif_queue_rx_enqueue` (a field moved from +0x4d8 to +0x4e8) touches the receive path, and the kext never calls it directly. Not yet booted on 25G332.
- System Information showed "Firmware Version: MtkWlanNative 0.4.11" on 0.4.12 and 0.4.13: `MTK_NATIVE_VERSION` was not bumped. It reads 0.4.13 now, and `build.sh` stops when it and Info.plist differ (seen in the report on [#3](https://github.com/watchthelight-labs/WTLWifiMenu/issues/3)).
- `tools/mtk-report.sh` also replaces MACs printed without leading zeros (netstat's route table showed the router's), IPv6 addresses and the `.local` host name.
- README: WPA2-PSK from the Wi-Fi menu confirmed on air by oliverchu (#3); notes on OpenCore keeping an old `boot-args` and on the leftover "Ethernet 2" service.

## 0.4.13 (2026-10-23)

- The Wi-Fi details in macOS are real now ([#1](https://github.com/watchthelight-labs/WTLWifiMenu/issues/1)). With MtkWlan 0.12.1, `wdutil info` on the author's Legion 5 shows PHY Mode 11ac, Channel 5g100/80, MCS Index 9, NSS 2, Guard Interval 400 and Tx Rate 866 Mbps. 0.4.11 showed 20 MHz and a fixed 54 Mbps.
  - `getMCS` (slot 479), `getMCS_VHT` (482) and `getGUARD_INTERVAL` (478) are answered, and requests 0x37, 0x39 and 0xc3 are no longer refused. The layouts were read from AppleBCMWLANCore on 25G241: getMCS_VHT has the MCS at +4, streams at +8, width in MHz at +0xc and the guard interval in ns at +0x10.
  - The current channel carries its real width and DFS flag, the same way Apple's `ChanSpecConvToApple80211Channel` writes them: 0x4 for 40 MHz (+0x200 when the extension is above), 0x400 for 80 MHz, 0x100 for channels 52 to 144.
  - `getRATE` reports the core's rate instead of the fixed link speed.
  - f0xeri's [qwz-macos](https://github.com/f0xeri/qwz-macos) 0.0.26 showed which requests Tahoe makes before it shows these fields.
- `setCOUNTRY_CODE` goes to the core (MtkWlan 0.12.1 `set_country`) instead of only being recorded. On N-261 airportd never set a country, so this has not run yet.
- A private Wi-Fi address the core refuses is tried again every 2 s, up to 6 times. Before, one refusal (for example during a join) left the firmware on the old address, and a network could look dead until Private Wi-Fi Address was turned off. Reported on an HP OMEN in [Game-5040/hackintosh-hp-8f06-macos26](https://github.com/Game-5040/hackintosh-hp-8f06-macos26).
- Still needs MtkWlan 0.11.x or later. With a core older than 0.12.1 the rate fields fall back to the old answers.

## 0.4.12 (built 2026-10-22; first boot 2026-10-23 together with 0.4.13)

- Pairs with MtkWlan 0.12.0. When the core's link supervisor resets the chip and reloads the firmware, the link goes down and comes back through the existing "link restored by core" path, so the Wi-Fi menu shows a short drop and rejoins.
- Works in macOS Recovery on 26.7.1 (25G241): Recovery's boot kernel collection carries the same IO80211Family 1200.13.1, IOSkywalkFamily and corecapture, so the ABI guard engages there too (checked from the Recovery volume, a boot test is next).

- Up to 128 networks go to the Wi-Fi menu instead of 48, to match the core's larger BSS cache (MtkWlan branch scan-cache-roc). With 48, weaker 2.4 GHz networks could still be left out in a crowded place. Idea from thanhnndev's [ga403wm-hackintosh 4257353](https://github.com/thanhnndev/ga403wm-hackintosh/commit/4257353065b34181b42993c946fb0956c47c6e32).

## 0.4.11 (2026-10-18)

- Tx queues restart on the next 2 ms tick after the core frees a slot. They used to wait for the 10 ms control poll, which capped upload near 190 Mbit/s. Frames refused while the link was down are asked for again when it comes up.
- Tx completion moves the pending list once per tick instead of once per packet.
- Measured on the author's laptop with MtkWlan 0.11.3: about 410 Mbit/s of local UDP on VHT 80 MHz 2x2, gateway ping 3-4 ms (5 ms before).

## 0.4.10 (2026-10-18)

- Claims the core at the end of `probe()` (`MTK_CORE_FE_CLAIM`, MtkWlan 0.11.1), before Apple's interface start can outlast the core's 15 s fallback. Before this, a slow `IO80211SkywalkInterface` start (17.2 s on one boot) let the Ethernet front end win by 230 ms and the Wi-Fi menu stayed empty until the next reboot. Every place `start()` gives up now declines, so the fallback still comes at once when the front end really cannot run.
- Debug boot-arg `mtknativedelay=<ms>` (at most 60000) holds the interface start back, to test the late path.

## 0.4.9 (2026-10-18)

- Link quality update events (0x27, 0x1dc bytes) once a second while the link is up. The Family's assoc timer declares "missed beacons" when its beacon and traffic timestamps are both older than 60 s, and only this event refreshes them; the beacon counter of 0.4.7 feeds a different counter. Proven on the author's laptop: ten quiet minutes without a leave.

## 0.4.8 (2026-10-18)

- Beacon-counter events are posted while the core holds the link, not only while its scan cache entry for the access point is fresh (the firmware keeps the associated AP's beacons to itself on some channels, and the Family raised BeaconLoss again).

## 0.4.7 (2026-10-18)

- Beacon-counter events (0xaf, u32 count at +0x54) once a second while the link is up. Without them the Family raises the fault "missed beacons timeout" about 60 s after every join and leaves the network. Under test.

## 0.4.6 (2026-10-18)

- The candidate BSSID is read at +0x21c (0.4.5 read the wrong field). Confirmed on the author's laptop: the join lands on the access point macOS chose.

## 0.4.5 (2026-10-18)

- `setWCL_ASSOCIATE` passes the first candidate's BSSID to the core (`apple80211AssocCandidates`: count at +0x214, entries of 18 bytes from +0x21c, BSSID at +6). 0.4.4 joined the core's own favourite access point and the Family re-joined every 15 to 60 s. Under test.

## 0.4.4 (2026-10-18)

- CoreCapture pipes and streams are started after the interface has bound the core. Starting them earlier took the native front end past the core's 15 s fallback and the Ethernet front end won the boot.

## 0.4.3 (2026-10-18)

- `IOService::start` on the CoreCapture data pipe, data stream and log pipe. The factories only alloc and init; a fault report completing on an unstarted data pipe panics in `CCDataPipe::enqueueBlob`.

## 0.4.2 (2026-10-18)

- One version string (`MTK_NATIVE_VERSION`), shown by the Family as "Firmware Version".

## 0.4.1 (2026-10-18)

- ioctls 0x170 `SET_MAC_ADDRESS` and 0x22d `DBRG_ENTROPY` admitted on the join path; a refused address answers NotPermitted.

## 0.4.0 (2026-10-18)

- The Family drives the association: `WCL_ASSOCIATE`, `WCL_LEAVE_NETWORK`, `WCL_JOIN_ABORT`, the events 0xd8 / 0x4e / 0xd3 / 0xd4 / 0xd5, `WCL_BSS_INFO`, `WCL_EXTENDED_BSS_INFO` (0x214 bytes on 25G241), `WCL_LINK_STATE_UPDATE`, `WCL_LINK_UP_DONE` and the link-path requests. The core's open-network fallback is off in native mode.

## 0.3.1 (2026-10-18)

- The ioctl allow-list admits `WCL_SCAN_REQ` and `WCL_SCAN_ABORT`; every WCL request is gated by `isCommandProhibited` first.

## 0.3.0 (2026-10-18)

- Scan requests run the core's scan; one 0xc9 event per BSS in the 25G241 layout (IEs at +0x44), then 0xed.

## 0.2.1 (2026-10-18)

- The interface follows the BSD link-layer address into the core (`set_mac`, core ops v4): macOS gives the interface a private address per network and the firmware must answer to it.

## 0.1.0 (2026-10-17)

- Controller and interface, Skywalk packet pool and queues, data path bridged to the core, ABI guard per macOS build.
