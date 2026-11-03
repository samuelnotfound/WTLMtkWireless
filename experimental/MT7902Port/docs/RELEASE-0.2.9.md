# MT7902 0.2.9: remove the timing regression

The 0.2.8 hardware capture shows matching channel-command responses, but CCK,
OFDM and IFS timing readbacks were zero and CF-End read back 0x9. The new strict
timing check therefore prevented all captured attempts from reaching BSS/STA
setup and authentication. It did not establish whether the explicit channel
switch improved the earlier authentication timeout.

This version removes the added MAC timing writes and their mandatory readback
check. Timing remains firmware-managed as in the MT7922 reference connection
path. The explicit firmware CHANNEL_SWITCH remains, with transport failures
still stopping setup. This removes a demonstrated regression; it does not claim
to resolve the earlier missing transmit completions.

wifi-channel-setup now compares read-only fixed-window and L1-remapped views of
the same four timing/CF-End registers. L1 remapping follows the pinned Linux
mt7921_reg_map_l1 procedure and restores the exact saved mapping afterward.
All chip access is on the single worker. No timing, power, antenna or DMA
registers are rewritten by this comparison. Equal zeros would not prove either
view is authoritative; differing views would identify an access-path issue.
Snapshots are retained in wifi-attempt-history.

Validation uses the existing sanitized protocol tests, x86_64 macOS build and
package/firmware verification. No new packet format is introduced. Hardware
authentication, WPA2, DHCP and Ethernet traffic remain unverified.

Replace both bundles, retain mt7902_stage=3 and reboot to discard 0.2.8's timing
writes. Try one connection and capture driver-ioreg.txt before rebooting.
