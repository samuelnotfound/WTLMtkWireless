# MT7902 0.2.12: HT20 router compatibility

The 0.2.11 hardware capture establishes successful hotspot WPA2 handshakes,
key installation, DHCP, an active Ethernet interface and sustained traffic.
The user reports internet through Android and iPhone hotspots. A recorded
hotspot link later received an AP deauthentication; long-term stability and
group-key renewal remain unverified. Router channel-44 requests still timed
out before authentication. The supplied router UI also shows n-only 2.4 GHz.

The port previously disabled HT negotiation entirely. This version enables a
constrained 802.11n profile for HT-capable OFDM-only APs: one spatial stream,
20 MHz, long guard interval, and only the AP-supported MCS0..7. Association HT
capabilities, BSS width/streams, STA PHY/rate/HT records and WTBL flags use that
profile. VHT/HE, wider channels, second-stream MCS, LDPC, STBC and SGI are not
advertised. WMM is advertised consistently in association/BSS/STA. Level 1
uses firmware-selected unicast QoS data rates; BA negotiation stays disabled.
Maximum A-MPDU exponent is conservatively 0; mandatory A-MSDU parsing remains.

CCK-capable 2.4 GHz APs retain the tested legacy path, covering both observed
hotspots. Failed HT association or key negotiation can fall back to legacy.
An authentication timeout does not falsely imply an HT association failure.
This addresses a concrete n-only compatibility gap; it does not establish that
the router's separate unanswered authentication requests have been resolved.

The inherited HT implementation cannot simply be enabled: it advertised two
streams and wide/VHT capabilities. This patch replaces its association HT
element and constrains the firmware peer records to match the MT7902 profile.
The new bounded encoder has byte fixtures, every MCS mask, output guards,
truncated-capacity rejection and null/empty-mask tests. Existing deadline,
observed TXS/TXFREE, packet/key and release tests run under sanitizers in CI,
followed by macOS x86_64 compilation and firmware/package verification.

Install both bundles and keep mt7902_stage=3; one reboot loads the changed
kext. No router setting change or new boot argument is required. Subsequent
connection attempts use the app. Test the router with its usual settings and
retain the 0.2.11 package as the confirmed hotspot baseline. Router HT traffic
and internet are not claimed as hardware passes until captured/tested.
