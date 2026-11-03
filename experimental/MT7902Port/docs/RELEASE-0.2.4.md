# MT7902 0.2.4: authentication transmit/receive diagnostics

The 0.2.3 hardware capture shows repeated successful BSS setup, AP station
creation, channel grants and station removal. Each attempt queues four open
authentication requests, the DMA consumer catches the producer, and no matching
authentication response reaches the worker. The attempt ends with -110 and
auth-timeout-fail. The password handshake has not started. Descriptor consumption
alone does not establish an over-the-air transmission or acknowledgement.

The receive dispatcher previously dropped type-0 TXS packets despite fixed-rate
TXWI requests already enabling HOST status with PID 3. This version decodes
Connac2 MPDU TXS records and counts ACK, ACK timeout, RTS timeout and queue timeout
reports for WCID1/PID3. It retains raw status dwords during management exchanges.
Packet length, record bounds and format handling have sanitized host tests.

New wifi-auth-diag and wifi-assoc-diag properties record each exchange window:
queued requests, matched replies, TXFREE releases/failures/retries, TXS counts,
raw receive/decode/matching rejection counts, and consumed descriptors per ring.
wifi-last-mgmt-tx records the most recent authentication/association TXWI, token,
rate, queue and antenna selector without frame contents or credentials. A TXS
ACK count means the ACK-error bits were clear; raw error words remain available.
Missing TXS reports alone do not prove the device failed to transmit. Counters
cover an exchange window and delayed completions may belong to an earlier frame.

Replace the kext and app, retain mt7902_stage=3, and reboot. Try one join, then
run bash collect-logs.sh before rebooting. Provide driver-ioreg.txt, including
wifi-auth-diag, wifi-last-mgmt-tx, wifi-postmortem and m3-log.

This is a diagnostic build. It does not change transmit power, RF channel rules,
authentication timing, frame formats or management acceptance policy. It does
not claim to fix the timeout. WPA2 association/key exchange, DHCP and Ethernet
traffic still require hardware validation. Firmware remains embedded and stage 2
remains the default/fallback.
