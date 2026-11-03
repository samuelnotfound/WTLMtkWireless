# MT7902 connection test, version 0.2.12

Experimental Intel x86_64 macOS Tahoe / Darwin 25 driver for PCI `14c3:7902`,
adapted from hbyonly1's MT7922/Connac2 driver. A user hardware test confirmed
firmware upload in 0.1.0 and repeated passive scans of 2.4/5 GHz networks in
0.1.1. The 0.2.0 connection test failed authentication on the test card and
subsequent retries received firmware station-setup status 65539 (0x00010003).
0.2.3 fixed missing station cleanup; hardware logs now confirm station setup,
cleanup and remain-on-channel grant succeed, but authentication times out.
0.2.4 added transmit-status and management-receive diagnostics: four queued
requests produced no completions or matching authentication responses in the
reported 2.4 GHz attempt. A user also reported a failed 5 GHz attempt without
a captured failure stage. 0.2.5 added Linux's firmware sleep-disable request;
the next capture confirmed it was submitted but channel-44 authentication still
timed out. 0.2.6 added raw completion counts and DMA/MAC snapshots; its hardware
capture also reached association once, then later attempts timed out during
authentication. 0.2.7 adds the missing firmware registration of the station MAC
before BSS setup and preserves the last eight attempt snapshots. The cause of
the timeouts remains unconfirmed; this change still needs hardware testing.
0.2.7 station registration succeeded on hardware but authentication still timed out.
0.2.8 adds the missing explicit home-channel switch and MAC timing setup.
`wifi-channel-setup` and `channel-switch-result` record the new setup.
0.2.8 timing readback failed on hardware and prevented authentication.
0.2.9 removes those timing writes and that blocker, retaining the explicit
firmware channel command and read-only fixed/L1 register comparisons.
0.2.10 aligns the legacy BSS QoS flag with the station and association request.
See docs/OFFLINE-AUDIT.md for the broader source comparison and remaining gaps.
0.2.10 hardware logs confirm one successful 5 GHz authentication with TXS ACK
and TXFREE, followed by an association timeout. 0.2.11 replaces fixed 150 ms
retries with bounded ACK-correlated waits and distinct request status IDs.
**0.2.11 completed WPA2/DHCP and carried hotspot traffic on user hardware.**
Router authentication still failed. 0.2.12 adds HT20/one-stream negotiation
for HT-capable OFDM-only APs while keeping CCK-capable hotspots on the tested
legacy path. Router HT traffic remains hardware-unverified.
Successful compilation is not a working-internet result.

## Install and test

1. Replace both `itlmt7902.kext` and `ItlWiFi7902.app` with this package's copies.
   Enable only this MT7902 driver; disable the older `MT7902PCI.kext`.
2. Merge `docs/OpenCore-merge-only.plist` into your existing OpenCore config if
   needed. It targets x86_64 Darwin 25.x. Keep a working test EFI available.
3. Add **`mt7902_stage=3`** to your existing OpenCore boot arguments for the
   connection test. Replace an existing stage argument; do not duplicate it.
4. Reboot and open **ItlWiFi7902.app**. Select a detected WPA2 network and enter
   its password. Start with the previously detected 2.4 GHz network on channel 1.
5. If authentication and the four-way handshake succeed, the driver creates an
   Ethernet-style interface. Check for a DHCP address in macOS Network settings,
   then test reaching your router and the internet.

Firmware is embedded in the kext; no separate firmware installation is needed.
The app displays disabled rows for unsupported security/channel combinations.
HeliPort and the native macOS Wi-Fi menu are not supported by this control ABI.

Stage 2 remains the default when no argument is supplied. It runs passive scanning
with connections disabled. Use `mt7902_stage=2` to return to scanning, `=1` for
firmware bootstrap followed by DMA shutdown, or `=0` for PCI ownership/chip ID.
Stage 3 waits for app requests, ignoring credentials in the kext personality.
The app may reconnect saved networks.

## Connection checkpoint limits

- One reported antenna chain and 20 MHz channels. HT-capable OFDM-only APs
  use a constrained HT20/WMM profile; CCK-capable 2.4 GHz APs use legacy mode.
- WPA2-PSK with CCMP group/pairwise encryption; PMF must be optional. PSK+SAE
  transition networks may use WPA2. WPA3-only, enterprise, TKIP, WEP and open
  networks are unsupported.
- Visible networks on channels 1-11, 36/40/44/48 or 149/153/157/161/165 only.
  No DFS, hidden-network probes, 6 GHz, VHT/HE or Block Ack negotiation.
  HT20 supports one stream with long GI and MCS0..7 only.
- Passive discovery; selected-channel domain setup and SAR/SKU commands with a
  10 dBm per-rate test ceiling before authentication. This experimental setup
  is not a certification of radio behavior.
- Scan requests are refused while connected; disconnect before rescanning.
- Initial four-way handshake/key installation is implemented. Group-key renewal
  and full power management still need work; long-lived links are not validated.
  Saved app passwords use the inherited local file store, not Keychain.

The inherited Ethernet path, TXFREE parser and WPA2 primitives have host tests,
and 0.2.11 hotspot captures establish initial WPA2/DHCP and traffic. They
do not establish router HT interoperability or long-term stability.

## Diagnostics

Run these after a failed or successful join:

```sh
ioreg -r -c itlmt7902 -l -w0
bash collect-logs.sh
```

Send `port-version`, `diagnostic-stage`, `join-supported`, `wifi-state`,
`wifi-error`, `m3-res`, `m3-log`, `wifi-handshake`, `wifi-events` and `stat-log`.
For authentication failures include `wifi-auth-diag`, `wifi-last-mgmt-tx` and
`wifi-postmortem`. The capture script includes these automatically.
Diagnostics include `wifi-completion-diag`, `wifi-rx-packet-types`,
`wifi-hw-tx`, `wifi-tx-memory` and any `wifi-txs-raw` / `wifi-txfree-raw` reports.
Version 0.2.7 adds `device-info-ready`, `device-info-result` and the bounded
`wifi-attempt-history`, so later retries cannot erase all earlier progress.
For a connection, also check `networksetup -listallhardwareports` and `ifconfig`
for the new Ethernet interface and IP address. Do not include saved password files.
`dma-quiesced=No` is expected while the scan/connection worker remains active.
An `auth-timeout-fail` means no authentication response was received, while
`sta-setup-fail` means the firmware rejected station setup before authentication.
Neither result establishes that a password is incorrect. Association timeouts
now use `assoc-timeout-fail`; `assoc-fail` indicates a received nonzero status.
See RELEASE-0.2.12.md in docs for the setup change and remaining uncertainty.

## Build and attribution

GitHub Actions runs sanitized host tests, builds the x86_64 kext and app on an
Intel macOS runner, and verifies both firmware images occur in the kext executable.
Locally on macOS, use `bash tests/run_all.sh`, `bash scripts/build.sh` and
`python3 scripts/package.py`. The pinned kernel SDK is fetched automatically
unless `MACKERNELSDK` names an existing checkout.

See `NOTICE.md` for source revisions and licenses. The original project's MT7922
hardware/performance claims in preserved upstream docs are not MT7902 results.
