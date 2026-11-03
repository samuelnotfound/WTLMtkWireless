# MT7902 0.2.10: consistent legacy QoS configuration

An offline audit found that BSS QBSS advertised QoS while the legacy STA/WTBL
records and association request did not. QBSS now follows fastActive, matching
those paths and the negotiated-configuration rule in Linux mt76. See
OFFLINE-AUDIT.md for the full comparison and remaining evidence gaps.

This retains the 0.2.9 removal of experimental timing writes and its fixed/L1
read-only diagnostics. The QoS correction is not a confirmed timeout fix.
No new hardware test is requested for this isolated correction. Further
hardware-dependent conclusions must remain pending rather than prompting
another reboot for every hypothesis.

The same build also queries the four diagnostic registers through the firmware
once per firmware instance, after restoring the L1 mapping. Replies must have
the register-access event type, sufficient length and matching address. A
fw_valid bitmask distinguishes verified replies from unavailable values. Query
timeouts are limited to one second each and are observational; they do not
rewrite registers or make the connection depend on a guessed timing readback.
The first comparison remains available across retries and is cleared on reset.
Host fixtures check valid values, truncation, wrong address/event/namespace,
packet type and null inputs.

Validation: existing sanitized protocol tests, macOS x86_64 compilation and
release/firmware checks through Actions. WPA2, DHCP and Ethernet remain
hardware-unverified. Stage 2 remains the default; stage 3 is the opt-in test.
