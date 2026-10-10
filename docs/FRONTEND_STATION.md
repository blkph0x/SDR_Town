# Station front-end

Tools > Station front-end stores the dish, claimed LNB noise figure, and
Bias-T rating used by a satellite pass. DEC-0210. This does not move a
rotator or apply DC until a later arm step confirms it.

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

Automatic AZ/EL uses the existing `rotctld` session. It does not link
Hamlib. Motion still requires the rotator to be armed with a fresh
position, or an explicit logged override. Soft limits clamp the command.
Manual jog pauses tracking. Abort order is: stop the worker, stop the
rotator, move to the saved park angles, turn Bias-T off, write the pass
summary. The app reports command-versus-reported error. It does not claim
sub-degree accuracy.

## DVB / SIGINT survey

D1 measures capture power, the strongest FFT bin, and bins above the
median. Symbol rate, PLS, LDPC, and a clear transport stream are not
implemented. Commercial decrypt is refused.

The margin label (masked, poor, fair, good) is an operator hint from dish
size, claimed NF, and elevation. It is not a link budget.
