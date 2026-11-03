# MT7902 0.2.11: correlate transmit status and response deadlines

The 0.2.10 capture established one successful open-system authentication on
5 GHz: a valid response, TXS ACK and TXFREE token release. Association then
timed out. Other attempts lacked completions inside their exchange windows.
The fixed PCI and firmware register views agreed for all four queries; L1
did not match CF-End. No timing writes or access-path substitutions are added.

Previously each phase queued up to four requests, 150 ms apart, independently
of actual transmit status. All fixed-rate frames used PID3, so a delayed auth
status could not be distinguished from a later association status.

This version assigns a distinct ordinary host PID to each auth/assoc submission
and allows up to three requests per phase. Each request waits at most 500 ms
for a correlated transmit status or a response. A timely successful ACK starts
a separate 150 ms response window; a negative status allows retry. Duplicate,
old-PID and overdue statuses cannot extend the deadline. Time uses elapsed
monotonic milliseconds, including wraparound, instead of counting poll loops.
These values are informed by Linux mac80211's separate queue/status and
ACK-to-response waits, not an assertion that its complete MLME is reproduced.

PIDs 4..126 are never reused within a firmware instance; PID3 remains reserved
for other fixed-rate frames. Linux's bit7 HAS_RATE range is avoided. Exhaustion
requires the existing quiesced firmware recovery before reuse. Payload tokens
remain owned until TXFREE or confirmed DMA shutdown, irrespective of timeouts.
At most 650 ms is spent on each request, excluding scheduler stalls; two phases
with three requests fit within the requested five-second ROC interval. Actual
ROC acceptance/duration and RF behavior still require hardware validation.

The deadline helper used by the driver has host tests for delayed ACKs, missing
status, negative status, old IDs, duplicate status, exact deadlines, inactive
state and clock rollover. TXWI tests check dynamic PID encoding and reserved
range rejection. Observed hardware TXS/TXFREE fixtures remain in the suite.
Actions runs sanitized tests, builds x86_64 kext/app and verifies release and
embedded firmware. This changes connection behavior, but is not a confirmed
internet fix: WPA2, DHCP and Ethernet traffic are still hardware-unverified.

When ready for the combined hardware test, replace both bundles, retain
mt7902_stage=3 and reboot once. Existing stage-3 connections can then be retried
from the app without a reboot. On failure, capture driver-ioreg.txt before
rebooting; auth/assoc diagnostics now include elapsed_ms and last_pid.
