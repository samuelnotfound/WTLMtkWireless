# MT7902 0.2.3: station cleanup and connection diagnostics

The 0.2.0 hardware screenshot confirms stage 3 and password submission work,
but does not show a successful connection. An earlier attempt reported
auth-fail; later attempts returned station-setup status 65539 (0x00010003)
before authentication. The screenshot cannot establish whether the original
authentication failure was a timeout or a negative response from the AP.

The connection worker previously only reset BSS state after a failed attempt.
It now follows the Linux mt7921 removal sequence: abort remain-on-channel,
disconnect the AP station using BASIC plus an empty RESET_AND_SET WTBL, then
reset the BSS. This avoids reusing an existing station record with the NEW flag.
A failed station cleanup requires firmware recovery before another attempt.
Whether stale state caused the reported firmware status still needs a test.

Legacy station BASIC and WTBL records now advertise QoS disabled, consistent
with the existing legacy association request that sends no WMM information IE.
Authentication timeouts produce auth-timeout-fail and a register/ring snapshot;
station, BSS and channel-command failures have distinct stages and app messages.
Firmware command status is logged in both decimal and hexadecimal.
The disconnect packet has a host fixture checked against the mt76 Connac2 ABI,
including output bounds and nested TLV counts.

Replace both the kext and companion app, keep mt7902_stage=3, and reboot before
testing. After one failed join, collect logs using bash collect-logs.sh from
the extracted package. Send driver-ioreg.txt and unified-log.txt as text.
Keep the password field hidden when taking screenshots.

Firmware is embedded. Passive scanning remains the default and stage 2 is the
fallback. Radio settings and WPA2/channel restrictions are unchanged. Successful
authentication, key exchange, DHCP and Ethernet traffic remain unverified on
MT7902; this package is a correction for testing, not a working-Wi-Fi release.
