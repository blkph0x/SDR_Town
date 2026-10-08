# SDR Town 0.2.133 experimental

## Wedged TX stop no longer hangs the process

Stopping hardware tone TX no longer waits forever on the live Soapy I/O
mutex. If `writeStream` is stuck in the driver, SDR Town detaches the
handle and returns. A later idle stop reclaims the handle.

P25 decoding, encrypted mute, loopback control, and RX detach are unchanged.

This remains an experimental tester build.
