# Station front-end

Tools > Station front-end stores the dish, claimed LNB noise figure, and
Bias-T rating used by a satellite pass. DEC-0210. Arm is fail-closed.
The lease and IF checkboxes are operator attestations. They are not a
radio lease and they do not measure the SDR span. Rotator motion uses the
panel's own `rotctld` client. Do not connect this panel and Tools >
Antenna Rotator & SWR to the same `rotctld` at the same time.

## Armed pass

Read armed pass copies azimuth and elevation from the planner position
that matches the armed satellite, adds Doppler to the armed nominal
frequency, and stores that true RF. It is accepted only when a pass is
armed, the look is finite, elevation is at or above the station mask
(default 10°), and `tleAgeSec` is known and inside the profile limit
(default 72 h). The TLE checkbox is set from that age check. A rejected
read clears it and does not move the predicted angles.

Follow armed pass repeats the same check once a second. If the pass is
no longer armed, or the TLE age is unknown or past the limit, the session
aborts. Elevation under the mask pauses tracking and does not command
that point. A missing look leaves the last accepted point in place.

The application installs this reader. A test binary that does not link
`PlannerCapture.cpp` reports "No armed satellite pass".

Save pass log writes `station-profile.json` and `metrics.jsonl` under the
application data directory `station-passes/<UTC stamp>`. The log records
commanded angles, reported angles, Doppler, and the true RF. It does not
retune the SDR and it does not claim a measured C/N.

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
the metric log. LEO track sends a new position every second. GEO park
sends the predicted azimuth and elevation once and holds. GEO box scan
walks a raster of at most 49 dwells around that prediction and logs each
command. It does not pick a peak. Manual arms power and does not move
the rotator. Abort order is: record stop-worker, send stop, arm again,
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
