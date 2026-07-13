# P25 one-RTL traffic source fix

This patch corrects the previous independent traffic-source implementation to match how sdrtrunk can work well with a single RTL-SDR.

Key correction:

- A traffic-channel source is a logical decoder/source abstraction, not always a second physical dongle.
- If the granted traffic downlink is already inside a running wideband sample passband, we create a separate traffic Receiver on the same SDR ring and keep the control receiver alive.
- If another SDR is available, we allocate/retune it for true simultaneous control+traffic.
- If only one SDR is available and the voice channel is outside the sampled passband, we now use one-RTL trunking mode: retune the primary RTL to the traffic frequency, pause control-channel decode while the call is active, and return to control on teardown.

This replaces the incorrect previous behavior that treated out-of-passband one-RTL traffic as unavailable unless a second SDR existed.

Expected log messages:

- `same-wideband-control-source`: traffic is DDC'd from the same sampled RF passband and control can continue.
- `dedicated-sdr-traffic-source`: traffic is decoded on another SDR and control can continue.
- `single-rtl-retune-traffic-source`: the single RTL is parked on traffic and control is paused until return, matching the common one-dongle sdrtrunk behavior.
