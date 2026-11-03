# MT7902 0.2.8: home-channel switch and transmit timing

The 0.2.7 capture confirms successful station MAC registration, BSS/STA setup
and channel grants. All eight retained attempts timed out during authentication
on channels 1 and 44. DMA consumed requests, but no TXS/TXFREE reports or matching
authentication replies arrived. The WPA2 password handshake was not reached.
Station registration did not resolve that failure.

Linux mt7921_set_channel sends MCU_EXT_CMD(CHANNEL_SWITCH) and then applies
mt792x_mac_set_timeing. The port previously sent SET_RX_PATH and ROC without
this home-channel setup. Version 0.2.8 adds the 76-byte legacy 20 MHz channel
switch after power/domain and device setup, before BSS/STA setup and ROC.
It waits for the matching MCU response. The inherited legacy MCU transport does
not decode an extended command's firmware status; channel-switch-result records
the transport result, not proof of an accepted RF configuration.

Transmit timing follows Linux coverage-class zero: CCK/OFDM timeouts, EIFS/RIFS,
2.4/5 GHz SIFS, 9 us short slots and OFDM24 CF-End. This matches the checkpoint's
short-slot BSS RLM. It temporarily gates TX/RX while writing timing, then releases
the gates and verifies readback. It refuses an inaccessible or already disabled
ARB rather than clearing a pre-existing gate. A setup failure stops the join
with channel-setup-fail. wifi-channel-setup and channel-switch-result are also
retained in wifi-attempt-history.

The channel packet has host fixtures for both bands, output guards, truncation
and unsupported-channel rejection. Actions runs sanitized protocol tests,
compiles the x86_64 kext/app and checks release contents and embedded firmware.
These checks do not establish a working hardware connection. Authentication,
WPA2, DHCP and Ethernet traffic remain unverified. This is a correction of
missing Linux setup, not a confirmed resolution of the captured timeout.

Install both bundles, retain mt7902_stage=3, reboot and try one network. Collect
driver-ioreg.txt before rebooting if it fails. No new boot argument is required.
Stage 2 scanning and the reported antenna/power limits remain unchanged.
