# SDR Town 0.2.132 experimental

## Loopback control requires a token

The local control server on `127.0.0.1:8765` no longer accepts
unauthenticated `/v1/status` or mutating routes. Set `--control-token` or
`SDR_TOWN_CONTROL_TOKEN`. `/v1/health` stays open so a client can see that
the process is up. `--control-allow-unauthenticated` restores the previous
tokenless loopback.

P25 decoding, encrypted mute, TX teardown, and RX detach are unchanged.

This remains an experimental tester build.
