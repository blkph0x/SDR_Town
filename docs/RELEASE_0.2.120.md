# SDR Town 0.2.120

Experimental usability update, paired with FUBAR 1.1.44.

- Visible workspace selector: Listen, P25 Trunking, HF / DX, Satellites / Aircraft,
  and Signal Analysis. Panels menu exposes the existing movable tools.
- Repeater Tones moves into its own panel, leaving more space for the receiver
  spectrum and waterfall. Existing controls, signals and saved settings remain.
- Satellite and Inmarsat controls inherit the desktop theme instead of forcing
  neon colours, large display headings and oversized buttons.
- Fixes the Inmarsat map response: the control DLL now receives an explicit
  success flag, including when the map is empty.
- Matching control DLL included. No P25, demodulator or audio pipeline change.

Validation covers Qt workspace navigation/persistence, Inmarsat map response,
the existing 16 CTest suites and frozen-P25 guard. Website tune/control and map
transport are checked through the actual companion. This is not new live RF
acceptance for every device or decoder; map tiles still require internet access.
Portable experimental package only; does not replace the signed stable updater.
