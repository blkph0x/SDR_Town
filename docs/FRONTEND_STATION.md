# Station front-end

Tools > Station front-end stores the dish, claimed LNB noise figure, and
Bias-T rating used by a satellite pass. DEC-0210. Arm is fail-closed.
The checkboxes for lease, IF span, and TLE age are operator attestations.
They are not a radio lease and they do not read a TLE file. Rotator motion
uses the panel's own `rotctld` client. Do not connect this panel and
Tools > Antenna Rotator & SWR to the same `rotctld` at the same time.

## LNB

Noise figure is the manufacturer's claim. An optional calibration offset is
stored separately. A claim below 0.2 dB is flagged and is not treated as a
measured G/T. The usual published Ku range is 0.2–0.8 dB. C-band units are
usually specified in kelvin. Noise temperature from a noise figure uses
T = 290 (10^(NF/10) − 1).

Universal Ku defaults are LO 9.75 GHz (low, no 22 kHz) and 10.60 GHz (high,
22 kHz). Vertical/left is 13 V. Horizontal/right is 18 V. The SDR is tuned
to the IF after Doppler is applied to the true RF. IF outside 950–2150 MHz
is rejected.

## Bias-T

Default is OFF for every new process. Enable needs the confirmation checkbox
and a selected source. The SDR's own Bias-T and an external supply cannot be
on together. A driver acknowledgement is not a measured voltage. Over-current,
LOS, abort, and shutdown command OFF. Unplug the hardware to guarantee DC is
gone after a crash.

## Rotator

`StationPassSession` drives the existing `RotatorController` (`+P`, `+p`,
`+S`). It does not link Hamlib. Connect, wait until position is fresh, arm
the rotator, then Arm pass. The session sends one planned position per
second. Soft limits are 0–360° azimuth and 0–90° elevation in this slice.
An override checkbox skips the fresh-position check and is written into
the metric log. Abort order is: record stop-worker, send stop, arm again,
move to the saved park angles (default 0, 0), then command Bias-T off.
A stop reply is not proof the motor is still. The metric log records
commanded AZ/EL, reported AZ/EL, and AZ error. The app does not claim
sub-degree accuracy.

## DVB / SIGINT survey

D1 measures capture power, the strongest FFT bin, and bins above the
median. Symbol rate, PLS, and LDPC are not implemented.
`inventoryClearTransportStream` only reads MPEG-TS packets the caller
already has: 188-byte packets, sync `0x47`, PID list, and a scrambled
count when the transport scrambling bits are not 00. It does not
demodulate and it does not read scrambled payloads. Commercial decrypt
is refused.

The margin label (masked, poor, fair, good) is an operator hint from dish
size, claimed NF, and elevation. It is not a link budget.
