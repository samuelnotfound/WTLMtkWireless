# MT7902 0.2.0: opt-in connection test

Hardware evidence from the user's 0.1.1 screenshot: repeated successful passive
scans, scan-result 0, actual MAC from firmware, NSS/chain mask 1, and discovery
of WPA2 APs on channels 1 and 44. This validates passive discovery on that card.
It does not validate transmission, association, WPA2 keys or Ethernet traffic.

Install both this kext and its app, add `mt7902_stage=3` to OpenCore boot arguments,
and reboot. Select a detected WPA2-PSK/CCMP network in ItlWiFi7902.app. Start with
the previously detected 2.4 GHz/channel 1 AP. If joining succeeds, macOS should
gain an Ethernet-style interface; verify its DHCP address and traffic separately.
Stage 2 remains the default and the fallback for this test.

The connection worker uses passive target discovery, the firmware-reported single
antenna chain and WF0 fixed-rate SPE index, legacy/20 MHz BSS setup, a restricted
non-DFS channel plan, SAR/SKU tables with a 10 dBm per-rate test ceiling, the
shared MT7902 event/TXFREE ring, and Connac2 station/CCMP key commands. The app
enables rows only when the driver reports a supported channel and RSN policy.
Host tests cover malformed/truncated RSN, PMF-required rejection, channel/domain
bounds and single-chain TXWI selection in addition to existing protocol vectors.

The inherited four-way handshake and Ethernet paths are enabled for hardware
testing. Group-key renewal, sleep/wake, faster modes, native Wi-Fi, HeliPort and
long-duration reliability remain outside this checkpoint. A build/test pass does
not establish that the card connects. Collect m3-log, m3-res, wifi-handshake,
wifi-events and stat-log after testing; see README.md for instructions.
