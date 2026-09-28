# SDR Town 0.2.118 Experimental

- Inmarsat map now supports optional ADSB.lol positioning for received Classic
  Aero aircraft. Explicit consent is required; online is off by default.
- Correct Classic Aero ICAO population and publish voice-channel identities to
  the aircraft registry without inventing coordinates or speech activity.
- Independent RF/internet position ages, expiry and offline fallback; optional
  bounded estimated movement, with source and measured anchor clearly shown.
- Highlight received aircraft-associated calls across watched channels and
  identify selected speaker source separately. Unknown identities remain unknown.
- Bounded asynchronous lookup, cancellation, backoff, hostile-response tests,
  local decision evidence and privacy-filtered opt-in remote counters.
- Reviewed diagnostic recordings retain voice identity/activity context.

P25, Inmarsat modem/FEC/vocoder and audio arbitration are unchanged. A map
estimate is not a navigation measurement. Live field validation of specific
aircraft/call matches remains open. See docs/INMARSAT_HYBRID_MAP.md.

Portable experimental tester release, not a signed stable updater replacement.
