# 0.2.10 capture: one confirmed authentication exchange

Six saved attempts: three channel-1 authentication timeouts, two channel-44
authentication timeouts and one channel-44 association timeout. The association
attempt received open-system authentication algorithm 0, transaction 2, status
0. Its auth diagnostics recorded three queued requests, one reply, one TXS ACK
and one TXFREE release. No attempt reached the WPA2 key handshake or Ethernet.

The hardware-observed completion prefixes are now regression fixtures for the
actual TXS/TXFREE codecs. They decode as WCID1/PID3 without ACK errors, and token
0x110 with one transmit attempt/status0 respectively. The TXS log captured only
the first four report words; unrecorded words are zero-filled in the fixture
and are not used by this decoder. No frame payloads or credentials are retained.

This rules out a universally nonfunctional transmit path and universal failure
to decode completion reports. It does not establish reliable DMA visibility,
RF transmission or completion delivery for the other queued frames. The four
association requests produced no reports or matching response within the current
exchange window. An AP rejection was not observed.

All four firmware register replies passed event/address/length validation.
Their values matched the fixed PCI window: three timing zeros and CF-End 0x1b.
The L1 comparison returned zeros including CF-End, so the remapped view cannot
be assumed interchangeable. The comparison occurred once, on channel 1; the
same saved snapshot on later channel-44 attempts is not a new measurement.
Do not rewrite timing or switch all MAC accesses to L1 based on these data.

The source audit should next examine the interval between descriptor fetch,
radio completion and response deadlines, including whether the firmware still
owns queued requests when retries or station teardown occur. A missing report
inside a short exchange is not proof the packet was never transmitted later.
The existing record lacks per-token submission/completion timestamps to settle
that question. No new hardware test is requested by these test/document changes.
