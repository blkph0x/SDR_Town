# P25 one-RTL return-to-control RF retune fix

Problem observed in field: the UI/status reported that the P25 control channel was being monitored, but the physical RTL-SDR was still tuned to a previous one-RTL traffic frequency outside the control-channel passband.

Root cause: the independent traffic-source release path treated all traffic sources as same-wideband logical sources and returned without retuning RF. That is correct only when the voice traffic was channelized from the existing control-channel wideband IQ. It is wrong for the single-RTL retune traffic-source mode, where the physical tuner has been moved away from the CC.

Fixes:

- Added an application-level `p25IndependentTrafficRetunedPrimary` flag.
- Added per-receiver `p25TrafficRetunesPrimary`.
- Set both flags when a traffic source is created from `single-rtl-retune-traffic-source`.
- On traffic release/return-to-control, if the source retuned the primary tuner, explicitly retune RF back to `p25AutoFollowReturnControlFreqHz` before reporting `Monitoring CC`.
- If retune back to CC fails, do not claim the control channel is being monitored.
- Manual P25 Monitor / Grant Test paths clear the one-RTL retuned state after explicitly tuning the selected CC.

Expected log for one-RTL return:

```
P25 one-RTL traffic source released; RF retuned back to control channel 420.35000MHz.
```

For same-wideband DDC traffic sources, the old no-retune release message remains correct because RF center never left the control-channel capture.
