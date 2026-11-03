# MT7902 0.2.6: raw completion and DMA/MAC diagnostics

The 0.2.5 capture confirms the sleep-disable request was submitted, but channel
44 authentication still timed out after four queued requests. No matching
TXFREE release or WCID1/PID3 TXS report was counted. Station setup, channel grant
and station removal continued to succeed. Password validation was not reached.

Previously the completion counters counted only reports that passed decoding
and matched an outstanding token or the expected WCID/PID. A zero count could
not distinguish missing packets from rejected or unmatched reports. This version
adds counts before classification/decoding plus the following exchange snapshots:

- wifi-rx-packet-types: packet types observed during the exchange, including
  types that the normal receive dispatcher ignores.
- wifi-completion-diag: raw TXS/TXFREE packet counts and parser/filter/token
  rejection counts. A TXS parse rejection counts a failed record; a TXFREE
  header rejection counts a failed packet.
- wifi-txs-raw / wifi-txfree-raw: the last completion packet header and first
  four body dwords during the exchange, when those packets are present.
- wifi-hw-tx: native MAC arbitration/transmit control, raw MPDU-attempt/success
  counter values before/after, TX ring base/count/prefetch and scheduler state.
  Counter reads follow the upstream watchdog and may consume read-clear stats.
  No assumption is made that a counter delta alone proves over-the-air delivery.
- wifi-tx-memory: the last management descriptor and hardware TXP read from CPU
  memory after the exchange, with expected header/payload physical addresses.
  This readback does not prove the device saw identical memory contents.

No frame payloads, passwords or keys are added to diagnostics. Radio settings,
packet formats, timing, firmware commands and management acceptance policy are
unchanged from 0.2.5. These are observations; this build does not claim to fix
the timeout. The sleep-disable change did not resolve the captured failure.

Install the kext and companion app, retain mt7902_stage=3, reboot, try one
network and collect logs before rebooting. driver-ioreg.txt contains all new
properties. Stage 2 remains the default/fallback. Firmware remains embedded;
WPA2 connection, DHCP and Ethernet traffic still require hardware validation.
