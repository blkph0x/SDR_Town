# Antenna rotator and SWR

Open **Tools > Antenna Rotator & SWR**. This first milestone supplies manual
absolute pointing, hardware readback, configurable soft limits, saved park
position and a read-only hardware SWR display. Automatic satellite/TLE tracking
and native serial-driver packaging are not implemented here.

## Connection

Install Hamlib separately and configure `rotctld` for the actual controller,
serial port and baud rate. SDR Town connects to its host/port (default
127.0.0.1:4533). This gives access to Hamlib-supported controllers, not only
the ten targets below. These are documented compatibility targets, NOT a
verified popularity ranking or physical-hardware certification.

| Controller / interface | Hamlib 4.7.1 model |
|---|---:|
| Yaesu GS-232B | 603 |
| Easycomm II (compatible homebrew controllers) | 202 |
| SPID Rot2Prog | 901 |
| SPID MD-01/02 in ROT2 mode | 903 |
| EA4TX ARS RCI AZ/EL | 1101 |
| Green Heron RT-21 | 405 |
| Idiom Press Rotor-EZ | 401 |
| AMSAT LVB Tracker | 607 |
| FoxDelta GS232/ST2 | 608 |
| M2 RC2800 | 1001 |

Source: [Hamlib supported rotators](https://github.com/Hamlib/Hamlib/wiki/Supported-Rotators).
Check your installed `rotctld -l`: model and axis capabilities vary. A Yaesu
motor alone is not a computer interface. Azimuth-only controllers need
elevation limits and targets set to zero; do not enable unsupported axes.

Example ONLY after verifying the physical model and connection:

```powershell
rotctld -m 603 -r COM5 -s 9600 -T 127.0.0.1 -t 4533
```

The port and baud above are examples, not automatic hardware detection.
Hamlib's TCP protocol does not provide encryption/authentication: keep it on
loopback or a trusted private/VPN network, never expose it to the Internet.

## Movement safety

Set actual cable/mechanical limits before connecting. Connect reads position
only. Arm explicitly, then Move or Park. The displayed bearing comes from
hardware feedback, not the requested target. No automatic reconnect or rearm.
Changes to limits take effect on the next connection; limits are locked while
connected. A stale/malformed/error response disarms. STOP takes priority after
the outstanding bounded transaction. Window close disarms and requests Stop.

A controller acknowledgement is not proof that a motor physically stopped.
Loss of power/network or unsupported Stop needs the physical interlock/stop.
The software does not replace limit switches or prevent another application
connected to the same controller from commanding movement.

## SWR

Connect the separate **SWR Meter** tab to a Hamlib `rigctld` server (default
127.0.0.1:4532) for equipment exposing PTT readback and the SWR level. SDR Town
only reads these values: it cannot key a transmitter or initiate a test carrier.
No reading is shown for idle, unsupported, invalid or disconnected hardware.
Receive-only SDRs cannot measure antenna SWR from received signal strength.
SWR requires appropriate directional measurement hardware and excitation;
this panel displays the hardware ratio, not a software estimate.

## Qualification

TCP fixtures cover command order, fragmented responses, movement limits,
arm/stop, mismatched responses, timeout and read-only meter behavior. Real
controller travel, mechanical stop and SWR accuracy require tester acceptance.
References: [rotctld protocol](https://hamlib.sourceforge.net/html/rotctld.1.html),
[rigctld levels](https://hamlib.sourceforge.net/html/rigctld.1.html).
